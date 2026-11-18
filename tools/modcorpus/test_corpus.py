#!/usr/bin/env python3
"""ctest driver of the corpus runner (alamo_modcorpus_tests; roadmap mcx7 t02, t03). No game data needed.

1. grade.py --selftest (the G0-G3 rules and the drop comparison on hand-made results).
2. A check-stage sweep of the conformance mods `stacks` (engine/apps/game/testdata/conformance/stacks): the grades and
   stacks its README promises (cycle and base-EaW mods G0 with their reason, declared and hinted submods stacked).
3. A check-stage sweep of the synthetic fixture (testdata/mod_fixture, with its fake Steam library): every mod G1,
   FixtureSub stacked on FixtureMod.
4. The regression check: a copy of the fixture swept once, then FixtureMod deliberately broken (its campaign list names
   only a campaign without Locations, so no campaign can start: a load-report error) and swept again. The second sweep
   must exit 1 and print the grade drop with the mod and the first new blocker; --write-md must fill the marked block.
5. The baseline and the cache: a rerun reuses every result and fails again (the baseline keeps the accepted grades) until
   --accept-drops records the drop; a workshop item without a Data folder is graded G0 "nothing to mount".

  python -I tools/modcorpus/test_corpus.py --bin <tree>/bin --work <scratch folder>
"""
from __future__ import annotations

import argparse
import contextlib
import io
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import grade  # noqa: E402
import run_corpus  # noqa: E402

TESTDATA = os.path.join(ROOT, "engine", "apps", "game", "testdata")

STACKS_WANT = {
    "CycleA": ("G0", "stack: dependency cycle: CycleA -> CycleB -> CycleA", ["CycleA", "CycleB"]),
    "CycleB": ("G0", "stack: dependency cycle: CycleB -> CycleA -> CycleB", ["CycleB", "CycleA"]),
    "EawOnly": ("G0", "stack: EawOnly: base-EaW mod: retail runs it with sweaw.exe", ["EawOnly"]),
    "EawGuess": ("G1", None, ["EawGuess"]),
    "OtherTc": ("G1", None, ["OtherTc"]),
    "StackHinted": ("G1", None, ["StackHinted", "StackParent"]),
    "StackParent": ("G1", None, ["StackParent"]),
    "StackSub": ("G1", None, ["StackSub", "StackParent"]),
    "StackTop": ("G1", None, ["StackTop", "StackHinted", "StackParent"]),
}
FIXTURE_WANT = {"900000001": ["900000001"], "FixtureChecks": ["FixtureChecks"], "FixtureMod": ["FixtureMod"],
                "FixtureSub": ["FixtureSub", "FixtureMod"]}

fails = 0


def check(name: str, ok: bool, detail: str = "") -> None:
    global fails
    print(("ok   " if ok else "FAIL ") + name + (f": {detail}" if detail and not ok else ""))
    if not ok:
        fails += 1


def sweep(args: list[str]) -> tuple[int, str]:
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = run_corpus.main(args)
    return rc, buf.getvalue()


