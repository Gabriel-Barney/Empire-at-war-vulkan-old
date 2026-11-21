#!/usr/bin/env python3
"""Retired by wr1 t07 (2026-11-21): there are no stored derived goldens to merge, regenerate or slow-check.

Kept only as a no-op because the verifier copy installed in <main checkout>\\logs\\verifier_task\\ (from before wr1 t07)
still calls `golden_merge.py slow BASE HEAD`. Delete this file once tools\\gate\\install_verifier_task.cmd has been rerun
(the current verifier.py no longer calls it). Every command exits 0.
"""
import sys

if __name__ == "__main__":
    print("golden_merge: retired by wr1 t07 (no stored goldens); nothing to do")
    sys.exit(0)
