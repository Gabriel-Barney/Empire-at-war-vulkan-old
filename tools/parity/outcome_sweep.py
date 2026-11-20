"""AI-vs-AI outcome sweeps: one skirmish scenario over seeds 1..N, judged by the share of seeds, not by one seed.

Standard library only; `parity_harness.py outcome` is the command line, this module the reader and the judge (ref4 t04,
2026-11-19). Before, every "battle ends" ctest in engine/apps/CMakeLists.txt was pinned to one hand-picked seed and
re-pinned whenever a sim change moved that seed's battle (21 re-pins in two weeks); a random-driven outcome is tier 2
(docs/DECISIONS.md "Parity tiers"): its distribution over seeds is what must hold. Determinism is proved by the CRC
golden runs (one fixed seed each), not here.

A scenario (tools/parity/outcomes.json) is the alamo_headless arguments without --seed, the seed count and what must
hold over those seeds:

  min_end_share   the share of seeds whose battle ends with a winner before the tick limit (default 0: no end needed);
  end_frames      [lo, hi]: the end frame of every ended seed lies in this range;
  max_lua_seeds   seeds allowed to log a Lua error that is not one of retail's documented errors (default 0;
                  skirmish_run.cpp kRetailLuaErrors, docs/DECISIONS.md "Retail Lua errors in the skai exit test");
  known_lua       [{"text", "group", "why"}]: a Lua error containing text is a known port bug that the open roadmap
                  group owns: reported with its group, not counted against max_lua_seeds; drop the entry when the
                  group lands its fix (any other non-retail error still fails);
  retail          [[run, end frame or null], ...]: the skai exit test (docs/DECISIONS.md "skai exit test as a tier-2
                  distribution", formerly tools/probe_mod/q2_traces/analysis/skai_exit.py): the port's end rate
                  against retail's by a two-sided Fisher exact test (pass while p >= 0.05), and, when both sides have
                  ended games, the median port end frame >= retail's earliest end frame - 3000 (one-sided: the tick cap
                  cuts the late side, and a port that ends too late ends too rarely, which the rate test sees).

Every seed must also run to its end without a crash, and a seed whose run fails one of alamo_headless's own checks
(--expect-battle-end's station-retreat and proposed-goal checks) for any reason but "not ended" or a Lua error fails
the scenario.
"""
import math
import os
import re
import subprocess

ALPHA = 0.05
FRAME_MARGIN = 3000
KEYS = {"args", "seeds", "why", "min_end_share", "end_frames", "max_lua_seeds", "known_lua", "retail"}

SUMMARY_RE = re.compile(r"^skirmish-ai \S+ workers -?\d+: (\d+) ticks .*?, (\d+) Lua errors, final CRC", re.M)
OUTCOME_RE = re.compile(r"^battle outcome: (ended|NOT ENDED), winner (\S+), victory (\S+), (\d+) of (\d+) frames", re.M)
BATTLE_RE = re.compile(r"^  (?:land|space) battle [^:]*: (\d+) frames.*?, winner (\S+?), victory (\S+?)(?:,|$)", re.M)
RETAIL_LUA = "  Lua error (retail, documented): "
OTHER_LUA = "  Lua error: "


def ticks_of(args):
    return int(args[args.index("--ticks") + 1]) if "--ticks" in args else None


def parse_run(text, ticks, rc=0):
    """One alamo_headless --skirmish-ai run's output -> its outcome."""
    r = {"rc": rc, "ran": False, "ended": False, "frame": None, "winner": "-", "victory": "-", "lua_total": 0,
         "lua_retail": text.count(RETAIL_LUA),
         "lua_texts": [ln[len(OTHER_LUA):] for ln in text.splitlines() if ln.startswith(OTHER_LUA)]}
    s = SUMMARY_RE.search(text)
    if s:
        r["ran"] = True
        r["lua_total"] = int(s.group(2))
    o = OUTCOME_RE.search(text)
    if o:  # --expect-battle-end: the host's own verdict
        r.update(ended=o.group(1) == "ended", winner=o.group(2), victory=o.group(3), frame=int(o.group(4)))
    else:
        b = BATTLE_RE.search(text)
        if b:
            frame = int(b.group(1))
            r.update(winner=b.group(2), victory=b.group(3), frame=frame,
                     ended=b.group(2) not in ("-", "none") and (ticks is None or frame < ticks))
    if not r["ended"]:
        r["frame"] = None
    r["lua_other"] = max(0, r["lua_total"] - r["lua_retail"])
    return r


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


