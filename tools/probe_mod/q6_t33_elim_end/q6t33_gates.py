#!/usr/bin/env python3
"""q6t33_gates.py - read-only poller of the retail galactic end-screen gates (FoC Steam x64 1.121), for the q6 t33 kit.

Every few milliseconds it reads, with ReadProcessMemory only (../common/a1mem.py; never writes, never suspends):
  gframe   u32  global logic frame (RVA 0xB0A320, MAIN_LOOP.md)
  mt / mf  current mode type (0 galactic) and its frame (game mode manager RVA 0xB153E0 +0x1C, cur mode +0x38 -> +0x10)
  top      the pointer the main loop's end-screen check reads (RVA 0xB15418 = mode manager +0x38, the current mode);
           mon = top +0x28 (the victory monitor);
           mon +0x20 u8 declared, +0x24 i32 countdown, +0x5C i32 hold counter, +0x79 u8 retry hold
           (docs/re/Q6_T27_ELIM_END_TIMING.md section 2, docs/re/BKC1_RE_ANSWERS.md Q3)
  msgbox   u8  a message box is up (RVA 0xA61E60)
  bink     u64 the global Bink handle (RVA 0xB30690), binkp u8 its playing byte (RVA 0xB3069B)
  ends     u8  the end screen is shown (RVA 0xA61E08; the end-screen builder returns early while it is set)
  skip     u8  the countdown is skipped while set (RVA 0xB15490)
  chg      u8  gate of the "planet changed hands" hint (RVA 0xB31410)
  GUI      the galactic GUI object the main loop updates (RVA 0xB27F60): +0x26A4 / +0x2908 / +0x2A34 the three hint
           gates, +0x26B1 a GUI movie is active, +0x26B2 a hint text is shown, +0x26B3 an encyclopedia pop-up is shown,
           +0x26B8 std::string the GUI movie's name (Light_Loop)
  cursor   GetCursorPos (screen pixels)
The main loop shows the end screen when mon is ready (hold < 1, declared, countdown < 1) and msgbox == 0 and bink == 0
(single player). A log line is written whenever a field other than the frames changes, and once every 30 gframes.

  Poller(proc, out_path).start(); ... .stop()          (q6t33_run.py)
  q6t33_gates.py --selftest                             field decoding on a fake memory image
"""
import ctypes
import ctypes.wintypes as wt
import struct
import sys
import threading
import time

RVA_FRAME = 0xB0A320
RVA_GMM = 0xB153E0
RVA_TOP = 0xB15418
RVA_MSGBOX = 0xA61E60
RVA_BINK = 0xB30690
RVA_BINKP = 0xB3069B
RVA_ENDS = 0xA61E08
RVA_SKIP = 0xB15490
RVA_CHG = 0xB31410
RVA_GUI = 0xB27F60
GUI_LO = 0x26A4             # one read of GUI +0x26A4 .. +0x2A38
GUI_HI = 0x2A38
GUI_FIELDS = (('g26a4', 0x26A4), ('mov', 0x26B1), ('hint', 0x26B2), ('pop', 0x26B3), ('g2908', 0x2908),
              ('g2a34', 0x2A34))
GUI_MOVIE_NAME = 0x26B8
MON_PTR = 0x28
FIELDS = ('mt', 'mon', 'decl', 'cd', 'hold', 'b79', 'msgbox', 'bink', 'binkp', 'ends', 'skip', 'chg',
          'g26a4', 'mov', 'hint', 'pop', 'g2908', 'g2a34', 'movie', 'cx', 'cy')

_user32 = ctypes.WinDLL('user32') if sys.platform == 'win32' else None


def cursor():
    if _user32 is None:
        return None, None
    p = wt.POINT()
    if not _user32.GetCursorPos(ctypes.byref(p)):
        return None, None
    return p.x, p.y


def u8(b, off=0):
    return b[off] if b and len(b) > off else None


def sample(p, cursor_fn=cursor):
    """One reading of every field as a dict (None for a failed read). `p` is an a1mem.Proc (read/u64/u32/i32)."""
    s = {'w': time.perf_counter()}
    base = p.base
    s['gf'] = p.u32(base + RVA_FRAME)
    g = p.read(base + RVA_GMM, 0x58)
    if g:
        s['mt'] = struct.unpack_from('<i', g, 0x1C)[0]
        cur = struct.unpack_from('<Q', g, 0x38)[0]
        s['mf'] = p.u32(cur + 0x10) if cur else None
    else:
        s['mt'] = s['mf'] = None
    top = p.u64(base + RVA_TOP)
    mon = p.u64(top + MON_PTR) if top else None
    s['mon'] = 1 if mon else 0
    m = p.read(mon, 0x80) if mon else None
    if m:
        s['decl'] = m[0x20]
        s['cd'] = struct.unpack_from('<i', m, 0x24)[0]
        s['hold'] = struct.unpack_from('<i', m, 0x5C)[0]
        s['b79'] = m[0x79]
    else:
        s['decl'] = s['cd'] = s['hold'] = s['b79'] = None
    s['msgbox'] = u8(p.read(base + RVA_MSGBOX, 1))
    bk = p.u64(base + RVA_BINK)
    s['bink'] = None if bk is None else (1 if bk else 0)
    s['binkp'] = u8(p.read(base + RVA_BINKP, 1))
    s['ends'] = u8(p.read(base + RVA_ENDS, 1))
    s['skip'] = u8(p.read(base + RVA_SKIP, 1))
    s['chg'] = u8(p.read(base + RVA_CHG, 1))
    gui = p.read(base + RVA_GUI + GUI_LO, GUI_HI - GUI_LO)
    for k, off in GUI_FIELDS:
        s[k] = u8(gui, off - GUI_LO)
    s['movie'] = '-'
    if s.get('mov'):
        name = p.std_string(base + RVA_GUI + GUI_MOVIE_NAME)
        s['movie'] = name if name else '?'
    s['cx'], s['cy'] = cursor_fn()
    return s


