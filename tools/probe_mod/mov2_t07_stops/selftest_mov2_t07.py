#!/usr/bin/env python3
"""selftest_mov2_t07.py - offline check of the mov2 t07 kit (no game):
  1. mov2_t07_track.py decoding on a fake memory image;
  2. builds the mod from extracted\\foc_data_text.zip (--no-text, offline corridor) and compiles the built free store;
  3. runs the generated probe Lua (config + s78n t14 helpers + this kit + drivers) under stock Lua 5.0.2 with a stub
     land engine (units accelerate / brake at 0.1 per frame, End Move while braking, a 40-frame hold when two MvM tanks
     meet); the stub writes the track lines the memory reader would;
  4. mov2_t07_analyse.py on those records and lines: every trial ends, each squad / unit gets a STOP and three
     REORDER lines, the MvM trials show the stub's 40-frame hold.
Ends with MOV2 T07 SELFTEST OK.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'common'))
import lua50  # noqa: E402
import build_mov2_t07_mod as bm  # noqa: E402
import mov2_t07_analyse  # noqa: E402
import mov2_t07_track  # noqa: E402

STUB = r'''
NOW = 0
GetCurrentTime = setmetatable({ Frame = function() return NOW / 30 end }, { __call = function() return NOW / 30 end })
function Game_Message(s) end
function Get_Game_Mode() return "Land" end
function Point_Camera_At(p) end
function Create_Position(x, y, z) return { x = x, y = y } end
function MkPlayer(name, human) return { Get_Faction_Name = function() return name end,
	Is_Human = function() return human end } end
PLAYERS = { Empire = MkPlayer("EMPIRE", true), Rebel = MkPlayer("REBEL", false) }
function Find_Player(n) return PLAYERS[n] end
PlayerObject = PLAYERS.Rebel
ServiceRate = 1
OBJS = {}
NEXT_ID = 1000
COMPANIES = { Imperial_Medium_Stormtrooper_Squad = { team = "Stormtrooper_Team", member = "Squad_Stormtrooper", n = 9 },
	Rebel_Mini_Infantry_Squad = { team = "Rebel_Trooper_Team", member = "Squad_Rebel_Trooper", n = 5 } }
SPEED = { Stormtrooper_Team = 1.248, Rebel_Trooper_Team = 1.56, T4B_Tank = 1.092, AT_ST_Walker = 1.872 }
function MkObj(tname, x, y, parent)
	NEXT_ID = NEXT_ID + 1
	local o = { id = NEXT_ID, tname = tname, x = x, y = y, v = 0, st = 22, valid = true, parent = parent,
		vmax = SPEED[tname] or 1.0, a = 0.1 }
	o.Is_Valid = function() return o.valid end
	o.Get_Object_ID = function() return o.id end
	o.Get_Type = function() return { Get_Name = function() return string.upper(o.tname) end } end
	o.Get_Position = function() return { Get_XYZ = function() return o.x, o.y, 0 end } end
	o.Get_Parent_Object = function() return o.parent end
	o.Make_Invulnerable = function(b) end
	o.Prevent_All_Fire = function(b) end
	o.Despawn = function() o.valid = false end
	o.Move_To = function(p) o.gx, o.gy = p.x, p.y o.st = 19 return o end
	table.insert(OBJS, o)
	return o
end
function Find_Object_Type(n) return { name = n } end
function Spawn_Unit(ty, pos, player)
	local c = COMPANIES[ty.name]
	if c then
		local k = MkObj(c.team, pos.x, pos.y, nil)
		for i = 1, c.n do
			local m = MkObj(c.member, pos.x + i, pos.y, k)
			m.follow = k
			m.ox = i
		end
		return { k }
	end
	return { MkObj(ty.name, pos.x, pos.y, nil) }
end
function Find_All_Objects_Of_Type(n)
	local r = {}
	for _, o in ipairs(OBJS) do
		if o.valid and string.upper(o.tname) == string.upper(n) then table.insert(r, o) end
	end
	return r
end
function Physics()
	for _, o in ipairs(OBJS) do
		if o.valid and o.follow == nil and o.gx ~= nil then
			local dx, dy = o.gx - o.x, o.gy - o.y
			local d = math.sqrt(dx * dx + dy * dy)
			if o.hold and o.hold > 0 then
				o.hold = o.hold - 1
				o.v = 0
			elseif d < 0.3 then
				o.v = 0
				o.st = 22
				o.gx = nil
			else
				if o.v * o.v / (2 * o.a) >= d then
					o.v = math.max(0.05, o.v - o.a)
					o.st = 20
				else
					o.v = math.min(o.vmax, o.v + o.a)
					o.st = 19
				end
				local s = math.min(o.v, d)
				o.x, o.y = o.x + dx / d * s, o.y + dy / d * s
			end
		end
	end
	for _, o in ipairs(OBJS) do
		if o.valid and o.follow ~= nil and o.follow.valid then
			o.x, o.y, o.v, o.st = o.follow.x + o.ox, o.follow.y, o.follow.v, o.follow.st
		end
	end
	-- an MvM pair that meets: the second one holds 40 frames, once
	local t = SQ and SQ.cur
	if t and t.kind == "mvm" and t.ua and t.ub and not t.stub_held then
		local dx, dy = t.ua.x - t.ub.x, t.ua.y - t.ub.y
		if dx * dx + dy * dy < 30 * 30 then
			t.stub_held = true
			t.ub.hold = 40
		end
	end
end
OUT = io.open(TRACK_OUT, "w")
function Track()
	local t = SQ and SQ.cur
	if not t or not t.units then return end
	for _, x in ipairs(t.units) do
		local o = x.o
		if o.valid then
			OUT:write(string.format("T|gf=%d|mf=%d|k=%d|r=%s|id=%d|x=%.3f|y=%.3f|z=0.00|fx=1.0000|fy=0.0000|fz=0.0000|st=0x%x|v=%.5f\n",
				NOW, NOW, t.k, x.role, o.id, o.x, o.y, o.st, o.v))
		end
	end
end
'''

MAIN = r'''
Base_Definitions()
while NOW < 30 * 60 * 20 and not SN.done do
	NOW = NOW + 1
	Physics()
	Track()
	if math.mod(NOW, 2) == 0 then FreeStoreService() end
end
OUT:close()
for i = 1, SN_N do print(SN_LOG[i]) end
'''


def main():
    lua = lua50.find_lua()
    if not lua:
        raise SystemExit('no lua50.exe (common\\build_lua50.cmd)')
    if mov2_t07_track.selftest() != 0:
        raise SystemExit('track selftest failed')
    work = tempfile.mkdtemp(prefix='mov2_t07_selftest_')
    try:
        mod = os.path.join(work, 'mod')
        cfg = bm.build(None, mod, os.path.join(ROOT, 'extracted', 'foc_data_text.zip'), no_text=True)
        fs = os.path.join(mod, 'Data', 'Scripts', 'FreeStore', 'BusyTacticalFreeStore.lua').replace('\\', '/')
        chk = os.path.join(work, 'compile.lua')
        with open(chk, 'w') as f:
            f.write('local f, e = loadfile([[%s]]) if not f then print("COMPILE " .. e) else print("COMPILED") end\n' % fs)
        r = subprocess.run([lua, chk], capture_output=True, text=True, timeout=60)
        if 'COMPILED' not in r.stdout:
            raise SystemExit('built free store does not compile: %s%s' % (r.stdout, r.stderr))
        track = os.path.join(work, 'track.log').replace('\\', '/')
        sim = os.path.join(work, 'sim.lua')
        with open(sim, 'w') as f:
            f.write('TRACK_OUT = [[%s]]\n' % track + STUB + '\n' + bm.probe_lua(cfg) + '\n' + MAIN)
        r = subprocess.run([lua, sim], capture_output=True, text=True, timeout=600)
        recs = [ln for ln in r.stdout.splitlines() if ln.startswith('M7Q|')]
        if r.returncode != 0 or not recs:
            raise SystemExit('Lua run failed:\n%s\n%s' % (r.stdout[-3000:], r.stderr[-3000:]))
        rd = os.path.join(work, 'run')
        os.makedirs(rd)
        with open(os.path.join(rd, 'records.log'), 'w') as f:
            f.write('\n'.join(recs) + '\n')
        shutil.copy(track, os.path.join(rd, 'track.log'))
        with open(os.path.join(rd, 'config.json'), 'w') as f:
            json.dump(cfg, f)
        text = mov2_t07_analyse.report(rd)
        print(text)
        kinds = [ln.split('|')[1] for ln in recs]
        n_trials = len(cfg['sq']['trials'])
        errs = [ln for ln in recs if ln.split('|')[1] in ('ERR', 'APIERR')]
        checks = {
            'DONE recorded': 'DONE' in kinds,
            'every trial ended': kinds.count('TEND') == n_trials,
            'no ERR / APIERR': not errs,
            'one STOP line per squad / unit trial': text.count('  STOP order') == n_trials - len(bm.MVM_TRIALS) - 1,
            'three REORDER lines per squad / unit trial': text.count('  REORDER ') == 3 * (n_trials - len(bm.MVM_TRIALS) - 1),
            'same-direction re-order keeps Move in the stub': 'REORDER same' in text,
            'MvM 40-frame hold found': 'x40 (' in text,
        }
        bad = [k for k, v in checks.items() if not v]
        if bad:
            raise SystemExit('FAILED: %s\n%s' % (bad, '\n'.join(errs[:5])))
        print('MOV2 T07 SELFTEST OK (%d trials, %d records)' % (n_trials, len(recs)))
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == '__main__':
    main()
