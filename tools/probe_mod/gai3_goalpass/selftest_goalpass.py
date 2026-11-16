#!/usr/bin/env python3
"""selftest_goalpass.py - offline check of the gai3 goalpass kit (no game is started, nothing is installed).

  selftest_goalpass.py [--keep]

1. Encodings: DR7 for three execute breakpoints (0x15) and cu8h_dbg's write watchpoint, the Resume Flag, the
   %-escaping of names, the std::string / pointer name finder on a synthetic buffer.
2. Parser on a synthetic log: per-owner passes, E check, seeded first pass, names, open pass, ignored menu hits.
3. Live: a fake "game" (a Python child) allocates a 0xCC0000-byte RWX block as its image, writes the retail
   instruction bytes at the retail RVAs (inc [rdi+0x188] at 0x6cb511 / 0x6cb2ae, call 0x6c8710 at 0x6cad26), a GMM
   and frame counter at their RVAs, two goal systems with owner chains, goal types, functions and planets, and then
   runs passes through that code. goalpass_dbg.py attaches with real hardware execute breakpoints. Checks: every
   call is logged once in order (RF works: no refire, the instruction runs once, so E at each Commit equals the hits
   of the pass), names resolve, the stop at the galactic mode frame detaches and the fake survives (exit 7), and a
   second run ends with the fake's exit.
Ends with "GOALPASS SELFTEST OK (<n> checks)", or lists the failures (exit 1).
"""
import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import goalpass_dbg as G  # noqa: E402
import goalpass_parse as P  # noqa: E402

PY = getattr(sys, '_base_executable', sys.executable)


class Checker:
    def __init__(self):
        self.n = 0
        self.fails = []

    def __call__(self, cond, what):
        self.n += 1
        if not cond:
            self.fails.append(what)
        return cond


# ---- 1. encodings ------------------------------------------------------------------------------------------------------

def check_encodings(ck):
    ck(G.DR7_EXEC == 0x15, 'DR7 exec for slots 0-2 = 0x15, got %x' % G.DR7_EXEC)
    ck(G.dr7_bits([0], rw=1, length=3) == G.D.DR7_WATCH, 'dr7_bits(write, 4 bytes) != cu8h DR7_WATCH')
    ck(G.DR7_SLOT_MASK == 0xFFF003F, 'DR7 slot mask %x' % G.DR7_SLOT_MASK)
    merged = (0xFFFFFFFF & ~G.DR7_SLOT_MASK) | G.DR7_EXEC      # slots 0-2 replaced, slot 3 (bits 6-7, 28-31) kept
    ck(merged == 0xF000FFD5, 'DR7 merge keeps slot 3, got %x' % merged)
    ck(G.with_rf(0x246) == 0x10246 and G.with_rf(0x10246) == 0x10246, 'RF bit 16')
    ck(G.OFF_EFLAGS == 0x44 and G.OFF_DR == (0x48, 0x50, 0x58, 0x60) and G.D.OFF_DR6 == 0x68 and G.D.OFF_DR7 == 0x70,
       'CONTEXT offsets (EFlags / Dr0-3 / Dr6 / Dr7)')
    ck(G.OFF_RSI == 0xA8 and G.OFF_RDI == 0xB0 and G.OFF_R12 == 0xD8 and G.OFF_R15 == 0xF0 and G.D.OFF_RIP == 0xF8,
       'CONTEXT register offsets')
    for s in ('Galactic_Conquer_Opponent', 'a b=c,d;e%f:g', 'Naboo'):
        e = G.esc(s)
        ck(' ' not in e and '=' not in e and ',' not in e and ';' not in e and G.unesc(e) == s, 'escape %r' % s)
    mem = {0x500000: b'Heap_Long_Goal_Type_Name\0', 0x600000: b'Func_Name\0junk'}

    def read(addr, n):
        for base, data in mem.items():
            if base <= addr < base + len(data):
                return (data[addr - base:] + b'\0' * n)[:n]
        return None
    buf = bytearray(0x80)
    struct.pack_into('<QQQQ', buf, 0x00, 0x500000, 0, 24, 31)                      # long std::string -> pointer
    buf[0x20:0x25] = b'Naboo'
    struct.pack_into('<QQ', buf, 0x30, 5, 15)                                      # inline std::string
    struct.pack_into('<Q', buf, 0x48, 0x600000)                                    # pointer to a C string
    struct.pack_into('<Q', buf, 0x50, 0x12345)                                     # pointer to nothing
    c = G.name_candidates(bytes(buf), read)
    ck(c[:3] == [('s', 0, 'Heap_Long_Goal_Type_Name'), ('s', 0x20, 'Naboo'), ('p', 0x48, 'Func_Name')],
       'name candidates %r' % c[:4])
    ck(G.std_string_from(bytes(buf), 0x20, read) == 'Naboo' and G.std_string_from(bytes(buf), 0x48, read) is None,
       'std_string_from')


