#!/usr/bin/env python3
"""Behaviour declaration and diff check (wr1 t03, "Goldens as computed diffs"; docs/DECISIONS.md "Workflow redesign").

Every commit that touches sim code (engine/ except render, audio, video and ui) carries a `Behaviour:` trailer:

  Behaviour: none                       no simulation result may change
  Behaviour: movement, combat           only these CRC system columns may change (names as in the logs' `systems` line)

`commit` compares the commit's scenario results (tools/goldens/scenario_results.py: every golden-backed headless run) with
its first parent's, per scenario and per system column, over every tick of the full CRC logs:
  - `none`: any difference fails;
  - a list: a difference in a column the list does not name fails (`combined` follows from the others and is ignored;
    `parity` too when a differing or declared system is one of its parts);
  - a sim commit without the trailer fails.
The report names scenario, the first differing tick window and the columns; for an intended change it is the review
artefact that replaces a regenerated golden.

`workers` compares, for this checkout's HEAD, every worker count with the 0-worker run; `compilers` compares the clang
tree's results with the MSVC tree's. Neither needs a stored file. Any difference fails (determinism rule 3).

  python -I tools/goldens/behaviour_check.py commit [REV] [--only REGEX] [--fast] [--report FILE]
  python -I tools/goldens/behaviour_check.py range BASE HEAD [--expand] [--log FILE]   the landing range (t04): every
      commit's declarations together, plus whether each scenario's computed diff agrees with its stored golden changing
  python -I tools/goldens/behaviour_check.py workers [--counts 1,4,15,16] [--only REGEX] [--fast]
  python -I tools/goldens/behaviour_check.py compilers [--only REGEX] [--fast]
  python -I tools/goldens/behaviour_check.py trailer [REV]       prints what the commit declares and whether it needs to
  python -I tools/goldens/behaviour_check.py parity BEFORE.json AFTER.json   parity-harness metrics that moved (t08);
      `commit --parity-before F --parity-after F` adds the same line to the report
  python -I tools/goldens/behaviour_check.py --selftest

Exit 0 when every check passes, 1 on a rejection, 2 on a setup error. `--fast` leaves out scenarios whose ctest entry is
labelled slow. Standard library only.
"""

from __future__ import annotations

import dataclasses
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import convert_goldens as cg  # noqa: E402
import scenario_results as sr  # noqa: E402

ROOT = sr.ROOT
NOT_SIM = ("engine/render/", "engine/audio/", "engine/video/", "engine/ui/")
IGNORED = {"combined"}


# ---------------------------------------------------------------- declaration

def parse_trailer(message: str) -> list[str] | None:
    """None: no trailer; []: `none`; else the declared column names (lower case). The last Behaviour: line wins."""
    found = None
    for line in message.splitlines():
        m = re.match(r"^\s*Behaviour\s*:\s*(.*?)\s*$", line, re.IGNORECASE)
        if m:
            found = m.group(1)
    if found is None:
        return None
    if found.strip().lower() in ("none", "no change", ""):
        return []
    return sorted({p.strip().lower() for p in re.split(r"[,\s]+", found) if p.strip()})


def touches_sim(paths: list[str]) -> bool:
    return any(p.startswith("engine/") and not p.startswith(NOT_SIM) for p in paths)


def commit_info(rev: str, repo: str = ROOT) -> tuple[str, str, str, list[str]]:
    sha = sr.git(repo, "rev-parse", rev).strip()
    parent = sr.git(repo, "rev-parse", f"{sha}^1").strip()
    msg = sr.git(repo, "log", "-1", "--format=%B", sha)
    paths = [p for p in sr.git(repo, "diff", "--name-only", parent, sha).splitlines() if p]
    return sha, parent, msg, paths


# ---------------------------------------------------------------- diff

@dataclasses.dataclass
class Diff:
    same: bool
    columns: list[str]                 # every column that differs on some common tick (systems, parity, combined)
    window: tuple[int, int] = (0, 0)   # first differing tick lies in window[0]..window[1]
    length: str = ""                   # the runs end at different ticks
    systems_changed: list[str] = dataclasses.field(default_factory=list)  # names added or removed from the system list

    def text(self) -> str:
        if self.same:
            return "identical"
        parts = []
        if self.systems_changed:
            parts.append("system list changed: " + ", ".join(self.systems_changed))
        if self.columns:
            parts.append(f"differs in {', '.join(self.columns)} (first difference in ticks {self.window[0]}..{self.window[1]})")
        if self.length:
            parts.append(self.length)
        return "; ".join(parts)


