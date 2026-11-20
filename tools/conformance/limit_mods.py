"""mcx5 t06: the limits conformance mods factions_80 and levels_10, and their G2 headless runs; ref4 t03: huge_delay.

    python -I tools/conformance/limit_mods.py build --out DIR
    python -I tools/conformance/limit_mods.py run --exe ALAMO_HEADLESS [--exe2 ALAMO_HEADLESS] --out DIR
                                                 [--mods factions_80,levels_10,huge_delay] [--runs space,land,galactic]
                                                 [--skirmish-ticks 3000] [--galactic-ticks 9000] [--game FOC_DIR]

build writes the two mods under DIR/Mods:
  Factions80  committed files (engine/apps/game/testdata/conformance/limits/Mods/Factions80): 72 filler factions listed
              before the stock eight, so Rebel / Empire / ... take faction (and player) indices 72..79 and every
              campaign holds 80 players (one per faction, docs/re/BKC1_RE_ANSWERS.md Q2).
  Levels10    FoC's GameConstants.xml (from extracted/foc_data_text.zip; retail data is never committed) with
              MaximumPoliticalControl, MaximumStarbaseLevel, MaximumGroundbaseLevel and MaximumSpecialStructures set
              to 10.
  HugeDelay   (ref4 t03) FoC's GameConstants.xml with the delays in HUGE_DELAY_TAGS set to 99999999999 seconds, so
              seconds x 30 is far outside int32 at the sites that turn them into frame counts (the L1 bombing-run and
              bombard services read theirs at the start of every land battle). The port converts them with the x86
              range behaviour of alamo/core/frames.hpp (INT32_MIN), so the MSVC and clang CRC logs must match
              (run --exe2); a plain float -> integer cast there is undefined and the two compilers differed (the
              class of the Age of Legends Defense_Duration_In_Secs split, mod golden aol_space).
run builds them into DIR, then runs alamo_headless on the FoC install (--game, default ALAMO_FOC_DATA) with
MODPATH=<mod> for each mod: space skirmish (_MP_SPACE_GEONOSIS, AI Empire vs AI Rebel), land skirmish
(_MP_LAND_UTAPAU), both --skirmish-ticks, and the galactic campaign (Heart of the Maw, economy, galactic AI, story,
scoring, object scripts) for --galactic-ticks. Each run goes once with --workers 0 and once with --workers 4, CRC logs
compared; with --exe2 (the clang build) the same runs again and the CRC logs compared across the two exes. G2 also
needs no crash or hang (exit code, timeout) and no port-caused Lua error (--fail-on-lua-errors on the skirmishes).
Exit 0 when every run passed; DIR/limit_mods.json holds the results.
"""
import argparse
import filecmp
import json
import os
import re
import shutil
import subprocess
import sys
import time
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMMITTED = ROOT / 'engine/apps/game/testdata/conformance/limits/Mods'
FOC_TEXT = ROOT / 'extracted/foc_data_text.zip'
LEVEL_TAGS = ('MaximumPoliticalControl', 'MaximumStarbaseLevel', 'MaximumGroundbaseLevel', 'MaximumSpecialStructures')
HUGE_DELAY_TAGS = ('Max_Bombing_Run_Interval_Seconds', 'Min_Bombing_Run_Interval_Seconds', 'Max_Bombard_Interval_Seconds',
                   'Min_Bombard_Interval_Seconds', 'Space_Retreat_Allowed_Countdown_Seconds',
                   'Land_Retreat_Allowed_Countdown_Seconds', 'Control_Point_Domination_Victory_Time_In_Secs')
MODS = {'factions_80': 'Factions80', 'levels_10': 'Levels10', 'huge_delay': 'HugeDelay'}
RUN_ARGS = {
    'space': ['--skirmish-ai', 'skirmish', '--map', '_MP_SPACE_GEONOSIS', '--ai', 'Empire,Rebel', '--fail-on-lua-errors'],
    'land': ['--skirmish-ai', 'skirmish', '--map', '_MP_LAND_UTAPAU', '--ai', 'Empire,Rebel', '--fail-on-lua-errors'],
    'galactic': ['--economy', '--galactic-ai', '--story', '--scoring', '--object-scripts'],
}


