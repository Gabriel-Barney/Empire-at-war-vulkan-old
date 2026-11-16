"""skai t15: which goal-system rule ended each retail plan, from a run of run_skai_t15_*.cmd.

Reads <run>/lua_records.txt (the Lua probe's plan records: PSTART / PBLOCK / PEXIT, make_plan_hooks.py) and
<run>/frida_t15.jsonl (frida/skai_t15_hooks.js events), and prints for every plan end that came from outside the script
(PEXIT with tf=?, the signature of a goal-system drop, docs/re/SKAI_T15_PLAN_RETEST.md §4) the rule that ended it:

  retest      the running-plan re-test 0x6c47c0 returned false for a PP of that plan definition in the same commit
  purge       Purge_Goals 0x69c750 ran just before
  release     the goal was released (0x693750) without a failed re-test: Resolve_Category's old-candidate release
              (or maintenance's desire <= 0 rule)
  ?           no plan_end event near it (the hooks missed it)

The Lua records carry game time t in seconds; the hooks carry the mode frame mf. t * 30 and mf differ by a constant
(load frames): it is calibrated as the median gap between each tf=? PEXIT and the nearest plan_end.

    toolchain\\venv\\Scripts\\python.exe -I tools\\probe_mod\\q2_traces\\analysis\\plan_ends.py <run dir> [--plan Destroy_Structure]
    ... plan_ends.py --selftest
"""
import argparse
import collections
import json
import os
import re
import statistics
import sys

FPS = 30.0
REC = re.compile(r"^P \S+ \d+ (\w+) (.*)$")
KV = re.compile(r"(\w+)=(\S*)")


def read_records(path):
    out = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = REC.match(line.rstrip("\n"))
            if not m:
                continue
            kv = dict(KV.findall(m.group(2)))
            kv["kind"] = m.group(1)
            try:
                kv["t"] = float(kv.get("t", "nan"))
            except ValueError:
                continue
            out.append(kv)
    return out


def read_events(path):
    ev = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line:
                try:
                    ev.append(json.loads(line))
                except ValueError:
                    pass
    return ev


def analyse(records, events, plan, window=45):
    exits = [r for r in records if r["kind"] == "PEXIT" and r.get("plan") == plan and r.get("tf") == "?"]
    ends = [e for e in events if e.get("k") == "plan_end" and isinstance(e.get("mf"), int) and e["mf"] >= 0]
    if not exits or not ends:
        return {"exits": len(exits), "plan_ends": len(ends), "rows": [], "offset": None, "plandef": None, "counts": {}}
    # Calibrate mf = t * 30 + offset.
    gaps = []
    for r in exits:
        x = r["t"] * FPS
        gaps.append(min((e["mf"] - x for e in ends), key=abs))
    offset = statistics.median(gaps)
    # The plan definition of this plan: the one most often at a matching plan_end.
    near = collections.Counter()
    for r in exits:
        x = r["t"] * FPS + offset
        for e in ends:
            if abs(e["mf"] - x) <= window:
                near[e.get("plandef")] += 1
    plandef = near.most_common(1)[0][0] if near else None
    pp_def = {}
    for e in events:
        if e.get("k") == "retest_enter":
            pp_def[e.get("pp")] = e.get("plandef")
    rows = []
    counts = collections.Counter()
    for r in exits:
        x = r["t"] * FPS + offset
        cand = [e for e in ends if e.get("plandef") == plandef and abs(e["mf"] - x) <= window]
        if not cand:
            rows.append((r["t"], r.get("target", "?"), "?", "", ""))
            counts["?"] += 1
            continue
        end = min(cand, key=lambda e: abs(e["mf"] - x))
        mf = end["mf"]
        same = [e for e in events if isinstance(e.get("mf"), int) and mf - 3 <= e["mf"] <= mf]
        failed = [e for e in same if e.get("k") == "retest_leave" and e.get("ret") == 0 and pp_def.get(e.get("pp")) == plandef]
        purged = [e for e in same if e.get("k") == "purge"]
        released = [e for e in same if e.get("k") == "goal_release" and e.get("goal") == end.get("goal")]
        if failed:
            rule = "retest"
        elif purged:
            rule = "purge"
        elif released:
            rule = "release"
        else:
            rule = "other"
        counts[rule] += 1
        detail = "ra %s bt %s" % (end.get("ra"), ",".join(end.get("bt", [])[:3]))
        if released:
            detail += "; release ra %s" % released[0].get("ra")
        rows.append((r["t"], r.get("target", "?"), rule, str(mf), detail))
    return {"exits": len(exits), "plan_ends": len(ends), "rows": rows, "offset": offset, "plandef": plandef, "counts": dict(counts)}