def column_diff(base: cg.CrcLog, new: cg.CrcLog) -> Diff:
    """Every differing column over all ticks both logs store (a checkpoint log stores fewer)."""
    if base.systems != new.systems or base.parity_parts != new.parity_parts:
        changed = sorted(set(base.systems) ^ set(new.systems)) or ["(order or parity parts)"]
        return Diff(False, [], systems_changed=changed)
    nb = {r.tick: r for r in new.records}
    cols: set[str] = set()
    prev, window = None, (0, 0)
    for r in base.records:
        o = nb.get(r.tick)
        if o is None:
            continue
        if r.key() == o.key():
            if not cols:
                prev = r.tick
            continue
        if not cols:
            window = ((prev + 1) if prev is not None else r.tick, r.tick)
        cols.update(base.systems[i] for i in range(len(r.systems)) if r.systems[i] != o.systems[i])
        if r.parity != o.parity:
            cols.add("parity")
        if r.combined != o.combined:
            cols.add("combined")
    length = ""
    if base.records and new.records and base.records[-1].tick != new.records[-1].tick:
        length = f"the runs end at different ticks ({base.records[-1].tick} before, {new.records[-1].tick} after)"
    elif bool(base.records) != bool(new.records):
        length = f"{len(base.records)} records before, {len(new.records)} after"
    order = {n: i for i, n in enumerate(base.systems + ["parity", "combined"])}
    return Diff(not cols and not length, sorted(cols, key=lambda c: order.get(c, 999)), window, length)


@dataclasses.dataclass
class Verdict:
    ok: bool
    reason: str


def judge(declared: list[str] | None, diff: Diff, parity_parts: list[str], sim: bool = True) -> Verdict:
    if diff.same:
        return Verdict(True, "identical")
    if declared is None:
        return Verdict(False, "results changed and the commit has no Behaviour: trailer"
                       if sim else "results changed although the commit touches no sim code and declares nothing")
    if not declared:
        return Verdict(False, "Behaviour: none, but " + diff.text())
    dec = set(declared)
    lower_parts = {p.lower() for p in parity_parts}
    cols = {c.lower() for c in diff.columns} - IGNORED
    if "parity" in cols and ((cols - {"parity"}) & lower_parts or dec & lower_parts):
        cols.discard("parity")
    undeclared = sorted(cols - dec)
    sys_undeclared = sorted(s for s in (x.lower() for x in diff.systems_changed) if s not in dec)
    if undeclared or sys_undeclared:
        what = undeclared + [f"system list ({', '.join(sys_undeclared)})"] * bool(sys_undeclared)
        return Verdict(False, f"undeclared change in {', '.join(what)}: {diff.text()}")
    if diff.length and not cols:
        return Verdict(False, f"only the run length changed ({diff.length}); declare the systems that cause it")
    return Verdict(True, f"declared change: {diff.text()}")


# ---------------------------------------------------------------- parity harness (wr1 t08)

def parity_moves(before: dict, after: dict) -> list[str]:
    """Metrics of two parity-harness report.json files (tools/parity/parity_harness.py, same scenario) whose port value or
    grade differs: the retail check stays the harness's; this only says which tier-2 metrics a change moved."""
    b = {r["metric"]: r for r in before.get("results", [])}
    a = {r["metric"]: r for r in after.get("results", [])}
    out = []
    for m in sorted(set(b) | set(a)):
        rb, ra = b.get(m), a.get(m)
        if rb is None or ra is None:
            out.append(f"{m}: {'new' if rb is None else 'gone'}")
            continue
        if rb.get("port") != ra.get("port") or rb.get("status") != ra.get("status"):
            grade = f" ({rb.get('status')} -> {ra.get('status')})" if rb.get("status") != ra.get("status") else ""
            out.append(f"{m}: port {rb.get('port')} -> {ra.get('port')}{grade}")
    return out


