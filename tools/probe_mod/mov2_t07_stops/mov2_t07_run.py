#!/usr/bin/env python3
"""mov2_t07_run.py - one mov2 t07 probe session (one land skirmish, hands-off): back up the player's profile, build +
install Mods\\Mov2T07Probe, launch FoC windowed with it (common\\launch_foc.py), scan the game's memory for the probe
records, track the probe's objects every logic frame (mov2_t07_track.py, read-only), write the report when the game
exits, then clean up and restore the profile (always, also after an error or Ctrl+C).

  mov2_t07_run.py [--game DIR] [--no-launch] [--no-profile] [--map NAME] [--reps N] [--dry-run]
  mov2_t07_run.py --report-only RUN_DIR

Output: tools\\probe_mod\\mov2_t07_stops\\out\\<yyyymmdd-hhmmss>\\ records.log (M7Q Lua records), track.log (T lines:
per logic frame position / facing / locomotor state / speed of every tracked object), config.json, report.txt,
run.json, run_status.txt, cleanup.log, profile_backup\\; out\\latest.txt names the newest run folder.
Exit codes: 0 report written; 10 no records; 20 preflight; 30 interrupted; 31 error. Last line MOV2_T07_RUN_DONE exit=<n>.
"""
import argparse
import datetime
import json
import os
import re
import subprocess
import sys
import time

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
PM = os.path.dirname(HERE)
for sub in ('', 'common', 'a1_tactical', 'q2_traces', 's78n_t14_squads'):
    sys.path.insert(0, os.path.join(PM, sub) if sub else HERE)
sys.path.insert(0, HERE)
from probe_run import say, running, set_state, scan_once  # noqa: E402
import probe_cleanup  # noqa: E402
import build_mov2_t07_mod as bm  # noqa: E402
import mov2_t07_analyse  # noqa: E402
import mov2_t07_track  # noqa: E402

NEEDLE = b'M7Q|'
RECORD_RE = re.compile(rb'M7Q\|[A-Z0-9_]+\|[ -~]{0,500}?\|END')
WINDOWED = ['WINDOWED', 'SCREENWIDTH=1920', 'SCREENHEIGHT=1080']
MODPATH = ['MODPATH=Mods\\' + bm.MOD_NAME]
DONE = 'MOV2_T07_RUN_DONE'


def cleanup(game, log=None, dry_run=False):
    return probe_cleanup.cleanup(game, (bm.MOD_NAME,), bm.MARKER, log, dry_run)


def steps(cfg):
    n = len(cfg['sq']['trials'])
    return r'''
 mov2 t07 session: one LAND skirmish battle, about 12 minutes in battle, hands-off. Notes: README.md
 Single Player > Skirmish > LAND, map %s. Slot 1 (you): EMPIRE, team 1. Slot 2: AI EASY, REBEL, team 2. Every
 other slot CLOSED. Default credits and settings. Start.
 Give NO orders, click nothing on the map, never pause, never change the game speed, never save, keep the game
 window in front (FoC pauses on focus loss). "MOV2 T07 PROBE: ... trials armed" appears in the first seconds.
 From 15 s on, %d trials run one after another on a flat strip far from both bases (squads and T4B tanks, yours,
 invulnerable; the camera follows). Wait for "MOV2 T07 PROBE: trials recorded", then Esc > Quit and quit the game.
 The run ends by itself (%s exit=<n>), cleans up and restores the profile.
''' % (cfg['map'].replace('_MP_LAND_', '').title(), n, DONE)


