#!/usr/bin/env python3
"""build_mov2_t07_mod.py - builds Mods\\Mov2T07Probe (mov2 t07: squad stop distance, re-order braking, MovingVsMoving
holds) from the installed game.

  build_mov2_t07_mod.py [--game DIR] [--out MODDIR] [--source-zip extracted\\foc_data_text.zip] [--no-text]
                        [--map NAME] [--reps N] [--dry-run]

Nothing from the game is stored in the repository. The mod holds:
  Data\\Scripts\\FreeStore\\BusyTacticalFreeStore.lua  the shipped free store + SN_CFG + the s78n t14 helpers (records
                                                      renamed M7Q) + mov2_t07_probe.lua + the s78n t14 drivers
  Data\\Text\\MasterTextFile_English.dat               shipped text + the two probe messages
  MOV2T07_PROBE_MARKER.txt
The corridor search is the s78n t14 one (../s78n_t14_squads/build_s78n_t14_mod.py corridor()) run on this kit's
check polyline: 900 straight, 120 to the left, 900 back, so the strip u 0..900, v 0..120 is passable land with map
objects >= 40 away (else 25, 0). --dry-run prints the trials and the corridor without writing anything.
"""
import argparse
import copy
import json
import math
import os
import shutil
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
PM = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(PM, 's78n_t14_squads'))
sys.path.insert(0, os.path.join(PM, 'common'))
sys.path.insert(0, PM)
import build_s78n_t14_mod as S  # noqa: E402

MOD_NAME = 'Mov2T07Probe'
MARKER = 'MOV2T07_PROBE_MARKER.txt'
DEFAULT_MAP = S.DEFAULT_MAP
CHECK_LEGS = [{'name': 'strip_a', 'turn': 0.0, 'len': 900.0}, {'name': 'strip_side', 'turn': 90.0, 'len': 120.0},
              {'name': 'strip_b', 'turn': 90.0, 'len': 900.0}]

# moves of a squad / unit trial, local (u along the corridor, v to the left); re = the re-order: once the unit is
# `after` from where the move started, a new Move_To to `to`
MOVES = [
    {'name': 'stop', 'to': [600.0, 30.0]},
    {'name': 'same', 'to': [200.0, 30.0], 're': {'after': 200.0, 'to': [100.0, 30.0]}},
    {'name': 'turn90', 'to': [700.0, 30.0], 're': {'after': 200.0, 'to': [300.0, 100.0]}},
    {'name': 'reverse', 'to': [800.0, 100.0], 're': {'after': 250.0, 'to': [300.0, 100.0]}},
]

SQUAD_TRIALS = [
    {'name': 'rows', 'kind': 'squad', 'company': 'Imperial_Medium_Stormtrooper_Squad', 'member': 'Squad_Stormtrooper',
     'team': 'Stormtrooper_Team', 'at': [100.0, 30.0]},
    {'name': 'dart', 'kind': 'squad', 'company': 'Rebel_Mini_Infantry_Squad', 'member': 'Squad_Rebel_Trooper',
     'team': 'Rebel_Trooper_Team', 'at': [100.0, 30.0]},
    {'name': 't4b', 'kind': 'unit', 'type': 'T4B_Tank', 'at': [100.0, 30.0]},
]

MVM_TRIALS = [
    {'name': 'mvm_headon', 'kind': 'mvm', 'a': {'type': 'T4B_Tank', 'from': [150.0, 50.0], 'to': [750.0, 50.0]},
     'b': {'type': 'T4B_Tank', 'from': [750.0, 50.0], 'to': [150.0, 50.0]}},
    {'name': 'mvm_offset12', 'kind': 'mvm', 'a': {'type': 'T4B_Tank', 'from': [150.0, 50.0], 'to': [750.0, 50.0]},
     'b': {'type': 'T4B_Tank', 'from': [750.0, 62.0], 'to': [150.0, 62.0]}},
    {'name': 'mvm_atst_t4b', 'kind': 'mvm', 'a': {'type': 'AT_ST_Walker', 'from': [150.0, 50.0], 'to': [750.0, 50.0]},
     'b': {'type': 'T4B_Tank', 'from': [750.0, 50.0], 'to': [150.0, 50.0]}},
]

