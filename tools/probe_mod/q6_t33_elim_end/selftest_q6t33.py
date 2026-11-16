#!/usr/bin/env python3
"""selftest_q6t33.py - offline check of the q6 t33 kit (no game):
  1. q6t33_gates.py field decoding on a fake memory image;
  2. builds the mod from extracted\\foc_data_text.zip (--no-text) and runs every variant's plot Lua under stock Lua
     5.0.2 (common\\lua50.py) with a stub galactic engine (30 frames per second, Change_Owner, Find_First_Object);
  3. feeds those records plus a synthetic gates.log to q6t33_parse.py: a D run whose end screen comes at ready (NOT
     HELD) and an E run held by a Light_Loop Bink while a hint text shows (HELD ... Bink open).
Ends with Q6T33 SELFTEST OK.
"""
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
import build_q6t33_mod  # noqa: E402
import q6t33_gates  # noqa: E402
import q6t33_parse  # noqa: E402

HARNESS = r'''
NOW = 0
OnEnter = "OnEnter"
function require(x) end
function GetCurrentTime() return NOW end
function Game_Message(s) end
function Get_Game_Mode() return "Galactic" end
OWNERS = { Tatooine = "Neutral", Byss = "Neutral", Abregado_Rae = "Neutral", Jabiim = "Rebel" }
function MkPlayer(name) return { Get_Faction_Name = function() return string.upper(name) end,
	Is_Human = function() return name == "Empire" end, name = name } end
PLAYERS = { Empire = MkPlayer("Empire"), Rebel = MkPlayer("Rebel"), Neutral = MkPlayer("Neutral") }
function Find_Player(n) return PLAYERS[n] end
function FindPlanet(n)
	return { Get_Owner = function() return PLAYERS[OWNERS[n]] end,
		Change_Owner = function(p) OWNERS[n] = p.name end,
		Get_Type = function() return { Get_Name = function() return string.upper(n) end } end }
end
SD_AT = SD_START
function Find_First_Object(t)
	if SD_AT == nil then return nil end
	return { Get_Planet_Location = function() return FindPlanet(SD_AT) end,
		Get_Parent_Object = function() return { Get_Type = function() return { Get_Name = function() return "Fleet" end } end } end }
end
CO = nil
function Create_Thread(name) CO = coroutine.create(_G[name]) end
function Sleep(t) local s = NOW while NOW - s < t do coroutine.yield() end end
dofile(SCRIPT)
Definitions()
Q6T_State_Begin(OnEnter)
local frame = 0
while CO and coroutine.status(CO) ~= "dead" and frame < 30 * 130 do
	local ok, err = coroutine.resume(CO)
	if not ok then print("LUAERROR " .. tostring(err)) break end
	frame = frame + 1
	NOW = frame / 30
end
for i = 1, Q6T_N do print(Q6T_LOG[i]) end
'''


def run_lua(lua, script, sd_start, work):
    h = os.path.join(work, 'harness.lua')
    with open(h, 'w') as f:
        f.write('SCRIPT = [[%s]]\nSD_START = %s\n' % (script, 'nil' if sd_start is None else '"%s"' % sd_start))
        f.write(HARNESS)
    r = subprocess.run([lua, h], capture_output=True, text=True, timeout=120)
    if r.returncode != 0 or 'LUAERROR' in r.stdout:
        raise SystemExit('Lua failed for %s:\n%s%s' % (script, r.stdout[-2000:], r.stderr[-2000:]))
    return [ln for ln in r.stdout.splitlines() if ln.startswith('Q6T|')]


def gate_line(w, gf, mf, **kw):
    s = {'w': w, 'gf': gf, 'mf': mf, 'mt': 0, 'mon': 1, 'decl': 0, 'cd': 0, 'hold': 0, 'b79': 0, 'msgbox': 0,
         'bink': 0, 'binkp': 0, 'ends': 0, 'skip': 0, 'chg': 1, 'g26a4': 1, 'mov': 0, 'hint': 0, 'pop': 0, 'g2908': 0,
         'g2a34': 0, 'movie': '-', 'cx': 100, 'cy': 100}
    s.update(kw)
    return q6t33_gates.line(s, 0.0)


