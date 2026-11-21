#!/usr/bin/env python3
"""Screenshot diff between two builds (wr1 t05; replaces the stored tests/visual_baselines when wr1 t07 removes them).

The game smoke scenarios of tools/goldens/scenarios.json (game_smoke_skirmish / _campaign / _land / _screens) keep every
PNG the run writes in their result folder (tools/goldens/scenario_results.py, cached outside git). This compares the
PNGs of two commits' results with tools/visual/png_diff.py at the baseline scale (320 px wide, its default tolerances)
and writes a diff image per mismatch to logs/visual_diff/<scenario>/. Nothing is stored in git: the parent's build is the
baseline.

  python -I tools/visual/visual_builds.py [REV] [--base REV] [--only REGEX] [--compiler msvc]
      REV defaults to HEAD, --base to REV's first parent. Results not in the cache are built and run (GPU and FoC data).

Exit 0 = every compared PNG matches, 1 = a mismatch, 2 = setup error. Standard library only.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools", "goldens"))
import scenario_results as sr  # noqa: E402


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("rev", nargs="?", default="HEAD")
    ap.add_argument("--base")
    ap.add_argument("--only", default="^game_smoke_")
    ap.add_argument("--compiler", default="msvc")
    ap.add_argument("--build", help="build tree whose ctest entries name the programs (default: this worktree's)")
    a = ap.parse_args(argv)
    rev = sr.git(ROOT, "rev-parse", a.rev).strip()
    base = sr.git(ROOT, "rev-parse", a.base or f"{rev}^").strip()
    build = a.build or sr.default_build(a.compiler)
    try:
        scen = [s for s in sr.discover(build) if re.search(a.only, s.name)]
    except RuntimeError as e:
        print(f"visual_builds: {e}", file=sys.stderr)
        return 2
    if not scen:
        print(f"visual_builds: no scenario matches {a.only!r}", file=sys.stderr)
        return 2
    cache = sr.Cache.default()
    head = sr.git(ROOT, "rev-parse", "HEAD").strip()
    worst = 0
    print(f"visual diff {base[:8]} -> {rev[:8]}")
    for s in scen:
        try:
            old = sr.result_for(cache, s, base, a.compiler)
            new = sr.result_here(cache, s, ROOT, build, a.compiler) if rev == head else sr.result_for(cache, s, rev, a.compiler)
        except RuntimeError as e:
            print(f"  {'SKIP' if str(e).startswith('skipped') else 'ERROR'} {s.name}: {e}")
            worst = max(worst, 0 if str(e).startswith("skipped") else 2)
            continue
        out = os.path.join(sr.main_checkout(ROOT), "logs", "visual_diff", s.name)
        for sub in sorted({os.path.dirname(r) for r in sr.result_files(new) if r.lower().endswith(".png")}):
            r = subprocess.run([sys.executable, "-I", os.path.join(HERE, "png_diff.py"), "compare", os.path.join(old, sub),
                                os.path.join(new, sub), "--scale", "320", "--diff-dir", os.path.join(out, sub)],
                               capture_output=True, text=True, errors="replace")
            tail = (r.stdout + r.stderr).strip().splitlines()[-1:] or [""]
            print(f"  {'PASS' if r.returncode == 0 else 'FAIL'} {s.name}{'/' + sub if sub else ''}: {tail[0]}")
            worst = max(worst, r.returncode)
    return worst


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