def known_count(scen, r):
    """Non-retail Lua errors of a run that are a known port bug of the scenario (known_lua)."""
    texts = [k["text"] for k in scen.get("known_lua", [])]
    return sum(1 for t in r["lua_texts"] if any(k in t for k in texts))


def seed_text(seed, r):
    if not r["ran"]:
        return "seed %d: CRASHED or no summary (exit %s)" % (seed, r["rc"])
    t = "seed %d: %s" % (seed, ("ended %d, winner %s, victory %s" % (r["frame"], r["winner"], r["victory"]))
                         if r["ended"] else "no end")
    if r["lua_total"]:
        t += ", Lua errors %d (%d retail)" % (r["lua_total"], r["lua_retail"])
    return t


def judge(sid, scen, runs):
    """runs: [(seed, parse_run result)]. Returns (ok, report lines)."""
    n = len(runs)
    expect_end = "--expect-battle-end" in scen["args"]
    ended = [(s, r["frame"]) for s, r in runs if r["ended"]]
    crashed = [s for s, r in runs if not r["ran"] or r["rc"] not in (0, 1)]
    known_seeds = [s for s, r in runs if known_count(scen, r)]
    lua_seeds = [s for s, r in runs if r["lua_other"] > known_count(scen, r)]
    # rc 1 with nothing that explains it: one of the host's own checks failed (station retreat, proposed goals).
    other = [s for s, r in runs if r["ran"] and r["rc"] == 1 and not r["lua_other"] and (r["ended"] or not expect_end)]
    lines = ["outcome %s: %d seeds (%d..%d), %s" % (sid, n, runs[0][0], runs[-1][0], " ".join(scen["args"]))]
    ok = True

    def verdict(cond, text):
        nonlocal ok
        ok = ok and cond
        lines.append("  %-5s %s" % ("pass" if cond else "FAIL", text))

    verdict(not crashed, "every seed runs to its end (crashed: %s)" % (crashed or "none"))
    need = float(scen.get("min_end_share", 0))
    share = len(ended) / n if n else 0.0
    verdict(share >= need - 1e-9, "ended %d/%d = %.0f%% (need >= %.0f%%)" % (len(ended), n, 100 * share, 100 * need))
    if "end_frames" in scen and ended:
        lo, hi = scen["end_frames"]
        out = [s for s, f in ended if not lo <= f <= hi]
        verdict(not out, "end frames %d..%d (need %d..%d; outside: %s)" % (
            min(f for _, f in ended), max(f for _, f in ended), lo, hi, out or "none"))
    allowed = int(scen.get("max_lua_seeds", 0))
    verdict(len(lua_seeds) <= allowed, "seeds with a non-retail Lua error: %s (allowed %d)" % (lua_seeds or "none", allowed))
    for k in scen.get("known_lua", []):
        hit = [s for s, r in runs if any(k["text"] in t for t in r["lua_texts"])]
        lines.append("  known %s, port bug of %s: seeds %s" % (k["text"][:70], k["group"], hit or "none"))
    verdict(not other, "seeds failing another check of the run: %s" % (other or "none"))
    if "retail" in scen:
        retail = [f for _, f in scen["retail"]]
        r_end = [f for f in retail if f is not None]
        p = fisher_two_sided(len(ended), n, len(r_end), len(retail))
        verdict(p >= ALPHA, "retail rate: port %d/%d, retail %d/%d %s, Fisher p = %.3f (need >= %g)" % (
            len(ended), n, len(r_end), len(retail), sorted(r_end), p, ALPHA))
        if r_end and ended:
            floor = min(r_end) - FRAME_MARGIN
            med = median([f for _, f in ended])
            verdict(med >= floor, "retail frames: port median %g >= retail earliest %d - %d = %d" % (
                med, min(r_end), FRAME_MARGIN, floor))
        else:
            lines.append("  n/a   retail frames (no ended game on one side)")
    lines += ["    " + seed_text(s, r) for s, r in runs]
    lines.append("outcome %s: %s" % (sid, "PASS" if ok else "FAIL"))
    return ok, lines