def fake_gates(ev_mf, held, gf_off=5000):
    """Samples around an event at mode frame ev_mf: declared at the event, countdown 30, end screen at ready or after
    `held` frames with a Light_Loop Bink and a hint text up from 10 frames before the event."""
    out = []
    for mf in range(ev_mf - 100, ev_mf + 30 + held + 40):
        gf = mf + gf_off
        kw = {}
        if mf >= ev_mf:
            kw.update(decl=1, cd=max(0, 30 - (mf - ev_mf)))
        if held and ev_mf - 10 <= mf < ev_mf + 30 + held:
            kw.update(bink=1, binkp=1, mov=1, hint=1, movie='Light_Loop')
        if mf >= ev_mf + 30 + held:
            kw.update(ends=1)
        out.append(gate_line(mf / 30.0, gf, mf, **kw))
    return out


def main():
    lua = lua50.find_lua()
    if not lua:
        raise SystemExit('no lua50.exe (common\\build_lua50.cmd)')
    if q6t33_gates.selftest() != 0:
        raise SystemExit('gates selftest failed')
    work = tempfile.mkdtemp(prefix='q6t33_selftest_')
    try:
        mod = os.path.join(work, 'mod')
        build_q6t33_mod.build(None, mod, os.path.join(ROOT, 'extracted', 'foc_data_text.zip'), no_text=True)
        recs = {}
        for v, (mode, msg, forces, _, _) in build_q6t33_mod.VARIANTS.items():
            script = os.path.join(mod, 'Data', 'Scripts', 'Story', 'Q6T33_Story_%s.lua' % v)
            sd = forces[0].split(',')[0].strip() if forces else None
            lines = run_lua(lua, script.replace('\\', '/'), sd, work)
            kinds = [ln.split('|')[1] for ln in lines]
            ev = 'AIGIVE' if mode == 'D' else 'TAKE'
            assert kinds[0] == 'TOP' and 'START' in kinds and ev in kinds and 'DONE' in kinds, (v, kinds[:5])
            assert all(len(ln) <= 380 for ln in lines), (v, max(len(ln) for ln in lines))
            evl = next(ln for ln in lines if ln.split('|')[1] == ev)
            assert 'ok=true' in evl, evl
            if v == 'E_SDBYSS':
                assert 'sd=BYSS' in evl, evl
            if v == 'E_NOFLEET':
                assert 'sd=none' in evl, evl
            say = [ln for ln in lines if ln.split('|')[1] == 'SAY']
            if v == 'E_NOMSG':
                assert any('text=none' in s for s in say), say
            fast = [ln for ln in lines if ln.split('|')[1] == 'HB' and 'dt=40.' in ln]
            if mode == 'E':
                assert len(fast) >= 3, ('0.25 s heartbeats expected', v, len(fast))
            recs[v] = lines
        # two fake runs through the analyser
        verdicts = {}
        for v, held in (('D_BASE', 0), ('E_BASE', 111)):
            rd = os.path.join(work, 'run_' + v)
            os.makedirs(rd)
            with open(os.path.join(rd, 'records.log'), 'w') as f:
                f.write('\n'.join(recs[v]) + '\n')
            ev = next(ln for ln in recs[v] if ln.split('|')[1] in ('TAKE', 'AIGIVE'))
            ev_f = int(next(x for x in ev.split('|') if x.startswith('f='))[2:])
            with open(os.path.join(rd, 'gates.log'), 'w') as f:
                f.write('\n'.join(fake_gates(ev_f, held)) + '\n')
            text, rows = q6t33_parse.report_run(rd)
            assert rows, text
            verdicts[v] = rows[0]
        d, e = verdicts['D_BASE'], verdicts['E_BASE']
        ok = (d[3] is not None and d[3] <= 2 and d[5].startswith('NOT HELD') and
              e[3] == 111 and 'Bink open' in e[5] and 'Light_Loop' in e[5] and 'hint' in e[5])
        print(q6t33_parse.report_runs([os.path.join(work, 'run_D_BASE'), os.path.join(work, 'run_E_BASE')]))
        if not ok:
            raise SystemExit('analyser verdicts wrong: %s / %s' % (d, e))
        print('Q6T33 SELFTEST OK (%d variants run under Lua 5.0.2)' % len(recs))
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == '__main__':
    main()
