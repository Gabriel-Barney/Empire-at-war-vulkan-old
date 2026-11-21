#!/usr/bin/env python3
"""Test tiers from measured cost, with budgets (wr3 t03).

Tiers: inner (the affected tests, tools/lint/affected_tests.py), landing (every test whose measured time is under the
per-test limit: the fast tier the verifier runs) and nightly (the rest: ctest label `slow`). The tier of each test is
derived from the last nightly run's timings, not from hand labels, and written to tools/lint/test_tiers.json; the
top-level CMakeLists.txt applies it after every test is registered (a nightly test gets the `slow` label, a landing
test loses a hand `slow` label; a test the file does not list keeps its hand label until the next regen). A landing
test that needs the fixture of a nightly test is nightly too.

The nightly run is ctest -j 8 with three trees building, the landing run -j 4: nightly times are scaled by the median
ratio of the tests both runs have (printed) before they are held against the limit.

  test_tiers.py regen [--nightly JUNIT] [--landing JUNIT] [--tree TREE]   rewrite test_tiers.json (lists each move)
  test_tiers.py exclude <tree> <out file>   the tests outside the landing tier (ctest --exclude-from-file; build_agent.cmd's
                                          fast tier); ALAMO_WC_SLOT=<k> also leaves out all but the k-th count of each
                                          worker-count group (the verifier rotates k per landing, wr3 t04)
  test_tiers.py check [--landing JUNIT]   budget lint: landing wall time over BUDGET_S, or a landing test over LIMIT_S,
                                          fails and names the tests to move or shorten
  test_tiers.py --selftest
Defaults: nightly = <main checkout>/.claude/worktrees/gate-dev/build-msvc/ctest_junit.xml, landing =
<main checkout>/.claude/worktrees/verifier/build-verify-msvc/ctest_junit.xml, tree = this checkout's build-wt-msvc.
"""

from __future__ import annotations

import json
import os
import statistics
import subprocess
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import build_trees  # noqa: E402

TIERS = os.path.join(HERE, "test_tiers.json")
LIMIT_S = 30.0    # a landing test longer than this (landing-run seconds) goes nightly
BUDGET_S = 180.0  # landing tier wall time on the reference PC
PROMOTE_S = 5.0   # a hand-labelled slow test this cheap moves to landing ...
TARGET_S = 100.0  # ... while the landing tier's estimated wall time stays under this (headroom under BUDGET_S)


def main_checkout() -> str:
    r = subprocess.run(["git", "-C", ROOT, "rev-parse", "--path-format=absolute", "--git-common-dir"],
                       capture_output=True, text=True)
    return os.path.dirname(r.stdout.strip()) if r.returncode == 0 else ROOT


def default_nightly() -> str:
    return os.path.join(main_checkout(), ".claude", "worktrees", "gate-dev", build_trees.main_tree("msvc"), "ctest_junit.xml")


def default_landing() -> str:
    return os.path.join(main_checkout(), ".claude", "worktrees", "verifier", build_trees.agent_tree("msvc", "verify"),
                        "ctest_junit.xml")


def read_junit(path: str) -> tuple[dict[str, float], float]:
    """{test: seconds}, wall seconds of the run."""
    root = ET.parse(path).getroot()
    times = {t.get("name"): float(t.get("time") or 0) for t in root.iter("testcase")}
    return times, float(root.get("time") or 0)


def fixtures(tree: str) -> tuple[dict[str, list[str]], dict[str, str], set[str]]:
    """{test: fixtures it requires}, {fixture: its setup test}, the tests that compare a stored golden (--golden or a
    testdata / golden path on their command line)."""
    ctest = os.path.join(ROOT, "toolchain", "venv", "Scripts", "ctest.exe")
    p = subprocess.run([ctest, "--test-dir", tree, "--show-only=json-v1"], capture_output=True, text=True, errors="replace")
    try:
        tests = json.loads(p.stdout).get("tests", [])
    except ValueError:
        return {}, {}, set()
    req, setup, golden = {}, {}, set()
    for t in tests:
        props = {x.get("name"): x.get("value") for x in t.get("properties", [])}
        req[t["name"]] = props.get("FIXTURES_REQUIRED") or []
        for f in props.get("FIXTURES_SETUP") or []:
            setup[f] = t["name"]
        cmd = " ".join(t.get("command") or []).replace("\\", "/")
        if "--golden" in cmd or "/testdata/" in cmd or "/golden" in cmd:
            golden.add(t["name"])
    return req, setup, golden