def check_scenarios(scens):
    """Problems with outcomes.json (unknown keys, missing fields); [] when it is well formed."""
    bad = []
    for sid, s in scens.items():
        if sid.startswith("_"):
            continue
        for k in set(s) - KEYS:
            bad.append("%s: unknown key %s" % (sid, k))
        for k in ("args", "seeds", "why"):
            if k not in s:
                bad.append("%s: no %s" % (sid, k))
        if "--seed" in s.get("args", []):
            bad.append("%s: --seed in args (the sweep adds it)" % sid)
        if ticks_of(s.get("args", [])) is None:
            bad.append("%s: no --ticks" % sid)
        for k in s.get("known_lua", []):
            if set(k) != {"text", "group", "why"}:
                bad.append("%s: known_lua entry needs text, group and why" % sid)
    return bad


def run_seed(exe, out_dir, scen, seed, reuse=False):
    """Runs (or, with reuse, rereads) one seed; returns (seed, parse_run result). Log: out_dir/seed_<s>.out."""
    log = os.path.join(out_dir, "seed_%d.out" % seed)
    rc_path = log + ".rc"
    if not (reuse and os.path.exists(log) and os.path.exists(rc_path)):
        cmd = [exe] + list(scen["args"]) + ["--seed", str(seed)]
        with open(log, "w", encoding="utf-8", errors="replace") as fh:
            rc = subprocess.run(cmd, stdout=fh, stderr=subprocess.STDOUT).returncode
        with open(rc_path, "w") as fh:
            fh.write("%d\n" % rc)
    with open(log, "r", encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    with open(rc_path) as fh:
        rc = int(fh.read().strip() or "0")
    return seed, parse_run(text, ticks_of(scen["args"]), rc)


def selftest(check):
    """Synthetic outputs only (no game data). check(cond, what) records a failure."""
    check(abs(fisher_two_sided(10, 10, 3, 5) - 0.0952) < 1e-3, "outcome: Fisher p of 10/10 vs 3/5")
    check(fisher_two_sided(1, 10, 1, 4) > 0.5 and fisher_two_sided(5, 10, 0, 1) == 1.0, "outcome: Fisher p near 1")
    check(fisher_two_sided(10, 10, 0, 5) < 0.001, "outcome: Fisher p of 10/10 vs 0/5")
    head = "skirmish-ai foc-land workers 0: 36000 ticks (1 ms), AI orders 5 rejected 0, 3 decisions, %d Lua errors, " \
           "final CRC 0123456789abcdef\n"
    land = "  land battle _MP_LAND_UTAPAU: 18429 frames, 158 spawned, winner Empire, victory 14, retreated -\n"
    r = parse_run(head % 0 + land, 36000)
    check(r["ran"] and r["ended"] and r["frame"] == 18429 and r["winner"] == "Empire", "outcome: land battle line")
    r = parse_run(head % 0 + land.replace("winner Empire", "winner -"), 36000)
    check(r["ran"] and not r["ended"] and r["frame"] is None, "outcome: no winner is no end")
    r = parse_run(head % 0 + land.replace("18429", "36000"), 36000)
    check(not r["ended"], "outcome: an end at the tick limit is no end")
    space = "  space battle _MP_SPACE_GEONOSIS: 7418 frames, winner Empire, victory all-destroyed\n"
    check(parse_run(head % 0 + space, 16000)["frame"] == 7418, "outcome: space battle line")
    exp = "battle outcome: NOT ENDED, winner -, victory -, 20000 of 20000 frames\n"
    r = parse_run(head % 0 + land + exp, 20000)
    check(not r["ended"], "outcome: the host's battle outcome line wins over the battle log")
    r = parse_run(head % 3 + "  Lua error (retail, documented): Move_To -- Target is already dead\n" * 2, 36000)
    check(r["lua_total"] == 3 and r["lua_retail"] == 2 and r["lua_other"] == 1, "outcome: retail Lua errors apart")
    check(not parse_run("skirmish-ai: run failed\n", 100, 3)["ran"], "outcome: no summary line is a crash")

    def runs(frames, lua=(), rc=None):
        out = []
        for i, f in enumerate(frames, 1):
            text = head % (1 if i in lua else 0) + (land.replace("18429", str(f)) if f else "")
            out.append((i, parse_run(text, 36000, (rc or {}).get(i, 1 if i in lua else 0))))
        return out
    sc = {"args": ["--ticks", "36000"], "seeds": 4, "why": "t", "min_end_share": 0.5, "end_frames": [10000, 35999]}
    check(judge("x", sc, runs([12000, None, 15000, None]))[0], "outcome: 2/4 ended passes at 50%")
    check(not judge("x", sc, runs([12000, None, None, None]))[0], "outcome: 1/4 ended fails at 50%")
    check(not judge("x", sc, runs([5000, 12000, None, None]))[0], "outcome: an end before the range fails")
    check(not judge("x", sc, runs([12000, 13000, None, None], lua=(3,)))[0], "outcome: a non-retail Lua error fails")
    check(judge("x", dict(sc, max_lua_seeds=1), runs([12000, 13000, None, None], lua=(3,)))[0],
          "outcome: max_lua_seeds allows one")
    check(not judge("x", sc, runs([12000, 13000, None, None], rc={1: 1}))[0], "outcome: an unexplained exit 1 fails")
    mdu = head % 1 + "  Lua error: AI_PLAN_X: SCRIPT ERROR: GameObjectWrapper::Build -- Object U is not a valid build location.\n"
    kr = runs([12000, 13000, None, None])
    kr[2] = (3, parse_run(mdu, 36000, 1))
    kn = dict(sc, known_lua=[{"text": "is not a valid build location", "group": "mdu1", "why": "w"}])
    check(judge("x", kn, kr)[0] and not judge("x", sc, kr)[0], "outcome: a known_lua error is reported, not failed")
    kr[3] = (4, parse_run(mdu.replace("valid build location", "valid target") , 36000, 1))
    check(not judge("x", kn, kr)[0], "outcome: another non-retail error still fails beside a known one")
    check(not judge("x", sc, runs([12000, 13000, None, None], rc={2: 3}))[0], "outcome: a crash exit fails")
    ex = dict(sc, args=["--ticks", "36000", "--expect-battle-end"])
    check(judge("x", ex, runs([12000, 13000, None, None], rc={3: 1, 4: 1}))[0],
          "outcome: exit 1 of a seed that did not end is its no-end, not another failure")
    # The skai exit judgement (formerly skai_exit.py's selftest): Empire AI, retail 5 of 7 ended from 22544 on.
    rt = [["a", None], ["b", 25707], ["c", None], ["d", 23746], ["e", 26522], ["f", 28196], ["g", 22544]]
    sk = {"args": ["--ticks", "36000"], "seeds": 10, "why": "t", "retail": rt}
    check(not judge("x", sk, runs([18000] * 10))[0], "outcome: retail frames - every port end too early fails")
    check(judge("x", sk, runs([24000] * 6 + [None] * 4))[0], "outcome: retail rate and frames pass")
    check(check_scenarios({"a": {"args": ["--ticks", "1"], "seeds": 1, "why": "w"}}) == [], "outcome: a good scenario")
    check(len(check_scenarios({"a": {"args": ["--seed", "1"], "seeds": 1, "bogus": 1}})) == 4,
          "outcome: unknown key, no why, --seed, no --ticks")
