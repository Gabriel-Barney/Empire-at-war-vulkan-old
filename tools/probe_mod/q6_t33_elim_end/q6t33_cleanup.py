#!/usr/bin/env python3
r"""q6t33_cleanup.py - removes Mods\Q6T33Probe from the game folder (only if it holds Q6T33_PROBE_MARKER.txt). Safe to
run any number of times; q6t33_run.py calls it at the end of every run (also after an error or Ctrl+C).

  q6t33_cleanup.py [--game DIR] [--dry-run] [--log FILE] [--no-pause]

The folder is the only thing the probe adds; no retail file is touched. Never save a probe campaign.
"""
import os
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'common'))
sys.path.insert(0, HERE)
import probe_cleanup  # noqa: E402
from q6t33_run import cleanup  # noqa: E402

if __name__ == '__main__':
    sys.exit(probe_cleanup.run_main(__doc__, cleanup, 'Q6T33_CLEANUP'))
