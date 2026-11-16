#!/usr/bin/env python3
"""q6t33_run.py - one q6 t33 / t35 probe run: build + install Mods\\Q6T33Probe, launch FoC windowed with it, poll the
end-screen gates from memory, take screenshots around the end, scan for the plot's records, write the report when the
game exits, clean up (always, also after an error or Ctrl+C).

  q6t33_run.py VARIANT [VARIANT ...] [--game DIR] [--no-launch] [--wait-start-sec N] [--scan-sec S]
  q6t33_run.py --report-only RUN_DIR [RUN_DIR ...]     re-read earlier runs (one summary table over all of them)

VARIANT is one of build_q6t33_mod.VARIANTS (E_BASE, E_NOMSG, E_NOFLEET, E_SDBYSS, E_CURPLANET, D_BASE, D_LONGMSG); the
mod always holds every campaign, the names only label the run and print its steps. One variant per game launch is the
intended use (a fresh GUI each time); several in one launch work too (the report splits them by the plot's records).

Output: tools\\probe_mod\\q6_t33_elim_end\\out\\<yyyymmdd-hhmmss>-<variants>\\
  gates.log     q6t33_gates.py lines (end-screen gates, victory monitor, GUI hint flags, cursor; every change)
  records.log   the plot's Lua records (Q6T|...|END, heap scan)
  shots\\        half-size PNGs every 0.5 s from 3 s before the victory monitor is declared until 3 s after the end
                screen (or 30 s); shots.csv maps each to the gate sample it was taken at; ctx_*.png one per 10 s
  report.txt    q6t33_parse.py; run.json, run_status.txt, cleanup.log; out\\latest.txt names the newest run folder
Exit codes: 0 report written; 10 no records; 20 preflight (game missing or already running); 30 interrupted; 31 error.
"""
import argparse
import collections
import ctypes
import ctypes.wintypes as wt
import datetime
import json
import os
import re
import struct
import subprocess
import sys
import threading
import time
import zlib

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
PM = os.path.dirname(HERE)
ROOT = os.path.dirname(os.path.dirname(PM))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(PM, 'common'))
sys.path.insert(0, os.path.join(PM, 'a1_tactical'))
from probe_run import say, running, set_state, scan_once  # noqa: E402
import probe_cleanup  # noqa: E402
import build_q6t33_mod  # noqa: E402
import q6t33_gates  # noqa: E402
import q6t33_parse  # noqa: E402

NEEDLE = b'Q6T|'
RECORD_RE = re.compile(rb'Q6T\|[A-Z0-9_]+\|[ -~]{0,380}?\|END')
WINDOWED = ['WINDOWED', 'SCREENWIDTH=1920', 'SCREENHEIGHT=1080']


def cleanup(game, log=None, dry_run=False):
    return probe_cleanup.cleanup(game, (build_q6t33_mod.MOD_NAME,), build_q6t33_mod.MARKER, log, dry_run)


def steps(variants):
    lines = [' q6 t33 probe run: %s. Full notes: tools\\probe_mod\\q6_t33_elim_end\\README.md' % ', '.join(variants),
             ' Never pause, never change the game speed, never save. Click nothing in the galaxy unless a step says so.']
    for i, v in enumerate(variants):
        mode, _, _, cur, what = build_q6t33_mod.VARIANTS[v]
        ev = 'at 40 s (Tatooine made neutral)' if mode == 'E' else 'at 20 s (Abregado Rae given to the Rebel)'
        park = ('on the TATOOINE planet sphere: the script centres the camera on Tatooine in the first second, so it is '
                'the planet in the middle of the screen (the encyclopedia pop-up must open)' if cur == 'planet' else
                'over EMPTY black space, at least 250 px from every planet, route and HUD element')
        lines += [' %d) Main menu > Single Player > Galactic Conquest > "Q6T33: %s" > EMPIRE > Start.' % (i + 1, v),
                  '    (%s)' % what,
                  '    As soon as the galaxy shows: move the mouse %s and do not move it again.' % park,
                  '    The event comes %s; the end screen should follow within about 10 s.' % ev,
                  '    When the end screen shows (or 30 s after the event): wait 3 s, then click its button back to the',
                  '    main menu (or Esc > Quit to main menu, never save).']
    lines.append(' Last: quit the game (main menu > Quit). The run ends by itself (Q6T33_RUN_DONE exit=<n>).')
    return '\n'.join(lines)


