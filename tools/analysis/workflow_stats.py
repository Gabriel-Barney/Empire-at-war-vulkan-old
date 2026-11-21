#!/usr/bin/env python3
"""Workflow statistics: what the build records and the dev history say about how work went.

Reads the roadmap build records (roadmap-store/data/builds/*.json) and the git history of a ref (default origin/dev)
and prints, for a date range: builds per day, the failed share split by failure kind and by compiler, recorded test
hours, builds and failures per worktree, the tests that fail most, the commits touching stored goldens and benchmark
scenarios, and the share of merge commits. Stage 12 (workflow redesign) is measured with it: wr0 t01 recorded the
baseline in docs/PERFORMANCE.md ("Workflow baseline"), wr6 t06 repeats it.

  python -I tools/analysis/workflow_stats.py --from 2026-10-30 --to 2026-11-21 [--ref origin/dev] [--top 10]
  python -I tools/analysis/workflow_stats.py ... --markdown     the baseline table for docs/PERFORMANCE.md
  python -I tools/analysis/workflow_stats.py ... --json         every figure as JSON
  python -I tools/analysis/workflow_stats.py --selftest

Dates are inclusive. A build record's day is the UTC date of its `at` field; a commit's day is its committer date
(git --since / --until). Standard library only.
"""

from __future__ import annotations

import argparse
import collections
import datetime as dt
import glob
import json
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# A stored expected-result file: anything under a folder named testdata, golden, goldens or benchmark (the headless,
# game and sim goldens, module golden folders, tests/goldens/mods, the benchmark scenario saves, orders and results).
# Scripts under tools/, the launcher's parser fixtures and the mod fixture trees (test inputs, not results) are not.
GOLDEN_RE = re.compile(r"(^|/)(testdata|golden|goldens|benchmark)/")
NOT_GOLDEN_RE = re.compile(r"^(tools|launcher)/|/mod_fixture/")
BENCHMARK_RE = re.compile(r"^tests/benchmark/")


def is_golden(path: str) -> bool:
    return bool(GOLDEN_RE.search(path)) and not NOT_GOLDEN_RE.search(path)


# ---------------------------------------------------------------- build records

def load_builds(builds_dir: str, day_from: dt.date, day_to: dt.date, local_dir: str | None = None) -> list[dict]:
    """Roadmap build records plus, since wr2 t06, the iteration builds kept locally (logs/build_records)."""
    out = []
    paths = sorted(glob.glob(os.path.join(builds_dir, "*.json")))
    if local_dir:
        paths += sorted(glob.glob(os.path.join(local_dir, "*.json")))
    for path in paths:
        with open(path, encoding="utf-8") as f:
            rec = json.load(f)
        at = rec.get("at")
        if not at:
            continue
        day = dt.datetime.fromisoformat(at).astimezone(dt.timezone.utc).date()
        if day_from <= day <= day_to:
            rec["_day"] = day.isoformat()
            out.append(rec)
    return out


def _exit(rec: dict, step: str) -> int:
    try:
        return int((rec.get("exits") or {}).get(step, 0))
    except (TypeError, ValueError):
        return 0


def failure_kind(rec: dict) -> str:
    """The record's failure_kind; records from before 2026-11-13 have none, so derive it from the step exit codes."""
    kind = rec.get("failure_kind")
    if kind:
        return kind
    if _exit(rec, "configure"):
        return "configure"
    if _exit(rec, "build"):
        return "compile"
    if _exit(rec, "ctest") or rec.get("tests_failed"):
        return "test"
    return "other"


NO_WORKTREE = "(not recorded)"  # records from before the worktree field