def launch(game, args):
    cmd = [sys.executable, '-I', os.path.join(PM, 'common', 'launch_foc.py'), '--game-root', game, '--no-windowed'] + args
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    say((r.stdout + r.stderr).strip())
    return r.returncode


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game', default=probe_cleanup.DEFAULT_GAME)
    ap.add_argument('--no-launch', action='store_true')
    ap.add_argument('--no-profile', action='store_true')
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--map', default=bm.DEFAULT_MAP)
    ap.add_argument('--reps', type=int, default=2)
    ap.add_argument('--wait-start-sec', type=float, default=900)
    ap.add_argument('--scan-sec', type=float, default=2.0)
    ap.add_argument('--report-only')
    a = ap.parse_args()
    if a.report_only:
        text = mov2_t07_analyse.report(a.report_only, os.path.join(a.report_only, 'report.txt'))
        say(text)
        return 0
    corr = os.path.join(a.game, 'corruption')
    out_root = os.path.join(HERE, 'out')
    run_dir = os.path.join(out_root, datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    cmdline = ' '.join(['StarWarsG.exe'] + WINDOWED + MODPATH)
    if a.dry_run:
        mod = os.path.join(run_dir, 'dryrun_mod', bm.MOD_NAME)
        cfg = bm.build(a.game, mod, map_name=a.map, reps=a.reps)
        say(bm.describe(cfg))
        say('\n' + '=' * 110 + steps(cfg) + '=' * 110)
        say('%s exit=0' % DONE)
        return 0
    if not os.path.isfile(os.path.join(corr, 'StarWarsG.exe')):
        say('StarWarsG.exe not found under', corr)
        say('%s exit=20' % DONE)
        return 20
    if running('StarWarsG.exe'):
        say('The game is already running; close it first.')
        say('%s exit=20' % DONE)
        return 20
    if not running('steam.exe'):
        say('WARNING: Steam does not seem to be running; start it first if the game refuses to launch.')
    import a1mem  # noqa: E402 - Windows-only ctypes
    import s78n_t14_cleanup  # noqa: E402 - restore_profile()
    os.makedirs(run_dir, exist_ok=True)
    with open(os.path.join(out_root, 'latest.txt'), 'w') as f:
        f.write(run_dir + '\n')
    info = {'game': a.game, 'modpath': MODPATH, 'started': time.time(), 'scans': 0, 'cmdline': cmdline}
    code = 0
    profile = proc = tracker = None
    set_state(run_dir, state='installing', run_dir=run_dir)
    try:
        if not a.no_profile:
            import q2_profile
            profile = q2_profile
            profile.snapshot(run_dir)
        cleanup(a.game, os.path.join(run_dir, 'cleanup.log'))
        cfg = bm.build(a.game, os.path.join(corr, 'Mods', bm.MOD_NAME), map_name=a.map, reps=a.reps)
        with open(os.path.join(run_dir, 'config.json'), 'w') as f:
            json.dump(cfg, f, indent=1)
        info['site'] = cfg.get('site_note')
        say(bm.describe(cfg))
        say('\n' + '=' * 110 + steps(cfg) + '=' * 110)
        t0 = time.time()
        if a.no_launch:
            say('Start the game yourself, in %s:\n  %s' % (corr, cmdline))
        elif launch(a.game, WINDOWED + MODPATH) != 0:
            raise RuntimeError('launch_foc.py could not start the game')
        set_state(run_dir, state='waiting_for_game_start', run_dir=run_dir, cmdline=cmdline)
        pids = []
        while not pids:
            if time.time() - t0 > a.wait_start_sec:
                raise RuntimeError('StarWarsG.exe did not start within %d s' % a.wait_start_sec)
            time.sleep(1)
            pids = a1mem.find_pids()
        proc = a1mem.Proc(pids[0])
        info['identity'] = 'ok' if proc.pe_identity() else 'unknown (not FoC 1.121? the object offsets would be wrong)'
        set_state(run_dir, state='game_running', run_dir=run_dir, pid=pids[0])
        say('Game running (pid %d, exe %s). Scanning records every %.0f s, tracking objects every frame ...' % (
            pids[0], info['identity'], a.scan_sec))
        tracker = mov2_t07_track.Tracker(proc, a1mem.Game(proc), os.path.join(run_dir, 'track.log')).start()
        seen = set()
        records = []
        with open(os.path.join(run_dir, 'records.log'), 'w') as log:
            while proc.alive():
                before = len(seen)
                new, st = scan_once(proc, NEEDLE, RECORD_RE, seen, log)
                info['scans'] += 1
                if new:
                    fresh = sorted(seen - set(records), key=lambda r: int(re.search(r'\|n=(\d+)\|', r).group(1))
                                   if re.search(r'\|n=(\d+)\|', r) else 0)
                    for r in fresh:
                        records.append(r)
                        tracker.feed(r)
                    say('  +%d records (%d total); track lines %d over %d frames' % (
                        len(seen) - before, len(seen), tracker.lines, tracker.frames))
                time.sleep(a.scan_sec)
        tracker.stop()
        info.update(records=len(seen), track_lines=tracker.lines, track_frames=tracker.frames)
        set_state(run_dir, state='collecting', run_dir=run_dir)
        text = mov2_t07_analyse.report(run_dir, os.path.join(run_dir, 'report.txt'))
        if not seen:
            code = 10
            say('NO RECORDS: did the battle run as a LAND skirmish with Empire human vs Rebel AI?')
        say(text)
    except KeyboardInterrupt:
        say('interrupted')
        code = 30
    except (Exception, SystemExit) as e:  # noqa: BLE001 - reported, cleanup still runs
        say('ERROR:', e)
        info['error'] = str(e)
        code = 31
    finally:
        if tracker is not None:
            try:
                tracker.stop()
            except Exception:  # noqa: BLE001
                pass
        if proc is not None:
            try:
                proc.close()
            except Exception:  # noqa: BLE001
                pass
        rep = cleanup(a.game, os.path.join(run_dir, 'cleanup.log'))
        info['cleanup'] = rep
        prof = None
        if profile is not None:
            try:
                import s78n_t14_cleanup  # noqa: E402
                ok_p, msg = s78n_t14_cleanup.restore_profile(run_dir)
                prof = ('verified: ' if ok_p else 'NOT DONE: ') + msg
                say('profile:', msg)
            except Exception as e:  # noqa: BLE001
                prof = 'ERROR %s' % e
        info['profile_restore'] = prof
        info['ended'] = time.time()
        info['exit_code'] = code
        with open(os.path.join(run_dir, 'run.json'), 'w') as f:
            json.dump(info, f, indent=1)
        set_state(run_dir, state='done', exit_code=code, cleanup='OK' if rep['ok'] else 'INCOMPLETE', profile=prof,
                  run_dir=run_dir)
        say('cleanup:', 'OK' if rep['ok'] else 'LEFT SOMETHING, see cleanup.log', '| profile:', prof,
            '| run folder:', run_dir)
        say('%s exit=%d' % (DONE, code))
    return code


if __name__ == '__main__':
    sys.exit(main())
