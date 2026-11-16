#!/usr/bin/env python3
r"""mov2_t07_cleanup.py - removes Mods\Mov2T07Probe from the game folder (only if it holds MOV2T07_PROBE_MARKER.txt) and
restores the player's profile saved by the last run (out\latest.txt, or --run-dir). Safe to repeat; mov2_t07_run.py
does the same at the end of every run.

  mov2_t07_cleanup.py [--game DIR] [--dry-run] [--log FILE] [--run-dir DIR] [--no-profile] [--no-pause]
"""
import argparse
import os
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
PM = os.path.dirname(HERE)
for sub in ('common', 'q2_traces', 's78n_t14_squads'):
    sys.path.insert(0, os.path.join(PM, sub))
sys.path.insert(0, HERE)
import probe_cleanup  # noqa: E402
from mov2_t07_run import cleanup  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game', default=probe_cleanup.DEFAULT_GAME)
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--log')
    ap.add_argument('--run-dir')
    ap.add_argument('--no-profile', action='store_true')
    ap.add_argument('--no-pause', action='store_true')
    a = ap.parse_args()
    rep = cleanup(a.game, a.log, a.dry_run)
    for x in rep['actions']:
        print(x)
    ok = rep['ok']
    if not a.no_profile:
        import s78n_t14_cleanup
        run_dir = a.run_dir
        latest = os.path.join(HERE, 'out', 'latest.txt')
        if not run_dir and os.path.exists(latest):
            run_dir = open(latest).read().strip()
        pok, msg = s78n_t14_cleanup.restore_profile(run_dir, a.dry_run)
        print(msg)
        ok = ok and pok
    print('MOV2_T07_CLEANUP %s' % ('OK' if ok else 'INCOMPLETE'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