def derive(nightly: dict[str, float], landing: dict[str, float], req: dict[str, list[str]], setup: dict[str, str],
           hand_slow: set[str], parallel: float = 4.0, limit: float = LIMIT_S,
           no_promote: set[str] = frozenset()) -> tuple[dict, float]:
    """({test: {"s": landing-scale seconds, "tier": ...}}, the scale applied to nightly times).
    Over the limit: nightly. Under it: landing, except a hand-labelled slow test, which moves to landing only when it
    is cheap (PROMOTE_S), needs no fixture outside the landing tier, is not in no_promote (stored-golden comparisons:
    a stale slow-tier golden would block every landing, and the verifier's Behaviour: check already compares the
    scenarios) and the landing tier's estimated wall (sum / parallel) stays under TARGET_S, cheapest first; the rest
    stay nightly (candidates for the next regen)."""
    both = [nightly[n] / landing[n] for n in nightly if n in landing and landing[n] > 0.5 and nightly[n] > 0]
    scale = statistics.median(both) if both else 1.0
    secs = {n: (landing[n] if n in landing else nightly[n] / scale) for n in set(nightly) | set(landing)}
    tier = {n: ("nightly" if s > limit or n in hand_slow else "landing") for n, s in secs.items()}
    wall = sum(s for n, s in secs.items() if tier[n] == "landing") / parallel
    for n in sorted((n for n in secs if tier[n] == "nightly" and secs[n] <= PROMOTE_S and n not in no_promote),
                    key=lambda n: (secs[n], n)):
        if any(tier.get(setup.get(f, ""), "nightly") != "landing" for f in req.get(n, [])):
            continue
        cost = secs[n] / parallel
        if wall + cost <= TARGET_S:
            tier[n] = "landing"
            wall += cost
    changed = True
    while changed:  # a test that needs a nightly test's fixture is nightly too
        changed = False
        for n in tier:
            if tier[n] == "landing" and any(tier.get(setup.get(f, ""), "landing") == "nightly" for f in req.get(n, [])):
                tier[n] = "nightly"
                changed = True
    return {n: {"s": round(secs[n], 1), "tier": tier[n]} for n in sorted(tier)}, scale