def line(s, t0):
    return 'G|w=%.4f|gf=%s|mf=%s|%s' % (s['w'] - t0, s['gf'], s['mf'], '|'.join('%s=%s' % (k, s[k]) for k in FIELDS))


class Poller:
    """Samples every `period` s on its own thread; `latest` holds the newest sample for the screenshot thread."""

    def __init__(self, proc, out_path, period=0.004, cursor_fn=cursor):
        self.p = proc
        self.out = open(out_path, 'w')
        self.period = period
        self.cursor_fn = cursor_fn
        self.t0 = time.perf_counter()
        self.latest = None
        self.samples = 0
        self.lines = 0
        self._stop = threading.Event()
        self._th = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self.out.write('# q6t33 gates: t0 perf_counter=%.4f wall=%s\n' % (self.t0, time.strftime('%Y-%m-%d %H:%M:%S')))
        self._th.start()
        return self

    def stop(self):
        self._stop.set()
        self._th.join(5)
        self.out.close()

    def _run(self):
        last_key, last_gf = None, None
        while not self._stop.is_set():
            try:
                s = sample(self.p, self.cursor_fn)
            except Exception as e:  # noqa: BLE001 - a failed sample is logged, the poller goes on
                self.out.write('# sample error %s\n' % e)
                time.sleep(0.1)
                continue
            self.samples += 1
            self.latest = s
            key = tuple(s[k] for k in FIELDS)
            gf = s['gf']
            if key != last_key or (gf is not None and (last_gf is None or gf - last_gf >= 30)):
                self.out.write(line(s, self.t0) + '\n')
                self.lines += 1
                last_key, last_gf = key, gf
            time.sleep(self.period)


def parse_line(text):
    """'G|w=..|gf=..|...' -> dict (numbers as int / float, 'None' as None)."""
    d = {}
    for kv in text.strip().split('|')[1:]:
        if '=' not in kv:
            continue
        k, v = kv.split('=', 1)
        if v == 'None':
            d[k] = None
            continue
        try:
            d[k] = float(v) if k == 'w' else int(v)
        except ValueError:
            d[k] = v
    return d


class FakeProc:
    """A memory image for the selftest: {address: bytes}."""

    def __init__(self, base, mem):
        self.base, self.mem = base, mem

    def read(self, addr, n):
        for a, b in self.mem.items():
            if a <= addr and addr + n <= a + len(b):
                return b[addr - a:addr - a + n]
        return None

    def u64(self, addr):
        b = self.read(addr, 8)
        return struct.unpack('<Q', b)[0] if b else None

    def u32(self, addr):
        b = self.read(addr, 4)
        return struct.unpack('<I', b)[0] if b else None

    def std_string(self, addr):
        b = self.read(addr, 0x20)
        if not b:
            return None
        size = struct.unpack_from('<Q', b, 0x10)[0]
        return b[:size].decode('ascii')


def selftest():
    base = 0x140000000
    gmm = bytearray(0x58)
    struct.pack_into('<i', gmm, 0x1C, 0)
    struct.pack_into('<Q', gmm, 0x38, 0x6000)   # RVA_TOP is GMM +0x38: the current mode
    mode = bytearray(0x30)
    struct.pack_into('<I', mode, 0x10, 1200)
    struct.pack_into('<Q', mode, MON_PTR, 0x7000)
    mon = bytearray(0x80)
    mon[0x20] = 1
    struct.pack_into('<i', mon, 0x24, 12)
    struct.pack_into('<i', mon, 0x5C, 0)
    gui = bytearray(GUI_HI - GUI_LO)
    gui[0x26B1 - GUI_LO] = 1
    gui[0x26B2 - GUI_LO] = 1
    o = GUI_MOVIE_NAME - GUI_LO
    gui[o:o + 10] = b'Light_Loop'
    struct.pack_into('<QQ', gui, o + 0x10, 10, 15)
    mem = {base + RVA_FRAME: struct.pack('<I', 1234), base + RVA_GMM: bytes(gmm), 0x6000: bytes(mode),
           0x7000: bytes(mon), base + RVA_MSGBOX: bytes([0]), base + RVA_BINK: struct.pack('<Q', 0xABC),
           base + RVA_BINKP: bytes([1]), base + RVA_ENDS: bytes([0]), base + RVA_SKIP: bytes([0]),
           base + RVA_CHG: bytes([1]), base + RVA_GUI + GUI_LO: bytes(gui)}
    s = sample(FakeProc(base, mem), cursor_fn=lambda: (100, 200))
    d = parse_line(line(s, s['w']))
    want = {'gf': 1234, 'mf': 1200, 'mt': 0, 'mon': 1, 'decl': 1, 'cd': 12, 'hold': 0, 'bink': 1, 'binkp': 1, 'msgbox': 0,
            'ends': 0, 'chg': 1, 'mov': 1, 'hint': 1, 'pop': 0, 'movie': 'Light_Loop', 'cx': 100, 'cy': 200}
    bad = {k: (d.get(k), v) for k, v in want.items() if d.get(k) != v}
    print('Q6T33 GATES SELFTEST', 'OK' if not bad else 'FAILED %s' % bad)
    return 0 if not bad else 1


if __name__ == '__main__':
    if '--selftest' in sys.argv:
        sys.exit(selftest())
    print(__doc__)
