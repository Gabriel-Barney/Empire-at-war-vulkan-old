#!/usr/bin/env python3
"""Run one mod scenario (tests/goldens/mods/<name>.json; roadmap mcx7 t04, docs/MOD_COMPAT_DESIGN.md §13.3).

A mod scenario is a short headless skirmish of a reference mod. Its CRC log used to be stored as tests/goldens/mods/<name>.crc;
since wr1 t07 (2026-11-21) it is a scenario of tools/goldens/scenarios.json, computed on the parent and the commit and
diffed by tools/goldens/behaviour_check.py (nightly tier). The spec file names the mod and the run:
  {"name": "...", "mod": "<workshop id>", "stack": ["STEAMMOD=<id>", ...], "args": [alamo_headless arguments],
   "fingerprint": "<data fingerprint of the mod when the spec was made; informational>"}
  python -I tools/goldens/mod_golden.py --spec S --exe alamo_headless.exe [more alamo_headless arguments, e.g. --crc-out F]
  python -I tools/goldens/mod_golden.py --spec S --fingerprint     print the installed mod's fingerprint (the scenario's
                                                                    input identity in the result cache key)
It skips (prints "skipped" and exits 0, so ctest_skips.py counts it) when ALAMO_FOC_DATA is not set or the workshop item
is not installed (the Steam libraries: the one holding the FoC folder, then ALAMO_STEAM_ROOT or the default client's
libraryfolders.vdf, as docs/MODS.md §3.1). Otherwise it runs alamo_headless --game <ALAMO_FOC_DATA> <args> <stack>
[--workers 4] <more arguments> and passes its exit code through (the ctest entry: the mod loads and runs without error).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys

FOC_APP = "32470"


def steam_libraries(game: str) -> list[str]:
    libs: list[str] = []
    p = os.path.abspath(game)
    while True:  # the library holding the FoC folder: the parent of its nearest steamapps ancestor
        parent = os.path.dirname(p)
        if os.path.basename(p).lower() == "steamapps":
            libs.append(parent)
            break
        if parent == p:
            break
        p = parent
    roots = [r for r in os.environ.get("ALAMO_STEAM_ROOT", "").split(";") if r] or [r"C:\Program Files (x86)\Steam"]
    for root in roots:
        libs.append(root)
        for vdf in (os.path.join(root, "steamapps", "libraryfolders.vdf"), os.path.join(root, "config", "libraryfolders.vdf")):
            try:
                with open(vdf, encoding="utf-8", errors="replace") as f:
                    text = f.read()
            except OSError:
                continue
            for m in re.finditer(r'"(?:path|\d+)"\s+"([^"]+)"', text):
                libs.append(m.group(1).replace("\\\\", "\\"))
    out, seen = [], set()
    for lib in libs:
        k = os.path.normcase(os.path.abspath(lib))
        if k not in seen:
            seen.add(k)
            out.append(lib)
    return out


def find_mod(game: str, mod_id: str) -> str | None:
    for lib in steam_libraries(game):
        d = os.path.join(lib, "steamapps", "workshop", "content", FOC_APP, mod_id)
        if os.path.isdir(os.path.join(d, "Data")):
            return d
    return None


def fingerprint(mod_dir: str) -> str:
    """SHA-1 (16 hex digits) of the sorted (relative path, size, write time) list of the mod's Data folder."""
    rows = []
    data = os.path.join(mod_dir, "Data")
    for base, _, files in os.walk(data):
        for n in files:
            p = os.path.join(base, n)
            st = os.stat(p)
            rows.append(f"{os.path.relpath(p, data).replace(os.sep, '/').lower()}|{st.st_size}|{st.st_mtime_ns}")
    return hashlib.sha1("\n".join(sorted(rows)).encode()).hexdigest()[:16]


def main() -> int:
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("--spec", required=True)
    ap.add_argument("--exe")
    ap.add_argument("--fingerprint", action="store_true")
    a, rest = ap.parse_known_args()
    with open(a.spec, encoding="utf-8") as f:
        spec = json.load(f)
    name = os.path.splitext(os.path.basename(a.spec))[0]
    game = os.environ.get("ALAMO_FOC_DATA", "")
    if not game or not os.path.isdir(game):
        print(f"mod golden {name}: ALAMO_FOC_DATA not set, skipped")
        return 0
    mod_dir = find_mod(game, spec["mod"])
    if mod_dir is None:
        print(f"mod golden {name}: workshop item {spec['mod']} ({spec.get('name', '')}) not installed next to ALAMO_FOC_DATA, skipped")
        return 0
    fp = fingerprint(mod_dir)
    if a.fingerprint:
        print(fp)
        return 0
    if not a.exe:
        ap.error("--exe is required")
    cmd = [a.exe, "--game", game] + list(spec["args"]) + list(spec.get("stack") or ["STEAMMOD=" + spec["mod"]])
    if "--workers" not in rest and "--workers" not in spec["args"]:
        cmd += ["--workers", "4"]
    cmd += rest
    print("$ " + subprocess.list2cmdline(cmd), flush=True)
    return subprocess.call(cmd)


if __name__ == "__main__":
    sys.exit(main())