def load(path: str) -> dict:
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", required=True)
    ap.add_argument("--work", required=True)
    a = ap.parse_args()
    work = os.path.abspath(a.work)
    if os.path.isdir(work):
        shutil.rmtree(work)
    os.makedirs(work)
    nosteam = os.path.join(work, "no_steam")  # ALAMO_STEAM_ROOT: no Steam client, so only the fixture's libraries count
    os.makedirs(nosteam)
    common = ["--bin", a.bin, "--stages", "check", "--no-slot", "--corpus", os.path.join(work, "none.json")]

    check("grade selftest", grade.selftest() == 0)

    # 2. conformance stacks
    out = os.path.join(work, "stacks")
    rc, text = sweep(common + ["--game", os.path.join(TESTDATA, "conformance", "stacks", "corruption"), "--steam-root", nosteam,
                               "--out-root", out, "--date", "2000-01-01"])
    check("stacks sweep exit 0", rc == 0, text[-2000:])
    mods = load(os.path.join(out, "2000-01-01", "matrix.json"))["mods"] if rc == 0 else {}
    check("stacks setups", sorted(mods) == sorted(STACKS_WANT), str(sorted(mods)))
    for key, (g, first, stack) in STACKS_WANT.items():
        e = mods.get(key, {})
        got = (e.get("grade", {}).get("grade"), (e.get("grade", {}).get("blocking") or [None])[0], e.get("stack"))
        check(f"stacks {key}", got == (g, first, stack), str(got))

    # 3. fixture
    fixture = os.path.join(TESTDATA, "mod_fixture")
    out = os.path.join(work, "fixture")
    rc, text = sweep(common + ["--game", os.path.join(fixture, "corruption"), "--steam-root", os.path.join(fixture, "steamlib"),
                               "--out-root", out, "--date", "2000-01-01"])
    check("fixture sweep exit 0", rc == 0, text[-2000:])
    mods = load(os.path.join(out, "2000-01-01", "matrix.json"))["mods"] if rc == 0 else {}
    for key, stack in FIXTURE_WANT.items():
        e = mods.get(key, {})
        check(f"fixture {key}", (e.get("grade", {}).get("grade"), e.get("stack")) == ("G1", stack), str(e.get("grade")))

    # 4. a deliberately broken mod drops a grade, the runner exits 1 and names it
    game = os.path.join(work, "broken", "corruption")
    shutil.copytree(os.path.join(fixture, "corruption"), game)
    out = os.path.join(work, "broken_sweeps")
    args = common + ["--game", game, "--steam-root", os.path.join(fixture, "steamlib"), "--out-root", out]
    rc, text = sweep(args + ["--date", "2000-01-01"])
    check("broken: first sweep exit 0", rc == 0 and "no earlier sweep" in text, text[-2000:])
    xml = os.path.join(game, "Mods", "FixtureMod", "Data", "XML")
    with open(os.path.join(xml, "CampaignFiles.xml"), "w", encoding="utf-8", newline="\n") as f:
        f.write("<CampaignFiles>\n  <File>ModCampaigns.xml</File>\n</CampaignFiles>\n")
    with open(os.path.join(xml, "ModCampaigns.xml"), "w", encoding="utf-8", newline="\n") as f:
        f.write('<Campaigns>\n  <Campaign Name="Mod_GC"><Locations></Locations></Campaign>\n</Campaigns>\n')
    md = os.path.join(work, "MODS.md")
    with open(md, "w", encoding="utf-8", newline="\n") as f:
        f.write(f"# t\n\nbefore\n{run_corpus.MD_BEGIN}\nold table\n{run_corpus.MD_END}\nafter\n")
    rc, text = sweep(args + ["--date", "2000-01-02", "--write-md", "--md", md])
    want = "GRADE DROP FixtureMod (FixtureMod): G1 -> G0, first new blocker: campaigns: no campaign has planets"
    check("broken: second sweep exit 1", rc == 1, f"exit {rc}")
    check("broken: drop names the mod and blocker", want in text, text[-2000:])
    with open(md, encoding="utf-8") as f:
        mdtext = f.read()
    check("broken: --write-md", "old table" not in mdtext and "| FixtureMod (`FixtureMod`) | FixtureMod | G0 |" in mdtext
          and mdtext.startswith("# t\n\nbefore\n") and mdtext.endswith(run_corpus.MD_END + "\nafter\n"), mdtext)

    # 5. the baseline kept the accepted grades: a rerun fails again (from the cache) until the drop is accepted
    rc, text = sweep(args + ["--date", "2000-01-02"])
    check("baseline: the drop fails again", rc == 1 and "GRADE DROP FixtureMod" in text, text[-2000:])
    check("cache: every setup cached", text.count(": cached (") == len(FIXTURE_WANT), text[-2000:])
    rc, text = sweep(args + ["--date", "2000-01-02", "--accept-drops"])
    check("baseline: --accept-drops records it", rc == 1 and "(drops accepted)" in text, text[-2000:])
    rc, text = sweep(args + ["--date", "2000-01-03"])
    check("baseline: after acceptance exit 0", rc == 0, text[-2000:])
    # a workshop item without Data (the fake library's 900000002) is listed with G0
    nodata = os.path.join(work, "nodata_lib")
    shutil.copytree(os.path.join(fixture, "steamlib"), nodata)
    os.makedirs(os.path.join(nodata, "steamapps", "workshop", "content", "32470", "900000002"))
    rc, text = sweep(common + ["--game", os.path.join(fixture, "corruption"), "--steam-root", nodata, "--out-root",
                               os.path.join(work, "nodata"), "--date", "2000-01-01"])
    mods = load(os.path.join(work, "nodata", "2000-01-01", "matrix.json"))["mods"] if rc == 0 else {}
    e = mods.get("900000002", {}).get("grade", {})
    check("no Data: graded G0", (e.get("grade"), e.get("blocking")) == ("G0", ["no Data folder: nothing to mount"]), str(e))

    print(f"modcorpus tests: {'FAILED, ' + str(fails) + ' check(s)' if fails else 'ok'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
