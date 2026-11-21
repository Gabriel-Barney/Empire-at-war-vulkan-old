#!/usr/bin/env python3
"""skai t22: the Rebel AI's space goal timeline on Geonosis, from a run of run_skai_t22_space.cmd (no Lua probe).

Reads <run>/frida_t22.jsonl (frida/skai_t22_space_hooks.js events, read-only hooks, docs/re/SKAI_T15_RESOLVE_HOOK_RE.md
for the layout). For the space AI player (the one whose space Resolve_Category calls hold Space_Scout goals, or
--player), it prints:

  - the first commits: each accepted goal (type, target; a space cell as CELL_x_y) and every failed activation
    (activation ok=0: the goal's accept failed, e.g. no free fighter; retail then puts -15 on that goal and target for
    20 s, SKAI_T22_SPACE_AI_RE Part 4);
  - every Space_Scout accept with its desire and the commit it was released in (goal_release / plan_end), so how long
    a retail scout goal lives without hooked Lua threads;
  - a summary: accepts per goal type, the first scout cell, scout goal lifetimes (median, range), failed scout accepts.

With --port it also reads port `--ai-log` files ("plan N SPACESCOUT for Space_Scout @CELL_x_y" and "plan N ... ended")
for the same summary from the port side. Space cells: camera box -4600..4600, 2000-unit cells (A1_READINGS §8;
--box / --size for another map).

    toolchain\\venv\\Scripts\\python.exe -I tools\\probe_mod\\q2_traces\\analysis\\space_goals.py <run dir>... [--port LOG...]
    ... space_goals.py --selftest
"""
import argparse
import collections
import json
import math
import os
import re
import statistics
import sys
import tempfile

SCOUT = "Space_Scout"
PORT_START = re.compile(r"^\s*(\d+) p(\d+) plan (\d+) \w+ for (\w+) @(\S+)")
PORT_END = re.compile(r"^\s*(\d+) p(\d+) plan (\d+) \w+ ended")


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


def cell_name(tgt, box, size):
    """'cell:x,y' -> CELL_i_j of the grid (x outer); anything else unchanged."""
    if not isinstance(tgt, str) or not tgt.startswith("cell:"):
        return tgt
    try:
        x, y = (float(v) for v in tgt[5:].split(","))
    except ValueError:
        return tgt
    return "CELL_%d_%d" % (int(math.floor((x - box[0]) / size)), int(math.floor((y - box[1]) / size)))


def space_player(events):
    """The player whose space Resolve_Category calls hold Space_Scout goals (most often), else None."""
    c = collections.Counter()
    for e in events:
        if e.get("k") == "resolve_goals" and e.get("mode") == "space":
            for g in e.get("active", []) + e.get("proposed", []):
                if g.get("type") == SCOUT:
                    c[e.get("player")] += 1
    return c.most_common(1)[0][0] if c else None


def timeline(events, player, box, size):
    """(commits, scouts, accepts per type, failed activations per type).

    commits: [(mf, [(type, target, d)] accepted, [(type, target)] failed)] in order;
    scouts: [{mf, cell, d, end}] for every accepted Space_Scout, end = mf of its goal_release (None while it lives)."""
    info = {}       # goal pointer -> (type, target, d) from the latest resolve_goals listing it
    commits = collections.OrderedDict()
    scouts = []
    live = {}       # goal pointer -> index into scouts
    accepts = collections.Counter()
    failed = collections.Counter()
    cur = None      # the space resolve of `player` being read (between resolve_goals and resolve_result)
    for e in events:
        k = e.get("k")
        mf = e.get("mf")
        if k == "resolve_goals":
            cur = e if (e.get("mode") == "space" and e.get("player") == player) else None
            if cur is not None:
                for g in cur.get("active", []) + cur.get("proposed", []):
                    info[g.get("g")] = (g.get("type"), cell_name(g.get("tgt"), box, size), g.get("d"))
            continue
        if k == "activation" and cur is not None and e.get("ok") == 0:
            t = info.get(e.get("goal"))
            if t:
                commits.setdefault(cur.get("mf"), ([], []))[1].append((t[0], t[1]))
                failed[t[0]] += 1
            continue
        if k == "resolve_result":
            if cur is not None and e.get("cat") == cur.get("cat"):
                for p in e.get("accepted", []):
                    t = info.get(p)
                    if not t:
                        continue
                    commits.setdefault(cur.get("mf"), ([], []))[0].append(t)
                    accepts[t[0]] += 1
                    if t[0] == SCOUT:
                        live[p] = len(scouts)
                        scouts.append({"mf": cur.get("mf"), "cell": t[1], "d": t[2], "end": None})
            cur = None
            continue
        if k in ("goal_release", "goal_dtor") and e.get("goal") in live:
            scouts[live.pop(e.get("goal"))]["end"] = mf
    return [(mf, a, f) for mf, (a, f) in commits.items()], scouts, accepts, failed