def regen(nightly_path: str, landing_path: str, tree: str) -> int:
    nightly, _ = read_junit(nightly_path)
    landing, _ = read_junit(landing_path) if os.path.isfile(landing_path) else ({}, 0.0)
    req, setup, golden = fixtures(tree)
    labels = hand_labels(tree)
    _, landing_wall = read_junit(landing_path) if landing else ({}, 0.0)
    parallel = (sum(landing.values()) / landing_wall) if landing_wall else 4.0
    tests, scale = derive(nightly, landing, req, setup, {n for n, l in labels.items() if "slow" in l}, parallel,
                          no_promote=golden)
    try:
        with open(TIERS, encoding="utf-8") as f:
            old = json.load(f).get("tests", {})
    except (OSError, ValueError):
        old = {}
    moves = []
    for n, t in tests.items():
        before = old.get(n, {}).get("tier") or ("nightly" if "slow" in labels.get(n, []) else "landing")
        if before != t["tier"]:
            moves.append(f"  {n}: {before} -> {t['tier']} ({t['s']} s)")
    doc = {"limit_s": LIMIT_S, "budget_s": BUDGET_S, "nightly_scale": round(scale, 2),
           "source": {"nightly": os.path.basename(os.path.dirname(os.path.dirname(os.path.dirname(nightly_path)))) + "/"
                      + os.path.basename(os.path.dirname(nightly_path)),
                      "landing": os.path.basename(os.path.dirname(landing_path)) if landing else None},
           "tests": tests}
    with open(TIERS, "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, indent=1, sort_keys=True)
        f.write("\n")
    n_land = sum(1 for t in tests.values() if t["tier"] == "landing")
    print(f"test_tiers: {len(tests)} tests, {n_land} landing, {len(tests) - n_land} nightly (nightly times / {scale:.2f}); "
          f"{len(moves)} change tier against the hand labels or the last regen")
    print("\n".join(moves))
    return 0


def hand_labels(tree: str) -> dict[str, list[str]]:
    ctest = os.path.join(ROOT, "toolchain", "venv", "Scripts", "ctest.exe")
    p = subprocess.run([ctest, "--test-dir", tree, "--show-only=json-v1"], capture_output=True, text=True, errors="replace")
    try:
        tests = json.loads(p.stdout).get("tests", [])
    except ValueError:
        return {}
    return {t["name"]: next((x.get("value") or [] for x in t.get("properties", []) if x.get("name") == "LABELS"), [])
            for t in tests}


def budget(landing: dict[str, float], wall: float, limit: float = LIMIT_S, budget_s: float = BUDGET_S) -> list[str]:
    problems = []
    if wall > budget_s:
        top = sorted(landing.items(), key=lambda kv: -kv[1])[:8]
        problems.append(f"landing tier wall {wall:.0f} s over the {budget_s:.0f} s budget; longest: "
                        + ", ".join(f"{n} {s:.0f} s" for n, s in top))
    for n, s in sorted(landing.items(), key=lambda kv: -kv[1]):
        if s > limit:
            problems.append(f"{n}: {s:.0f} s in the landing tier, over the {limit:.0f} s limit: shorten it to the part that "
                            "pins behaviour, or regen the tiers so it moves nightly")
    return problems


def check(landing_path: str) -> int:
    if not os.path.isfile(landing_path):
        print(f"test_tiers check: no landing run at {landing_path}")
        return 0
    landing, wall = read_junit(landing_path)
    problems = budget(landing, wall)
    print("\n".join(problems) if problems else
          f"test_tiers check: landing tier {len(landing)} tests, wall {wall:.0f} s (budget {BUDGET_S:.0f} s), "
          f"longest {max(landing.values(), default=0):.0f} s (limit {LIMIT_S:.0f} s): ok")
    return 1 if problems else 0


def load_tiers() -> dict[str, str]:
    try:
        with open(TIERS, encoding="utf-8") as f:
            return {n: v["tier"] for n, v in json.load(f).get("tests", {}).items()}
    except (OSError, ValueError, KeyError):
        return {}


def nightly_tests(labels: dict[str, list[str]], tiers: dict[str, str]) -> set[str]:
    """The tests outside the landing tier: the measured tier, or the hand `slow` label for a test not measured yet."""
    return {n for n, l in labels.items() if tiers.get(n, "nightly" if "slow" in l else "landing") == "nightly"}


def rotation_skips(names: set[str], slot: int) -> set[str]:
    """Worker-count entries <base>_w<N> (the base, the 0-worker producer, stays) other than the slot-th count of each
    group (counts sorted, slot taken modulo the group size): the landing run checks one rotating count (wr3 t04)."""
    import re
    groups: dict[str, list[int]] = {}
    for n in names:
        m = re.match(r"^(.*)_w(\d+)$", n)
        if m and m.group(1) in names:
            groups.setdefault(m.group(1), []).append(int(m.group(2)))
    skip = set()
    for base, counts in groups.items():
        counts.sort()
        keep = counts[slot % len(counts)]
        skip |= {f"{base}_w{c}" for c in counts if c != keep}
    return skip


def exclude(tree: str, out: str, slot: int | None) -> int:
    labels = hand_labels(tree)
    if not labels:
        print("test_tiers exclude: ctest cannot list the tree; falling back to the slow label")
    skip = nightly_tests(labels, load_tiers())
    landing = set(labels) - skip
    rot = rotation_skips(landing, slot) if slot is not None else set()
    with open(out, "w", encoding="utf-8") as f:
        f.write("".join(n + "\n" for n in sorted(skip | rot)))
    print(f"test tiers: landing tier {len(landing) - len(rot)} of {len(labels)} tests"
          + (f" (worker-count slot {slot}: {len(rot)} other counts left to the nightly run)" if slot is not None else ""))
    return 0


def selftest() -> int:
    bad = []

    def ok(c, what):
        if not c:
            bad.append(what)

    nightly = {"a": 2.0, "b": 100.0, "c": 4.0, "d": 70.0, "e": 8.0}
    landing = {"a": 1.0, "c": 2.0, "e": 4.0}
    req = {"e": ["fx_d"], "c": []}
    setup = {"fx_d": "d"}
    t, scale = derive(nightly, landing, req, setup, set())
    ok(abs(scale - 2.0) < 1e-9, f"scale is the median nightly / landing ratio: {scale}")
    ok(t["b"]["tier"] == "nightly" and t["b"]["s"] == 50.0, f"a 100 s nightly test (50 s scaled) is nightly: {t['b']}")
    ok(t["a"]["tier"] == "landing" and t["c"]["tier"] == "landing", "short tests are landing")
    ok(t["d"]["tier"] == "nightly" and t["e"]["tier"] == "nightly", "a test needing a nightly fixture is nightly")
    t, _ = derive({"x": 2.0, "y": 8.0, "z": 12.0}, {}, {}, {}, {"x", "y", "z"}, parallel=1.0)
    ok(t["x"]["tier"] == "landing" and t["y"]["tier"] == "nightly", f"a cheap hand-slow test is promoted: {t}")
    big = {f"t{i}": 4.0 for i in range(200)}
    t, _ = derive(big, big, {}, {}, set(big), parallel=1.0)
    ok(sum(1 for v in t.values() if v["tier"] == "landing") == int(TARGET_S // 4.0), "promotion stops at the target")
    ok(budget({"a": 10.0}, 60.0) == [], "within budget")
    p = budget({"a": 40.0, "b": 10.0}, 200.0)
    ok(len(p) == 2 and "over the 180 s budget" in p[0] and p[1].startswith("a:"), f"over budget and over limit: {p}")
    ok(nightly_tests({"a": ["slow"], "b": [], "c": ["slow"], "d": []}, {"a": "landing", "b": "nightly"}) == {"b", "c"},
       "measured tier wins over the hand label; an unmeasured test keeps its label")
    names = {"x", "x_w1", "x_w4", "x_w15", "x_w16", "y_w4", "z", "z_w1", "z_w4"}
    s0, s1, s5 = rotation_skips(names, 0), rotation_skips(names, 1), rotation_skips(names, 5)
    ok(s0 == {"x_w4", "x_w15", "x_w16", "z_w4"}, f"slot 0 keeps w1 of each group: {s0}")
    ok(s1 == {"x_w1", "x_w15", "x_w16", "z_w1"}, f"slot 1 keeps w4: {s1}")
    ok(s5 == s1, "the slot wraps per group size")
    ok(all(any(f"x_w{c}" not in rotation_skips(names, k) for k in range(4)) for c in (1, 4, 15, 16)),
       "every count of a four-count group runs within four landings")
    for b in bad:
        print("test_tiers selftest FAIL: " + b)
    if bad:
        return 1
    print("test_tiers selftest: ok")
    return 0


def main(argv: list[str]) -> int:
    def opt(name: str, default: str) -> str:
        return argv[argv.index(name) + 1] if name in argv else default

    if "--selftest" in argv:
        return selftest()
    if argv[:1] == ["regen"]:
        return regen(opt("--nightly", default_nightly()), opt("--landing", default_landing()),
                     opt("--tree", os.path.join(ROOT, build_trees.agent_tree("msvc"))))
    if argv[:1] == ["exclude"] and len(argv) >= 3:
        slot = os.environ.get("ALAMO_WC_SLOT")
        return exclude(argv[1], argv[2], int(slot) if slot and slot.isdigit() else None)
    if argv[:1] == ["check"]:
        return check(opt("--landing", default_landing()))
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
