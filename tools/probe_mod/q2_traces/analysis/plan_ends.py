"""skai t15: which goal-system rule ended each retail plan, from a run of run_skai_t15_*.cmd.

Reads <run>/lua_records.txt (the Lua probe's plan records: PSTART / PBLOCK / PEXIT, make_plan_hooks.py) and
<run>/frida_t15.jsonl (frida/skai_t15_hooks.js events). The plan's definition pointer is found by matching its PEXIT
records with tf=? (a plan end from outside the script, docs/re/SKAI_T15_PLAN_RETEST.md §4) to the plan_end hook events
(mf = t * 30 + a constant, calibrated as the median gap). Then every plan_end event of that definition is classified by
where the end came from (docs/re/SKAI_T15_PLAN_RETEST.md, "Retail hook runs"):

  purge         plan end called from Purge_Goals (0x69c8cc; Burn_Units_Land's Purge_Goals(PlayerObject))
  script        plan end from the plan's own exit (0x725bf0)
  otherwise the goal destructor ended it; the goal's budget release (0x614880) says which rule dropped the goal:
  retest        released right after a re-test 0x6c47c0 returned false: Resolve_Category (0x6c8c9a) or maintenance (0x722c2d)
  displaced     an old candidate not picked again at the end of Resolve_Category (0x6c95b0)
  recheck       released in Resolve_Category's weighted random phase re-check (0x6c9996) [M]
  maint         another maintenance rule (0x722bda / 0x722d01: not fundable, desire <= 0)
  other         none of these

Then, for every displaced end, the Resolve_Category of that commit from the resolve_goals / resolve_result events
(kit since skai t15 2026-11-16, docs/re/SKAI_T15_RESOLVE_HOOK_RE.md): each active goal and proposal of the category with
type, target, desire, effective desire (hysteresis), state and plan, marked kept / accepted / dropped.

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
RELEASE = {"0x6c8c9a": "retest", "0x722c2d": "retest", "0x6c95b0": "displaced", "0x6c9996": "recheck",
           "0x722bda": "maint", "0x722d01": "maint"}


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


def find_plandef(records, events, plan, window=45):
    """(plan definition pointer, mf offset, PEXIT tf=? count) of `plan`, or (None, None, n)."""
    exits = [r for r in records if r["kind"] == "PEXIT" and r.get("plan") == plan and r.get("tf") == "?"]
    ends = [e for e in events if e.get("k") == "plan_end" and isinstance(e.get("mf"), int) and e["mf"] >= 0]
    if not exits or not ends:
        return None, None, len(exits)
    offset = statistics.median(min((e["mf"] - r["t"] * FPS for e in ends), key=abs) for r in exits)
    near = collections.Counter()
    for r in exits:
        x = r["t"] * FPS + offset
        for e in ends:
            if abs(e["mf"] - x) <= window:
                near[e.get("plandef")] += 1
    return (near.most_common(1)[0][0] if near else None), offset, len(exits)


def classify(events, plandef):
    """[(mf, rule)] for every plan_end of `plandef`, in event order."""
    rows = []
    for i, e in enumerate(events):
        if e.get("k") != "plan_end" or e.get("plandef") != plandef:
            continue
        ra = e.get("ra")
        if ra == "0x69c8cc":
            rows.append((e.get("mf"), "purge"))
            continue
        if ra == "0x725bf0":
            rows.append((e.get("mf"), "script"))
            continue
        rule = "other"
        j = i - 1
        while j >= 0 and isinstance(events[j].get("mf"), int) and events[j]["mf"] >= e.get("mf", 0) - 2:
            x = events[j]
            if x.get("k") == "budget_release" and x.get("goal") == e.get("goal"):
                rule = RELEASE.get(x.get("ra"), "other")
                if rule == "retest":  # only when the event just before the release is a false re-test
                    k = j - 1
                    while k >= 0 and events[k].get("k") == "goal_dtor":
                        k -= 1
                    if not (events[k].get("k") == "retest_leave" and events[k].get("ret") == 0):
                        rule = "other"
                break
            j -= 1
        rows.append((e.get("mf"), rule))
    return rows


def resolve_at(events, mf, window=2):
    """[(resolve_goals event, resolve_result event or None)] of the land Resolve_Category calls at commit `mf`."""
    out = []
    for i, e in enumerate(events):
        if e.get("k") != "resolve_goals" or not isinstance(e.get("mf"), int) or abs(e["mf"] - mf) > window:
            continue
        res = None
        for x in events[i + 1:]:
            if x.get("k") == "resolve_result" and x.get("cat") == e.get("cat"):
                res = x
                break
            if x.get("k") == "resolve_goals":
                break
        out.append((e, res))
    return out


def resolve_lines(goals_ev, result_ev):
    kept = set(result_ev.get("survivors", [])) if result_ev else set()
    accepted = set(result_ev.get("accepted", [])) if result_ev else set()
    lines = ["  commit mf %s, player %s, category %s" % (goals_ev.get("mf"), goals_ev.get("player"), goals_ev.get("cat"))]
    for role, key in (("active", "active"), ("proposal", "proposed")):
        for g in goals_ev.get(key, []):
            if result_ev is None:
                mark = "?"
            elif role == "active":
                mark = "kept" if g.get("g") in kept else "dropped"
            else:
                mark = "accepted" if g.get("g") in accepted else "rejected"
            lines.append("    %-8s %-8s %-24s %-28s d %7.3f eff %7.3f st %s %s" %
                         (role, mark, g.get("type"), g.get("tgt"), g.get("d", float("nan")),
                          g.get("eff", float("nan")), g.get("st"), g.get("plan", "")))
    return lines


def report(rows, plan, plandef, offset, exits):
    print("%s: plan definition %s, %d PEXIT tf=? records, mf = t*30 %+.0f, %d plan ends" %
          (plan, plandef, exits, offset or 0, len(rows)))
    by = collections.defaultdict(list)
    for mf, rule in rows:
        by[rule].append(mf)
    for rule, mfs in sorted(by.items(), key=lambda kv: -len(kv[1])):
        print("  %-10s %3d  mf %s%s" % (rule, len(mfs), " ".join(str(m) for m in mfs[:14]), " ..." if len(mfs) > 14 else ""))


def selftest():
    recs = [{"kind": "PEXIT", "plan": "Destroy_Structure", "tf": "?", "t": 100.0, "target": "A"},
            {"kind": "PEXIT", "plan": "Destroy_Structure", "tf": "?", "t": 200.0, "target": "B"}]
    off = 120
    ev = [{"k": "retest_enter", "pp": "p1", "plandef": "D", "mf": 3000 + off},
          {"k": "retest_leave", "pp": "p1", "ret": 0, "mf": 3000 + off},
          {"k": "budget_release", "goal": "g1", "ra": "0x6c8c9a", "mf": 3000 + off},
          {"k": "goal_dtor", "goal": "g1", "mf": 3000 + off},
          {"k": "plan_end", "plan": "x", "plandef": "D", "goal": "g1", "mf": 3000 + off, "ra": "0x691887"},
          {"k": "budget_release", "goal": "g2", "ra": "0x6c95b0", "mf": 6001 + off},
          {"k": "plan_end", "plan": "y", "plandef": "D", "goal": "g2", "mf": 6001 + off, "ra": "0x691887"},
          {"k": "plan_end", "plan": "z", "plandef": "E", "goal": "g3", "mf": 6001 + off, "ra": "0x691887"},
          {"k": "plan_end", "plan": "w", "plandef": "D", "goal": "g4", "mf": 7000 + off, "ra": "0x69c8cc"},
          {"k": "plan_end", "plan": "v", "plandef": "D", "goal": "g5", "mf": 7100 + off, "ra": "0x725bf0"}]
    plandef, offset, n = find_plandef(recs, ev, "Destroy_Structure")
    rows = classify(ev, plandef)
    ok = (plandef == "D" and abs(offset - off) <= 1 and n == 2 and
          [r for _, r in rows] == ["retest", "displaced", "purge", "script"])
    rec = read_records_from_text("P 0000 2 PEXIT t=697.5 frame=697.5 plan=Destroy_Structure player=EMPIRE target=X tf=? n=?\n")
    ok = ok and len(rec) == 1 and rec[0]["tf"] == "?" and rec[0]["t"] == 697.5
    # Resolve candidate dump (skai t15): the displacing proposal is named at the commit of a displaced end.
    rev = [{"k": "resolve_goals", "mf": 6001 + off, "player": 1, "cat": 4,
            "active": [{"g": "g2", "type": "Destroy_Structure", "tgt": "obj:792", "d": 15.0, "eff": 16.0, "st": 2,
                        "plan": "DestroyStructure"}],
            "proposed": [{"g": "p1", "type": "Clear_Infantry", "tgt": "zone:Z", "d": 17.0, "eff": 17.0, "st": 0,
                          "plan": "ClearInfantry"}]},
           {"k": "resolve_result", "mf": 6001 + off, "player": 1, "cat": 4, "survivors": ["p1"], "accepted": ["p1"]}]
    found = resolve_at(rev, 6001 + off)
    text = "\n".join(resolve_lines(*found[0])) if found else ""
    ok = ok and len(found) == 1 and "active   dropped  Destroy_Structure" in text and \
        "proposal accepted Clear_Infantry" in text and resolve_at(rev, 9000) == []
    print("plan_ends selftest: " + ("OK" if ok else "FAILED %r %r" % (plandef, rows)))
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
    plandef, offset, n = find_plandef(records, events, a.plan)
    if plandef is None:
        print("%s: no plan definition found (%d PEXIT tf=? records)" % (a.plan, n))
        return 1
    rows = classify(events, plandef)
    report(rows, a.plan, plandef, offset, n)
    if any(e.get("k") == "resolve_goals" for e in events):
        for mf, rule in rows:
            if rule != "displaced":
                continue
            print("displaced at mf %s:" % mf)
            found = resolve_at(events, mf)
            if not found:
                print("  no resolve_goals event at that commit")
            for ge, re_ in found:
                print("\n".join(resolve_lines(ge, re_)))
    else:
        print("(no resolve_goals events: a kit before the skai t15 Resolve hook)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