def build(out):
    mods = Path(out) / 'Mods'
    f80 = mods / 'Factions80'
    if f80.exists():
        shutil.rmtree(f80)
    shutil.copytree(COMMITTED / 'Factions80', f80)
    with zipfile.ZipFile(FOC_TEXT) as z:
        stock = z.read('Data/XML/GAMECONSTANTS.XML').decode('latin-1')
    for folder, tags, value in (('Levels10', LEVEL_TAGS, '10'), ('HugeDelay', HUGE_DELAY_TAGS, '99999999999')):
        text = stock
        for tag in tags:
            text, n = re.subn(r'<' + tag + r'>[^<]*</' + tag + '>', f'<{tag}> {value} </{tag}>', text)
            if n != 1:
                raise SystemExit(f'limit_mods: {tag} found {n} times in FoC GameConstants.xml (expected 1)')
        xml = mods / folder / 'Data' / 'XML'
        xml.mkdir(parents=True, exist_ok=True)
        (xml / 'GameConstants.xml').write_bytes(text.encode('latin-1'))
    return {name: mods / folder for name, folder in MODS.items()}


def run_one(exe, game, mod, kind, workers, ticks, crc, log):
    cmd = [str(exe), '--game', str(game), f'MODPATH={mod}'] + RUN_ARGS[kind] + [
        '--ticks', str(ticks), '--workers', str(workers), '--quiet', '--crc-out', str(crc)]
    t0 = time.time()
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=3600)
        code, out = r.returncode, (r.stdout or '') + (r.stderr or '')
    except subprocess.TimeoutExpired:
        code, out = 'timeout', ''
    log.write_text(' '.join(cmd) + '\n' + out, encoding='utf8')
    return {'cmd': cmd, 'exit': code, 'seconds': round(time.time() - t0, 1), 'log': str(log),
            'tail': out.strip().splitlines()[-3:]}


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument('action', choices=['build', 'run'])
    ap.add_argument('--out', required=True)
    ap.add_argument('--exe')
    ap.add_argument('--exe2')
    ap.add_argument('--game', default=os.environ.get('ALAMO_FOC_DATA', ''))
    ap.add_argument('--mods', default='factions_80,levels_10,huge_delay')
    ap.add_argument('--runs', default='space,land,galactic')
    ap.add_argument('--skirmish-ticks', type=int, default=3000)
    ap.add_argument('--galactic-ticks', type=int, default=9000)
    a = ap.parse_args(argv)
    out = Path(a.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    if not FOC_TEXT.is_file():
        print(f'limit_mods: {FOC_TEXT} not found (FoC GameConstants for levels_10), skipped')
        return 0
    mods = build(out)
    if a.action == 'build':
        print('limit_mods: built ' + ', '.join(f'{k} -> {v}' for k, v in mods.items()))
        return 0
    if not a.exe:
        print('limit_mods: run needs --exe')
        return 2
    if not a.game or not Path(a.game).is_dir():
        print('limit_mods: ALAMO_FOC_DATA not set (or --game missing), skipped')
        return 0
    exes = [('a', Path(a.exe))] + ([('b', Path(a.exe2))] if a.exe2 else [])
    results, ok = [], True
    for name in a.mods.split(','):
        for kind in a.runs.split(','):
            ticks = a.galactic_ticks if kind == 'galactic' else a.skirmish_ticks
            crcs = {}
            entry = {'mod': name, 'run': kind, 'ticks': ticks, 'runs': []}
            for tag, exe in exes:
                for w in (0, 4):
                    stem = out / f'{name}_{kind}_{tag}_w{w}'
                    r = run_one(exe, a.game, mods[name], kind, w, ticks, stem.with_suffix('.crc'), stem.with_suffix('.log'))
                    r.update({'exe': tag, 'workers': w})
                    entry['runs'].append(r)
                    crcs[(tag, w)] = stem.with_suffix('.crc')
            entry['exits_ok'] = all(r['exit'] == 0 for r in entry['runs'])
            ref = crcs[('a', 0)]
            entry['crc_identical'] = entry['exits_ok'] and all(
                p.exists() and filecmp.cmp(ref, p, shallow=False) for p in crcs.values())
            entry['passed'] = entry['exits_ok'] and entry['crc_identical']
            ok = ok and entry['passed']
            results.append(entry)
            print(f"{name:12s} {kind:9s} {'PASS' if entry['passed'] else 'FAIL'}  exits "
                  + ' '.join(f"{r['exe']}w{r['workers']}={r['exit']}({r['seconds']}s)" for r in entry['runs'])
                  + f"  crc {'identical' if entry['crc_identical'] else 'DIFFER'}", flush=True)
            if not entry['passed']:
                for r in entry['runs']:
                    if r['exit'] != 0:
                        print('    ' + r['log'] + ': ' + ' | '.join(r['tail']))
    (out / 'limit_mods.json').write_text(json.dumps(results, indent=1, default=str), encoding='utf8')
    print('limit_mods: ' + ('all runs passed' if ok else 'FAILED') + f' ({out / "limit_mods.json"})')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
