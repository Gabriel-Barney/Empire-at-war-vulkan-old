"""skai exit test (tier 2): idle-human Utapau land skirmish, port distribution against the retail runs.

Standard library only. For each AI (Rebel vs idle Empire, Empire vs idle Underworld, Underworld vs idle Empire) it runs
`alamo_headless --skirmish-ai foc-land --map _MP_LAND_UTAPAU` for seeds 1..N, 36000 ticks, reads whether the battle ended
and at which frame, and judges the port against the retail runs in RETAIL below (docs/DECISIONS.md, "skai exit test as a
tier-2 distribution", 2026-11-16):

  rate    the share of games that end: two-sided Fisher exact test, port ends / runs against retail ends / runs,
          pass while p >= 0.05 (a difference small samples cannot tell apart from chance passes);
  frames  only when retail and the port both have ended games: the median end frame of the port's ended games must be
          >= retail's earliest end frame - 3000 (about 100 s of game time). One-sided: the 36000 cap cuts the late
          side, and a port that ends too late ends too rarely, which the rate test sees.

    toolchain\\venv\\Scripts\\python.exe -I tools\\probe_mod\\q2_traces\\analysis\\skai_exit.py
        [--exe build-wt-msvc\\bin\\alamo_headless.exe] [--seeds 10] [--jobs 4] [--out DIR] [--reuse]
    ... skai_exit.py --selftest

--reuse reads <out>/<AI>_s<seed>.out from an earlier run instead of running. Exit 0 = every AI passes, 1 = a check fails.
"""
import argparse
import concurrent.futures as cf
import math
import os
import re
import subprocess
import sys

TICKS = 36000
FRAME_MARGIN = 3000
ALPHA = 0.05

# AI -> (idle human faction, retail runs: (run id under tools/probe_mod/q2_traces/out, end frame or None))
RETAIL = {
    "Rebel": ("Empire", [
        ("20261108-220755 (SKAI_T06_LAND_RUNS s1)", None),
        ("20261112-095520 (s6)", None),
        ("20261116-012157 (s8)", None),
        ("20261116-062402 (s9)", 27265),
    ]),
    "Empire": ("Underworld", [
        ("20261114-210208 (B0418_UTAPAU_AI_RUNS)", None),
        ("20261116-005957 (SKAI_T15_PLAN_RETEST)", 25707),
        ("20261116-065045 (B1)", None),
        ("20261116-071736 (B2)", 23746),
        ("20261116-074211 (B3)", 26522),
        ("20261116-133939 (skai-a; palace falls 28196, trace ends 28404)", 28196),
    ]),
    "Underworld": ("Empire", [
        ("20261114-212910 (B0418_UTAPAU_AI_RUNS)", None),
        ("20261116-140323 (skai-a; command center falls 28391, trace ends ~28599)", 28391),
        ("20261116-142451 (skai-a)", None),
    ]),
}

END_RE = re.compile(r"land battle \S+: (\d+) frames.*?winner (\S+?),")


def fisher_two_sided(a, n1, b, n2):
    """p of a two-sided Fisher exact test on a/n1 against b/n2 (sum of tables no more likely than the observed one)."""
    k = a + b
    n = n1 + n2

    def pmf(x):
        return math.comb(n1, x) * math.comb(n2, k - x) / math.comb(n, k)

    obs = pmf(a)
    lo, hi = max(0, k - n2), min(k, n1)
    return min(1.0, sum(pmf(x) for x in range(lo, hi + 1) if pmf(x) <= obs * (1 + 1e-9)))


def median(v):
    s = sorted(v)
    m = len(s) // 2
    return s[m] if len(s) % 2 else (s[m - 1] + s[m]) / 2


def judge(ai, port_frames, runs):
    """port_frames: end frame or None per port seed. Returns (ok, lines)."""
    retail = [f for _, f in RETAIL[ai][1]]
    r_end = [f for f in retail if f is not None]
    p_end = [f for f in port_frames if f is not None]
    p = fisher_two_sided(len(p_end), len(port_frames), len(r_end), len(retail))
    ok_rate = p >= ALPHA
    lines = [f"{ai} AI vs idle {RETAIL[ai][0]}: port ends {len(p_end)}/{len(port_frames)}"
             f"{' ' + str(sorted(p_end)) if p_end else ''}, retail {len(r_end)}/{len(retail)}"
             f"{' ' + str(sorted(r_end)) if r_end else ''}",
             f"  rate   Fisher p = {p:.3f} -> {'pass' if ok_rate else 'FAIL'} (needs >= {ALPHA})"]
    ok_frames = True
    if r_end and p_end:
        floor = min(r_end) - FRAME_MARGIN
        med = median(p_end)
        ok_frames = med >= floor
        lines.append(f"  frames port median {med:g} vs retail earliest {min(r_end)} - {FRAME_MARGIN} = {floor} -> "
                     f"{'pass' if ok_frames else 'FAIL'}")
    else:
        lines.append("  frames n/a (no ended game on one side)")
    return ok_rate and ok_frames, lines


def parse(text):
    m = END_RE.search(text)
    if not m:
        return None
    frames = int(m.group(1))
    return frames if m.group(2) not in ("-", "none") and frames < TICKS else None


def run_one(exe, out, ai, seed):
    log = os.path.join(out, f"{ai}_s{seed}.out")
    cmd = [exe, "--skirmish-ai", "foc-land", "--map", "_MP_LAND_UTAPAU", "--human", RETAIL[ai][0], "--ai", ai,
           "--seed", str(seed), "--ticks", str(TICKS), "--workers", "1", "--quiet"]
    with open(log, "w") as f:
        subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT)
    return log


def selftest():
    assert abs(fisher_two_sided(10, 10, 3, 5) - 0.0952) < 1e-3
    assert fisher_two_sided(1, 10, 1, 4) > 0.5 and fisher_two_sided(5, 10, 0, 1) == 1.0
    assert fisher_two_sided(10, 10, 0, 5) < 0.001
    line = "  land battle _MP_LAND_UTAPAU: 18429 frames, 158 spawned, winner Empire, victory 14, retreated -"
    assert parse(line) == 18429
    assert parse(line.replace("winner Empire", "winner -")) is None
    assert parse("land battle _MP_LAND_UTAPAU: 36000 frames, winner -, victory -") is None
    ok, _ = judge("Empire", [18000] * 10, None)
    assert not ok
    ok, _ = judge("Empire", [24000] * 6 + [None] * 4, None)
    assert ok
    print("SELFTEST OK")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=os.path.join("build-wt-msvc", "bin", "alamo_headless.exe"))
    ap.add_argument("--seeds", type=int, default=10)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--out", default=os.path.join("tools", "probe_mod", "q2_traces", "out", "skai_exit"))
    ap.add_argument("--reuse", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    os.makedirs(a.out, exist_ok=True)
    seeds = range(1, a.seeds + 1)
    jobs = [(ai, s) for ai in RETAIL for s in seeds]
    if not a.reuse:
        with cf.ThreadPoolExecutor(a.jobs) as ex:
            list(ex.map(lambda t: run_one(a.exe, a.out, *t), jobs))
    ok_all = True
    for ai in RETAIL:
        frames = []
        for s in seeds:
            with open(os.path.join(a.out, f"{ai}_s{s}.out"), errors="replace") as f:
                frames.append(parse(f.read()))
        ok, lines = judge(ai, frames, None)
        ok_all = ok_all and ok
        print("\n".join(lines))
    print("skai exit (tier 2):", "PASS" if ok_all else "FAIL")
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