def parity_line(before_path: str, after_path: str) -> str:
    import json
    with open(before_path, encoding="utf-8") as f:
        before = json.load(f)
    with open(after_path, encoding="utf-8") as f:
        after = json.load(f)
    moves = parity_moves(before, after)
    head = (f"parity metrics moved ({before.get('scenario')}, harness on {before.get('commit')} -> {after.get('commit')}): ")
    return head + ("none" if not moves else f"{len(moves)}: " + "; ".join(moves))


# ---------------------------------------------------------------- runs

def load(path: str) -> cg.CrcLog:
    with open(path, encoding="utf-8", newline="") as f:
        return cg.parse(f.read())


def pick(scenarios: list[sr.Scenario], only: str | None, fast: bool, slow_tests: set[str]) -> list[sr.Scenario]:
    out = [s for s in scenarios if not only or re.search(only, s.name) or re.search(only, s.test)]
    if fast:
        out = [s for s in out if s.test not in slow_tests]
    return out


def slow_tests(build_dir: str) -> set[str]:
    import json
    r = subprocess.run([sr.find_ctest(), "--test-dir", build_dir, "--show-only=json-v1"], capture_output=True, text=True)
    out = set()
    for t in json.loads(r.stdout).get("tests", []):
        for p in t.get("properties", []):
            if p.get("name") == "LABELS" and "slow" in (p.get("value") or []):
                out.add(t["name"])
    return out


def check_commit(rev: str, scen: list[sr.Scenario], cache: sr.Cache, build_dir: str, compiler: str) -> tuple[bool, list[str]]:
    sha, parent, msg, paths = commit_info(rev)
    declared = parse_trailer(msg)
    sim = touches_sim(paths)
    lines = [f"behaviour check {sha[:8]} against its parent {parent[:8]}: "
             + ("declares none" if declared == [] else f"declares {', '.join(declared)}" if declared else "no Behaviour: trailer")
             + ("; touches sim code" if sim else "; touches no sim code")]
    ok = True
    if sim and declared is None:
        ok = False
        lines.append("  FAIL the commit touches sim code and has no Behaviour: trailer (none, or the changed system columns)")
    head = sr.git(ROOT, "rev-parse", "HEAD").strip()
    for s in scen:
        try:
            if head == sha and not sr.source_dirty(ROOT):
                new_p = sr.result_here(cache, s, ROOT, build_dir, compiler)
            else:
                new_p = sr.result_for(cache, s, sha, compiler)
            old_p = sr.result_for(cache, s, parent, compiler)
        except RuntimeError as e:
            ok = False
            lines.append(f"  ERROR {s.name}: {e}")
            continue
        old, new = load(old_p), load(new_p)
        d = column_diff(old, new)
        v = judge(declared, d, old.parity_parts, sim)
        ok &= v.ok
        lines.append(f"  {'PASS' if v.ok else 'FAIL'} {s.name}: {v.reason}")
    return ok, lines


def range_declarations(base: str, head: str, repo: str = ROOT) -> tuple[list[str] | None, list[str]]:
    """The union of the Behaviour: trailers of the non-merge commits in base..head, and the sim commits without one.
    None when no commit declares anything."""
    shas = [s for s in sr.git(repo, "rev-list", "--no-merges", f"{base}..{head}").split() if s]
    declared: set[str] | None = None
    missing = []
    for sha in shas:
        msg = sr.git(repo, "log", "-1", "--format=%B", sha)
        paths = [p for p in sr.git(repo, "diff-tree", "--no-commit-id", "--name-only", "-r", sha).splitlines() if p]
        d = parse_trailer(msg)
        if d is None:
            if touches_sim(paths):
                missing.append(sha[:8])
            continue
        declared = (declared or set()) | set(d)
    return (sorted(declared) if declared is not None else None), missing


def agreement(diff_same: bool, golden_changed: bool) -> str:
    """Expand phase (wr1 t04): a stored golden is regenerated exactly when its scenario's result changes, so the computed
    diff and the stored-golden change must coincide. Returns '' when they agree, else what disagrees."""
    if diff_same and golden_changed:
        return "stored golden changed but the computed results are identical"
    if not diff_same and not golden_changed:
        return "computed results differ but the stored golden did not change"
    return ""