def report(res, plan):
    print("%s: %d PEXIT tf=? records, %d plan_end events, mf = t*30 %+.0f, plan definition %s" %
          (plan, res["exits"], res["plan_ends"], res["offset"] or 0, res["plandef"]))
    for t, target, rule, mf, detail in res["rows"]:
        print("  t %8.2f  mf %6s  %-8s %-32s %s" % (t, mf, rule, target, detail))
    print("rules: " + ", ".join("%s %d" % kv for kv in sorted(res["counts"].items())))


def selftest():
    recs = [{"kind": "PEXIT", "plan": "Destroy_Structure", "tf": "?", "t": 100.0, "target": "A"},
            {"kind": "PEXIT", "plan": "Destroy_Structure", "tf": "?", "t": 200.0, "target": "B"},
            {"kind": "PEXIT", "plan": "Destroy_Structure", "tf": "MainForce", "t": 250.0, "target": "C"}]
    off = 120
    ev = [{"k": "retest_enter", "pp": "p1", "plandef": "D", "mf": 3000 + off},
          {"k": "retest_leave", "pp": "p1", "ret": 0, "mf": 3000 + off},
          {"k": "plan_end", "plan": "x", "plandef": "D", "goal": "g1", "mf": 3000 + off, "ra": "0x691900", "bt": []},
          {"k": "goal_release", "goal": "g2", "mf": 6001 + off, "ra": "0x6c9000"},
          {"k": "plan_end", "plan": "y", "plandef": "D", "goal": "g2", "mf": 6001 + off, "ra": "0x691900", "bt": []},
          {"k": "plan_end", "plan": "z", "plandef": "E", "goal": "g3", "mf": 6001 + off, "ra": "0x691900", "bt": []}]
    res = analyse(recs, ev, "Destroy_Structure")
    ok = (res["exits"] == 2 and res["plandef"] == "D" and abs(res["offset"] - off) <= 1 and
          [r[2] for r in res["rows"]] == ["retest", "release"])
    rec = read_records_from_text("P 0000 2 PEXIT t=697.5 frame=697.5 plan=Destroy_Structure player=EMPIRE target=X tf=? n=?\n")
    ok = ok and len(rec) == 1 and rec[0]["tf"] == "?" and rec[0]["t"] == 697.5
    print("plan_ends selftest: " + ("OK" if ok else "FAILED %r" % res))
    return 0 if ok else 1


def read_records_from_text(text):
    import tempfile
    fd, path = tempfile.mkstemp(suffix=".txt")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write(text)
        return read_records(path)
    finally:
        os.remove(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run", nargs="?")
    ap.add_argument("--plan", default="Destroy_Structure")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.run:
        ap.error("run dir needed")
    records = read_records(os.path.join(a.run, "lua_records.txt"))
    events = read_events(os.path.join(a.run, "frida_t15.jsonl"))
    kinds = collections.Counter(e.get("k") for e in events)
    print("frida events: " + ", ".join("%s %d" % kv for kv in sorted(kinds.items())))
    fails = collections.Counter(e.get("pp") for e in events if e.get("k") == "retest_leave" and e.get("ret") == 0)
    print("re-test false results: %d (distinct PPs %d)" % (sum(fails.values()), len(fails)))
    report(analyse(records, events, a.plan), a.plan)
    return 0


if __name__ == "__main__":
    sys.exit(main())