def lifetimes(scouts):
    return [s["end"] - s["mf"] for s in scouts if isinstance(s.get("end"), int) and isinstance(s.get("mf"), int)]


def port_log(path):
    scouts, accepts, starts = [], collections.Counter(), {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = PORT_START.match(line)
            if m:
                accepts[m.group(4)] += 1
                if m.group(4) == SCOUT:
                    starts[(m.group(2), m.group(3))] = len(scouts)
                    scouts.append({"mf": int(m.group(1)), "cell": m.group(5), "d": None, "end": None})
                continue
            m = PORT_END.match(line)
            if m and (m.group(2), m.group(3)) in starts:
                scouts[starts.pop((m.group(2), m.group(3)))]["end"] = int(m.group(1))
    return scouts, accepts


def summary(label, runs, out):
    first = collections.Counter()
    acc = collections.Counter()
    life = []
    fails = 0
    logged = any(failed is not None for _, _, failed in runs)
    for scouts, accepts, failed in runs:
        if scouts:
            first[scouts[0]["cell"]] += 1
        acc.update(accepts)
        life += lifetimes(scouts)
        fails += failed.get(SCOUT, 0) if failed is not None else 0
    n = max(len(runs), 1)
    out.append("%s summary over %d run(s):" % (label, len(runs)))
    out.append("  first scout: " + (", ".join("%s %d" % kv for kv in first.most_common()) or "-"))
    out.append("  accepts per run: " + ", ".join("%s %.1f" % (t, c / n) for t, c in sorted(acc.items())))
    if life:
        out.append("  scout goal lifetime (frames): median %d, range %d..%d, %d ended" %
                   (statistics.median(life), min(life), max(life), len(life)))
    out.append("  failed scout accepts per run: " + ("%.1f" % (fails / n) if logged else "not in the port log"))


def run_report(run, player, box, size, commits_shown, out):
    path = os.path.join(run, "frida_t22.jsonl")
    events = read_events(path) if os.path.isfile(path) else []
    p = player if player is not None else space_player(events)
    commits, scouts, accepts, failed = timeline(events, p, box, size)
    out.append("retail %s: player %s, %d events, %d commits with accepts or failures" %
               (os.path.basename(os.path.normpath(run)), p, len(events), len(commits)))
    for mf, acc, fail in commits[:commits_shown]:
        out.append("  commit mf %s: accepted %s%s" % (
            mf, ", ".join("%s %s (d %.2f)" % (t, tg, d if isinstance(d, (int, float)) else float("nan")) for t, tg, d in acc)
            or "-", ("; failed " + ", ".join("%s %s" % x for x in fail)) if fail else ""))
    for s in scouts:
        out.append("  scout mf %s %-9s d %s end %s" % (s["mf"], s["cell"], s["d"], s["end"]))
    return scouts, accepts, failed


def selftest():
    ok = True

    def check(c, what):
        nonlocal ok
        print(("  ok    " if c else "  FAIL  ") + what)
        ok = ok and c
    box, size = (-4600.0, -4600.0, 4600.0, 4600.0), 2000.0
    check(cell_name("cell:-3600,-3600", box, size) == "CELL_0_0" and cell_name("cell:-3600,400", box, size) == "CELL_0_2"
          and cell_name("obj:41", box, size) == "obj:41", "cell names from centres (x outer)")
    g = lambda p, t, tg, d, st=0: {"g": p, "type": t, "tgt": tg, "d": d, "st": st}  # noqa: E731
    ev = [
        {"k": "hello"},
        {"k": "resolve_goals", "mf": 69, "player": 1, "cat": 3, "mode": "space", "active": [],
         "proposed": [g("0xa", "Secure_Build_Pad_Space", "obj:41", 20.0), g("0xb", "Secure_Build_Pad_Space", "obj:44", 20.0)]},
        {"k": "resolve_result", "mf": 69, "player": 1, "cat": 3, "mode": "space", "survivors": ["0xa", "0xb"],
         "accepted": ["0xa", "0xb"]},
        {"k": "resolve_goals", "mf": 69, "player": 1, "cat": 4, "mode": "space", "active": [],
         "proposed": [g("0xc", SCOUT, "cell:-3600,-3600", 10.0), g("0xd", SCOUT, "cell:-3600,-1600", 10.0)]},
        {"k": "activation", "mf": 69, "goal": "0xc", "ok": 0},
        {"k": "activation", "mf": 69, "goal": "0xd", "ok": 0},
        {"k": "resolve_result", "mf": 69, "player": 1, "cat": 4, "mode": "space", "survivors": [], "accepted": []},
        {"k": "resolve_goals", "mf": 69, "player": 0, "cat": 4, "mode": "land", "active": [],
         "proposed": [g("0xe", SCOUT, "zone:x", 1.0)]},
        {"k": "resolve_goals", "mf": 170, "player": 1, "cat": 4, "mode": "space", "active": [],
         "proposed": [g("0xf", SCOUT, "cell:-3600,400", 10.0)]},
        {"k": "activation", "mf": 170, "goal": "0xf", "ok": 1},
        {"k": "resolve_result", "mf": 170, "player": 1, "cat": 4, "mode": "space", "survivors": ["0xf"], "accepted": ["0xf"]},
        {"k": "goal_release", "mf": 1170, "goal": "0xf"},
    ]
    check(space_player(ev) == 1, "the space AI player is found")
    commits, scouts, accepts, failed = timeline(ev, 1, box, size)
    check(len(commits) == 2 and [x[0] for x in commits[0][1]] == ["Secure_Build_Pad_Space"] * 2 and
          commits[0][2] == [(SCOUT, "CELL_0_0"), (SCOUT, "CELL_0_1")], "first commit: two pads accepted, two scouts failed")
    check(scouts == [{"mf": 170, "cell": "CELL_0_2", "d": 10.0, "end": 1170}] and lifetimes(scouts) == [1000],
          "scout accept, cell and lifetime")
    check(accepts == {"Secure_Build_Pad_Space": 2, SCOUT: 1} and failed == {SCOUT: 2}, "accept and failure counts")
    with tempfile.TemporaryDirectory() as d:
        run = os.path.join(d, "20261117-000000-skirmish-vanilla")
        os.makedirs(run)
        with open(os.path.join(run, "frida_t22.jsonl"), "w", encoding="utf-8") as f:
            for e in ev:
                f.write(json.dumps(e) + "\n")
        log = os.path.join(d, "port_ai.txt")
        with open(log, "w", encoding="ascii") as f:
            f.write("  69 p1 plan 3 SPACESCOUT for Space_Scout @CELL_0_0 MainForce\n"
                    "  69 p1 plan 2 MOVETOLOCATIONRUSH for Secure_Build_Pad_Space @OBJECT_41 MainForce\n"
                    "  1069 p1 plan 3 SPACESCOUT ended success\n")
        ps, pa = port_log(log)
        check(ps == [{"mf": 69, "cell": "CELL_0_0", "d": None, "end": 1069}] and pa[SCOUT] == 1, "port log: scout and end")
        out = []
        r = run_report(run, None, box, size, 5, out)
        summary("retail", [r], out)
        summary("port", [(ps, pa, None)], out)
        text = "\n".join(out)
        check("first scout: CELL_0_2 1" in text and "failed scout accepts per run: 2.0" in text and
              "first scout: CELL_0_0 1" in text, "report text")
    print("SPACE_GOALS SELFTEST " + ("OK" if ok else "FAILED"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("runs", nargs="*", help="retail run folders (frida_t22.jsonl)")
    ap.add_argument("--port", nargs="*", default=[], help="port --ai-log files")
    ap.add_argument("--player", type=int, help="AI player id (default: the one with space Space_Scout goals)")
    ap.add_argument("--box", nargs=4, type=float, default=[-4600, -4600, 4600, 4600])
    ap.add_argument("--size", type=float, default=2000.0)
    ap.add_argument("--commits", type=int, default=8, help="first commits to print per run")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    out = []
    retail = [run_report(r, a.player, a.box, a.size, a.commits, out) for r in a.runs]
    if retail:
        summary("retail", retail, out)
    if a.port:
        summary("port", [port_log(p) + (None,) for p in a.port], out)
    print("\n".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
