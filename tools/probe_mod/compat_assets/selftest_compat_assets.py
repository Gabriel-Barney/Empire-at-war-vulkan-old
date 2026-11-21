#!/usr/bin/env python3
"""selftest_compat_assets.py - offline check of the compat_assets kit (no game run).

  toolchain\\venv\\Scripts\\python.exe -I tools\\probe_mod\\compat_assets\\selftest_compat_assets.py [--game DIR]
                                                                                              [--validator EXE]

1. alo_chunks round trip: parse + write of a synthetic chunk tree is byte-identical.
2. With the install (--game, default the Steam path): builds the mod into a temp folder; every slot's files exist;
   with --validator (build-*/bin/formats_validate.exe) every broken copy fails the strict parser with the reason the
   slot names and every control passes.
3. The generated free store runs under stock Lua 5.0.2 (common/lua50.py) with stubbed engine calls through every slot
   and the overview; the analyser reads the printed records back: every slot spawned, the probe finished.
Exit 0 = all checks passed.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'common'))
import alo_chunks as A  # noqa: E402
import build_compat_assets_mod as bm  # noqa: E402
import compat_assets_analyse as an  # noqa: E402
import lua50  # noqa: E402

EXPECT = {  # slot -> text the strict parser's failure must contain
    'NOCONN': 'no connections chunk', 'NOINFO': 'mesh without info chunk', 'SPILL': 'mesh without info chunk',
    'COLL': 'collision node child index', 'TRAIL': 'overruns parent', 'BORDER': 'invalid parent',
    'ANZ': 'rotation block data size', 'ANG': 'rotation block data size', 'ANS': 'rotation block data size',
    'SKIN': 'skin bone index', 'PBAD': 'emitter property', 'OVR': 'overruns parent', 'EMPTY': 'empty file',
}

STUBS = r'''
CA_T = 0
function GetCurrentTime() return CA_T / 30 end
function Get_Game_Mode() return "Land" end
PlayerObject = { Get_Faction_Name = function() return "REBEL" end, Is_Human = function() return false end }
function Find_Player(n) return { Is_Human = function() return true end } end
function Create_Position(x, y, z) return { x = x, y = y, z = z } end
function Find_Object_Type(n) return { name = n } end
CA_NEXT_ID = 100
function Spawn_Unit(ty, pos, pl)
  if string.find(ty.name, "PCTRL") or string.find(ty.name, "PBAD") then error("particle: not a unit") end
  CA_NEXT_ID = CA_NEXT_ID + 1
  local id = CA_NEXT_ID
  return { { Is_Valid = function() return true end, Get_Object_ID = function() return id end,
             Make_Invulnerable = function() end, Prevent_AI_Usage = function() end } }
end
function Create_Generic_Object(ty, pos, pl)
  CA_NEXT_ID = CA_NEXT_ID + 1
  local id = CA_NEXT_ID
  return { Is_Valid = function() return true end, Get_Object_ID = function() return id end,
           Make_Invulnerable = function() end, Prevent_AI_Usage = function() end }
end
function Point_Camera_At(p) end
function Game_Message(id) end
ServiceRate = 1
'''

DRIVER = r'''
Base_Definitions()
for i = 1, 20000 do
  CA_T = i
  FreeStoreService()
  if CA.done then break end
end
for i = 1, CA_N do print(CA_LOG[i]) end
'''


def check(cond, msg, fails):
    print(('ok    ' if cond else 'FAIL  ') + msg)
    if not cond:
        fails.append(msg)


def roundtrip(fails):
    leaf = struct.pack('<II', 0x201, 4) + b'\x01\0\0\0'
    inner = struct.pack('<II', 0x200, 0x80000000 | len(leaf)) + leaf
    buf = inner + struct.pack('<II', 0x600, 0x80000000)
    check(A.write(A.parse(buf)) == buf, 'chunk tree round trip is byte-identical', fails)


def built(game, validator, fails):
    tmp = tempfile.mkdtemp(prefix='cap_selftest_')
    try:
        out = os.path.join(tmp, bm.MOD_NAME)
        cfg = bm.build(game, out, offline=True)
        models = os.path.join(out, 'Data', 'Art', 'Models')
        for key, _, _, _, defect in bm.SLOTS:
            check(os.path.exists(os.path.join(models, 'CAP_%s.ALO' % key)), 'slot %s: model file built' % key, fails)
        lua = open(os.path.join(out, 'Data', 'Scripts', 'FreeStore', 'BusyTacticalFreeStore.lua'), 'rb').read()
        if validator:
            validator = os.path.abspath(validator)
            for key, _, _, _, defect in bm.SLOTS:
                f = 'CAP_%s_IDLE_00.ALA' % key if defect.startswith('ala_') else 'CAP_%s.ALO' % key
                r = subprocess.run([validator, os.path.join(models, f)], capture_output=True, text=True)
                failed = '**FAIL**' in r.stdout
                if key in EXPECT:
                    check(failed and EXPECT[key] in r.stdout, 'slot %s: strict parser rejects %s (%s)' % (
                        key, f, EXPECT[key]), fails)
                else:
                    check(not failed, 'slot %s: control %s passes the strict parser' % (key, f), fails)
        return cfg, lua
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def run_lua(cfg, freestore, fails):
    exe = lua50.find_lua()
    if not exe:
        print('skip  Lua 5.0.2 not available (common/build_lua50.cmd)')
        return
    # the probe part of the generated free store (after the shipped script): CA_CFG + compat_assets_probe.lua
    text = freestore.decode('latin-1').replace('\r\n', '\n')
    probe = text[text.index('-- generated by build_compat_assets_mod.py'):]
    tmp = tempfile.mkdtemp(prefix='cap_lua_')
    try:
        path = os.path.join(tmp, 'drive.lua')
        with open(path, 'w', newline='\n') as f:
            f.write(STUBS + '\n' + probe + '\n' + DRIVER)
        r = subprocess.run([exe, path], capture_output=True, text=True, timeout=60)
        check(r.returncode == 0, 'Lua 5.0 runs the probe (%s)' % r.stderr.strip()[:200], fails)
        log = os.path.join(tmp, 'records.log')
        with open(log, 'w') as f:
            f.write(r.stdout)
        res = an.analyse(an.load(log), cfg)
        check(res['armed'] and res['done'], 'probe armed and finished', fails)
        check(len(res['slots']) == len(bm.SLOTS), 'every slot reached (%d)' % len(res['slots']), fails)
        check(all(s.get('alive_at_end') == '1' for s in res['slots']), 'every stub spawn alive at the end', fails)
        hows = {s['key']: s.get('how') for s in res['slots']}
        check(hows.get('PCTRL') == 'generic_object', 'a type Spawn_Unit refuses goes through Create_Generic_Object',
              fails)
        check(not res['apierr'] or all(e['kind'] == 'APIERR' for e in res['apierr']), 'no ERR records', fails)
        # a crash after SHOW of the 4th slot: the analyser names it
        lines = [ln for ln in r.stdout.splitlines() if ln.startswith('CAQ|')]
        fourth = bm.SLOTS[3][0]
        cut = next(i for i, ln in enumerate(lines) if '|SHOW|' in ln and '|key=%s|' % fourth in ln)
        with open(log, 'w') as f:
            f.write('\n'.join(lines[:cut + 1]) + '\n')
        res = an.analyse(an.load(log), cfg)
        check(res['stopped_in'] == fourth, 'a run that stops on slot %s is pinned to it' % fourth, fails)
        print(an.render(an.analyse(an.load(log), cfg), cfg).splitlines()[0])
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game', default=bm.DEFAULT_GAME)
    ap.add_argument('--validator')
    a = ap.parse_args()
    fails = []
    roundtrip(fails)
    if os.path.isdir(os.path.join(a.game, 'corruption')):
        cfg, lua = built(a.game, a.validator, fails)
        run_lua(cfg, lua, fails)
    else:
        print('skip  no install at %s: build and Lua checks need the shipped models' % a.game)
    print('SELFTEST %s (%d failed)' % ('PASS' if not fails else 'FAIL', len(fails)))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
