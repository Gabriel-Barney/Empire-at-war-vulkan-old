#!/usr/bin/env python3
"""skai t22: which space cells the Rebel AI scouts on Geonosis, retail against the port (docs/re/SKAI_T22_GEONOSIS_SCOUT.md).

Retail: a run of `run_skirmish.cmd -LuaProbe` (q2_traces kit). Reads <run>/lua_records.txt, the plan records of the
space hooks (make_plan_hooks.py: Space_Scout, Secure_Build_Pad_Space, Build_Refinery_Space,
Tactical_Multiplayer_Build_Space_Units_Generic), and <run>/trace.q2t for the positions of the probe's reference
objects. Each Space_Scout PSTART carries its target's distance to three objects that stand still (own = the AI's
station, emp = the Empire station, pad = a mineral pad, as <type>#<id>:<distance>); the cell is the grid cell whose
centre fits those distances best (grid: the camera box and cell size of the map, Geonosis by default).

Port: an `alamo_headless --skirmish-ai ... --ai-log FILE` log, whose "plan N SPACESCOUT for Space_Scout @CELL_x_y"
lines name the cell directly.

Prints per run the scouts in order (frame, cell, desire d and its terms dfs / des, fit residual), then a summary over
the runs: how often each cell is the first scout, the share of scouts per cell, plan starts per plan and the unit types
the build plan's blocks returned. The question it answers (t22): does retail's first scout take CELL_0_2 where the port
takes CELL_0_0 from the same tie of desire 10?

    toolchain\\venv\\Scripts\\python.exe -I tools\\probe_mod\\q2_traces\\analysis\\space_scout.py <run dir>... [--port LOG...]
    ... space_scout.py --selftest
"""
import argparse
import collections
import math
import os
import re
import struct
import sys
import tempfile

REC = re.compile(r"^P \S+ \d+ (\w+) (.*)$")
KV = re.compile(r"(\w+)=(\S*)")
REF = re.compile(r"^([A-Za-z0-9_\-]+)#(-?\d+):(\S+)$")
UNITS = re.compile(r" units=\d+((?: \S+)*)\s*$")
PORT_SCOUT = re.compile(r"^\s*(\d+) p(\d+) plan (\d+) (\w+) for (\w+) @(\S+)")
SCOUT = "Space_Scout"


def f32(h):
    return struct.unpack(">f", bytes.fromhex(h))[0]


def read_records(path):
    out = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = REC.match(line.rstrip("\n"))
            if m:
                kv = dict(KV.findall(m.group(2)))
                u = UNITS.search(m.group(2))
                kv["unit_types"] = u.group(1).split() if u else []
                out.append((m.group(1), kv))
    return out


def object_positions(trace, ids):
    """First (x, y) of each wanted object id in a q2 trace (O lines after the header)."""
    pos = {}
    if not ids or not os.path.isfile(trace):
        return pos
    hdr = True
    with open(trace, encoding="ascii", errors="replace") as f:
        for line in f:
            p = line.split()
            if not p:
                continue
            if hdr:
                if p[0] == "end_header":
                    hdr = False
                continue
            if p[0] == "O" and len(p) > 5:
                oid = int(p[1])
                if oid in ids and oid not in pos:
                    pos[oid] = (f32(p[4]), f32(p[5]))
                    if len(pos) == len(ids):
                        break
    return pos


def cells(box, size):
    x0, y0, x1, y1 = box
    nx = int(math.ceil((x1 - x0) / size))
    ny = int(math.ceil((y1 - y0) / size))
    return {"CELL_%d_%d" % (x, y): (x0 + (x + 0.5) * size, y0 + (y + 0.5) * size) for x in range(nx) for y in range(ny)}


def fit_cell(grid, refs):
    """(cell, rms residual) for refs [(x, y, distance)]; None without refs."""
    if not refs:
        return None, None
    best = None
    for name, (cx, cy) in grid.items():
        e = sum((math.hypot(cx - x, cy - y) - d) ** 2 for x, y, d in refs)
        if best is None or e < best[1]:
            best = (name, e)
    return best[0], math.sqrt(best[1] / len(refs))


