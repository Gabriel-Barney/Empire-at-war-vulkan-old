#!/usr/bin/env python3
"""compat_assets_run.py - one compat_assets probe session (roadmap mcx2 t02): back up the player's profile, build +
install Mods\\CompatAssetsProbe, launch FoC with it (or wait for you to), scan the game's memory for the probe records,
screenshot the game window every --shot-sec seconds from the first record on, write the report when the game exits,
then clean up and restore the profile (always, also after an error or Ctrl+C).

  compat_assets_run.py [--game DIR] [--non-interactive] [--no-launch] [--windowed] [--no-profile] [--dry-run]
                       [--only KEY,KEY] [--skip KEY,KEY] [--map NAME] [--shot-sec S] [--scan-sec S]
                       [--report-only RECORDS.log]

  --only / --skip     slot keys (build_compat_assets_mod.py --list); after a crash, rerun with --skip <the slot that
                      was spawning> (the report names it)
  --non-interactive   never waits for a key; progress lines; ends with CAP_RUN_DONE exit=<n>
  --no-launch         install and scan only; start the game yourself (launch_foc.py from a Claude shell)
  --windowed          launch with WINDOWED SCREENWIDTH=1920 SCREENHEIGHT=1080
  --dry-run           offline check: build the mod into out\\<run>\\dryrun_mod (nothing installed), print the plan
  --report-only       rebuild report.txt from an earlier run's records.log

Output: tools\\probe_mod\\compat_assets\\out\\<yyyymmdd-hhmmss>\\ with records.log, report.txt, report.json,
config.json, run.json, run_status.txt, shots\\shot_<nnnn>_<seconds>.png (half size), cleanup.log, profile_backup\\;
out\\latest.txt names the newest run folder.
Exit codes: 0 report written with records; 10 no records; 20 preflight; 30 interrupted; 31 error.
"""
import argparse
import datetime
import json
import os
import re
import subprocess
import sys
import threading
import time

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
PM = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(PM, 'common'))
sys.path.insert(0, os.path.join(PM, 'q2_traces'))
sys.path.insert(0, os.path.join(PM, 'q6_t33_elim_end'))
from probe_run import say, running, set_state, scan_once  # noqa: E402
import build_compat_assets_mod as bm  # noqa: E402
import compat_assets_cleanup  # noqa: E402
import compat_assets_analyse  # noqa: E402

import launch_foc as foc  # noqa: E402  (common/launch_foc.py: the one place a kit starts StarWarsG.exe)
WINDOWED = foc.WINDOWED
MODPATH = ['MODPATH=Mods\\' + bm.MOD_NAME]
NEEDLE = b'CAQ|'
RECORD_RE = re.compile(rb'CAQ\|[A-Z0-9_]+\|[ -~]{0,520}?\|END')
DONE = 'CAP_RUN_DONE'


def steps(cfg):
    secs = (cfg['start_frame'] + len(cfg['slots']) * (cfg['hold'] + cfg['gap']) + cfg['overview']) / 30.0
    return r'''
 compat_assets session: one LAND skirmish battle, about %.1f minutes in battle, hands-off. Notes: README.md
 Single Player > Skirmish > LAND, map GEONOSIS. Slot 1 (you): EMPIRE, team 1. Slot 2: AI EASY, REBEL, team 2.
 Every other slot CLOSED. Default credits and settings. Start.
 Give NO orders, click nothing on the map, never pause, never save, keep the game window in front (FoC pauses on
 focus loss; the screenshots copy the screen area of the window).
 "COMPAT ASSETS PROBE armed" appears at 10 s. Then %d slots, about 10 s each: a unit appears far from both bases,
 the camera points at it and the line "CAP SLOT <n> <KEY>: ..." names it. Wait for "COMPAT ASSETS PROBE done", then
 Esc > Quit and quit the game. If the game crashes or freezes, close it: the report names the slot that was spawning
 (rerun with --skip <KEY>). The run ends by itself (%s), cleans up and restores the profile.
''' % (secs / 60.0, len(cfg['slots']), DONE)