# ---- 2. parser on a synthetic log --------------------------------------------------------------------------------------

def synthetic_log(path):
    L = ['GOALPASS 1 t=selftest sites=6cb511,6cb2ae,6cad26 stop_galactic_frame=700 arm=now',
         'ATTACH pid=1 base=140000000 process_base=140000000',
         'EXE base=140000000 pe=1 timestamp=67002393 size_of_image=cc0000 known=1',
         'SITE k=T rva=6cb511 addr=1406cb511 bytes=ff8788010000 ok=1',
         'ARM why=arm_now threads=5 failed=0 t=0 mt=1 mf=10 gf=10',
         'PLAYER ptr=a0 id=3 index=2 human=0 faction=f0 fname=UNDERWORLD',
         'PLAYER ptr=b0 id=0 index=0 human=0 faction=f1 fname=REBEL',
         'GTYPE ptr=1000 name=Galactic_Conquer_Opponent flags=2 category=1 reach=3 cands=- dump=-',
         'GTYPE ptr=2000 name=- flags=10 category=4 reach=4 cands=s@40:Global%20x;p@48:Build_Tech_Global dump=-',
         'FUNC ptr=f00 name=- cands=- dump=-',
         'PLANET ptr=3000 name=Naboo index=7 type=e0 cands=- dump=-',
         'PLANET ptr=3100 name=- index=9 type=0 cands=- dump=-']
    n = [0]

    def hit(k, gs, own, mf, mt=0, **f):
        n[0] += 1
        d = dict(n=n[0], k=k, tid=1, rip='6cb511', gf=1000 + mf, mt=mt, mode='m', mf=mf, gs=gs, own=own,
                 pl='1/2/a0', q=3, src=0, tgt=0, e=0)
        d.update(f)
        L.append('HIT ' + ' '.join('%s=%s' % kv for kv in d.items()))
    hit('T', 'aa', 9, 5, mt=1, gt='1000', fn='f00', tl='1', po='3000', pi=7)         # main-menu hit: ignored
    hit('T', 'g1', 3, 10, gt='1000', fn='f00', tl='1', po='3000', pi=7, e=500)       # pass 1 (seeded): E 502 vs 2
    hit('G', 'g1', 3, 10, gt='2000', fn='f00', src=1, e=501)
    hit('C', 'g1', 3, 11, e=502, q=3)
    hit('T', 'g2', 0, 11, gt='1000', fn='f00', tl='1', po='3100', pi=9, e=40)
    hit('C', 'g2', 0, 11, e=41, q=3)
    for i in range(4):                                                               # pass 2: E 4 == 4 seen
        hit('T', 'g1', 3, 12 + i, gt='1000', fn='f00', tl='1', po='3100', pi=9, e=i, tgt=i)
    hit('C', 'g1', 3, 16, e=4, q=3)
    hit('G', 'g1', 3, 17, gt='2000', fn='f00', src=1, e=0)                           # pass 3 open at the stop
    L.append('STOP why=galactic_frame_700 hits=%d T=6 G=2 C=3 late=0' % n[0])
    L.append('DETACH ok=1 err=0')
    with open(path, 'w', newline='\n') as f:
        f.write('\n'.join(L) + '\n')