def retail_run(run, grid):
    recs = read_records(os.path.join(run, "lua_records.txt"))
    ids = set()
    for kind, kv in recs:
        if kind == "PSTART" and kv.get("plan") == SCOUT:
            for k in ("own", "emp", "pad"):
                m = REF.match(kv.get(k, ""))
                if m:
                    ids.add(int(m.group(2)))
    pos = object_positions(os.path.join(run, "trace.q2t"), ids)
    scouts, starts, units = [], collections.Counter(), collections.Counter()
    for kind, kv in recs:
        plan = kv.get("plan", "?")
        if kind == "PSTART":
            starts[plan] += 1
        if kind == "PBLOCK" and plan == "Tactical_Multiplayer_Build_Space_Units_Generic":
            for u in kv["unit_types"]:
                units[u] += 1
        if kind != "PSTART" or plan != SCOUT:
            continue
        refs = []
        for k in ("own", "emp", "pad"):
            m = REF.match(kv.get(k, ""))
            if m and int(m.group(2)) in pos:
                try:
                    refs.append(pos[int(m.group(2))] + (float(m.group(3)),))
                except ValueError:
                    pass
        cell, res = fit_cell(grid, refs)
        scouts.append({"frame": kv.get("frame", "?"), "player": kv.get("player", "?"), "cell": cell or "?",
                       "res": res, "d": kv.get("d", "?"), "dfs": kv.get("dfs", "?"), "des": kv.get("des", "?"),
                       "refs": len(refs)})
    return scouts, starts, units


def port_log(path):
    scouts, starts = [], collections.Counter()
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = PORT_SCOUT.match(line)
            if not m:
                continue
            starts[m.group(5)] += 1
            if m.group(5) == SCOUT:
                scouts.append({"frame": m.group(1), "player": "p" + m.group(2), "cell": m.group(6).lstrip("@"),
                               "res": 0.0, "d": "-", "dfs": "-", "des": "-", "refs": 0})
    return scouts, starts, collections.Counter()


def report(label, runs, out):
    first = collections.Counter()
    share = collections.Counter()
    starts_all = collections.Counter()
    units_all = collections.Counter()
    n = 0
    for name, (scouts, starts, units) in runs:
        n += 1
        out.append("%s %s: %d scouts" % (label, name, len(scouts)))
        for s in scouts:
            res = "-" if s["res"] is None else "%.0f" % s["res"]
            out.append("  f %6s %-6s %-9s d=%s dfs=%s des=%s fit=%s refs=%d" % (
                s["frame"], s["player"], s["cell"], s["d"], s["dfs"], s["des"], res, s["refs"]))
        if scouts:
            first[scouts[0]["cell"]] += 1
        for s in scouts:
            share[s["cell"]] += 1
        starts_all.update(starts)
        units_all.update(units)
    tot = sum(share.values()) or 1
    out.append("%s summary over %d runs:" % (label, n))
    out.append("  first scout: " + ", ".join("%s %d" % kv for kv in first.most_common()))
    out.append("  scouts per cell: " + ", ".join("%s %d (%.0f%%)" % (c, k, 100.0 * k / tot) for c, k in share.most_common()))
    out.append("  plan starts per run: " + ", ".join("%s %.1f" % (p, k / max(n, 1)) for p, k in sorted(starts_all.items())))
    if units_all:
        out.append("  units built per run (build plan blocks): " +
                   ", ".join("%s %.1f" % (u, k / max(n, 1)) for u, k in units_all.most_common()))