class Shooter:
    """Half-size PNG of the game window every `period` seconds while `active` is set."""

    def __init__(self, pid, out_dir, period):
        import q6t33_run as shots  # screen copy helpers (GDI BitBlt + a stdlib PNG writer)
        self.shots = shots
        shots.dpi_aware()
        self.pid = pid
        self.dir = out_dir
        self.period = period
        self.active = threading.Event()
        self.stop_ev = threading.Event()
        self.n = 0
        self.t0 = time.time()
        os.makedirs(out_dir, exist_ok=True)
        self.th = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self.th.start()

    def stop(self):
        self.stop_ev.set()
        self.th.join(5)

    def _run(self):
        while not self.stop_ev.is_set():
            if self.active.is_set():
                try:
                    hwnd = self.shots.game_window(self.pid)
                    img = self.shots.grab(hwnd) if hwnd else None
                    if img:
                        self.n += 1
                        self.shots.write_half_png(os.path.join(self.dir, 'shot_%04d_%06.1fs.png' % (
                            self.n, time.time() - self.t0)), img)
                except Exception:  # noqa: BLE001 - a missed frame is not fatal
                    pass
            self.stop_ev.wait(self.period)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game', default=bm.DEFAULT_GAME)
    ap.add_argument('--non-interactive', action='store_true')
    ap.add_argument('--no-launch', action='store_true')
    ap.add_argument('--windowed', action='store_true')
    ap.add_argument('--no-profile', action='store_true')
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--only', default='')
    ap.add_argument('--skip', default='')
    ap.add_argument('--map', default=bm.DEFAULT_MAP)
    ap.add_argument('--wait-start-sec', type=float, default=900)
    ap.add_argument('--scan-sec', type=float, default=3.0)
    ap.add_argument('--shot-sec', type=float, default=2.0)
    ap.add_argument('--report-only')
    a = ap.parse_args()
    split = lambda s: [x.strip().upper() for x in s.split(',') if x.strip()]  # noqa: E731

    def build(out, offline=False):
        return bm.build(a.game, out, split(a.only), split(a.skip), a.map, offline)

    if a.report_only:
        d = os.path.dirname(os.path.abspath(a.report_only))
        cfgp = os.path.join(d, 'config.json')
        text, res = compat_assets_analyse.report(a.report_only, os.path.join(d, 'report.txt'),
                                                 os.path.join(d, 'report.json'), cfgp if os.path.exists(cfgp) else None)
        say(text)
        return 0 if res['records'] else 10
    corr = foc.corruption_dir(a.game)
    exe = foc.game_exe(a.game)
    out_root = os.path.join(HERE, 'out')
    run_dir = os.path.join(out_root, datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    cmdline = ' '.join(['StarWarsG.exe'] + (WINDOWED if a.windowed or a.no_launch else []) + MODPATH)
    if a.dry_run:
        os.makedirs(run_dir, exist_ok=True)
        mod = os.path.join(run_dir, 'dryrun_mod', bm.MOD_NAME)
        cfg = build(mod, offline=True)
        with open(os.path.join(run_dir, 'config.json'), 'w') as f:
            json.dump(cfg, f, indent=1)
        say('DRY RUN: built %s (%d files)' % (mod, len(cfg['files'])))
        say('\n'.join(bm.describe(cfg)))
        say('\n' + '=' * 110 + steps(cfg) + '=' * 110)
        say('Command line (in %s): %s' % (corr, cmdline))
        say('%s exit=0' % DONE)
        return 0
    if not os.path.isfile(exe):
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
    os.makedirs(run_dir, exist_ok=True)
    with open(os.path.join(out_root, 'latest.txt'), 'w') as f:
        f.write(run_dir + '\n')
    records_log = os.path.join(run_dir, 'records.log')
    cfg_path = os.path.join(run_dir, 'config.json')
    info = {'game': a.game, 'modpath': MODPATH, 'started': time.time(), 'scans': 0, 'cmdline': cmdline}
    code = 0
    profile = None
    proc = None
    shooter = None
    set_state(run_dir, state='installing', run_dir=run_dir)
    try:
        if not a.no_profile:
            import q2_profile
            profile = q2_profile
            profile.snapshot(run_dir)
        compat_assets_cleanup.cleanup(a.game, os.path.join(run_dir, 'cleanup.log'))
        cfg = build(os.path.join(corr, 'Mods', bm.MOD_NAME))
        with open(cfg_path, 'w') as f:
            json.dump(cfg, f, indent=1)
        say('\n'.join(bm.describe(cfg)))
        say('\n' + '=' * 110 + steps(cfg) + '=' * 110)
        t0 = time.time()
        if not a.no_launch:
            foc.start(a.game, MODPATH, windowed=a.windowed)
            say('Game launched: ' + cmdline)
        else:
            say('Start the game yourself, in %s:\n  %s' % (corr, cmdline))
            say('From a Claude shell: toolchain\\venv\\Scripts\\python.exe -I tools\\probe_mod\\common\\launch_foc.py '
                + MODPATH[0])
        set_state(run_dir, state='waiting_for_game_start', run_dir=run_dir, cmdline=cmdline)
        pids = []
        while not pids:
            if time.time() - t0 > a.wait_start_sec:
                raise RuntimeError('StarWarsG.exe did not start within %d s' % a.wait_start_sec)
            time.sleep(2)
            pids = a1mem.find_pids()
        proc = a1mem.Proc(pids[0])
        info['identity'] = 'ok' if proc.pe_identity() else 'unknown (not FoC 1.121?)'
        shooter = Shooter(pids[0], os.path.join(run_dir, 'shots'), a.shot_sec)
        shooter.start()
        set_state(run_dir, state='game_running', run_dir=run_dir)
        say('Game running (pid %d, exe %s). Scanning for records every %.0f s until it exits ...' % (
            pids[0], info['identity'], a.scan_sec))
        seen = set()
        last_slot = None
        with open(records_log, 'w') as log:
            while proc.alive():
                new, st = scan_once(proc, NEEDLE, RECORD_RE, seen, log)
                info['scans'] += 1
                if new:
                    kinds = [r.split('|')[1] for r in seen]
                    if 'ARM' in kinds and not shooter.active.is_set():
                        shooter.active.set()
                        say('  armed: screenshots every %.1f s' % a.shot_sec)
                    if 'DONE' in kinds and shooter.active.is_set():
                        time.sleep(a.shot_sec * 2)
                        shooter.active.clear()
                        say('  probe done: Esc > Quit, then quit the game')
                    slots = [re.search(r'\|key=([A-Z0-9]+)', r).group(1) for r in seen if '|SHOW|' in r]
                    if slots and slots[-1] != last_slot:
                        last_slot = slots[-1]
                    say('  +%d records (%d total, %.0f MB scanned), slots shown: %d' % (
                        new, len(seen), st['bytes'] / 1e6, len(slots)))
                time.sleep(a.scan_sec)
        info['exit_seen'] = time.time()
        info['shots'] = shooter.n
        set_state(run_dir, state='collecting', run_dir=run_dir)
        info['records'] = len(seen)
        text, res = compat_assets_analyse.report(records_log, os.path.join(run_dir, 'report.txt'),
                                                 os.path.join(run_dir, 'report.json'), cfg_path)
        if not res['records']:
            code = 10
            say('NO RECORDS (%d scans): did the battle run as a LAND skirmish with Empire human vs Rebel AI?' %
                info['scans'])
        say(text)
    except KeyboardInterrupt:
        say('interrupted')
        code = 30
    except (Exception, SystemExit) as e:  # noqa: BLE001 - reported, cleanup still runs
        say('ERROR:', e)
        info['error'] = str(e)
        code = 31
    finally:
        if shooter is not None:
            shooter.stop()
        if proc is not None:
            try:
                proc.close()
            except Exception:  # noqa: BLE001
                pass
        rep = compat_assets_cleanup.cleanup(a.game, os.path.join(run_dir, 'cleanup.log'))
        info['cleanup'] = rep
        prof = None
        if profile is not None:
            try:
                ok_p, msg = compat_assets_cleanup.restore_profile(run_dir)
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