def check_parser(ck, tmp):
    log = os.path.join(tmp, 'synthetic.log')
    synthetic_log(log)
    out = os.path.join(tmp, 'synthetic_out')
    ck(P.parse_to(log, out) == 0, 'parse_to rc')
    rep = open(os.path.join(out, 'report.txt')).read()
    passes = [ln.split('\t') for ln in open(os.path.join(out, 'passes.tsv')).read().splitlines()]
    evals = [ln.split('\t') for ln in open(os.path.join(out, 'evals.tsv')).read().splitlines()]
    hp = passes[0]
    rows = [dict(zip(hp, r)) for r in passes[1:]]
    ck(len(rows) == 3, 'synthetic: 3 committed passes, got %d' % len(rows))
    if len(rows) == 3:
        ck(rows[0]['owner'] == '3' and rows[0]['faction'] == 'UNDERWORLD' and rows[0]['E'] == '502' and
           rows[0]['evals_seen'] == '2' and rows[0]['delta'] == '500' and rows[0]['note'] == 'first',
           'synthetic pass 1 (seeded) %r' % rows[0])
        ck(rows[1]['owner'] == '0' and rows[1]['faction'] == 'REBEL' and rows[1]['note'] == 'first',
           'synthetic g2 pass 1 %r' % rows[1])
        ck(rows[2]['pass'] == '2' and rows[2]['E'] == '4' and rows[2]['evals_seen'] == '4' and rows[2]['delta'] == '0'
           and rows[2]['quota'] == '3' and rows[2]['note'] == 'ok' and rows[2]['targeted'] == '4',
           'synthetic pass 2 %r' % rows[2])
    he = evals[0]
    er = [dict(zip(he, r)) for r in evals[1:]]
    ck(len(er) == 8, 'synthetic: 8 galactic evals, got %d' % len(er))
    if er:
        ck(er[0]['goal_type'] == 'Galactic_Conquer_Opponent' and er[0]['planet'] == 'Naboo', 'eval names %r' % er[0])
        ck(er[1]['goal_type'] == 'Build_Tech_Global' and er[1]['kind'] == 'G' and er[1]['planet'] == '',
           'name from cands (skips the non-identifier one) %r' % er[1])
        ck(er[3]['planet'] == 'idx:9', 'planet without a name falls back to its index %r' % er[3])
        ck(er[-1]['pass'] == '3(open)', 'open pass row %r' % er[-1])
    ck('E check (E logged by Commit == evaluations seen, every pass after the first): OK (1 passes, 0 mismatches)'
       in rep, 'report E check line')
    ck('mt=1 T:1' in rep, 'report counts the ignored menu hit')
    ck('open pass: owner 3 UNDERWORLD gs=g1 pass 3: 1 evals' in rep, 'report open pass')
    ck('own=3 src=1 -> 2000 (Build_Tech_Global)' in rep, 'report source order')


# ---- 3. live: fake image with the retail instruction bytes at the retail RVAs -----------------------------------------

