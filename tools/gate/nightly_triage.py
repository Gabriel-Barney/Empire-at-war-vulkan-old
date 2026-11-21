#!/usr/bin/env python3
r"""Nightly deep run triage (wr2 t05): attribute each failure of the nightly gate and file it on the roadmap.

tools\gate_scheduled.cmd runs build_gate.cmd on origin/dev's tip every night (MSVC + clang + sanitizers, every test: the
slow tier, the fuzz smoke, the parity harness and seed runs, the worker-count matrix), then calls this with the gate's
logs. For each failing test:
  1. a failure already filed last night (state in <main checkout>\logs\nightly_state.json) updates its roadmap task
     ("seen again <date> on <sha>") instead of filing a new one;
  2. a new failure is attributed: the first commit between the last night it passed and tonight's tip that fails it,
     found by bisecting first-parent commits in the gate worktree (build_agent.cmd msvc bisect all "^test$"; at most
     ALAMO_NIGHTLY_BISECTS tests a night, default 3, and 8 steps each; the rest are filed unattributed);
  3. it is filed as an S2 task of the standing group `ngt1` (Nightly gate failures, box PROC) with the summary path.
     A failure already filed elsewhere is mapped by hand in nightly_state.json ("open": {test: {"task": "group/tNN"}})
     and only gets the "seen again" line.
A test that passes again closes nothing by itself: its task says when it last failed. The roadmap page's build line
(meta/page `build`) gets tonight's result and date, so its age shows.

  python -I tools/gate/nightly_triage.py --sha SHA --rc N --logs DIR --main MAIN_CHECKOUT [--wt GATE_WORKTREE] [--dry-run]
  python -I tools/gate/nightly_triage.py --selftest
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
import tempfile
import time
from typing import Callable

GROUP = "ngt1"
FAIL_RE = re.compile(r"^\s*\d+\s+-\s+(\S+)\s+\((Failed|Timeout|SEGFAULT|Exception|Child aborted|Not Run|SIGTRAP|OTHER_FAULT)\)")
LOGS = ("msvc_build.txt", "clang_build.txt", "asan_build.txt")


def failing_tests(text: str) -> list[str]:
    return sorted({m.group(1) for line in text.splitlines() for m in [FAIL_RE.match(line)] if m})


def gate_failures(logs_dir: str) -> dict[str, list[str]]:
    """test -> the build kinds (msvc / clang / asan) it failed in."""
    out: dict[str, list[str]] = {}
    for name in LOGS:
        p = os.path.join(logs_dir, name)
        if os.path.isfile(p):
            with open(p, encoding="utf-8", errors="replace") as f:
                for t in failing_tests(f.read()):
                    out.setdefault(t, []).append(name.split("_")[0])
    return out


def bisect(commits: list[str], fails: Callable[[str], bool], max_steps: int = 8) -> str | None:
    """commits oldest..newest; commits[-1] fails, the commit before commits[0] passed. The first failing commit, or None
    when max_steps ran out (then the answer is somewhere in the remaining range)."""
    lo, hi, steps = -1, len(commits) - 1, 0  # lo passes (or is before the list), hi fails
    while hi - lo > 1:
        if steps >= max_steps:
            return None
        mid = (lo + hi) // 2
        steps += 1
        if fails(commits[mid]):
            hi = mid
        else:
            lo = mid
    return commits[hi]


def task_text(test: str, kinds: list[str], sha: str, culprit: str | None, subject: str, summary: str, date: str) -> str:
    who = f"first failing commit {culprit[:8]} ({subject})" if culprit else "not attributed (bisect skipped or ran out)"
    return (f"[nightly] {test} fails on dev ({', '.join(kinds)}; nightly gate {date} on {sha[:8]}): {who}. "
            f"Summary: {summary}. Fix it, or file it under the group that owns the area and drop this task. "
            f"Acceptance: the test passes in a nightly gate run.")


def decide(state: dict, failures: dict[str, list[str]]) -> tuple[list[str], list[str]]:
    """(new failures to attribute and file, known failures to update)."""
    new = [t for t in sorted(failures) if t not in state.get("open", {})]
    known = [t for t in sorted(failures) if t in state.get("open", {})]
    return new, known


# ---------------------------------------------------------------- roadmap writes

def roadmap(main: str, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, os.path.join(main, "roadmap-store", "roadmap_store.py"), *args],
                          capture_output=True, text=True, errors="replace")


def ensure_group(main: str) -> None:
    if os.path.isfile(os.path.join(main, "roadmap-store", "data", "items", GROUP + ".json")):
        return
    doc = [{"op": "set", "collection": "items", "doc_id": GROUP, "data": {
        "title": "NGT1 Nightly gate and flaky test failures", "tag": "NGT1", "node": "PROC", "status": "partly", "preset": "high",
        "mod": "(any)", "order": 9000, "after": [], "by": "nightly-triage", "updated": time.strftime("%Y-%m-%d"), "cap": 50,
        "why": "Standing group for failures the nightly deep run attributes and files (wr2 t05, tools/gate/nightly_triage.py) and for tests flaky three times in a week (wr3 t05, tools/lint/flaky_retry.py).",
        "exit": "No open [nightly] task: every failure filed here was fixed, or moved to the group that owns its area.",
        "tasks": {}}}]
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8") as f:
        json.dump(doc, f)
    roadmap(main, "write", f.name)
    os.unlink(f.name)


def file_task(main: str, text: str) -> str:
    r = roadmap(main, "backlog", "add", text, "--by", "nightly-triage", "--sev", "S2", "--to", GROUP, "--force", "--kind", "fix")
    m = re.search(rf"{GROUP} (t\d+)", r.stdout + r.stderr)
    return f"{GROUP}/{m.group(1)}" if m else "?"


def update_task(main: str, ref: str, line: str) -> None:
    """ref is group/task (a failure may be mapped by hand in nightly_state.json to a task of the group that owns it)."""
    group, _, tid = ref.partition("/")
    if not tid:
        group, tid = GROUP, ref
    path = os.path.join(main, "roadmap-store", "data", "items", group + ".json")
    if not os.path.isfile(path):
        return
    with open(path, encoding="utf-8") as f:
        item = json.load(f)
    task = item.get("tasks", {}).get(tid)
    if not task or task.get("s") != "todo":
        return
    task["t"] = task["t"] + " " + line
    item["updated"] = time.strftime("%Y-%m-%d")
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8") as f:
        json.dump([{"op": "set", "collection": "items", "doc_id": group, "data": item}], f)
    roadmap(main, "write", f.name)
    os.unlink(f.name)


def set_build_line(main: str, line: str) -> None:
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8") as f:
        json.dump([{"op": "update", "collection": "meta", "doc_id": "page", "data": {"build": line}}], f)
    roadmap(main, "write", f.name)
    os.unlink(f.name)


# ---------------------------------------------------------------- the run

def git(repo: str, *a: str) -> str:
    return subprocess.run(["git", "-C", repo, *a], capture_output=True, text=True, errors="replace").stdout


def run(sha: str, rc: int, logs_dir: str, main: str, wt: str | None, dry: bool) -> int:
    state_path = os.path.join(main, "logs", "nightly_state.json")
    try:
        with open(state_path, encoding="utf-8") as f:
            state = json.load(f)
    except (OSError, ValueError):
        state = {}
    state.setdefault("open", {})
    state.setdefault("last_pass", {})
    date = time.strftime("%Y-%m-%d")
    failures = gate_failures(logs_dir)
    summary = os.path.join(main, "logs", "gate_dev_summary.txt")
    new, known = decide(state, failures)
    budget = int(os.environ.get("ALAMO_NIGHTLY_BISECTS", "3"))
    if not dry and failures:
        ensure_group(main)
    for t in known:
        line = f"Seen again {date} on {sha[:8]}."
        print(f"nightly_triage: {t}: known ({state['open'][t]['task']}); {line}")
        if not dry:
            update_task(main, state["open"][t]["task"], line)
    for t in new:
        culprit = None
        good = state["last_pass"].get(t) or state.get("last_green")
        if wt and good and budget > 0:
            budget -= 1
            commits = [c for c in git(wt, "rev-list", "--first-parent", "--reverse", f"{good}..{sha}").split() if c]

            def fails(c: str, test: str = t) -> bool:
                subprocess.run(["git", "-C", wt, "checkout", "-q", "-f", "--detach", c], capture_output=True)
                r = subprocess.run(["cmd", "/d", "/c", os.path.join(wt, "build_agent.cmd"), "msvc", "bisect", "all", f"^{test}$"],
                                   cwd=wt, capture_output=True, text=True, errors="replace")
                return r.returncode != 0
            if commits:
                culprit = bisect(commits, fails)
            subprocess.run(["git", "-C", wt, "checkout", "-q", "-f", "--detach", sha], capture_output=True)
        subject = git(wt or main, "log", "-1", "--format=%s", culprit).strip() if culprit else ""
        text = task_text(t, failures[t], sha, culprit, subject, summary, date)
        print(f"nightly_triage: {t}: new; " + (f"culprit {culprit[:8]}" if culprit else "not attributed"))
        tid = "dry-run" if dry else file_task(main, text)
        state["open"][t] = {"task": tid, "first_seen": date, "sha": sha, "culprit": culprit}
    # tests that passed tonight: remember the commit for the next bisect; an open entry stays until its task is closed
    for t in list(state["open"]):
        if t not in failures:
            state["open"][t]["passed_again"] = date
    if rc == 0 and not failures:
        state["last_green"] = sha
    for t in state["open"]:
        if t not in failures:
            state["last_pass"][t] = sha
    line = (f"nightly gate {date} on {sha[:8]}: " + ("PASS" if rc == 0 and not failures else
            f"FAIL ({len(failures)} test{'s' if len(failures) != 1 else ''}: {', '.join(sorted(failures)[:4])}"
            + (", ..." if len(failures) > 4 else "") + ")"))
    print("nightly_triage: " + line)
    if not dry:
        set_build_line(main, line)
        os.makedirs(os.path.dirname(state_path), exist_ok=True)
        with open(state_path, "w", encoding="utf-8") as f:
            json.dump(state, f, indent=1, sort_keys=True)
    return 0


# ---------------------------------------------------------------- selftest

def selftest() -> int:
    bad = []

    def check(c, what):
        if not c:
            bad.append(what)

    log = ("The following tests FAILED:\n\t338 - parity_harness_hotm_galactic (Failed)             slow\n"
           "\t 93 - alamo_fuzz_smoke (Timeout)\nErrors while running CTest\n")
    check(failing_tests(log) == ["alamo_fuzz_smoke", "parity_harness_hotm_galactic"], f"parse: {failing_tests(log)}")
    with tempfile.TemporaryDirectory() as tmp:
        open(os.path.join(tmp, "msvc_build.txt"), "w").write(log)
        open(os.path.join(tmp, "asan_build.txt"), "w").write("\t 93 - alamo_fuzz_smoke (Failed)\n")
        f = gate_failures(tmp)
        check(f == {"alamo_fuzz_smoke": ["msvc", "asan"], "parity_harness_hotm_galactic": ["msvc"]}, f"kinds: {f}")

    commits = [f"c{i}" for i in range(10)]
    for culprit in (0, 4, 9):
        calls = []

        def fails(c, k=culprit):
            calls.append(c)
            return int(c[1:]) >= k
        got = bisect(commits, fails)
        check(got == f"c{culprit}", f"bisect finds c{culprit} (got {got})")
        check(len(calls) <= 4, f"bisect over 10 commits uses at most 4 builds ({calls})")
    check(bisect(commits, lambda c: int(c[1:]) >= 3, max_steps=1) is None, "bisect gives up after max_steps")

    # filed once: an injected failure is new the first night and known the second
    state: dict = {"open": {}}
    new, known = decide(state, {"t_slow": ["msvc"]})
    check(new == ["t_slow"] and known == [], "first night: new")
    state["open"]["t_slow"] = {"task": "t01"}
    new, known = decide(state, {"t_slow": ["msvc"]})
    check(new == [] and known == ["t_slow"], "second night: the same failure updates its task")
    txt = task_text("t_slow", ["msvc"], "a" * 40, "b" * 40, "subject", "summary.txt", "2026-11-22")
    check(txt.startswith("[nightly] t_slow fails") and "bbbbbbbb (subject)" in txt, f"task text: {txt}")

    for b in bad:
        print("nightly_triage selftest FAIL: " + b)
    if bad:
        return 1
    print("nightly_triage selftest: ok")
    return 0


def main(argv: list[str]) -> int:
    import argparse
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--sha")
    ap.add_argument("--rc", type=int, default=1)
    ap.add_argument("--logs")
    ap.add_argument("--main")
    ap.add_argument("--wt")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest()
    if not (a.sha and a.logs and a.main):
        ap.error("--sha, --logs and --main are needed")
    return run(a.sha, a.rc, a.logs, a.main, a.wt, a.dry_run)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