def selftest():
    grid = cells((-4600, -4600, 4600, 4600), 2000)
    ok = True

    def check(c, what):
        nonlocal ok
        print(("  ok    " if c else "  FAIL  ") + what)
        ok = ok and c
    check(grid["CELL_0_0"] == (-3600, -3600) and grid["CELL_0_2"] == (-3600, 400) and len(grid) == 25,
          "Geonosis grid: 5 x 5 cells, CELL_0_0 at (-3600,-3600)")
    with tempfile.TemporaryDirectory() as d:
        stations = {901: (3046.0, 2606.0), 902: (-3155.0, -2530.0), 903: (-1278.0, -3756.0)}
        with open(os.path.join(d, "trace.q2t"), "w", encoding="ascii") as f:
            f.write("Q2TRACE 1\nend_header\nF 1 1 1 0 0 0 0\n")
            for oid, (x, y) in stations.items():
                f.write("O %d 0 1 %s %s 00000000 0 0 0 0 0 -1 -1 -1 0 0\n" % (
                    oid, struct.pack(">f", x).hex(), struct.pack(">f", y).hex()))
        cx, cy = grid["CELL_0_2"]
        refs = " ".join("%s=T#%d:%.1f" % (k, oid, math.hypot(cx - x, cy - y))
                        for k, (oid, (x, y)) in zip(("own", "emp", "pad"), stations.items()))
        with open(os.path.join(d, "lua_records.txt"), "w", encoding="ascii") as f:
            f.write("Q2LUARECORDS 1\n")
            f.write("P 0001 1 PSTART t=50 frame=1500 plan=Space_Scout player=REBEL target=x tf=MainForce n=1 "
                    "d=10 dfs=6000 des=9.9999998e+17 %s count=0 units=0\n" % refs)
            f.write("P 0001 2 PSTART t=1 frame=30 plan=Tactical_Multiplayer_Build_Space_Units_Generic tf=ReserveForce\n")
            f.write("P 0001 3 PBLOCK t=2 frame=60 plan=Tactical_Multiplayer_Build_Space_Units_Generic tf=ReserveForce "
                    "k=1 count=2 units=2 X-WING Y-WING\n")
        scouts, starts, units = retail_run(d, grid)
        check(len(scouts) == 1 and scouts[0]["cell"] == "CELL_0_2" and scouts[0]["res"] < 1.0 and scouts[0]["refs"] == 3,
              "retail: the scout's cell from three reference distances %s" % scouts)
        check(starts["Space_Scout"] == 1 and units == {"X-WING": 1, "Y-WING": 1}, "retail: plan starts and built units")
        log = os.path.join(d, "port_ai.txt")
        with open(log, "w", encoding="ascii") as f:
            f.write("  1071 p1 plan 6 SPACESCOUT for Space_Scout @CELL_0_0 MainForce\n"
                    "  1576 p1 plan 7 SPACESCOUT for Space_Scout @CELL_0_2 MainForce\n")
        ps, pst, _ = port_log(log)
        check([s["cell"] for s in ps] == ["CELL_0_0", "CELL_0_2"] and pst["Space_Scout"] == 2, "port: cells from the AI log")
        out = []
        report("retail", [("r1", (scouts, starts, units))], out)
        check(any("first scout: CELL_0_2 1" in x for x in out), "report: first-scout tally")
    print("SPACE_SCOUT SELFTEST " + ("OK" if ok else "FAILED"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("runs", nargs="*", help="retail run folders (lua_records.txt + trace.q2t)")
    ap.add_argument("--port", nargs="*", default=[], help="port --ai-log files")
    ap.add_argument("--box", nargs=4, type=float, default=[-4600, -4600, 4600, 4600],
                    help="space region grid box (camera box; Geonosis default)")
    ap.add_argument("--size", type=float, default=2000.0, help="AI_SpaceEvaluatorRegionSize")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    grid = cells(a.box, a.size)
    out = []
    if a.runs:
        report("retail", [(os.path.basename(os.path.normpath(r)), retail_run(r, grid)) for r in a.runs], out)
    if a.port:
        report("port", [(os.path.basename(os.path.dirname(p)) or p, port_log(p)) for p in a.port], out)
    print("\n".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
