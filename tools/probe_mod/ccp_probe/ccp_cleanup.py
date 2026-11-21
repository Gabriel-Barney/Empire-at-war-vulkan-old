#!/usr/bin/env python3
r"""ccp_cleanup.py - removes everything the ccpgu t02 probe put into the game folder and restores the player's profile.
Safe to run any number of times; ccp_run.py does the same at the end of every run (also after an error or Ctrl+C).

  ccp_cleanup.py [--game DIR] [--dry-run] [--log FILE] [--run-dir DIR] [--no-profile] [--no-pause]

* <game>\corruption\Mods\CcpProbe is deleted only if it holds CCP_PROBE_MARKER.txt (build_ccp_mod.py made it). That
  folder is the only thing the probe adds to the game folder; no retail file is touched, other kits' folders are left.
* Profile: ccp_run.py snapshots the options registry tree (HKCU\Software\Petroglyph\StarWars FOC) and the Saved Games
  Save / Recordings files before anything else (q2_traces\q2_profile.py, out\<run>\profile_backup\), then presets the
  skirmish game type and land map in that tree (see ccp_run.py --preset). This script puts the snapshot back, verifies
  it and moves files the run created into out\<run>\profile_new\ (nothing is deleted). --run-dir picks the run
  (default: out\latest.txt). A snapshot already restored (profile_restore_done.txt) is not applied again. Skipped while
  StarWarsG.exe runs (the game writes its options again on exit).
"""
import argparse
import os
import sys
import time

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'q2_traces'))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'common'))
import probe_cleanup  # noqa: E402
from probe_run import running  # noqa: E402

DEFAULT_GAME = probe_cleanup.DEFAULT_GAME
MOD_NAME = 'CcpProbe'
MARKER = 'CCP_PROBE_MARKER.txt'
from payload import PROFILE_DONE as DONE  # noqa: E402,F401  (common/payload.py)
import payload  # noqa: E402


def restore_profile(run_dir, dry_run=False):
    return payload.restore_profile(run_dir, dry_run, again='run cleanup_ccp.cmd again')


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
    print('CCP_CLEANUP %s' % ('OK' if ok else 'INCOMPLETE'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