def check_range(base: str, head: str, scen: list[sr.Scenario], cache: sr.Cache, build_dir: str, compiler: str,
                log_path: str | None) -> tuple[bool, list[str]]:
    """Landing check, expand phase: base..head (the landing range) per scenario, the range's declarations judged, and the
    agreement with the stored goldens recorded. Report only: the stored goldens stay authoritative until wr1 t07."""
    base_sha = sr.git(ROOT, "rev-parse", base).strip()
    head_sha = sr.git(ROOT, "rev-parse", head).strip()
    declared, missing = range_declarations(base_sha, head_sha)
    lines = [f"behaviour check {base_sha[:8]}..{head_sha[:8]}: "
             + ("declares nothing" if declared is None else "declares none" if not declared else "declares " + ", ".join(declared))
             + (f"; sim commits without a Behaviour: trailer: {', '.join(missing)}" if missing else "")]
    ok, agree, disagree = True, 0, 0
    for s in scen:
        try:
            old_p = sr.result_for(cache, s, base_sha, compiler)
            new_p = sr.result_here(cache, s, ROOT, build_dir, compiler) if head_sha == sr.git(ROOT, "rev-parse", "HEAD").strip() \
                else sr.result_for(cache, s, head_sha, compiler)
        except RuntimeError as e:
            ok = False
            lines.append(f"  ERROR {s.name}: {e}")
            continue
        old, new = load(old_p), load(new_p)
        d = column_diff(old, new)
        v = judge(declared, d, old.parity_parts, True)
        changed = bool(sr.git(ROOT, "diff", "--name-only", base_sha, head_sha, "--", s.golden).strip())
        bug = agreement(d.same, changed)
        agree += not bug
        disagree += bool(bug)
        ok &= v.ok
        lines.append(f"  {'PASS' if v.ok else 'FAIL'} {s.name}: {v.reason}" + (f"  [CHECKER BUG? {bug}]" if bug else ""))
    summary = (f"{time.strftime('%Y-%m-%dT%H:%M:%S')} {base_sha[:8]}..{head_sha[:8]} scenarios {len(scen)} agree {agree} "
               f"disagree {disagree} declaration {'PASS' if ok else 'FAIL'}" + (f" missing-trailer {','.join(missing)}" if missing else ""))
    lines.append("  landing record: " + summary)
    if log_path:
        os.makedirs(os.path.dirname(log_path), exist_ok=True)
        with open(log_path, "a", encoding="utf-8") as f:
            f.write(summary + "\n")
    return ok and not disagree, lines


def check_workers(counts: list[int], scen: list[sr.Scenario], cache: sr.Cache, build_dir: str, compiler: str) -> tuple[bool, list[str]]:
    lines = [f"worker-count check at HEAD {sr.git(ROOT, 'rev-parse', '--short', 'HEAD').strip()}: workers {counts} against 0"]
    ok = True
    for s in scen:
        try:
            base = load(sr.result_here(cache, s, ROOT, build_dir, compiler, workers=0))
            for n in counts:
                d = column_diff(base, load(sr.result_here(cache, s, ROOT, build_dir, compiler, workers=n)))
                ok &= d.same
                lines.append(f"  {'PASS' if d.same else 'FAIL'} {s.name} w{n}: {d.text()}")
        except RuntimeError as e:
            ok = False
            lines.append(f"  ERROR {s.name}: {e}")
    return ok, lines


def check_compilers(scen: list[sr.Scenario], cache: sr.Cache) -> tuple[bool, list[str]]:
    lines = [f"compiler check at HEAD {sr.git(ROOT, 'rev-parse', '--short', 'HEAD').strip()}: clang tree against MSVC tree"]
    ok = True
    msvc, clang = sr.default_build("msvc"), sr.default_build("clang")
    for s in scen:
        try:
            d = column_diff(load(sr.result_here(cache, s, ROOT, msvc, "msvc")), load(sr.result_here(cache, s, ROOT, clang, "clang")))
        except RuntimeError as e:
            ok = False
            lines.append(f"  ERROR {s.name}: {e}")
            continue
        ok &= d.same
        lines.append(f"  {'PASS' if d.same else 'FAIL'} {s.name}: {d.text()}")
    return ok, lines


# ---------------------------------------------------------------- selftest

def _log(systems, rows, parity_parts=("a",), checkpoint=0):
    log = cg.CrcLog(systems=list(systems), parity_parts=list(parity_parts), checkpoint=checkpoint)
    for tick, vals in rows:
        par = sum(vals[systems.index(p)] for p in parity_parts if p in systems)
        log.records.append(cg.Record(tick, sum(vals) + par, par, list(vals)))
    return log