CFG = {
    'session': 'mov2_t07',
    'ai_faction': 'REBEL',       # the AI whose free store runs the probe
    'human_faction': 'Empire',   # every probe unit is spawned for this (human, idle) player
    'sq': {
        'start_frame': 450,      # 15 s into the battle
        'ident_wait': 15,
        'settle': 150,           # frames after the read before the first order
        'hold': 120,             # frames standing still between moves
        'still_polls': 3,
        'moved_min': 2.0,
        'leg_max': 1500,
        'mvm_min': 300,          # an MvM trial runs at least this long after the orders
        'mvm_still': 20,         # ... and ends when both stood still for this many polls
        'trial_max': 9000,
        'gap': 45,
        'trace_every': 15,       # sparse Lua position trace (the memory reader has every frame)
        'nofire': True,
        'camera': True,
    },
}

TEXTS = [
    ('TEXT_MOV2_T07_ARMED', 'MOV2 T07 PROBE: stop / re-order / head-on trials armed. Give no orders, do not pause, '
                            'keep the window in front. About 11 minutes.'),
    ('TEXT_MOV2_T07_DONE', 'MOV2 T07 PROBE: trials recorded. Esc > Quit, then quit the game.'),
]

S78N_LUA = os.path.join(PM, 's78n_t14_squads', 's78n_t14_probe.lua')
HELPERS_FROM, HELPERS_TO = 'SN_LOG = {}', '-- the squad trials'
DRIVERS_FROM = '-- ---- arming and the service drivers'


def s78n_parts():
    """(helpers, drivers) of the s78n t14 probe, records renamed S8Q -> M7Q."""
    text = open(S78N_LUA, encoding='ascii').read()
    a = text.index(HELPERS_FROM)
    b = text.index(HELPERS_TO)
    b = text.rindex('\n-- ====', a, b)
    c = text.index(DRIVERS_FROM)
    helpers, drivers = text[a:b], text[c:]
    helpers = helpers.replace('"S8Q|"', '"M7Q|"')
    assert '"M7Q|"' in helpers and 'S8Q' not in helpers, 's78n_t14_probe.lua record prefix moved'
    assert 'function SN_Arm()' in drivers and 'SQ_Tick' in drivers, 's78n_t14_probe.lua drivers moved'
    return helpers, drivers


def config(game, offline, map_name=DEFAULT_MAP, reps=2):
    cfg = copy.deepcopy(CFG)
    saved = S.LEGS
    S.LEGS = CHECK_LEGS
    try:
        site, theta0, far, note = S.site_for(game, map_name, offline)
    finally:
        S.LEGS = saved
    sq = cfg['sq']
    r = math.radians(theta0)
    sq['site'] = {'x': float(site['x']), 'y': float(site['y']), 'z': float(site['z'])}
    sq['theta0'] = float(theta0)
    sq['ux'], sq['uy'] = round(math.cos(r), 6), round(math.sin(r), 6)
    sq['vx'], sq['vy'] = round(-math.sin(r), 6), round(math.cos(r), 6)
    sq['far'] = float(far)
    sq['map'] = map_name.upper()
    trials = []
    for _ in range(max(1, int(reps))):
        for t in SQUAD_TRIALS:
            trials.append(dict(t, moves=MOVES))
    for t in MVM_TRIALS:
        trials.append(dict(t))
    trials.append(dict(MVM_TRIALS[0], name='mvm_headon2'))
    sq['trials'] = trials
    cfg['site_note'] = note
    cfg['map'] = map_name.upper()
    return cfg