def build_stats(recs: list[dict], day_from: dt.date, day_to: dt.date, top: int) -> dict:
    n = len(recs)
    failed = [r for r in recs if r.get("status") != "pass"]
    days = (day_to - day_from).days + 1
    per_day = collections.Counter(r["_day"] for r in recs)
    per_day_failed = collections.Counter(r["_day"] for r in failed)

    by_kind = collections.Counter(failure_kind(r) for r in failed)
    comp_total = collections.Counter(r.get("compiler") or "?" for r in recs)
    comp_failed = collections.Counter(r.get("compiler") or "?" for r in failed)
    kind_comp = collections.Counter((failure_kind(r), r.get("compiler") or "?") for r in failed)

    wt_total = collections.Counter(r.get("worktree") or NO_WORKTREE for r in recs)
    wt_failed = collections.Counter(r.get("worktree") or NO_WORKTREE for r in failed)

    test_fail = collections.Counter(t for r in recs for t in (r.get("tests_failed") or []))
    test_seconds = sum(float(r.get("test_seconds") or 0.0) for r in recs)

    return {
        "builds": n,
        "days": days,
        "builds_per_day_mean": round(n / days, 1) if days else 0.0,
        "builds_per_day": {d: [per_day[d], per_day_failed[d]] for d in sorted(per_day)},
        "failed": len(failed),
        "failed_share": round(len(failed) / n, 4) if n else 0.0,
        "failed_by_kind": dict(by_kind.most_common()),
        "by_compiler": {c: [comp_total[c], comp_failed[c]] for c in sorted(comp_total)},
        "failed_by_kind_and_compiler": {f"{k}/{c}": v for (k, c), v in sorted(kind_comp.items())},
        "test_hours": round(test_seconds / 3600.0, 1),
        "worktrees": len(wt_total),
        "per_worktree_top": [[w, wt_total[w], wt_failed[w]] for w, _ in wt_total.most_common(top)],
        "most_failing_tests": test_fail.most_common(top),
    }


# ---------------------------------------------------------------- git history

def git_log(repo: str, ref: str, day_from: dt.date, day_to: dt.date) -> list[tuple[str, int, list[str]]]:
    """(sha, parent count, files) per commit; merges list no files (git log's default)."""
    cmd = ["git", "-C", repo, "log", ref, f"--since={day_from.isoformat()}T00:00:00",
           f"--until={day_to.isoformat()}T23:59:59", "--format=COMMIT %H %P", "--name-only"]
    text = subprocess.run(cmd, check=True, capture_output=True, text=True, encoding="utf-8").stdout
    commits: list[tuple[str, int, list[str]]] = []
    for line in text.splitlines():
        if line.startswith("COMMIT "):
            parts = line.split()
            commits.append((parts[1], len(parts) - 2, []))
        elif line.strip() and commits:
            commits[-1][2].append(line.strip())
    return commits


def git_stats(commits: list[tuple[str, int, list[str]]], top: int) -> dict:
    n = len(commits)
    merges = sum(1 for _, parents, _ in commits if parents > 1)
    golden_commits = 0
    bench_commits = 0
    per_file = collections.Counter()
    for _, parents, files in commits:
        if parents > 1:
            continue
        golden = sorted({f for f in files if is_golden(f)})
        if golden:
            golden_commits += 1
            per_file.update(golden)
        if any(BENCHMARK_RE.search(f) for f in files):
            bench_commits += 1
    return {
        "commits": n,
        "merges": merges,
        "merge_share": round(merges / n, 4) if n else 0.0,
        "golden_commits": golden_commits,
        "golden_share": round(golden_commits / n, 4) if n else 0.0,
        "benchmark_commits": bench_commits,
        "golden_files_top": per_file.most_common(top),
    }


# ---------------------------------------------------------------- output

def pct(x: float) -> str:
    return f"{100.0 * x:.1f}%"


def print_text(s: dict) -> None:
    b, g = s["builds"], s["git"]
    print(f"Workflow statistics {s['from']} .. {s['to']} ({b['days']} days), ref {s['ref']} at {s['ref_sha'][:8]}")
    print()
    print(f"builds            {b['builds']}  ({b['builds_per_day_mean']} per day)")
    print(f"failed            {b['failed']}  ({pct(b['failed_share'])})")
    for kind, v in b["failed_by_kind"].items():
        print(f"  {kind:<15} {v:>5}  ({pct(v / b['builds']) if b['builds'] else '0'} of builds)")
    print("by compiler       builds  failed  share")
    for c, (tot, fl) in b["by_compiler"].items():
        print(f"  {c:<15} {tot:>6}  {fl:>6}  {pct(fl / tot) if tot else '-'}")
    print("failed by kind and compiler")
    for k, v in b["failed_by_kind_and_compiler"].items():
        print(f"  {k:<26} {v:>5}")
    print(f"test hours        {b['test_hours']}")
    print()
    print("builds per day    builds  failed")
    for d, (tot, fl) in b["builds_per_day"].items():
        print(f"  {d}      {tot:>6}  {fl:>6}")
    print()
    print(f"worktrees         {b['worktrees']}   top by builds: builds  failed")
    for w, tot, fl in b["per_worktree_top"]:
        print(f"  {w:<40} {tot:>6}  {fl:>6}")
    print()
    print("most failing tests")
    for t, v in b["most_failing_tests"]:
        print(f"  {v:>5}  {t}")
    print()
    print(f"commits           {g['commits']}")
    print(f"merges            {g['merges']}  ({pct(g['merge_share'])})")
    print(f"golden-touching   {g['golden_commits']}  ({pct(g['golden_share'])}; non-merge commits touching a stored golden)")
    print(f"benchmark scen.   {g['benchmark_commits']}  (commits touching tests/benchmark/)")
    print("most changed golden files")
    for f, v in g["golden_files_top"]:
        print(f"  {v:>5}  {f}")


