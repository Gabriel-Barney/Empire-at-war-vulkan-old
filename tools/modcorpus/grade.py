#!/usr/bin/env python3
"""Compatibility grades G0-G3 for one mod or stack (docs/MOD_COMPAT_DESIGN.md §1.3, §12.2, §12.3; roadmap mcx7 t02).

The corpus runner (run_corpus.py) collects one *result* per setup: the load report summary of `alamo_mod_check` (v4) and
the outcome of each headless run and smoke. This module turns that into a grade, the blockers that keep it from the next
grade, and compares two sweeps (a grade drop is a regression). The rules live here once; the C++ load report supplies
only its static part (`grade.grade` G0 / G1 and `grade.g3_static` with its `blocking` list).

Grades (each includes the ones below it):
  G0 Refused  the load report has errors (the data does not load, a base-EaW mod, a stack cycle, a check error), or
              alamo_mod_check itself failed.
  G1 Loads    0 errors.
  G2 Runs     G1, and every headless run passed: space and land skirmish (3000 ticks, workers 0 and 4, CRC logs equal),
              one galactic campaign (9000 ticks): exit 0, no timeout, AI orders and decisions, no Lua error beyond the
              documented retail ones (ASan sweeps: no sanitizer report). A mod with no skirmish map of a kind has that
              run "n/a" (nothing to check, not a blocker).
  G3 Plays    G2, and the hidden smokes (skirmish, campaign) passed and the report's static G3 part holds
              (`grade.g3_static`: no honour gap, binding gap, missing reward or token, asset reject or unconfirmed
              salvage in the mod's own content).
  G4 needs retail runs (§14) and is never given here.

Accepted blockers: corpus.json may list, per setup, blockers that are explained as retail-equivalent or as the mod's own
design (with a `why` and a reference, e.g. MODS.md §10.4 t44). A blocker matching one (regex search) is reported under
`accepted` and does not lower the grade.

Stages: a sweep that skipped the runs (--stages check) cannot give more than G1, one that skipped the smokes not more
than G2. A drop is counted against the previous grade capped the same way, so a quicker sweep is not a regression.

  python -I tools/modcorpus/grade.py --selftest
  python -I tools/modcorpus/grade.py RESULT.json [--corpus corpus.json]   grade one per-setup result file
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys

GRADES = ("G0", "G1", "G2", "G3")
RUNS = ("space", "land", "galactic")
SMOKES = ("smoke_skirmish", "smoke_campaign")
ALL_STAGES = ("check", "runs", "smokes")


def grade_index(g: str) -> int:
    return GRADES.index(g) if g in GRADES else -1


def stage_cap(stages) -> str:
    """The highest grade a sweep with these stages can give."""
    stages = set(stages or ALL_STAGES)
    if "runs" not in stages:
        return "G1"
    if "smokes" not in stages:
        return "G2"
    return "G3"


def _report_blockers(report: dict | None) -> list[str]:
    """Why the static check refuses the mod (G0)."""
    if report is None:
        return ["mod_check: no report"]
    out: list[str] = []
    if report.get("tool_error"):
        out.append("mod_check: " + report["tool_error"])
    for e in report.get("stack_errors", []):
        out.append("stack: " + e)
    if report.get("base_eaw"):
        out.append("base-EaW mod (D1)")
    if report.get("loaded") is False:
        out.append("load: " + (report.get("load_error") or "the data does not load"))
    for e in report.get("check_errors", []):
        out.append("check: " + e)
    if report.get("no_startable_campaign"):
        out.append("campaigns: no campaign has planets")
    errors = int(report.get("errors", 0) or 0)
    if errors and not out:
        out.append(f"mod_check: {errors} error(s)")
    return out


def _run_blockers(name: str, run: dict | None, asan: bool = False) -> tuple[list[str], list[str]]:
    """(blockers, notes) of one headless run."""
    if run is None:
        return [f"{name}: not run"], []
    st = run.get("status")
    if st == "n/a":
        return [], [f"{name}: n/a ({run.get('why', 'nothing to run')})"]
    if st == "timeout":
        return [f"{name}: timeout after {run.get('seconds', '?')} s"], []
    out: list[str] = []
    if st not in ("ok", "failed"):
        out.append(f"{name}: {st or 'no status'}")
    if run.get("exit") not in (0, None):
        out.append(f"{name}: exit {run.get('exit')}" + (f" ({run['error']})" if run.get("error") else ""))
    if asan and run.get("sanitizer"):
        out.append(f"{name}: sanitizer report: {run['sanitizer']}")
    if run.get("parsed") is False:
        out.append(f"{name}: no summary line")
        return out, []
    if name != "galactic" and run.get("deterministic") is False:
        out.append(f"{name}: workers 0 vs 4 differ" + (f": {run['compare']}" if run.get("compare") else ""))
    if int(run.get("orders", 0) or 0) == 0:
        out.append(f"{name}: no AI orders")
    if int(run.get("decisions", 0) or 0) == 0:
        out.append(f"{name}: no AI decisions")
    lua = int(run.get("lua_errors", 0) or 0) - int(run.get("retail_lua_errors", 0) or 0)
    if lua > 0:
        out.append(f"{name}: {lua} Lua error(s)" + (f": {run['first_lua_error']}" if run.get("first_lua_error") else ""))
    return out, []


def _smoke_blockers(name: str, smoke: dict | None) -> list[str]:
    if smoke is None:
        return [f"{name}: not run"]
    st = smoke.get("status")
    if st == "ok":
        return []
    if st == "timeout":
        return [f"{name}: timeout after {smoke.get('seconds', '?')} s"]
    detail = smoke.get("error") or ""
    return [f"{name}: {st or 'failed'} (exit {smoke.get('exit')})" + (f": {detail}" if detail else "")]


def _accept(blockers: list[str], accepts: list[dict]) -> tuple[list[str], list[dict]]:
    kept, accepted = [], []
    for b in blockers:
        hit = next((a for a in accepts if re.search(a.get("blocker", "^$"), b)), None)
        if hit:
            accepted.append({"blocker": b, "why": hit.get("why", ""), "ref": hit.get("ref", "")})
        else:
            kept.append(b)
    return kept, accepted


def grade(result: dict, accepts: list[dict] | None = None) -> dict:
    """{grade, blocking (what keeps it from the next grade, first = first blocker), accepted, notes, cap}."""
    accepts = accepts or []
    stages = result.get("stages") or list(ALL_STAGES)
    cap = stage_cap(stages)
    report = result.get("report")
    notes: list[str] = []
    accepted: list[dict] = []

    g0 = _report_blockers(report)
    g0, acc = _accept(g0, accepts)
    accepted += acc
    if g0:
        return {"grade": "G0", "blocking": g0, "accepted": accepted, "notes": notes, "cap": cap}
    if cap == "G1":
        notes.append("runs not in this sweep: G1 at most")
        return {"grade": "G1", "blocking": [], "accepted": accepted, "notes": notes, "cap": cap}

    runs = result.get("runs") or {}
    asan = bool(result.get("asan_mode"))
    g1: list[str] = []
    for name in RUNS:
        b, n = _run_blockers(name, runs.get(name), asan)
        g1 += b
        notes += n
    for name, run in sorted((result.get("asan") or {}).items()):  # --asan: the same runs under the ASan build
        b, n = _run_blockers("asan " + name, run, True)
        g1 += b
        notes += n
    g1, acc = _accept(g1, accepts)
    accepted += acc
    if g1:
        return {"grade": "G1", "blocking": g1, "accepted": accepted, "notes": notes, "cap": cap}
    if cap == "G2":
        notes.append("smokes not in this sweep: G2 at most")
        return {"grade": "G2", "blocking": [], "accepted": accepted, "notes": notes, "cap": cap}

    g2: list[str] = []
    for name in SMOKES:
        g2 += _smoke_blockers(name, (result.get("smokes") or {}).get(name))
    rg = (report or {}).get("grade") or {}
    if not rg.get("g3_static", False):
        g2 += ["static: " + b for b in rg.get("blocking", [])] or ["static: G3 part open"]
    g2, acc = _accept(g2, accepts)
    accepted += acc
    if g2:
        return {"grade": "G2", "blocking": g2, "accepted": accepted, "notes": notes, "cap": cap}
    return {"grade": "G3", "blocking": [], "accepted": accepted, "notes": notes, "cap": cap}


def _norm_blocker(b: str) -> str:
    """A blocker without its counts, so '3 Lua error(s)' and '4 Lua error(s)' are the same blocker."""
    return re.sub(r"\d+", "N", b.split(": ", 2)[0] + ": " + (b.split(": ", 2)[1] if ": " in b else ""))


def _entry(e: dict) -> tuple[str, list[str]]:
    """(grade, blocking) of a matrix entry: run_corpus writes {"grade": {"grade", "blocking", ...}}, a flat
    {"grade": "G1", "blocking": [...]} is read too."""
    g = e.get("grade", "G0")
    if isinstance(g, dict):
        return g.get("grade", "G0"), list(g.get("blocking", []))
    return g, list(e.get("blocking", []))


def compare(previous: dict, current: dict) -> dict:
    """Compare two matrices ({"stages": [...], "mods": {key: {"grade": ..., "blocking": [...]}}}).

    Returns {"drops": [{key, before, after, first_new_blocker}], "raised": [...], "new": [keys], "gone": [keys]}. A previous
    grade above what the current stages can give is capped first (a quicker sweep is not a drop)."""
    prev_mods, cur_mods = previous.get("mods", {}), current.get("mods", {})
    cap = stage_cap(current.get("stages"))
    out = {"drops": [], "raised": [], "new": sorted(set(cur_mods) - set(prev_mods)), "gone": sorted(set(prev_mods) - set(cur_mods))}
    for key in sorted(set(prev_mods) & set(cur_mods)):
        (before, old_blocking), (after, blockers) = _entry(prev_mods[key]), _entry(cur_mods[key])
        before_capped = GRADES[min(grade_index(before), grade_index(cap))] if grade_index(before) >= 0 else before
        if grade_index(after) < grade_index(before_capped):
            old = {_norm_blocker(b) for b in old_blocking}
            new = [b for b in blockers if _norm_blocker(b) not in old]
            out["drops"].append({"key": key, "before": before, "after": after,
                                 "first_new_blocker": (new or blockers or ["(none listed)"])[0]})
        elif grade_index(after) > grade_index(before):
            out["raised"].append({"key": key, "before": before, "after": after})
    return out


def accepts_for(corpus: dict, key: str) -> list[dict]:
    return list((corpus.get("mods", {}).get(key) or {}).get("accept", [])) + list(corpus.get("accept_all", []))


# ---------------------------------------------------------------------------------------------------------------- selftest

def _ok_run(**kw) -> dict:
    r = {"status": "ok", "exit": 0, "orders": 10, "decisions": 20, "lua_errors": 0, "retail_lua_errors": 0, "deterministic": True}
    r.update(kw)
    return r


def _full(**kw) -> dict:
    res = {"stages": list(ALL_STAGES),
           "report": {"errors": 0, "loaded": True, "grade": {"grade": "G1", "g3_static": True, "blocking": []}},
           "runs": {"space": _ok_run(), "land": _ok_run(), "galactic": _ok_run()},
           "smokes": {"smoke_skirmish": {"status": "ok", "exit": 0}, "smoke_campaign": {"status": "ok", "exit": 0}}}
    res.update(kw)
    return res


def selftest() -> int:
    fails = 0

    def check(name, got, want):
        nonlocal fails
        if got != want:
            fails += 1
            print(f"FAIL {name}: got {got!r}, want {want!r}")

    check("full G3", grade(_full())["grade"], "G3")
    check("no report", grade({"stages": ["check"]})["grade"], "G0")
    r = grade(_full(report={"errors": 1, "loaded": False, "load_error": "GameConstants.xml unparsable"}))
    check("load error G0", (r["grade"], r["blocking"][0]), ("G0", "load: GameConstants.xml unparsable"))
    check("base-EaW G0", grade(_full(report={"errors": 1, "base_eaw": True}))["blocking"], ["base-EaW mod (D1)"])
    check("stack cycle G0", grade(_full(report={"errors": 1, "stack_errors": ["cycle: A -> B -> A"]}))["blocking"],
          ["stack: cycle: A -> B -> A"])
    check("bare error count", grade(_full(report={"errors": 3}))["blocking"], ["mod_check: 3 error(s)"])
    check("check stage cap", grade(_full(stages=["check"]))["grade"], "G1")
    check("runs stage cap", grade(_full(stages=["check", "runs"]))["grade"], "G2")
    res = _full()
    res["runs"]["space"] = _ok_run(deterministic=False, compare="tick 12 differs")
    r = grade(res)
    check("nondeterministic G1", (r["grade"], r["blocking"]), ("G1", ["space: workers 0 vs 4 differ: tick 12 differs"]))
    res = _full()
    res["runs"]["land"] = {"status": "n/a", "why": "no land skirmish map"}
    check("n/a land is no blocker", grade(res)["grade"], "G3")
    res = _full()
    res["runs"]["galactic"] = _ok_run(orders=0, lua_errors=2, retail_lua_errors=1)
    check("galactic blockers", grade(res)["blocking"], ["galactic: no AI orders", "galactic: 1 Lua error(s)"])
    check("retail lua allowed", grade(_full(runs={"space": _ok_run(lua_errors=2, retail_lua_errors=2), "land": _ok_run(),
                                                  "galactic": _ok_run()}))["grade"], "G3")
    res = _full()
    res["runs"]["space"] = {"status": "timeout", "seconds": 900}
    check("timeout", grade(res)["blocking"], ["space: timeout after 900 s"])
    res = _full()
    res["runs"]["space"] = _ok_run(status="failed", exit=3221225477, error="crash")
    check("crash", grade(res)["blocking"][0], "space: exit 3221225477 (crash)")
    res = _full()
    res["runs"]["galactic"] = _ok_run(orders=0)
    acc = [{"blocker": r"^galactic: no AI orders$", "why": "AI starts only after the human's popup", "ref": "MODS.md §10.4 t45"}]
    r = grade(res, acc)
    check("accepted blocker", (r["grade"], len(r["accepted"])), ("G3", 1))
    res = _full(smokes={"smoke_skirmish": {"status": "failed", "exit": 1, "error": "step 12 of 91"}})
    r = grade(res)
    check("smoke failed", r["blocking"], ["smoke_skirmish: failed (exit 1): step 12 of 91", "smoke_campaign: not run"])
    res = _full()
    res["report"]["grade"] = {"grade": "G1", "g3_static": False, "blocking": ["honour_gap Foo.Bar"]}
    check("static open", grade(res)["blocking"], ["static: honour_gap Foo.Bar"])
    res = _full(asan_mode=True, asan={"space": _ok_run(sanitizer="heap-use-after-free")})
    check("asan report", grade(res)["blocking"], ["asan space: sanitizer report: heap-use-after-free"])

    prev = {"stages": list(ALL_STAGES), "mods": {"A": {"grade": "G2", "blocking": ["smoke_campaign: failed (exit 1)"]},
                                                 "B": {"grade": "G1", "blocking": ["space: 3 Lua error(s)"]},
                                                 "C": {"grade": "G3", "blocking": []}, "D": {"grade": "G1", "blocking": []}}}
    cur = {"stages": list(ALL_STAGES), "mods": {"A": {"grade": "G0", "blocking": ["load: bad XML"]},
                                                "B": {"grade": "G1", "blocking": ["space: 5 Lua error(s)"]},
                                                "C": {"grade": "G3", "blocking": []}, "E": {"grade": "G1", "blocking": []}}}
    c = compare(prev, cur)
    check("drop", c["drops"], [{"key": "A", "before": "G2", "after": "G0", "first_new_blocker": "load: bad XML"}])
    check("new / gone", (c["new"], c["gone"]), (["E"], ["D"]))
    quick = {"stages": ["check"], "mods": {"C": {"grade": "G1", "blocking": []}}}
    check("capped previous is no drop", compare(prev, quick)["drops"], [])
    check("raise", compare(cur, prev)["raised"], [{"key": "A", "before": "G0", "after": "G2"}])
    nested = {"stages": list(ALL_STAGES), "mods": {"A": {"grade": {"grade": "G0", "blocking": ["load: bad XML"]}}}}
    check("nested matrix entries", compare(prev, nested)["drops"][0]["after"], "G0")
    check("norm blocker", _norm_blocker("space: 3 Lua error(s): x"), _norm_blocker("space: 4 Lua error(s): y"))

    print(f"grade selftest: {'FAILED, ' + str(fails) + ' case(s)' if fails else 'ok'}")
    return 1 if fails else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("result", nargs="?")
    ap.add_argument("--corpus", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "corpus.json"))
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.result:
        ap.error("RESULT.json or --selftest")
    with open(a.result, encoding="utf-8") as f:
        res = json.load(f)
    corpus = {}
    if os.path.isfile(a.corpus):
        with open(a.corpus, encoding="utf-8") as f:
            corpus = json.load(f)
    print(json.dumps(grade(res, accepts_for(corpus, res.get("key", ""))), indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