def lua_config(cfg):
    lua_cfg = {k: v for k, v in cfg.items() if k not in ('site_note', 'map', 'files', 'freestore_source')}
    return '-- generated by build_mov2_t07_mod.py\nSN_CFG = %s\n' % S.lua_value(lua_cfg, '')


def probe_lua(cfg):
    helpers, drivers = s78n_parts()
    own = open(os.path.join(HERE, 'mov2_t07_probe.lua'), encoding='ascii').read()
    return (lua_config(cfg) + '\n-- ==== record / engine helpers of tools/probe_mod/s78n_t14_squads/s78n_t14_probe.lua '
            '====\n' + helpers + '\n' + own + '\n-- ==== drivers of s78n_t14_probe.lua ====\n' + drivers)


def prepare(out):
    if os.path.exists(out):
        if not os.path.exists(os.path.join(out, MARKER)):
            raise SystemExit('refusing to overwrite %s: not a mov2 t07 probe folder' % out)
        shutil.rmtree(out)
    os.makedirs(out)
    S.write(os.path.join(out, MARKER),
            b'mov2 t07 probe mod (tools/probe_mod/mov2_t07_stops); removed by cleanup_mov2_t07.cmd\r\n')


def build(game, out, source_zip=None, no_text=False, map_name=DEFAULT_MAP, reps=2):
    src = S.Source(game, source_zip)
    cfg = config(game, src.zip is not None, map_name, reps)
    prepare(out)
    shipped, fs_src = src.need(S.FREESTORE)
    text = shipped.decode('latin-1').replace('\r\n', '\n').rstrip('\n')
    files = ['Data\\Scripts\\FreeStore\\BusyTacticalFreeStore.lua']
    S.write(os.path.join(out, files[0]), S.crlf(text + '\n\n' + probe_lua(cfg)))
    if not no_text:
        files.append('Data\\Text\\MasterTextFile_English.dat')
        S.write(os.path.join(out, files[1]), S.texts_dat(src, TEXTS))
    cfg['files'] = files
    cfg['freestore_source'] = fs_src
    return cfg


def describe(cfg):
    sq = cfg['sq']
    out = ['map %s, corridor: %s' % (cfg['map'], cfg['site_note']),
           'local frame: u = (%.3f, %.3f), v = (%.3f, %.3f) from (%.0f, %.0f)' % (sq['ux'], sq['uy'], sq['vx'], sq['vy'],
                                                                                sq['site']['x'], sq['site']['y'])]
    for i, t in enumerate(sq['trials']):
        if t['kind'] == 'mvm':
            out.append('  %2d %-13s %s %s -> %s  vs  %s %s -> %s' % (i + 1, t['name'], t['a']['type'], t['a']['from'],
                                                                    t['a']['to'], t['b']['type'], t['b']['from'],
                                                                    t['b']['to']))
        else:
            out.append('  %2d %-13s %s at %s: %s' % (i + 1, t['name'], t.get('company', t.get('type')), t['at'],
                                                    ', '.join(m['name'] for m in t['moves'])))
    return '\n'.join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game', default=S.DEFAULT_GAME)
    ap.add_argument('--out')
    ap.add_argument('--source-zip')
    ap.add_argument('--no-text', action='store_true')
    ap.add_argument('--map', default=DEFAULT_MAP)
    ap.add_argument('--reps', type=int, default=2)
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()
    if a.dry_run:
        cfg = config(a.game, a.source_zip is not None, a.map, a.reps)
        print(describe(cfg))
        print(json.dumps(cfg['sq'], indent=1)[:2000])
        return
    cfg = build(a.game, a.out or os.path.join(a.game, 'corruption', 'Mods', MOD_NAME), a.source_zip, a.no_text,
                a.map, a.reps)
    print(describe(cfg))
    print('built: ' + ', '.join(cfg['files']))


if __name__ == '__main__':
    main()
