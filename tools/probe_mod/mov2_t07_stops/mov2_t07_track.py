#!/usr/bin/env python3
"""mov2_t07_track.py - frame-exact, read-only tracker of the probe's objects in a running retail FoC (Steam x64 1.121).

The probe Lua names the object ids of each trial in a TRACK record (k, role:id;...). The runner passes the records it
finds to Tracker.feed(); the tracker looks each id up once in the current mode's object store (../common/a1mem.py
Game.objects) and then, every time the global logic frame (RVA 0xB0A320) changes, writes one line per tracked object:

  T|gf=<global frame>|mf=<mode frame>|k=<trial>|r=<role>|id=<id>|x=|y=|z=|fx=|fy=|fz=|st=<locomotor state>|v=<speed>

  x y z    obj +0x78 (position)         fx fy fz  obj +0x84 (the three floats after it: the facing the Walk code tests)
  st       obj +0xA8 -> pack +0x48 (locomotor state, MOVEMENT 3.2: 0x13 moving, 0x14 End Move, 0x15 arriving,
           0x16 stopped)                v  pack +0xEC (current speed per frame)
An object whose id at obj +0x50 no longer matches is dropped (despawned). Every read is ReadProcessMemory; the
tracker never writes, injects or suspends. A TEND record drops the trial's ids.

  mov2_t07_track.py --selftest       decoding on a fake memory image
"""
import re
import struct
import sys
import threading
import time

RVA_FRAME = 0xB0A320
OBJ_ID, OBJ_POS, OBJ_FACE, OBJ_LOCO = 0x50, 0x78, 0x84, 0xA8
LOCO_STATE, LOCO_SPEED = 0x48, 0xEC
TRACK_RE = re.compile(r'M7Q\|TRACK\|.*?\|k=(\d+)\|name=([^|]*)\|ids=([^|]*)')
TEND_RE = re.compile(r'M7Q\|TEND\|.*?\|k=(\d+)\|')


def read_obj(p, ptr):
    b = p.read(ptr + OBJ_ID, OBJ_LOCO + 8 - OBJ_ID)
    if not b:
        return None
    oid = struct.unpack_from('<i', b, 0)[0]
    x, y, z = struct.unpack_from('<fff', b, OBJ_POS - OBJ_ID)
    fx, fy, fz = struct.unpack_from('<fff', b, OBJ_FACE - OBJ_ID)
    pack = struct.unpack_from('<Q', b, OBJ_LOCO - OBJ_ID)[0]
    st = v = None
    if pack:
        pb = p.read(pack + LOCO_STATE, LOCO_SPEED + 4 - LOCO_STATE)
        if pb:
            st = struct.unpack_from('<i', pb, 0)[0]
            v = struct.unpack_from('<f', pb, LOCO_SPEED - LOCO_STATE)[0]
    return oid, x, y, z, fx, fy, fz, st, v


def fmt(gf, mf, k, role, oid, o):
    _, x, y, z, fx, fy, fz, st, v = o
    return ('T|gf=%d|mf=%s|k=%s|r=%s|id=%d|x=%.3f|y=%.3f|z=%.2f|fx=%.4f|fy=%.4f|fz=%.4f|st=%s|v=%s' % (
        gf, mf, k, role, oid, x, y, z, fx, fy, fz, '?' if st is None else hex(st),
        '?' if v is None else '%.5f' % v))


