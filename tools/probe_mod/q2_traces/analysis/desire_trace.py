"""skai t15: the Destroy_Structure goal desire per enemy base structure over time, retail against the port.

Standard library only. Retail: the TDESIRE records of a run folder's lua_records.txt (GameScoring probe, every 10th
tactical sample, about every 10 s): d = EvaluatePerception("Should_Destroy_Structure", ai, structure). Port: the
`Should_Destroy_Structure: <obj>=<d> ...` lines that `alamo_headless --skirmish-ai ... --ai-log FILE` writes every 600
frames, with the object types from that log's object list. For the Underworld base types the equation is
0.001 + 15 * (1 - EnemyForce at the target's location), so EF = 1 - (d - 0.001) / 15 is printed too.

    toolchain\\venv\\Scripts\\python.exe -I tools\\probe_mod\\q2_traces\\analysis\\desire_trace.py
        [--retail <run dir>] [--port <ai log>] [--ai <retail player id, default 1>] [--step 1800]
    ... desire_trace.py --selftest
"""
import argparse
import collections
import os
import re
import sys

TDESIRE = re.compile(r" TDESIRE lframe=(\S+) ai=(\S+) obj=(\S+)#(\S+) d=(\S+)")
PORT_D = re.compile(r"^\s*Should_Destroy_Structure: (.*)$")
PORT_T = re.compile(r"^state t=(\d+)")
PORT_OBJ = re.compile(r"(\d+):([A-Za-z_0-9]+)/p(\d+)")


def ef(d):
    return 1.0 - (d - 0.001) / 15.0


def retail_series(run, ai):
    out = collections.defaultdict(list)  # "TYPE#id" -> [(frame, d)]
    with open(os.path.join(run, "lua_records.txt"), encoding="latin-1") as f:
        for line in f:
            m = TDESIRE.search(line)
            if not m or m.group(2) != ai or m.group(5) == "?":
                continue
            try:
                frame, d = float(m.group(1)), float(m.group(5))
            except ValueError:
                continue
            out[m.group(3) + "#" + m.group(4)].append((int(round(frame)), d))
    return out


def port_series(log):
    names = {}
    out = collections.defaultdict(list)
    t = 0
    with open(log, encoding="latin-1") as f:
        for line in f:
            m = PORT_T.match(line)
            if m:
                t = int(m.group(1))
                continue
            for oid, tn, _ in PORT_OBJ.findall(line):
                names.setdefault(oid, tn.upper())
            m = PORT_D.match(line)
            if m:
                for item in m.group(1).split():
                    oid, _, val = item.partition("=")
                    try:
                        d = float(val)
                    except ValueError:
                        continue  # n/a: not evaluated
                    out[names.get(oid, "?") + "#" + oid].append((t, d))
    return out


def show(title, series, step):
    print(title)
    for key in sorted(series):
        pts, last, row = series[key], -step, []
        for frame, d in pts:
            if frame - last >= step:
                last = frame
                row.append("%d:%.2f(ef %.2f)" % (frame, d, ef(d)))
        print("  %-34s %s" % (key, " ".join(row)))


def selftest():
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        with open(os.path.join(tmp, "lua_records.txt"), "w", encoding="latin-1") as f:
            f.write("T 0001 5 TDESIRE lframe=300 ai=1 obj=U_GROUND_PALACE#77 d=15.001\n")
            f.write("T 0001 6 TDESIRE lframe=600 ai=1 obj=U_GROUND_PALACE#77 d=7.501\n")
            f.write("T 0001 7 TDESIRE lframe=600 ai=0 obj=E_GROUND_BARRACKS#9 d=?\n")
        r = retail_series(tmp, "1")
        assert r == {"U_GROUND_PALACE#77": [(300, 15.001), (600, 7.501)]}, r
        log = os.path.join(tmp, "ai.log")
        with open(log, "w", encoding="latin-1") as f:
            f.write("  bases: 790:U_Ground_Palace/p0 hull 2400/2400\n")
            f.write("state t=601 battle frame 600: p0 credits 1\n    Should_Destroy_Structure: 790=5.579541 791=n/a\n")
        p = port_series(log)
        assert p == {"U_GROUND_PALACE#790": [(601, 5.579541)]}, p
        assert abs(ef(7.501) - 0.5) < 1e-9
    print("SELFTEST OK")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--retail")
    ap.add_argument("--port")
    ap.add_argument("--ai", default="1")
    ap.add_argument("--step", type=int, default=1800)
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if a.retail:
        show("retail %s (ai %s)" % (a.retail, a.ai), retail_series(a.retail, a.ai), a.step)
    if a.port:
        show("port %s" % a.port, port_series(a.port), a.step)
    return 0


if __name__ == "__main__":
    sys.exit(main())