# ---------------------------------------------------------------------------------------------- screenshots
_u32 = ctypes.WinDLL('user32', use_last_error=True)
_gdi = ctypes.WinDLL('gdi32', use_last_error=True)
_u32.GetDC.restype = wt.HDC
_u32.GetDC.argtypes = [wt.HWND]
_u32.ReleaseDC.argtypes = [wt.HWND, wt.HDC]
_gdi.CreateCompatibleDC.restype = wt.HDC
_gdi.CreateCompatibleDC.argtypes = [wt.HDC]
_gdi.CreateCompatibleBitmap.restype = wt.HBITMAP
_gdi.CreateCompatibleBitmap.argtypes = [wt.HDC, ctypes.c_int, ctypes.c_int]
_gdi.SelectObject.restype = wt.HGDIOBJ
_gdi.SelectObject.argtypes = [wt.HDC, wt.HGDIOBJ]
_gdi.BitBlt.argtypes = [wt.HDC, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wt.HDC, ctypes.c_int,
                        ctypes.c_int, wt.DWORD]
_gdi.GetDIBits.argtypes = [wt.HDC, wt.HBITMAP, wt.UINT, wt.UINT, ctypes.c_void_p, ctypes.c_void_p, wt.UINT]
_gdi.DeleteObject.argtypes = [wt.HGDIOBJ]
_gdi.DeleteDC.argtypes = [wt.HDC]
EnumWindowsProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)


def dpi_aware():
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(2)   # physical pixels for cursor, rects and the screen copy
    except Exception:  # noqa: BLE001
        try:
            _u32.SetProcessDPIAware()
        except Exception:  # noqa: BLE001
            pass


def game_window(pid):
    found = []

    def cb(hwnd, _):
        p = wt.DWORD()
        _u32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        if p.value == pid and _u32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True

    _u32.EnumWindows(EnumWindowsProc(cb), 0)
    best, area = None, 0
    for h in found:
        x, y, w, hh = client_rect(h)
        if w * hh > area:
            best, area = h, w * hh
    return best


def client_rect(hwnd):
    r = wt.RECT()
    _u32.GetClientRect(hwnd, ctypes.byref(r))
    p = wt.POINT(0, 0)
    _u32.ClientToScreen(hwnd, ctypes.byref(p))
    return p.x, p.y, r.right - r.left, r.bottom - r.top


def grab(hwnd):
    """Screen copy of the client area (the game window is in front during a run) as (w, h, BGRA)."""
    ox, oy, w, h = client_rect(hwnd)
    if w <= 0 or h <= 0:
        return None
    sdc = _u32.GetDC(None)
    mdc = _gdi.CreateCompatibleDC(sdc)
    bmp = _gdi.CreateCompatibleBitmap(sdc, w, h)
    old = _gdi.SelectObject(mdc, bmp)
    _gdi.BitBlt(mdc, 0, 0, w, h, sdc, ox, oy, 0x00CC0020 | 0x40000000)
    bi = ctypes.create_string_buffer(struct.pack('<IiiHHIIiiII', 40, w, -h, 1, 32, 0, 0, 0, 0, 0, 0), 44)
    buf = ctypes.create_string_buffer(w * h * 4)
    _gdi.GetDIBits(mdc, bmp, 0, h, buf, bi, 0)
    _gdi.SelectObject(mdc, old)
    _gdi.DeleteObject(bmp)
    _gdi.DeleteDC(mdc)
    _u32.ReleaseDC(None, sdc)
    return w, h, buf.raw


def write_half_png(path, img):
    w, h, data = img
    w2 = w // 2
    rows = []
    for y in range(0, h - 1, 2):
        row = data[y * w * 4:(y * w + w2 * 2) * 4]
        rgb = bytearray(w2 * 3)
        rgb[0::3] = row[2::8]
        rgb[1::3] = row[1::8]
        rgb[2::3] = row[0::8]
        rows.append(b'\x00' + bytes(rgb))

    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)

    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w2, len(rows), 8, 2, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(b''.join(rows), 3)) + chunk(b'IEND', b''))