def selftest() -> int:
    bad = []

    def check(c, what):
        if not c:
            bad.append(what)

    S = ["movement", "combat", "ai"]
    base = _log(S, [(t, [t, 2 * t, 3 * t]) for t in range(0, 200)], parity_parts=("movement",))

    def changed(col, from_tick, end=200):
        rows = []
        for t in range(0, end):
            v = [t, 2 * t, 3 * t]
            if t >= from_tick:
                v[S.index(col)] += 1
            rows.append((t, v))
        return _log(S, rows, parity_parts=("movement",))

    # trailer parsing
    check(parse_trailer("x\n\nBehaviour: none\n") == [], "none parses to []")
    check(parse_trailer("x\n\nBehaviour: Movement, combat\nCo-Authored-By: a") == ["combat", "movement"], "list parses")
    check(parse_trailer("x\n") is None, "no trailer is None")
    check(touches_sim(["engine/tactical/a.cpp"]) and not touches_sim(["engine/render/a.cpp", "docs/x.md"]), "sim paths")

    same = column_diff(base, changed("ai", 10_000))
    check(same.same, "identical logs compare same")
    check(judge([], same, ["movement"]).ok and judge(None, same, ["movement"]).ok, "identical passes with or without a trailer")

    d_combat = column_diff(base, changed("combat", 70))
    check(d_combat.columns == ["combat", "combined"] and d_combat.window == (70, 70), f"combat diff: {d_combat}")
    check(not judge([], d_combat, ["movement"]).ok, "rejected: Behaviour: none with a sim change")
    check(not judge(None, d_combat, ["movement"]).ok, "rejected: no trailer with a change")
    check(judge(["combat"], d_combat, ["movement"]).ok, "accepted: declared column")
    v = judge(["movement"], d_combat, ["movement"])
    check(not v.ok and "combat" in v.reason, f"rejected: undeclared column named ({v.reason})")

    # two columns, the second only late: both found
    late = changed("combat", 70)
    for r in late.records:
        if r.tick >= 150:
            r.systems[S.index("ai")] += 5
            r.combined += 5
    d_two = column_diff(base, late)
    check(d_two.columns == ["combat", "ai", "combined"], f"a late second column is found: {d_two.columns}")
    check(not judge(["combat"], d_two, ["movement"]).ok, "rejected: late undeclared column")
    check(judge(["combat", "ai"], d_two, ["movement"]).ok, "accepted: both declared")

    # parity follows a parity part
    d_mov = column_diff(base, changed("movement", 5))
    check("parity" in d_mov.columns, "a parity part moves the parity column")
    check(judge(["movement"], d_mov, ["movement"]).ok, "accepted: parity explained by a declared part")

    # length and system list
    d_len = column_diff(base, _log(S, [(t, [t, 2 * t, 3 * t]) for t in range(0, 150)], parity_parts=("movement",)))
    check(not d_len.same and d_len.length and not judge(["combat"], d_len, ["movement"]).ok, "rejected: only the length changed")
    d_sys = column_diff(base, _log(S + ["fog"], [(0, [0, 0, 0, 0])], parity_parts=("movement",)))
    check(d_sys.systems_changed == ["fog"], f"system list change named: {d_sys.systems_changed}")
    check(not judge([], d_sys, ["movement"]).ok and judge(["fog"], d_sys, ["movement"]).ok, "system list change needs its name")

    # checkpoint log against a full log: compared on the stored ticks
    cp = cg.checkpointed(changed("combat", 70), 64)
    d_cp = column_diff(base, cp)
    check(d_cp.columns == ["combat", "combined"] and d_cp.window == (65, 128), f"checkpoint window: {d_cp}")

    # injected worker-count and compiler differences are plain diffs of two runs of one commit: any column fails
    w4 = changed("ai", 64)
    check(not column_diff(base, w4).same, "an injected worker-count difference is caught")
    clang = changed("movement", 199)
    check(not column_diff(base, clang).same and column_diff(base, clang).window == (199, 199), "an injected compiler difference is caught")

    # parity harness: an injected rule change that shifts a tracked metric shows, an unchanged one does not
    rep = {"scenario": "hotm_galactic", "commit": "aaaa", "results": [
        {"metric": "build.ticks[X@NABOO]", "port": 480, "status": "PASS"},
        {"metric": "outcome.planets_final[Rebel]", "port": 5, "status": "PASS"}]}
    shifted = {"scenario": "hotm_galactic", "commit": "bbbb", "results": [
        {"metric": "build.ticks[X@NABOO]", "port": 480, "status": "PASS"},
        {"metric": "outcome.planets_final[Rebel]", "port": 4, "status": "FAIL"}]}
    moves = parity_moves(rep, shifted)
    check(moves == ["outcome.planets_final[Rebel]: port 5 -> 4 (PASS -> FAIL)"], f"parity metric move reported: {moves}")
    check(parity_moves(rep, rep) == [], "no parity move for the same report")

    # expand phase: computed diff and stored-golden change must coincide
    check(agreement(True, False) == "" and agreement(False, True) == "", "agreement when both or neither change")
    check("identical" in agreement(True, True) and "did not change" in agreement(False, False), "disagreement both ways")

    for b in bad:
        print("behaviour_check selftest FAIL: " + b)
    if bad:
        return 1
    print("behaviour_check selftest: ok")
    return 0