def print_markdown(s: dict) -> None:
    b, g = s["builds"], s["git"]
    kinds = ", ".join(f"{k} {v}" for k, v in b["failed_by_kind"].items())
    comps = ", ".join(f"{c} {fl}/{tot} ({pct(fl / tot) if tot else '-'})" for c, (tot, fl) in b["by_compiler"].items())
    print(f"| Figure ({s['from']} .. {s['to']}, `{s['ref']}` at `{s['ref_sha'][:8]}`) | Value |")
    print("|---|---|")
    print(f"| Recorded builds | {b['builds']} ({b['builds_per_day_mean']} per day over {b['days']} days) |")
    print(f"| Failed builds | {b['failed']} ({pct(b['failed_share'])}) |")
    print(f"| Failed by kind | {kinds} |")
    print(f"| Failed / builds by compiler | {comps} |")
    print(f"| Recorded test hours | {b['test_hours']} |")
    print(f"| Worktrees with builds | {b['worktrees']} |")
    print(f"| Dev commits | {g['commits']} |")
    print(f"| Merge commits | {g['merges']} ({pct(g['merge_share'])}) |")
    print(f"| Commits touching stored goldens | {g['golden_commits']} ({pct(g['golden_share'])}) |")
    print(f"| Commits touching benchmark scenarios | {g['benchmark_commits']} |")


def collect(builds_dir: str, repo: str, ref: str, day_from: dt.date, day_to: dt.date, top: int,
            local_dir: str | None = None) -> dict:
    sha = subprocess.run(["git", "-C", repo, "rev-parse", ref], check=True, capture_output=True,
                         text=True).stdout.strip()
    return {
        "from": day_from.isoformat(), "to": day_to.isoformat(), "ref": ref, "ref_sha": sha,
        "builds": build_stats(load_builds(builds_dir, day_from, day_to, local_dir), day_from, day_to, top),
        "git": git_stats(git_log(repo, ref, day_from, day_to), top),
    }


# ---------------------------------------------------------------- selftest

