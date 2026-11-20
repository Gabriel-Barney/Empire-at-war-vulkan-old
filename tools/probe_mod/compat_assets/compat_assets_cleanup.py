#!/usr/bin/env python3
r"""compat_assets_cleanup.py - removes what the compat_assets probe put into the game folder and restores the player's profile.
Safe to run any number of times; compat_assets_run.py does the same at the end of every run (also after an error or Ctrl+C).

  compat_assets_cleanup.py [--game DIR] [--dry-run] [--log FILE] [--run-dir DIR] [--no-profile] [--no-pause]

* <game>\corruption\Mods\CompatAssetsProbe is deleted only if it holds CAP_PROBE_MARKER.txt (build_compat_assets_mod.py made it).
  That folder is the only thing the probe adds; no retail file is touched, other kits' Mods folders are left.
* Profile: compat_assets_run.py snapshots the registry options (HKCU\Software\Petroglyph\StarWars FOC) and Saved Games
  before the game starts (..\q2_traces\q2_profile.py, out\<run>\profile_backup\). This script restores that snapshot
  and verifies it (files the run created go to out\<run>\profile_new\; nothing is deleted). --run-dir picks the run
  (default: out\latest.txt). A snapshot already restored (profile_restore_done.txt) is not applied again. Skipped
  while StarWarsG.exe is running.
"""
import argparse
import os
import sys
import time

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'common'))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'q2_traces'))
import probe_cleanup  # noqa: E402
from probe_run import running  # noqa: E402

DEFAULT_GAME = probe_cleanup.DEFAULT_GAME
MOD_NAME = 'CompatAssetsProbe'
MARKER = 'CAP_PROBE_MARKER.txt'
from payload import PROFILE_DONE as DONE, restore_profile  # noqa: E402,F401  (common/payload.py)


def cleanup(game, log=None, dry_run=False):
    return probe_cleanup.cleanup(game, (MOD_NAME,), MARKER, log, dry_run)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game', default=DEFAULT_GAME)
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--log')
    ap.add_argument('--run-dir')
    ap.add_argument('--no-profile', action='store_true')
    ap.add_argument('--no-pause', action='store_true', help='for the .cmd wrapper: end without waiting for a key')
    a = ap.parse_args()
    rep = cleanup(a.game, a.log, a.dry_run)
    for x in rep['actions']:
        print(x)
    ok = rep['ok']
    if not a.no_profile:
        run_dir = a.run_dir
        if not run_dir:
            latest = os.path.join(HERE, 'out', 'latest.txt')
            if os.path.exists(latest):
                run_dir = open(latest).read().strip()
        pok, msg = restore_profile(run_dir, a.dry_run)
        print(msg)
        ok = ok and pok
    print('CAP_CLEANUP %s' % ('OK' if ok else 'INCOMPLETE'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