class Shooter:
    """Every 0.5 s: grab the client area into a 3 s ring. From the first sample with the victory monitor declared,
    the ring and every later grab are saved until 3 s after the end screen shows (or 30 s). One context shot per 10 s
    while the galactic mode runs."""

    def __init__(self, pid, poller, out_dir, period=0.5, ring_s=3.0, after_s=3.0, max_s=30.0):
        self.pid, self.poller, self.dir = pid, poller, out_dir
        self.period, self.after_s, self.max_s = period, after_s, max_s
        self.ring = collections.deque(maxlen=int(ring_s / period))
        self.saved = 0
        self.csv = None
        self._stop = threading.Event()
        self._th = threading.Thread(target=self._run, daemon=True)

    def start(self):
        os.makedirs(self.dir, exist_ok=True)
        self.csv = open(os.path.join(self.dir, 'shots.csv'), 'w')
        self.csv.write('file,w,gf,mf,decl,cd,hold,msgbox,bink,hint,pop,mov,ends,cx,cy,client\n')
        self._th.start()
        return self

    def stop(self):
        self._stop.set()
        self._th.join(10)
        if self.csv:
            self.csv.close()

    def _save(self, name, img, s, rect):
        try:
            write_half_png(os.path.join(self.dir, name), img)
        except OSError:
            return
        self.saved += 1
        t0 = self.poller.t0
        self.csv.write('%s,%.4f,%s\n' % (name, s['w'] - t0, ','.join(str(s.get(k)) for k in (
            'gf', 'mf', 'decl', 'cd', 'hold', 'msgbox', 'bink', 'hint', 'pop', 'mov', 'ends', 'cx', 'cy')) +
            ',%d %d %d %d' % rect))
        self.csv.flush()

    def _run(self):
        hwnd, last_ctx, burst_from, end_seen, n = None, 0.0, None, None, 0
        while not self._stop.is_set():
            time.sleep(self.period)
            s = self.poller.latest
            if s is None:
                continue
            if not hwnd:
                hwnd = game_window(self.pid)
                if not hwnd:
                    continue
            try:
                img = grab(hwnd)
                rect = client_rect(hwnd)
            except Exception:  # noqa: BLE001 - a lost window: look it up again
                hwnd = None
                continue
            if img is None:
                continue
            n += 1
            now = time.perf_counter()
            tag = 'gf%s' % s.get('gf')
            if s.get('decl') == 1 and burst_from is None and s.get('mt') == 0:
                burst_from, end_seen = now, None
                for k, (ri, rs, rr) in enumerate(self.ring):
                    self._save('shot_%05d_pre%d_gf%s.png' % (n - len(self.ring) + k, k, rs.get('gf')), ri, rs, rr)
                self.ring.clear()
            if burst_from is not None:
                self._save('shot_%05d_%s.png' % (n, tag), img, s, rect)
                if s.get('ends') == 1 and end_seen is None:
                    end_seen = now
                if (end_seen is not None and now - end_seen >= self.after_s) or now - burst_from >= self.max_s:
                    burst_from = None
                    # next campaign of the same launch: wait for the monitor to be cleared first
                    while not self._stop.is_set() and (self.poller.latest or {}).get('decl') == 1:
                        time.sleep(0.2)
                continue
            self.ring.append((img, s, rect))
            if s.get('mt') == 0 and s.get('mon') == 1 and now - last_ctx >= 10:
                last_ctx = now
                self._save('ctx_%05d_%s.png' % (n, tag), img, s, rect)


