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

A scenario's result is a folder (tools/goldens/scenario_results.py). Each file in it is compared by its kind: a CRC log
(`ALAMO_CRC`) per system column as above; any other text or binary file byte for byte, a difference charged to the
scenario's `system` column (tools/goldens/scenarios.json; the first differing line is named); a PNG screenshot with the
tolerances of tools/visual/png_diff.py, reported but never judged (screens are not simulation results). A scenario that
needs what this machine lacks (game data, a GPU) is reported as SKIP and does not fail the check.

`workers` compares, for this checkout's HEAD, every worker count with the 0-worker run; `compilers` compares the clang
tree's results with the MSVC tree's. Neither needs a stored file. Any difference fails (determinism rule 3).

  python -I tools/goldens/behaviour_check.py commit [REV] [--only REGEX] [--fast] [--report FILE]
  python -I tools/goldens/behaviour_check.py range BASE HEAD [--expand] [--log FILE]   the landing range (t04): every
      commit's declarations together, plus whether each scenario's computed diff agrees with its stored golden changing
  python -I tools/goldens/behaviour_check.py workers [--counts 1,4,15,16] [--only REGEX] [--fast]
  python -I tools/goldens/behaviour_check.py compilers [--only REGEX] [--fast]
  python -I tools/goldens/behaviour_check.py goldens [--only REGEX] [--fast]   wr1 t05 / t07: this checkout's results
      against the tracked goldens they replace (a scenario that does not reproduce its golden covers nothing)
  python -I tools/goldens/behaviour_check.py nightly --history FILE [--build DIR]   the nightly gate's run of the
      scenarios marked `nightly` (GPU smoke, the long galactic AI campaign): range from the last gated night in FILE
      (tools/gate_scheduled.cmd's history) to HEAD, report only until wr1 t07, logged to logs/behaviour_nightly.txt

Scenarios marked `nightly` in tools/goldens/scenarios.json are left out of commit / range (the landing check) unless
--nightly is given.
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

sys.path.insert(0, os.path.join(os.path.dirname(HERE), "visual"))
import png_diff  # noqa: E402

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
    summary: str = ""                  # set for a result folder of several files: the per-file description
    notes: list[str] = dataclasses.field(default_factory=list)  # report-only differences (screenshots)

    def text(self) -> str:
        note = ("; screenshots (report only): " + "; ".join(self.notes)) if self.notes else ""
        if self.same:
            return "identical" + note
        if self.summary:
            return self.summary + note
        parts = []
        if self.systems_changed:
            parts.append("system list changed: " + ", ".join(self.systems_changed))
        if self.columns:
            parts.append(f"differs in {', '.join(self.columns)} (first difference in ticks {self.window[0]}..{self.window[1]})")
        if self.length:
            parts.append(self.length)
        return "; ".join(parts) + note


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


def _is_crc(path: str) -> bool:
    with open(path, "rb") as f:
        return f.read(9) == b"ALAMO_CRC"


def _first_line_diff(a: bytes, b: bytes) -> int:
    la, lb = a.splitlines(), b.splitlines()
    for i, (x, y) in enumerate(zip(la, lb)):
        if x != y:
            return i + 1
    return min(len(la), len(lb)) + 1


def _drop_lines(data: bytes, patterns: tuple) -> bytes:
    """data without the lines (CR stripped) that match one of the scenario's ignore regexes."""
    rx = [re.compile(p.encode()) for p in patterns]
    keep = [ln.rstrip(b"\r") for ln in data.split(b"\n") if not any(r.search(ln) for r in rx)]
    return b"\n".join(keep)


def compare_results(scenario: sr.Scenario, old_dir: str, new_dir: str, only: set | None = None) -> tuple[Diff, list[str]]:
    """(diff, parity parts) of two result folders of one scenario, file by file (module docstring). A headless scenario's
    single result.crc gives exactly column_diff's Diff."""
    of, nf = sr.result_files(old_dir), sr.result_files(new_dir)
    if only is not None:  # a subset of outputs (those the parent has, or the ones a golden covers); PNGs stay
        of = {r: p for r, p in of.items() if r in only or r.lower().endswith(".png")}
        nf = {r: p for r, p in nf.items() if r in only or r.lower().endswith(".png")}
    per_output = dict(scenario.output_systems)
    default_charge = (scenario.system or scenario.name).lower()
    parts: list[str] = []
    per_file: list[tuple[str, Diff]] = []
    notes: list[str] = []
    for rel in sorted(set(of) | set(nf)):
        a, b = of.get(rel), nf.get(rel)
        charge = per_output.get(rel, default_charge).lower()
        if rel.lower().endswith(".png"):
            if a is None or b is None:
                notes.append(f"{rel} only {'after' if a is None else 'before'}")
                continue
            frac, worst, size = png_diff.compare_images(a, b, 4, scale=320)
            if size or frac > 0.005:
                notes.append(f"{rel} differs ({size or f'{frac:.2%} of pixels, max {worst}'})")
            continue
        if a is None or b is None:
            per_file.append((rel, Diff(False, [charge], length=f"written {'only after' if a is None else 'only before'}")))
            continue
        if _is_crc(a) and _is_crc(b):
            ol, nl = load(a), load(b)
            parts = parts or list(ol.parity_parts)
            d = column_diff(ol, nl)
        else:
            with open(a, "rb") as fa, open(b, "rb") as fb:
                ba, bb = fa.read(), fb.read()
            if b"\0" not in ba[:4096] and b"\0" not in bb[:4096]:  # text: line endings do not count
                ba, bb = ba.replace(b"\r\n", b"\n"), bb.replace(b"\r\n", b"\n")
                if scenario.ignore:
                    ba, bb = _drop_lines(ba, scenario.ignore), _drop_lines(bb, scenario.ignore)
            if ba == bb:
                continue
            d = Diff(False, [charge], summary=f"differs from line {_first_line_diff(ba, bb)} (charged to {charge})")
        if not d.same:
            per_file.append((rel, d))
    if not per_file:
        return Diff(True, [], notes=notes), parts
    if len(per_file) == 1 and {r for r in set(of) | set(nf) if not r.lower().endswith(".png")} == {"result.crc"}:
        d = per_file[0][1]
        d.notes = notes
        return d, parts
    cols: list[str] = []
    systems: list[str] = []
    for _, d in per_file:
        cols += [c for c in d.columns if c not in cols]
        systems += [x for x in d.systems_changed if x not in systems]
    first_crc = next((d for _, d in per_file if d.window != (0, 0)), None)
    length = "; ".join(f"{rel}: {d.length}" for rel, d in per_file if d.length and not d.columns)
    return Diff(False, cols, first_crc.window if first_crc else (0, 0), length, systems,
                summary="; ".join(f"{rel}: {d.text()}" for rel, d in per_file), notes=notes), parts


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


def pick(scenarios: list[sr.Scenario], only: str | None, fast: bool, slow_tests: set[str],
         nightly: str = "all") -> list[sr.Scenario]:
    """nightly: all, exclude (the landing check) or only (the nightly gate)."""
    out = [s for s in scenarios if not only or re.search(only, s.name) or re.search(only, s.test)]
    if nightly == "exclude":
        out = [s for s in out if not s.nightly]
    elif nightly == "only":
        out = [s for s in out if s.nightly]
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
        shared = sr.parent_outputs(ROOT, parent, sha, s)
        if shared is None:
            lines.append(f"  SKIP {s.name}: new scenario or run (not in {parent[:8]}'s registry): no parent result to compare")
            continue
        try:
            if head == sha and not sr.source_dirty(ROOT):
                new_p = sr.result_here(cache, s, ROOT, build_dir, compiler)
            else:
                new_p = sr.result_for(cache, s, sha, compiler)
            old_p = sr.result_for(cache, s, parent, compiler)
        except RuntimeError as e:
            ok &= _skip_or_error(s, e, lines)
            continue
        d, parts = compare_results(s, old_p, new_p, shared if s.kind != "headless" else None)
        v = judge(declared, d, parts, sim)
        ok &= v.ok
        lines.append(f"  {'PASS' if v.ok else 'FAIL'} {s.name}: {v.reason}{_new_outputs(s, shared)}{_run_exits(old_p, new_p)}")
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
    skipped_new = 0
    for s in scen:
        shared = sr.parent_outputs(ROOT, base_sha, head_sha, s)
        if shared is None:
            skipped_new += 1
            lines.append(f"  SKIP {s.name}: new scenario or run (not in {base_sha[:8]}'s registry): no base result to compare")
            continue
        try:
            old_p = sr.result_for(cache, s, base_sha, compiler)
            new_p = sr.result_here(cache, s, ROOT, build_dir, compiler) if head_sha == sr.git(ROOT, "rev-parse", "HEAD").strip() \
                else sr.result_for(cache, s, head_sha, compiler)
        except RuntimeError as e:
            ok &= _skip_or_error(s, e, lines)
            continue
        sub = shared if s.kind != "headless" else None
        d, parts = compare_results(s, old_p, new_p, sub)
        v = judge(declared, d, parts, True)
        ok &= v.ok
        note = _new_outputs(s, shared) + _run_exits(old_p, new_p)
        if not s.agreement:
            lines.append(f"  {'PASS' if v.ok else 'FAIL'} {s.name}: {v.reason}{note} (no golden agreement check)")
            continue
        # agreement compares only the outputs that replace a tracked golden (extra outputs replace none)
        covered = {o for o, _ in zip(s.outputs or ("result.crc",), s.covers())}
        covered = covered & shared if sub is not None else covered
        d_cov, _ = compare_results(s, old_p, new_p, covered) if s.kind != "headless" else (d, parts)
        changed = bool(sr.git(ROOT, "diff", "--name-only", base_sha, head_sha, "--", *s.covers()).strip())
        bug = agreement(d_cov.same, changed)
        agree += not bug
        disagree += bool(bug)
        lines.append(f"  {'PASS' if v.ok else 'FAIL'} {s.name}: {v.reason}{note}" + (f"  [CHECKER BUG? {bug}]" if bug else ""))
    summary = (f"{time.strftime('%Y-%m-%dT%H:%M:%S')} {base_sha[:8]}..{head_sha[:8]} scenarios {agree + disagree} agree {agree} "
               f"disagree {disagree} declaration {'PASS' if ok else 'FAIL'}" + (f" missing-trailer {','.join(missing)}" if missing else "")
               + (f" new {skipped_new}" if skipped_new else ""))
    lines.append("  landing record: " + summary)
    if log_path:
        os.makedirs(os.path.dirname(log_path), exist_ok=True)
        with open(log_path, "a", encoding="utf-8") as f:
            f.write(summary + "\n")
    return ok and not disagree, lines


def _run_exits(*dirs: str) -> str:
    """' (runs exited N / M)' when a stored run's own checks failed (its outputs were still compared), else ''."""
    import json
    codes = []
    for d in dirs:
        try:
            with open(os.path.join(d, "meta.json"), encoding="utf-8") as f:
                codes.append(int(json.load(f).get("exit") or 0))
        except (OSError, ValueError, TypeError):
            codes.append(0)
    return f" (runs exited {' / '.join(map(str, codes))}: their own checks failed)" if any(codes) else ""


def _skip_or_error(s: sr.Scenario, e: RuntimeError, lines: list[str]) -> bool:
    """Report a scenario that could not run: SKIP (what it needs is missing here; passes) or ERROR (fails)."""
    if str(e).startswith("skipped"):
        lines.append(f"  SKIP {s.name}: {e}")
        return True
    lines.append(f"  ERROR {s.name}: {e}")
    return False


def last_gated_sha(history: str, head: str) -> str | None:
    """The newest commit in the nightly gate's history file (lines '... <sha> dev gate exit N', the sha short or full)
    other than head; as written there (rev-parse it)."""
    found = None
    try:
        with open(history, encoding="utf-8", errors="replace") as f:
            for line in f:
                m = re.search(r"\b([0-9a-f]{7,40}) dev gate exit ", line)
                if m and not head.startswith(m.group(1)):
                    found = m.group(1)
    except OSError:
        return None
    return found


def check_goldens(scen: list[sr.Scenario], cache: sr.Cache, build_dir: str, compiler: str) -> tuple[bool, list[str]]:
    """Each scenario's HEAD result against the tracked files it covers (wr1 t05 acceptance, t07 precondition). A CRC v1
    result is compared on the checkpoints a v2 golden keeps; a tracked file is laid out under the output name it
    replaces, so the compare rules are the parent-versus-commit ones."""
    import shutil
    import tempfile
    lines = [f"golden coverage at HEAD {sr.git(ROOT, 'rev-parse', '--short', 'HEAD').strip()}: results against tracked goldens"]
    ok = True
    for s in scen:
        try:
            res = sr.result_here(cache, s, ROOT, build_dir, compiler)
        except RuntimeError as e:
            ok &= _skip_or_error(s, e, lines)
            continue
        tracked = tempfile.mkdtemp(prefix="goldens_")
        try:
            outs = (s.outputs or ("result.crc",))[:len(s.covers())]  # extra outputs replace no golden
            for rel, g in zip(outs, s.covers()):
                dst = os.path.join(tracked, rel)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copyfile(os.path.join(ROOT, g), dst)
            # compare only the files that have a tracked copy (a result may hold more: screenshots, the run's CRC)
            view = tempfile.mkdtemp(prefix="result_")
            for rel in outs:
                dst = os.path.join(view, rel)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copyfile(os.path.join(res, rel), dst)
            d, _ = compare_results(s, tracked, view)
            shutil.rmtree(view, ignore_errors=True)
        finally:
            shutil.rmtree(tracked, ignore_errors=True)
        ok &= d.same
        lines.append(f"  {'PASS' if d.same else 'FAIL'} {s.name} ({len(s.covers())} file(s)): {d.text()}")
    return ok, lines


def _new_outputs(s: sr.Scenario, shared: set | None) -> str:
    """' (new outputs, not compared: ...)' for outputs the parent's definition does not list."""
    if s.kind == "headless" or shared is None:
        return ""
    new = [o for o in s.outputs if o not in shared]
    return f" (new outputs, not compared: {', '.join(new)})" if new else ""


def check_workers(counts: list[int], scen: list[sr.Scenario], cache: sr.Cache, build_dir: str, compiler: str) -> tuple[bool, list[str]]:
    lines = [f"worker-count check at HEAD {sr.git(ROOT, 'rev-parse', '--short', 'HEAD').strip()}: workers {counts} against 0"]
    ok = True
    for s in scen:
        if s.kind != "headless" and not s.workers_arg:
            lines.append(f"  SKIP {s.name}: the test runs its own worker counts and compares them itself")
            continue
        try:
            base = sr.result_here(cache, s, ROOT, build_dir, compiler, workers=0)
            for n in counts:
                d, _ = compare_results(s, base, sr.result_here(cache, s, ROOT, build_dir, compiler, workers=n))
                ok &= d.same
                lines.append(f"  {'PASS' if d.same else 'FAIL'} {s.name} w{n}: {d.text()}")
        except RuntimeError as e:
            ok &= _skip_or_error(s, e, lines)
    return ok, lines


def check_compilers(scen: list[sr.Scenario], cache: sr.Cache) -> tuple[bool, list[str]]:
    lines = [f"compiler check at HEAD {sr.git(ROOT, 'rev-parse', '--short', 'HEAD').strip()}: clang tree against MSVC tree"]
    ok = True
    msvc, clang = sr.default_build("msvc"), sr.default_build("clang")
    for s in scen:
        try:
            d, _ = compare_results(s, sr.result_here(cache, s, ROOT, msvc, "msvc"), sr.result_here(cache, s, ROOT, clang, "clang"))
        except RuntimeError as e:
            ok &= _skip_or_error(s, e, lines)
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

    # result folders (wr1 t05): CRC files by column, text charged to the scenario's system, PNGs report only
    import shutil
    import tempfile
    tmp = tempfile.mkdtemp(prefix="bc_selftest_")
    try:
        def folder(name, files):
            d = os.path.join(tmp, name)
            for rel, data in files.items():
                os.makedirs(os.path.dirname(os.path.join(d, rel)), exist_ok=True)
                with open(os.path.join(d, rel), "wb") as f:
                    f.write(data)
            return d

        ws = sr.Scenario("ai_x", "g/a.txt", "t", ("x",), "<build>", None, "write", ("g/a.txt", "g/b.crc"),
                         ("a.txt", "b.crc"), "battle.ai")
        o = folder("o", {"a.txt": b"frame 0 1\nframe 150 2\n", "b.crc": b"ALAMO_CRC 1\nsystems 1 x\nparity 0\n0 1 0 1\n"})
        n_same = folder("n_same", {"a.txt": b"frame 0 1\nframe 150 2\n", "b.crc": b"ALAMO_CRC 1\nsystems 1 x\nparity 0\n0 1 0 1\n"})
        n_txt = folder("n_txt", {"a.txt": b"frame 0 1\nframe 150 3\n", "b.crc": b"ALAMO_CRC 1\nsystems 1 x\nparity 0\n0 1 0 1\n"})
        n_crc = folder("n_crc", {"a.txt": b"frame 0 1\nframe 150 2\n", "b.crc": b"ALAMO_CRC 1\nsystems 1 x\nparity 0\n0 2 0 2\n"})
        n_gone = folder("n_gone", {"b.crc": b"ALAMO_CRC 1\nsystems 1 x\nparity 0\n0 1 0 1\n"})
        d, _ = compare_results(ws, o, n_same)
        check(d.same, f"identical folders compare same: {d.text()}")
        wi = dataclasses.replace(ws, ignore=(r"^# (?!t=)",))
        oi = folder("oi", {"a.txt": b"# header\r\n# elapsed 812 ms\n# t=1\nx\n"})
        ni = folder("ni", {"a.txt": b"# header\n# elapsed 977 ms\n# t=1\nx\n"})
        nj = folder("nj", {"a.txt": b"# header\n# elapsed 977 ms\n# t=2\nx\n"})
        check(compare_results(wi, oi, ni)[0].same, "ignored lines (wall clock) and CR do not count")
        check(not compare_results(wi, oi, nj)[0].same, "a kept comment line still counts")
        check(compare_results(ws, folder("crlf", {"a.txt": b"frame 0 1\r\nframe 150 2\r\n",
                                                  "b.crc": b"ALAMO_CRC 1\nsystems 1 x\nparity 0\n0 1 0 1\n"}), o)[0].same,
              "CRLF and LF text compare the same")
        check(not compare_results(dataclasses.replace(ws, outputs=("a.rec",)), folder("b1", {"a.rec": b"\0\1\r\n"}),
                                  folder("b2", {"a.rec": b"\0\1\n"}))[0].same, "binary files compare byte for byte")
        d, _ = compare_results(ws, o, n_txt)
        check(not d.same and d.columns == ["battle.ai"] and "line 2" in d.text(), f"text diff charged to the system: {d.text()}")
        check(not judge([], d, []).ok and judge(["battle.ai"], d, []).ok and not judge(["combat"], d, []).ok,
              "a text diff needs its system declared")
        wsys = dataclasses.replace(ws, output_systems=(("a.txt", "commands"),))
        check(compare_results(wsys, o, n_txt)[0].columns == ["commands"], "a per-output system overrides the scenario's")
        check(compare_results(ws, o, n_txt, {"b.crc"})[0].same, "only: outputs outside the subset are not compared")
        d, _ = compare_results(ws, o, n_crc)
        check(not d.same and d.columns == ["x", "combined"] and "b.crc:" in d.text(), f"CRC file in a folder by column: {d.text()}")
        d, _ = compare_results(ws, o, n_gone)
        check(not d.same and "a.txt" in d.text() and "battle.ai" in d.columns, f"an output written only before: {d.text()}")
        # PNGs: a differing screenshot is a note, not a difference
        def png(rgba):
            pth = os.path.join(tmp, "p.png")
            png_diff.write_png(pth, 8, 8, [bytes(rgba) * 8 for _ in range(8)])
            with open(pth, "rb") as f:
                return f.read()
        po = folder("po", {"result.crc": b"ALAMO_CRC 1\nsystems 1 x\nparity 0\n0 1 0 1\n", "s/a.png": png((0, 0, 0, 255))})
        pn = folder("pn", {"result.crc": b"ALAMO_CRC 1\nsystems 1 x\nparity 0\n0 1 0 1\n", "s/a.png": png((200, 0, 0, 255))})
        d, _ = compare_results(ws, po, pn)
        check(d.same and d.notes and "s/a.png" in d.text(), f"a screenshot difference is reported only: {d.text()}")
        hs = sr.Scenario("h", "g/h.crc", "t", ("x",), "<build>", None)
        ho = folder("ho", {"result.crc": b"ALAMO_CRC 1\nsystems 1 x\nparity 0\n0 1 0 1\n1 1 0 1\n"})
        hn = folder("hn", {"result.crc": b"ALAMO_CRC 1\nsystems 1 x\nparity 0\n0 1 0 1\n1 2 0 2\n"})
        d, _ = compare_results(hs, ho, hn)
        check(d.text() == "differs in x, combined (first difference in ticks 1..1)", f"headless text unchanged: {d.text()}")
        lines: list[str] = []
        check(_skip_or_error(hs, RuntimeError("skipped, needs ALAMO_FOC_DATA"), lines) and lines[0].startswith("  SKIP"),
              "a missing need is a SKIP that passes")
        check(not _skip_or_error(hs, RuntimeError("build failed"), lines), "any other run error fails")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    # nightly scenarios: left out of the landing check, alone in the nightly run
    sx = [sr.Scenario("a", "g", "t", ("x",), "<build>", None), sr.Scenario("b", "g", "t", ("x",), "<build>", None, nightly=True)]
    check([x.name for x in pick(sx, None, False, set(), "exclude")] == ["a"], "landing check leaves nightly scenarios out")
    check([x.name for x in pick(sx, None, False, set(), "only")] == ["b"], "nightly run takes only them")
    import tempfile as _tf
    with _tf.TemporaryDirectory() as td:
        hist = os.path.join(td, "h.txt")
        with open(hist, "w") as f:
            f.write("Sat 11/20/2026 2:00 " + "a" * 40 + " dev gate exit 0\nx " + "b" * 40 + " dev gate skipped: x\n"
                    "Sun 11/21/2026  3:44:07.24 " + "c" * 8 + " dev gate exit 1\n")
        check(last_gated_sha(hist, "c" * 40) == "a" * 40 and last_gated_sha(hist, "d" * 40) == "c" * 8,
              "the last gated night other than tonight's commit (short shas as the gate writes them)")

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
    ap.add_argument("command", nargs="?",
                    choices=("commit", "range", "workers", "compilers", "goldens", "nightly", "trailer", "parity"))
    ap.add_argument("--nightly", action="store_true", help="commit / range: include the scenarios marked nightly")
    ap.add_argument("--history", help="nightly: tools\\gate_scheduled.cmd's history file")
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
    build = os.path.abspath(a.build) if a.build else sr.default_build(a.compiler)
    try:
        mode = "only" if a.command == "nightly" else "all" if (a.nightly or a.command in ("goldens", "workers", "compilers")) \
            else "exclude"
        scen = pick(sr.discover(build), a.only, a.fast, slow_tests(build) if a.fast else set(), mode)
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
    elif a.command == "nightly":
        if not a.history:
            ap.error("nightly --history FILE")
        head = sr.git(ROOT, "rev-parse", "HEAD").strip()
        base = last_gated_sha(a.history, head)
        if base:
            try:
                base = sr.git(ROOT, "rev-parse", base).strip()
            except RuntimeError:
                base = None
        if not base:
            print(f"behaviour check nightly: no earlier gated commit in {a.history}; nothing to compare")
            return 0
        log = os.path.join(sr.main_checkout(ROOT), "logs", "behaviour_nightly.txt")
        ok, lines = check_range(base, head, scen, cache, build, a.compiler, log)
        lines.append("RESULT " + ("PASS" if ok else "FAIL") + " (expand phase: report only, the stored goldens decide)")
        print("\n".join(lines))
        return 0
    elif a.command == "goldens":
        ok, lines = check_goldens(scen, cache, build, a.compiler)
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