def selftest() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        bdir = os.path.join(tmp, "builds")
        os.makedirs(bdir)
        recs = [
            {"at": "2026-11-01T10:00:00+00:00", "status": "pass", "compiler": "msvc", "worktree": "a",
             "test_seconds": 1800.0, "tests_failed": []},
            {"at": "2026-11-01T23:30:00-02:00", "status": "fail", "compiler": "clang", "worktree": "a",
             "failure_kind": "test", "test_seconds": 1800.0, "tests_failed": ["t_x", "t_y"]},  # UTC: 11-02
            {"at": "2026-11-02T08:00:00+00:00", "status": "fail", "compiler": "msvc", "worktree": "b",
             "exits": {"configure": 0, "build": 2, "ctest": 8}, "tests_failed": []},  # old record: kind derived
            {"at": "2026-11-02T09:00:00+00:00", "status": "fail", "compiler": "msvc", "worktree": "b",
             "failure_kind": "test", "test_seconds": 3600.0, "tests_failed": ["t_x"]},
            {"at": "2026-11-05T09:00:00+00:00", "status": "fail", "compiler": "msvc", "worktree": "c"},  # out of range
        ]
        for i, r in enumerate(recs):
            with open(os.path.join(bdir, f"r{i}.json"), "w", encoding="utf-8") as f:
                json.dump(r, f)

        repo = os.path.join(tmp, "repo")
        env = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@t", GIT_COMMITTER_NAME="t",
                   GIT_COMMITTER_EMAIL="t@t")

        def git(*a: str, date: str = "2026-11-01T12:00:00+00:00") -> None:
            e = dict(env, GIT_AUTHOR_DATE=date, GIT_COMMITTER_DATE=date)
            subprocess.run(["git", "-C", repo, *a], check=True, capture_output=True, env=e)

        def put(path: str, text: str) -> None:
            full = os.path.join(repo, path)
            os.makedirs(os.path.dirname(full), exist_ok=True)
            with open(full, "w", encoding="utf-8") as f:
                f.write(text)

        os.makedirs(repo)
        git("init", "-q", "-b", "dev")
        put("engine/a.cpp", "1")
        git("add", "-A"); git("commit", "-q", "-m", "code")
        git("checkout", "-q", "-b", "side")
        put("engine/apps/headless/testdata/x_golden.crc", "1")
        put("tools/goldens/regen.py", "1")
        git("add", "-A"); git("commit", "-q", "-m", "golden", date="2026-11-01T13:00:00+00:00")
        git("checkout", "-q", "dev")
        put("tests/benchmark/land.abench", "1")
        put("engine/apps/game/testdata/mod_fixture/corruption/Data/XML/a.xml", "1")
        git("add", "-A"); git("commit", "-q", "-m", "bench", date="2026-11-01T14:00:00+00:00")
        git("merge", "-q", "--no-ff", "-m", "merge", "side", date="2026-11-01T15:00:00+00:00")
        put("tools/x.py", "1")
        put("launcher/src/testdata/y.txt", "1")
        git("add", "-A"); git("commit", "-q", "-m", "tools only", date="2026-11-02T10:00:00+00:00")
        put("engine/b.cpp", "1")
        git("add", "-A"); git("commit", "-q", "-m", "late", date="2026-11-04T10:00:00+00:00")  # out of range

        s = collect(bdir, repo, "dev", dt.date(2026, 11, 1), dt.date(2026, 11, 2), 5)
        b, g = s["builds"], s["git"]
        checks = [
            ("builds", b["builds"], 4),
            ("failed", b["failed"], 3),
            ("per day", b["builds_per_day"], {"2026-11-01": [1, 0], "2026-11-02": [3, 3]}),
            ("by kind", b["failed_by_kind"], {"test": 2, "compile": 1}),
            ("by compiler", b["by_compiler"], {"clang": [1, 1], "msvc": [3, 2]}),
            ("test hours", b["test_hours"], 2.0),
            ("worktrees", b["per_worktree_top"], [["a", 2, 1], ["b", 2, 2]]),
            ("failing tests", b["most_failing_tests"], [("t_x", 2), ("t_y", 1)]),
            ("commits", g["commits"], 5),
            ("merges", g["merges"], 1),
            ("golden commits", g["golden_commits"], 2),
            ("benchmark commits", g["benchmark_commits"], 1),
            ("golden files", dict(g["golden_files_top"]),
             {"engine/apps/headless/testdata/x_golden.crc": 1, "tests/benchmark/land.abench": 1}),
        ]
        bad = [(name, got, want) for name, got, want in checks if got != want]
        for name, got, want in bad:
            print(f"selftest FAIL {name}: got {got!r}, want {want!r}")
        if bad:
            return 1
        print(f"workflow_stats selftest: ok ({len(checks)} checks)")
        return 0


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--from", dest="day_from", help="first day, YYYY-MM-DD (inclusive)")
    ap.add_argument("--to", dest="day_to", help="last day, YYYY-MM-DD (inclusive; default today)")
    ap.add_argument("--ref", default="origin/dev", help="git ref whose history is counted (default origin/dev)")
    ap.add_argument("--repo", default=ROOT, help="git repository (default this checkout)")
    ap.add_argument("--builds", default=os.path.join(ROOT, "roadmap-store", "data", "builds"),
                    help="build record folder")
    ap.add_argument("--local", default=os.path.join(ROOT, "logs", "build_records"),
                    help="local iteration build records (wr2 t06; default <repo>/logs/build_records; '' for none)")
    ap.add_argument("--top", type=int, default=10, help="rows in the top-N lists")
    out = ap.add_mutually_exclusive_group()
    out.add_argument("--json", action="store_true", help="print every figure as JSON")
    out.add_argument("--markdown", action="store_true", help="print the baseline table for docs/PERFORMANCE.md")
    ap.add_argument("--selftest", action="store_true", help="run the built-in test on a temporary fixture")
    args = ap.parse_args(argv)
    if args.selftest:
        return selftest()
    if not args.day_from:
        ap.error("--from is required")
    day_from = dt.date.fromisoformat(args.day_from)
    day_to = dt.date.fromisoformat(args.day_to) if args.day_to else dt.datetime.now(dt.timezone.utc).date()
    s = collect(args.builds, args.repo, args.ref, day_from, day_to, args.top, args.local or None)
    if args.json:
        print(json.dumps(s, indent=1))
    elif args.markdown:
        print_markdown(s)
    else:
        print_text(s)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