# ---------------------------------------------------------------- command line

def main(argv: list[str]) -> int:
    import argparse
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("command", nargs="?", choices=("commit", "range", "workers", "compilers", "trailer", "parity"))
    ap.add_argument("--log", help="range: append the one-line landing record to this file")
    ap.add_argument("--expand", action="store_true", help="range: report only, exit 0 (wr1 t04 expand phase)")
    ap.add_argument("rev", nargs="?", default="HEAD")
    ap.add_argument("rev2", nargs="?", help="parity: the after report.json (rev is the before one)")
    ap.add_argument("--parity-before", help="commit: the parent build's parity-harness report.json")
    ap.add_argument("--parity-after", help="commit: the commit build's parity-harness report.json")
    ap.add_argument("--only")
    ap.add_argument("--fast", action="store_true", help="leave out scenarios whose ctest entry is labelled slow")
    ap.add_argument("--counts", default="1,4,15,16")
    ap.add_argument("--compiler", default="msvc")
    ap.add_argument("--build")
    ap.add_argument("--report", help="also write the report to this file")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest()
    if a.command == "trailer":
        sha, parent, msg, paths = commit_info(a.rev)
        d = parse_trailer(msg)
        print(f"{sha[:8]}: touches sim code: {touches_sim(paths)}; declares: "
              + ("nothing" if d is None else "none" if not d else ", ".join(d)))
        return 0 if (d is not None or not touches_sim(paths)) else 1
    if a.command == "parity":
        if not a.rev2:
            ap.error("parity BEFORE.json AFTER.json")
        print(parity_line(a.rev, a.rev2))
        return 0
    if not a.command:
        ap.error("a command is needed")
    build = a.build or sr.default_build(a.compiler)
    try:
        scen = pick(sr.discover(build), a.only, a.fast, slow_tests(build) if a.fast else set())
    except RuntimeError as e:
        print(f"behaviour_check: {e}", file=sys.stderr)
        return 2
    cache = sr.Cache.default()
    if a.command == "commit":
        ok, lines = check_commit(a.rev, scen, cache, build, a.compiler)
        if a.parity_before and a.parity_after:
            lines.append("  " + parity_line(a.parity_before, a.parity_after))
    elif a.command == "range":
        if not a.rev2:
            ap.error("range BASE HEAD")
        if a.log == "auto":
            a.log = os.path.join(sr.main_checkout(ROOT), "logs", "behaviour_landings.txt")
        ok, lines = check_range(a.rev, a.rev2, scen, cache, build, a.compiler, a.log)
        if a.expand:
            lines.append("RESULT " + ("PASS" if ok else "FAIL") + " (expand phase: report only, the stored goldens decide)")
            print("\n".join(lines))
            return 0
    elif a.command == "workers":
        ok, lines = check_workers([int(x) for x in a.counts.split(",") if x], scen, cache, build, a.compiler)
    else:
        ok, lines = check_compilers(scen, cache)
    lines.append("RESULT " + ("PASS" if ok else "FAIL"))
    text = "\n".join(lines)
    print(text)
    if a.report:
        with open(a.report, "w", encoding="utf-8") as f:
            f.write(text + "\n")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