FAKE = r'''
import ctypes, json, os, struct, sys, time
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
k32.VirtualAlloc.restype = ctypes.c_void_p
k32.VirtualAlloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint32, ctypes.c_uint32]
k32.FlushInstructionCache.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
k32.GetCurrentProcess.restype = ctypes.c_void_p
SIZE = 0xCC0000
base = k32.VirtualAlloc(None, SIZE, 0x3000, 0x40)        # MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE
assert base
def put(rva, data):
    ctypes.memmove(base + rva, bytes(data), len(data))
def q(rva, v): put(rva, struct.pack('<Q', v))
def i(rva, v): put(rva, struct.pack('<i', v))
def geti(rva): return struct.unpack('<i', ctypes.string_at(base + rva, 4))[0]
PRO = bytes.fromhex('57 56 4154 4157 4889cf 4989d7 4d89c4 4c89ce')   # push rdi rsi r12 r15; rdi=rcx r15=rdx r12=r8 rsi=r9
EPI = bytes.fromhex('415f 415c 5e 5f c3')
def fn(start, site, site_bytes, after=b''):
    code = PRO + b'\x90' * (site - start - len(PRO)) + site_bytes + after + EPI
    put(start, code)
    return ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p)(base + start)
INC = bytes.fromhex('ff8788010000')
T = fn(0x6cb4f0, 0x6cb511, INC)
Gl = fn(0x6cb290, 0x6cb2ae, INC)
put(0x6c8710, b'\xc3')
rel = 0x6c8710 - (0x6cad26 + 5)
C = fn(0x6cad00, 0x6cad26, b'\xe8' + struct.pack('<i', rel), bytes.fromhex('c78788010000 00000000'))
k32.FlushInstructionCache(k32.GetCurrentProcess(), ctypes.c_void_p(base), SIZE)
# data: GMM (mode type 0 galactic, current mode), global frame, objects in a data area
GMM, FS, MODE = 0xB153E0, 0xB0A320, 0xC00000
i(GMM + 0x1C, 0); q(GMM + 0x38, base + MODE); i(GMM + 0x50, 1)
nxt = [0xA00000]
def alloc(n):
    r = nxt[0]; nxt[0] += (n + 0xFF) & ~0xFF; return r
def std_string(rva, s):
    b = s.encode()
    if len(b) < 16:
        put(rva, b + b'\0'); q(rva + 0x10, len(b)); q(rva + 0x18, 15)
    else:
        h = alloc(len(b) + 1); put(h, b + b'\0')
        q(rva, base + h); q(rva + 0x10, len(b)); q(rva + 0x18, (len(b) | 15))
def player(pid, index, fac):
    f = alloc(0x200); std_string(f + 0x30, fac)
    p = alloc(0x200); i(p + 0x48, index); i(p + 0x4C, pid); put(p + 0x62, b'\0'); q(p + 0x68, base + f)
    return p
def goal_system(p, seed, quota):
    gs = alloc(0x200); a = alloc(0x40); b = alloc(0x40)
    q(gs + 0x28, base + a); q(a + 0x28, base + b); q(b + 0x28, base + p)
    i(gs + 0x178, quota); i(gs + 0x188, seed)
    return gs
def goal_type(name, flags):
    g = alloc(0x200); std_string(g, name); i(g + 0x20, flags); i(g + 0x2C, 3); return g
def func(name):
    f = alloc(0x200); s = alloc(0x40); put(s, name.encode() + b'\0'); q(f + 0x18, base + s); return f
def planet(name, idx):
    ty = alloc(0x200); std_string(ty + 0xF8, name)
    po = alloc(0x400); q(po + 0x298, base + ty); put(po + 0x335, bytes([idx]))
    tl = alloc(0x40); q(tl + 0x28, base + po)
    return tl
GS = [goal_system(player(3, 2, 'UNDERWORLD'), 100, 3), goal_system(player(0, 0, 'REBEL'), 0, 2)]
GT = [goal_type('Galactic_Conquer_Opponent', 2), goal_type('Build_Tech', 0x10)]
FN = [func('Conquer_Function'), func('Tech_Function')]
PL = [planet('Naboo', 7), planet('Kessel', 12)]
open(sys.argv[1], 'w').write('%x\n' % base)
while not os.path.exists(sys.argv[2]):
    time.sleep(0.01)
frames = int(sys.argv[3])
events = []
for mf in range(1, frames + 1):
    i(MODE + 0x10, mf); i(FS, 5000 + mf)
    for g, gs in enumerate(GS):
        if (mf + g) % 4 == 0:
            events.append(['C', g, None, None, mf, geti(gs + 0x188)])
            C(base + gs, None, None, None)
            continue
        for k in range(1 + (mf + g) % 2):
            t = (mf + k) % 2
            i(gs + 0x180, k)
            if t == 0:
                events.append(['T', g, 0, (mf + k + g) % 2, mf, geti(gs + 0x188)])
                T(base + gs, base + GT[0], base + FN[0], base + PL[(mf + k + g) % 2])
            else:
                events.append(['G', g, 1, None, mf, geti(gs + 0x188)])
                Gl(base + gs, base + GT[1], base + FN[1], None)
    time.sleep(0.003)
json.dump(events, open(sys.argv[2] + '.events', 'w'))
time.sleep(float(sys.argv[4]))
sys.exit(7)
'''