class Tracker:
    def __init__(self, proc, game, out_path, period=0.002):
        self.p, self.game = proc, game
        self.out = open(out_path, 'w')
        self.period = period
        self.want = {}        # id -> (k, role), not found yet
        self.ptr = {}         # id -> (ptr, k, role)
        self.lines = 0
        self.frames = 0
        self.lock = threading.Lock()
        self._stop = threading.Event()
        self._th = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self._th.start()
        return self

    def stop(self):
        self._stop.set()
        self._th.join(5)
        self.out.close()

    def feed(self, record):
        m = TRACK_RE.search(record)
        if m:
            k = m.group(1)
            with self.lock:
                for item in m.group(3).split(';'):
                    if ':' in item:
                        role, oid = item.split(':', 1)
                        try:
                            self.want[int(oid)] = (k, role)
                        except ValueError:
                            pass
            self.out.write('# track k=%s %s %s\n' % (k, m.group(2), m.group(3)))
            return
        m = TEND_RE.search(record)
        if m:
            k = m.group(1)
            with self.lock:
                for oid in [i for i, v in self.ptr.items() if v[1] == k]:
                    del self.ptr[oid]
                for oid in [i for i, v in self.want.items() if v[0] == k]:
                    del self.want[oid]
            self.out.write('# tend k=%s\n' % k)

    def _resolve(self):
        mode = self.game.mode()
        if not mode or not mode.get('ptr'):
            return
        objs = self.game.objects(mode['ptr']) or []
        with self.lock:
            for oid, _owner, tname, _x, _y, _z, _flags, ptr in objs:
                if oid in self.want:
                    k, role = self.want.pop(oid)
                    self.ptr[oid] = (ptr, k, role)
                    self.out.write('# found id=%d k=%s r=%s type=%s ptr=%#x\n' % (oid, k, role, tname, ptr))

    def _run(self):
        last_gf, last_resolve = None, 0.0
        while not self._stop.is_set():
            try:
                if self.want and time.perf_counter() - last_resolve > 0.25:
                    last_resolve = time.perf_counter()
                    self._resolve()
                gf = self.p.u32(self.p.base + RVA_FRAME)
                if gf is not None and gf != last_gf and self.ptr:
                    last_gf = gf
                    self.frames += 1
                    mode = self.game.mode()
                    mf = mode.get('frame') if mode else None
                    with self.lock:
                        items = list(self.ptr.items())
                    for oid, (ptr, k, role) in items:
                        o = read_obj(self.p, ptr)
                        if o is None or o[0] != oid:
                            with self.lock:
                                self.ptr.pop(oid, None)
                            self.out.write('# lost id=%d k=%s\n' % (oid, k))
                            continue
                        self.out.write(fmt(gf, mf, k, role, oid, o) + '\n')
                        self.lines += 1
                elif gf is not None:
                    last_gf = gf if not self.ptr else last_gf
            except Exception as e:  # noqa: BLE001 - logged; the tracker goes on
                self.out.write('# error %s\n' % e)
                time.sleep(0.05)
            time.sleep(self.period)


def parse_line(text):
    d = {}
    for kv in text.strip().split('|')[1:]:
        k, v = kv.split('=', 1)
        if k in ('gf', 'id'):
            d[k] = int(v)
        elif k == 'mf':
            d[k] = int(v) if v not in ('None', '?') else None
        elif k == 'st':
            d[k] = int(v, 16) if v != '?' else None
        elif k in ('k', 'r'):
            d[k] = v
        else:
            d[k] = float(v) if v != '?' else None
    return d


def selftest():
    class P:
        base = 0x140000000

        def __init__(self, mem):
            self.mem = mem

        def read(self, a, n):
            for s, b in self.mem.items():
                if s <= a and a + n <= s + len(b):
                    return b[a - s:a - s + n]
            return None
    obj = bytearray(0x60)
    struct.pack_into('<i', obj, 0, 4242)
    struct.pack_into('<fff', obj, OBJ_POS - OBJ_ID, 10.5, -3.25, 7.0)
    struct.pack_into('<fff', obj, OBJ_FACE - OBJ_ID, 0.6, 0.8, 0.0)
    struct.pack_into('<Q', obj, OBJ_LOCO - OBJ_ID, 0x9000)
    pack = bytearray(0xB0)
    struct.pack_into('<i', pack, 0, 0x14)
    struct.pack_into('<f', pack, LOCO_SPEED - LOCO_STATE, 0.5)
    p = P({0x8000 + OBJ_ID: bytes(obj), 0x9000 + LOCO_STATE: bytes(pack)})
    o = read_obj(p, 0x8000)
    d = parse_line(fmt(100, 90, '3', 'c', 4242, o))
    ok = (d['id'] == 4242 and d['x'] == 10.5 and d['y'] == -3.25 and d['st'] == 0x14 and d['v'] == 0.5 and
          d['fy'] == 0.8 and d['k'] == '3' and d['r'] == 'c' and d['gf'] == 100)
    print('MOV2 T07 TRACK SELFTEST', 'OK' if ok else 'FAILED %s' % d)
    return 0 if ok else 1


if __name__ == '__main__':
    if '--selftest' in sys.argv:
        sys.exit(selftest())
    print(__doc__)