# ---------------------------------------------------------------------------------------------- run
def launch(game, args):
    """common/launch_foc.py: a game started from a Claude shell otherwise exits at once (see its docstring)."""
    cmd = [sys.executable, '-I', os.path.join(PM, 'common', 'launch_foc.py'), '--game-root', game, '--no-windowed'] + args
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    say((r.stdout + r.stderr).strip())
    return r.returncode


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('variants', nargs='*')
    ap.add_argument('--game', default=probe_cleanup.DEFAULT_GAME)
    ap.add_argument('--no-launch', action='store_true')
    ap.add_argument('--wait-start-sec', type=float, default=600)
    ap.add_argument('--scan-sec', type=float, default=2.0)
    ap.add_argument('--report-only', nargs='+')
    a = ap.parse_args()
    if a.report_only:
        text = q6t33_parse.report_runs(a.report_only)
        say(text)
        return 0
    bad = [v for v in a.variants if v not in build_q6t33_mod.VARIANTS]
    if not a.variants or bad:
        say('give one or more variants of: %s (unknown: %s)' % (', '.join(build_q6t33_mod.VARIANTS), bad))
        say('Q6T33_RUN_DONE exit=20')
        return 20
    dpi_aware()
    import a1mem  # noqa: E402 - Windows-only ctypes
    corr = os.path.join(a.game, 'corruption')
    if not os.path.isfile(os.path.join(corr, 'StarWarsG.exe')):
        say('StarWarsG.exe not found under', corr)
        say('Q6T33_RUN_DONE exit=20')
        return 20
    if running('StarWarsG.exe'):
        say('The game is already running; close it first.')
        say('Q6T33_RUN_DONE exit=20')
        return 20
    if not running('steam.exe'):
        say('WARNING: Steam does not seem to be running; start it first if the game refuses to launch.')
    out_root = os.path.join(HERE, 'out')
    run_dir = os.path.join(out_root, datetime.datetime.now().strftime('%Y%m%d-%H%M%S') + '-' + '+'.join(a.variants))
    os.makedirs(run_dir, exist_ok=True)
    with open(os.path.join(out_root, 'latest.txt'), 'w') as f:
        f.write(run_dir + '\n')
    mods = ['MODPATH=Mods\\' + build_q6t33_mod.MOD_NAME]
    info = {'game': a.game, 'variants': a.variants, 'modpath': mods, 'started': time.time(), 'scans': 0}
    code = 0
    poller = shooter = None
    set_state(run_dir, state='installing', run_dir=run_dir)
    try:
        cleanup(a.game, os.path.join(run_dir, 'cleanup.log'))
        build_q6t33_mod.build(a.game, os.path.join(corr, 'Mods', build_q6t33_mod.MOD_NAME))
        say('\n' + '=' * 110 + '\n' + steps(a.variants) + '\n' + '=' * 110)
        cmdline = ' '.join(['StarWarsG.exe'] + WINDOWED + mods)
        t0 = time.time()
        if a.no_launch:
            say('Start the game yourself, in %s:\n  %s' % (corr, cmdline))
        elif launch(a.game, WINDOWED + mods) != 0:
            raise RuntimeError('launch_foc.py could not start the game')
        set_state(run_dir, state='waiting_for_game_start', run_dir=run_dir, cmdline=cmdline)
        pids = []
        while not pids:
            if time.time() - t0 > a.wait_start_sec:
                raise RuntimeError('StarWarsG.exe did not start within %d s' % a.wait_start_sec)
            time.sleep(1)
            pids = a1mem.find_pids()
        proc = a1mem.Proc(pids[0])
        info['identity'] = 'ok' if proc.pe_identity() else 'unknown (not FoC 1.121? the gate addresses would be wrong)'
        say('Game running (pid %d, exe %s). Polling gates, scanning records every %.0f s until it exits ...' % (
            pids[0], info['identity'], a.scan_sec))
        set_state(run_dir, state='game_running', run_dir=run_dir, pid=pids[0])
        poller = q6t33_gates.Poller(proc, os.path.join(run_dir, 'gates.log')).start()
        shooter = Shooter(pids[0], poller, os.path.join(run_dir, 'shots')).start()
        seen = set()
        with open(os.path.join(run_dir, 'records.log'), 'w') as log:
            while proc.alive():
                new, st = scan_once(proc, NEEDLE, RECORD_RE, seen, log)
                info['scans'] += 1
                if new:
                    say('  +%d records (%d total); gate lines %d, shots %d' % (new, len(seen), poller.lines,
                                                                              shooter.saved))
                time.sleep(a.scan_sec)
        shooter.stop()
        poller.stop()
        proc.close()
        info.update(records=len(seen), gate_samples=poller.samples, gate_lines=poller.lines, shots=shooter.saved)
        set_state(run_dir, state='collecting', run_dir=run_dir)
        text = q6t33_parse.report_runs([run_dir], os.path.join(run_dir, 'report.txt'))
        if not seen:
            code = 10
            say('NO RECORDS: did a Q6T33 campaign run?')
        say(text)
    except KeyboardInterrupt:
        say('interrupted')
        code = 30
    except Exception as e:  # noqa: BLE001 - reported, cleanup still runs
        say('ERROR:', e)
        info['error'] = str(e)
        code = 31
    finally:
        for th in (shooter, poller):
            try:
                if th:
                    th.stop()
            except Exception:  # noqa: BLE001
                pass
        rep = cleanup(a.game, os.path.join(run_dir, 'cleanup.log'))
        info['cleanup'] = rep
        info['ended'] = time.time()
        info['exit_code'] = code
        with open(os.path.join(run_dir, 'run.json'), 'w') as f:
            json.dump(info, f, indent=1)
        set_state(run_dir, state='done', exit_code=code, cleanup='OK' if rep['ok'] else 'INCOMPLETE', run_dir=run_dir)
        say('cleanup:', 'OK' if rep['ok'] else 'LEFT SOMETHING, see cleanup.log', '| run folder:', run_dir)
        say('Q6T33_RUN_DONE exit=%d' % code)
    return code


if __name__ == '__main__':
    sys.exit(main())