def run_live(ck, tmp, label, frames, stop_frame, linger):
    fake = os.path.join(tmp, 'fake_game.py')
    with open(fake, 'w', newline='\n') as f:
        f.write(FAKE)
    base_f, go_f = os.path.join(tmp, label + '.base'), os.path.join(tmp, label + '.go')
    p = subprocess.Popen([PY, '-I', fake, base_f, go_f, str(frames), str(linger)])
    t0 = time.time()
    while not os.path.exists(base_f) and time.time() - t0 < 30:
        time.sleep(0.02)
    time.sleep(0.1)
    base = int(open(base_f).read().strip(), 16)
    log = os.path.join(tmp, label + '.log')
    stop = os.path.join(tmp, label + '.stop')
    dbg = subprocess.Popen([PY, '-I', os.path.join(HERE, 'goalpass_dbg.py'), '--out', log, '--pid', str(p.pid),
                            '--image-base', hex(base), '--allow-unknown-exe', '--stop-galactic-frame', str(stop_frame),
                            '--stop-file', stop, '--max-hits', '20000'], stdout=subprocess.DEVNULL)

    def go_when_armed():
        t1 = time.time()
        while time.time() - t1 < 30:
            if os.path.exists(log) and 'ARM ' in open(log).read():
                break
            time.sleep(0.05)
        open(go_f, 'w').write('go')
    threading.Thread(target=go_when_armed, daemon=True).start()
    watchdog = threading.Timer(90, lambda: open(stop, 'w').close())   # a refiring breakpoint would never end
    watchdog.start()
    try:
        drc = dbg.wait(120)
        rc = p.wait(60)
    finally:
        watchdog.cancel()
    lines = open(log).read().splitlines()
    events = json.load(open(go_f + '.events')) if os.path.exists(go_f + '.events') else []
    hits = [P.parse_kv(ln.partition(' ')[2]) for ln in lines if ln.startswith('HIT ')]
    ck(drc == 0, '%s: debugger exit code %s' % (label, drc))
    ck(rc == 7, '%s: fake exit code %s (want 7: the target must survive the debugger)' % (label, rc))
    ck(not any(ln.startswith(('LATE_HIT', 'RF_FAIL', 'CTX_FAIL', 'EMERGENCY')) for ln in lines),
       '%s: no LATE_HIT / RF_FAIL / CTX_FAIL lines' % label)
    ck(all(('ok=1' in ln) for ln in lines if ln.startswith('SITE ')) and
       sum(ln.startswith('SITE ') for ln in lines) == 3, '%s: three site byte checks ok' % label)
    if stop_frame:
        want = []
        for e in events:
            want.append(e)
            if e[4] >= stop_frame:
                break
    else:
        want = events
    gs_of = {}
    for h in hits:
        gs_of.setdefault(h['own'], h['gs'])
    got = [[h['k'], 0 if h['own'] == '3' else 1, h.get('gt'), h.get('po'), int(h['mf']), int(h['e'])] for h in hits]
    if stop_frame and len(got) == len(want) - 1:
        want = want[:-1]        # the 0.1 s mode poll may stop it just before the first hit at the stop frame
    ok_len = ck(len(got) == len(want), '%s: %d hits logged, %d calls made up to the stop (refire or miss)' %
                (label, len(got), len(want)))
    if ok_len:
        ck([g[0] for g in got] == [w[0] for w in want] and [g[1] for g in got] == [w[1] for w in want] and
           [g[4] for g in got] == [w[4] for w in want], '%s: hit kinds / owners / mode frames in call order' % label)
        ck([g[5] for g in got] == [w[5] for w in want], '%s: E before each hit (the inc ran exactly once)' % label)
    names = {}
    for ln in lines:
        if ln.startswith(('GTYPE ', 'PLANET ', 'FUNC ', 'PLAYER ')):
            d = P.parse_kv(ln.partition(' ')[2])
            names[(ln.split()[0], d.get('ptr'))] = d
    gtn = sorted(P.pick_name(d)[0] or '-' for (t, _p), d in names.items() if t == 'GTYPE')
    pln = sorted('%s/%s' % (P.pick_name(d)[0], d.get('index')) for (t, _p), d in names.items() if t == 'PLANET')
    fnn = sorted(P.pick_name(d)[0] or '-' for (t, _p), d in names.items() if t == 'FUNC')
    fac = sorted('%s:%s' % (d.get('id'), d.get('fname')) for (t, _p), d in names.items() if t == 'PLAYER')
    ck(gtn == ['Build_Tech', 'Galactic_Conquer_Opponent'], '%s: goal type names %r' % (label, gtn))
    ck(pln == ['Kessel/12', 'Naboo/7'], '%s: planet names / index bytes %r' % (label, pln))
    ck(fnn == ['Conquer_Function', 'Tech_Function'], '%s: function names from cands %r' % (label, fnn))
    ck(fac == ['0:REBEL', '3:UNDERWORLD'], '%s: players / factions %r' % (label, fac))
    end = [ln for ln in lines if ln.startswith(('STOP ', 'EXIT '))]
    if stop_frame:
        ck(any(ln.startswith('STOP why=galactic_frame_%d' % stop_frame) for ln in end), '%s: STOP at the galactic '
           'frame %r' % (label, end))
        ck(any(ln.startswith('DETACH ok=1') for ln in lines), '%s: DETACH ok=1' % label)
        ck(any(ln.startswith('DISARM why=stop') and 'failed=0' in ln for ln in lines), '%s: disarm in all threads'
           % label)
    else:
        ck(any(ln.startswith('EXIT ') for ln in end), '%s: EXIT logged %r' % (label, end))
    out = os.path.join(tmp, label + '_out')
    ck(P.parse_to(log, out) == 0, '%s: parse' % label)
    rep = open(os.path.join(out, 'report.txt')).read()
    ck('E check (E logged by Commit == evaluations seen, every pass after the first): OK' in rep and
       'MISMATCH' not in open(os.path.join(out, 'passes.tsv')).read(), '%s: parser E check OK' % label)
    rows = open(os.path.join(out, 'passes.tsv')).read().splitlines()
    first_uw = [r for r in rows[1:] if r.split('\t')[0] == '3'][:1]
    ck(bool(first_uw) and first_uw[0].split('\t')[11] == '100', '%s: the seeded first pass shows delta 100 %r' %
       (label, first_uw))
    print('live %s: %d hits, %d calls, rc=%s' % (label, len(got), len(events), rc))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--keep', action='store_true')
    a = ap.parse_args()
    ck = Checker()
    tmp = tempfile.mkdtemp(prefix='goalpass_selftest_')
    try:
        check_encodings(ck)
        check_parser(ck, tmp)
        run_live(ck, tmp, 'stop', frames=60, stop_frame=30, linger=2.0)
        run_live(ck, tmp, 'exit', frames=24, stop_frame=0, linger=0.3)
    finally:
        if a.keep:
            print('kept', tmp)
        else:
            shutil.rmtree(tmp, ignore_errors=True)
    for f in ck.fails:
        print('FAIL', f)
    if ck.fails:
        print('GOALPASS SELFTEST FAILED (%d of %d checks)' % (len(ck.fails), ck.n))
        return 1
    print('GOALPASS SELFTEST OK (%d checks)' % ck.n)
    return 0


if __name__ == '__main__':
    sys.exit(main())
