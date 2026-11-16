#!/usr/bin/env python3
"""goalpass_dbg.py - per-candidate trace of the galactic AI goal passes in retail FoC (roadmap gai3 t06; BKG1 B.8).

  goalpass_dbg.py --out-dir DIR [--label NAME]      # run mode: DIR\\<stamp>-goalpass[-NAME]\\, DIR\\latest.txt, parse
  goalpass_dbg.py --out LOG                         # log only
      [--pid N | --wait-process-sec 1800] [--arm-now (default) | --arm-file PATH] [--stop-file PATH]
      [--stop-galactic-frame 700] [--max-hits 3000000] [--allow-unknown-exe] [--non-interactive]
  goalpass_dbg.py --cleanup --out-dir DIR           # stop a leftover debugger of this kit (stop file), nothing else

A variant of ../cu8h_probes/cu8h_dbg.py (same attach, arm-file and stop handling; its Win32 pieces are imported from
there). Instead of the LCG write watchpoint it sets three hardware EXECUTE breakpoints (DR7 R/W = 00, LEN = 00) in
every thread, new threads too, at (RVAs relative to the real image base; docs/re/BKG1_RE_ANSWERS.md B.3 / B.8):
  Dr0  0x6cb511  targeted evaluation  `inc [rdi+0x188]`  rdi goal system, r15 goal type, r12 function, rsi target
  Dr1  0x6cb2ae  global evaluation    `inc [rdi+0x188]`  rdi, r15, r12
  Dr2  0x6cad26  Commit (pass end)    `call 0x6c8710`    rdi; [rdi+0x188] = E of the pass, [rdi+0x178] = its quota
The registers were re-read with dumpbin /disasm of the retail exe (2026-11-15): r14 = the (goal type, function) pair,
r15 = [r14], r12 = [r14+8], rsi = target list [rdi+0x180] entry. The bytes at each site are checked before arming.
An execute breakpoint fires before the instruction runs, so E on an evaluation hit is the value before its increment.
On every hit the Resume Flag (EFLAGS bit 16) is set in the thread's context, so the instruction runs once and the
breakpoint does not refire. The game's memory is never written; only debug registers and RF are set.

Log lines (key=value, strings %-escaped): GOALPASS 1, ATTACH, EXE, SITE, ARM / DISARM, GSYS, PLAYER, GTYPE, FUNC,
PLANET, HIT, LATE_HIT, STOP, DETACH, EXIT. HIT fields: n k(T targeted / G global / C commit) tid gf (global frame
0x140b0a320) mt (GMM mode type, 0 galactic) mode mf (current mode's frame) gs (rdi) own (player id
[[[rdi+0x28]+0x28]+0x28]+0x4C [M]) pl (the three chain pointers) q src tgt e (+0x178 / +0x17C / +0x180 / +0x188),
and for T / G gt (r15) fn (r12), for T tl (rsi) po (planet object [rsi+0x28]) pi (index byte po+0x335 [M]).
Names: GTYPE name = the MSVC std::string at goal type +0x00 (AIGoalType ctor 0x6b7c70 assigns the XML name there;
[H] read of the ctor, [M] that it is the Name attribute); PLANET name = std::string at (po+0x298 type)+0xF8 (the
object-type name, a1mem TYPE_NAME, validated by the Q2 runs). Each first sight also logs `cands=` (every std::string
or pointer to ASCII in the first 0x200 bytes) and a hex dump, so a wrong offset can be fixed offline.

Stopping: the current mode is galactic (mt 0) at mode frame >= --stop-galactic-frame (checked on every hit and
polled every 0.1 s); --stop-file appears; --max-hits; the game exits. On a stop Dr0-Dr7 are cleared in every
thread inside a debug event, then DebugActiveProcessStop; DebugSetProcessKillOnExit(FALSE) keeps the game alive.
Offline check: selftest_goalpass.py (a fake image with the same instruction bytes at the same RVAs).
"""
import argparse
import ctypes
import datetime
import os
import struct
import sys
import time
import urllib.parse

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)       # -I drops the script folder from sys.path
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'cu8h_probes'))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'a1_tactical'))
import cu8h_dbg as D  # noqa: E402  (Win32 pieces: k32, Ctx, Log, kv, constants, Tracer base)
import a1mem  # noqa: E402  (find_pids, exe identity, FACTIONS)

RVA_TARGETED, RVA_GLOBAL, RVA_COMMIT = 0x6CB511, 0x6CB2AE, 0x6CAD26
SITES = (('T', RVA_TARGETED, bytes.fromhex('ff8788010000')),     # inc dword [rdi+0x188]
         ('G', RVA_GLOBAL, bytes.fromhex('ff8788010000')),
         ('C', RVA_COMMIT, bytes.fromhex('e8e5d9ffff')))         # call 0x6c8710
GS_QUOTA, GS_E = 0x178, 0x188
GS_OWNER_CHAIN = (0x28, 0x28, 0x28)
P_INDEX, P_ID, P_HUMAN, P_FACTION = 0x48, 0x4C, 0x62, 0x68
TL_PLANET, OBJ_TYPE, TYPE_NAME, PLANET_INDEX = 0x28, 0x298, 0xF8, 0x335
GT_NAME, GT_FLAGS, GT_CATEGORY, GT_REACH = 0x00, 0x20, 0x28, 0x2C
DUMP = 0x200
FALLBACK_IMAGE = 0xCC0000

EFLAGS_RF = 1 << 16
OFF_EFLAGS = 0x44
OFF_DR = (0x48, 0x50, 0x58, 0x60)
OFF_RSI, OFF_RDI, OFF_R12, OFF_R14, OFF_R15 = 0xA8, 0xB0, 0xD8, 0xE8, 0xF0
CONTEXT_CONTROL = 0x100001
DR6_HITS = 0x7
D.k32.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
D.k32.WaitForSingleObject.restype = ctypes.c_uint32


def dr7_bits(slots, rw=0, length=0):
    """DR7 value enabling the local breakpoints `slots` with R/W `rw` and LEN `length` (execute: 0, 0)."""
    v = 0
    for i in slots:
        v |= 1 << (2 * i)
        v |= ((rw & 3) | ((length & 3) << 2)) << (16 + 4 * i)
    return v


DR7_EXEC = dr7_bits(range(len(SITES)))          # 0x15
DR7_SLOT_MASK = 0
for _i in range(len(SITES)):
    DR7_SLOT_MASK |= (0x3 << (2 * _i)) | (0xF << (16 + 4 * _i))


def with_rf(eflags):
    return eflags | EFLAGS_RF


def esc(s):
    return urllib.parse.quote(s, safe='!"#$&\'()*+-./:<>?@[]^_`{|}~')


def unesc(s):
    return urllib.parse.unquote(s)


def printable(b):
    return len(b) > 0 and all(0x20 <= c < 0x7F for c in b)


def std_string_from(buf, off, read):
    """MSVC std::string at buf[off:off+0x20]: inline text when capacity < 16, else a pointer. None when not one."""
    if off + 0x20 > len(buf):
        return None
    size, cap = struct.unpack_from('<QQ', buf, off + 0x10)
    if not (0 < size <= 128 and size <= cap) or cap > 1 << 20:
        return None
    if cap < 16:
        raw = buf[off:off + size]
    else:
        raw = read(struct.unpack_from('<Q', buf, off)[0], size)
    if not raw or not printable(raw):
        return None
    return raw.decode('ascii')


def c_string_at(read, addr, n=64):
    if not addr or addr < 0x10000 or addr > 0x7FFFFFFFFFFF:
        return None
    raw = read(addr, n)
    if not raw:
        return None
    raw = raw.split(b'\0', 1)[0]
    return raw.decode('ascii') if len(raw) >= 3 and printable(raw) else None


def name_candidates(buf, read, limit=16):
    """[(kind, offset, text)]: std::strings ('s') and pointers to NUL-terminated ASCII ('p') in buf."""
    out = []
    for off in range(0, len(buf) - 7, 8):
        s = std_string_from(buf, off, read)
        if s is not None:
            out.append(('s', off, s))
        else:
            q = struct.unpack_from('<Q', buf, off)[0]
            t = c_string_at(read, q)
            if t is not None:
                out.append(('p', off, t))
        if len(out) >= limit:
            break
    return out


def fmt_cands(c):
    return ';'.join('%s@%x:%s' % (k, o, esc(t)) for k, o, t in c) or '-'


class GoalTracer(D.Tracer):
    def __init__(self, a, log):
        super().__init__(a, log)
        self.site_addr = {}            # kind -> absolute address
        self.addr_kind = {}
        self.seen_gs, self.seen_pl, self.seen_gt, self.seen_fn, self.seen_po = set(), set(), set(), set(), set()
        self.hits_kind = {'T': 0, 'G': 0, 'C': 0}
        self.late = 0
        self.last_mode_poll = 0.0
        self.refused = None

    def attach(self):
        """cu8h_dbg's attach, retried for 15 s: launch_foc.py holds the new game as its debugger for a moment."""
        t0 = time.time()
        while True:
            try:
                return super().attach()
            except OSError as e:
                if self.hp:
                    D.k32.CloseHandle(self.hp)
                    self.hp = None
                if time.time() - t0 > 15:
                    raise
                self.log('ATTACH_RETRY ' + D.kv(err=str(e).replace(' ', '_')))
                time.sleep(0.5)

    # memory helpers
    def u8(self, addr):
        b = self.read(addr, 1)
        return b[0] if b else None

    def i32(self, addr):
        b = self.read(addr, 4)
        return struct.unpack('<i', b)[0] if b else None

    def rd(self, addr, n):
        return self.read(addr, n) if addr and addr > 0xFFFF else None

    def hexptr(self, x):
        return '%x' % (x or 0)

    # exe checks, on attach
    def check_image(self):
        lfanew = self.u32(self.base + 0x3C)
        pe = lfanew is not None and lfanew < 0x1000 and self.u32(self.base + lfanew) == 0x4550
        ts = self.u32(self.base + lfanew + 8) if pe else None
        soi = self.u32(self.base + lfanew + 24 + 56) if pe else None
        self.image_end = self.base + (soi or FALLBACK_IMAGE)
        known = ts == a1mem.FOC_TIMESTAMP and soi == a1mem.FOC_SIZE_OF_IMAGE
        self.log('EXE ' + D.kv(base='%x' % self.base, pe=int(pe), timestamp='%x' % (ts or 0),
                               size_of_image='%x' % (soi or 0), known=int(known)))
        sites_ok = True
        for kind, rva, want in SITES:
            addr = self.base + rva
            got = self.read(addr, len(want))
            ok = got == want
            sites_ok &= ok
            self.site_addr[kind] = addr
            self.addr_kind[addr] = kind
            self.log('SITE ' + D.kv(k=kind, rva='%x' % rva, addr='%x' % addr, bytes=(got or b'').hex() or '-',
                                    ok=int(ok)))
        if not self.a.allow_unknown_exe:
            if not known:
                self.refused = 'unknown_exe'
            elif not sites_ok:
                self.refused = 'site_bytes'
        elif not sites_ok:
            self.refused = 'site_bytes'       # wrong bytes = wrong addresses: never arm there

    # debug registers
    def set_dr(self, h, on):
        c = self.ctx2
        if not c.get(h, D.CONTEXT_DEBUG_REGISTERS):
            return False
        c.setflags(D.CONTEXT_DEBUG_REGISTERS)
        if on:
            for i, (kind, _rva, _b) in enumerate(SITES):
                c.setq(OFF_DR[i], self.site_addr[kind])
            c.setq(D.OFF_DR6, 0)
            c.setq(D.OFF_DR7, (c.q(D.OFF_DR7) & ~DR7_SLOT_MASK) | DR7_EXEC)
        else:
            for off in OFF_DR:
                c.setq(off, 0)
            c.setq(D.OFF_DR6, 0)
            c.setq(D.OFF_DR7, 0)
        return c.set(h)

    def apply(self, on, why):
        """Call only inside a debug event (all threads frozen)."""
        ok = bad = 0
        for _tid, h in list(self.threads.items()):
            if self.set_dr(h, on):
                ok += 1
            else:
                bad += 1
        self.armed = on
        m = self.mode()
        self.log('%s %s' % ('ARM' if on else 'DISARM', D.kv(why=why, threads=ok, failed=bad, t='%.3f' % time.time(),
                                                              mt=m[0], mf=m[3], gf=m[4])))

    def resume_past(self, h):
        """Set RF and clear Dr6 in the hit thread (self.ctx holds its full context)."""
        c = self.ctx
        ctypes.c_uint32.from_address(c.addr + OFF_EFLAGS).value = with_rf(
            ctypes.c_uint32.from_address(c.addr + OFF_EFLAGS).value)
        c.setq(D.OFF_DR6, 0)
        c.setflags(CONTEXT_CONTROL | D.CONTEXT_DEBUG_REGISTERS)
        if not c.set(h):
            self.log('RF_FAIL ' + D.kv(err=ctypes.get_last_error()))

    # first-sight records
    def note_player(self, p):
        if not p or p in self.seen_pl:
            return
        self.seen_pl.add(p)
        fac = self.u64(p + P_FACTION)
        fname = None
        if fac:
            b = self.rd(fac, 0x400)
            if b:
                for off in range(0, 0x400 - 0x1F, 8):
                    s = std_string_from(b, off, self.read)
                    if s and s.upper() in a1mem.FACTIONS:
                        fname = s.upper()
                        break
        self.log('PLAYER ' + D.kv(ptr='%x' % p, id=self.i32(p + P_ID), index=self.i32(p + P_INDEX),
                                  human=self.u8(p + P_HUMAN), faction='%x' % (fac or 0), fname=fname or '-'))

    def note_object(self, tag, ptr, extra, name):
        b = None
        for n in (DUMP, 0x100, 0x80, 0x40):        # an object near the end of a mapped region: dump less
            b = self.rd(ptr, n)
            if b:
                break
        cands = name_candidates(b, self.read) if b else []
        self.log(tag + ' ' + D.kv(ptr='%x' % ptr, name=esc(name) if name else '-', **extra,
                                  cands=fmt_cands(cands), dump=b.hex() if b else '-'))

    def note_gt(self, gt):
        if not gt or gt in self.seen_gt:
            return
        self.seen_gt.add(gt)
        b = self.rd(gt, 0x40)
        name = std_string_from(b, GT_NAME, self.read) if b else None
        extra = {}
        if b:
            extra = dict(flags='%x' % struct.unpack_from('<I', b, GT_FLAGS)[0],
                         category=struct.unpack_from('<i', b, GT_CATEGORY)[0],
                         reach=struct.unpack_from('<i', b, GT_REACH)[0])
        self.note_object('GTYPE', gt, extra, name)

    def note_fn(self, fn):
        if not fn or fn in self.seen_fn:
            return
        self.seen_fn.add(fn)
        self.note_object('FUNC', fn, {}, None)

    def note_po(self, po, pi):
        if not po or po in self.seen_po:
            return
        self.seen_po.add(po)
        ty = self.u64(po + OBJ_TYPE)
        tb = self.rd(ty, TYPE_NAME + 0x20) if ty else None
        name = std_string_from(tb, TYPE_NAME, self.read) if tb else None
        self.note_object('PLANET', po, dict(index=pi, type='%x' % (ty or 0)), name)

    # a hit
    def on_goal_hit(self, tid, h, kind):
        c = self.ctx
        rip = c.q(D.OFF_RIP)
        gs = c.q(OFF_RDI)
        self.hits += 1
        self.hits_kind[kind] += 1
        mt, _sk, cur, mf, gf = self.mode()
        hdr = self.rd(gs + GS_QUOTA, 0x18)
        q = src = tgt = e = None
        if hdr:
            q, src, tgt, _flag, e = struct.unpack_from('<iiiIi', hdr, 0)
        chain, p = [], gs
        for off in GS_OWNER_CHAIN:
            p = self.u64(p + off) if p else None
            chain.append(p or 0)
        own = self.i32(chain[-1] + P_ID) if chain[-1] else None
        if gs not in self.seen_gs:
            self.seen_gs.add(gs)
            self.log('GSYS ' + D.kv(gs='%x' % gs, chain='/'.join('%x' % x for x in chain), own=own, mt=mt, mf=mf))
        self.note_player(chain[-1])
        f = dict(n=self.hits, k=kind, tid=tid, rip=self.rva(rip), gf=gf, mt=mt, mode='%x' % (cur or 0), mf=mf,
                 gs='%x' % gs, own=own, pl='/'.join('%x' % x for x in chain), q=q, src=src, tgt=tgt, e=e)
        if kind in ('T', 'G'):
            gt, fn = c.q(OFF_R15), c.q(OFF_R12)
            self.note_gt(gt)
            self.note_fn(fn)
            f.update(gt='%x' % gt, fn='%x' % fn)
            if kind == 'T':
                tl = c.q(OFF_RSI)
                po = self.u64(tl + TL_PLANET) if tl else None
                pi = self.u8(po + PLANET_INDEX) if po else None
                self.note_po(po, pi)
                f.update(tl='%x' % tl, po='%x' % (po or 0), pi=pi)
        self.log('HIT ' + D.kv(**f))
        self.resume_past(h)
        if self.a.max_hits and self.hits >= self.a.max_hits:
            self.stop_why = self.stop_why or 'max_hits'
        self.check_stop_frame(mt, mf)

    def check_stop_frame(self, mt, mf):
        n = self.a.stop_galactic_frame
        if n and mt == 0 and mf is not None and mf >= n:
            self.stop_why = self.stop_why or 'galactic_frame_%d' % mf

    def poll_controls(self):
        super().poll_controls()
        now = time.time()
        if self.base is not None and self.armed and now - self.last_mode_poll >= 0.1:
            self.last_mode_poll = now
            m = self.mode()
            self.check_stop_frame(m[0], m[3])

    # main loop (cu8h_dbg's, with the stop taken inside whatever event is current)
    def run(self):
        self.attach()
        detached = exited = False
        try:
            while True:
                self.poll_controls()
                need = self.stop_why is not None or (self.want_armed != self.armed and self.base is not None)
                if need and self.base is not None:
                    self.request_break()
                if not D.k32.WaitForDebugEvent(self.ev, 100):
                    continue
                code, pid, tid = struct.unpack_from('<III', self.ev.raw, 0)
                u = 16
                cont = D.DBG_CONTINUE
                if code == D.CREATE_PROCESS_DEBUG_EVENT:
                    hfile, _hp, hthread, base = struct.unpack_from('<QQQQ', self.ev.raw, u)
                    if hfile:
                        D.k32.CloseHandle(hfile)
                    self.base = self.a.image_base or base
                    self.threads[tid] = hthread
                    self.log('ATTACH ' + D.kv(pid=self.pid, base='%x' % self.base, process_base='%x' % base))
                    self.check_image()
                    if self.refused:
                        self.stop_why = 'refused_' + self.refused
                    elif self.a.arm_now:
                        self.want_armed = True
                elif code == D.CREATE_THREAD_DEBUG_EVENT:
                    hthread = struct.unpack_from('<Q', self.ev.raw, u)[0]
                    self.threads[tid] = hthread
                    if self.armed:
                        self.set_dr(hthread, True)
                elif code == D.EXIT_THREAD_DEBUG_EVENT:
                    self.threads.pop(tid, None)
                elif code == D.LOAD_DLL_DEBUG_EVENT:
                    hfile = struct.unpack_from('<Q', self.ev.raw, u)[0]
                    if hfile:
                        D.k32.CloseHandle(hfile)
                elif code == D.EXIT_PROCESS_DEBUG_EVENT:
                    self.log('EXIT ' + D.kv(code=struct.unpack_from('<I', self.ev.raw, u)[0], hits=self.hits))
                    D.k32.ContinueDebugEvent(pid, tid, D.DBG_CONTINUE)
                    exited = True
                    break
                elif code == D.EXCEPTION_DEBUG_EVENT:
                    exc = struct.unpack_from('<I', self.ev.raw, u)[0]
                    if exc == D.EXCEPTION_SINGLE_STEP:
                        cont = self.on_single_step(tid)
                    elif exc in (D.EXCEPTION_BREAKPOINT, D.STATUS_WX86_BREAKPOINT):
                        self.break_pending = False
                        if self.stop_why is None and self.want_armed != self.armed:
                            self.apply(self.want_armed, 'arm_file' if self.a.arm_file else 'arm_now')
                    else:
                        cont = D.DBG_EXCEPTION_NOT_HANDLED
                if self.stop_why is not None:
                    # every thread is frozen during a debug event: clear the breakpoints, continue, detach
                    if self.armed:
                        self.apply(False, 'stop')
                    self.log('STOP ' + D.kv(why=self.stop_why, hits=self.hits, T=self.hits_kind['T'],
                                            G=self.hits_kind['G'], C=self.hits_kind['C'], late=self.late))
                    D.k32.ContinueDebugEvent(pid, tid, cont)
                    if self.drain(0.5):
                        exited = True
                    else:
                        detached = self.detach()
                    break
                D.k32.ContinueDebugEvent(pid, tid, cont)
        finally:
            if not detached and not exited:
                self.emergency_stop()
            if self.hp:
                D.k32.CloseHandle(self.hp)
        return self.hits

    def on_single_step(self, tid):
        h = self.threads.get(tid)
        if h is None:
            h = D.k32.OpenThread(D.THREAD_ACCESS, False, tid)
            self.threads[tid] = h
        c = self.ctx
        if not c.get(h, D.CONTEXT_ALL):
            self.log('CTX_FAIL ' + D.kv(tid=tid, err=ctypes.get_last_error()))
            return D.DBG_EXCEPTION_NOT_HANDLED
        kind = self.addr_kind.get(c.q(D.OFF_RIP))
        if not (c.q(D.OFF_DR6) & DR6_HITS) or kind is None:
            return D.DBG_EXCEPTION_NOT_HANDLED       # not ours (the game's own single step)
        if self.armed:
            self.on_goal_hit(tid, h, kind)
        else:
            # a hit taken in the same moment as a disarm: never hand it to the game
            self.late += 1
            self.log('LATE_HIT ' + D.kv(tid=tid, k=kind))
            self.resume_past(h)
        return D.DBG_CONTINUE

    def drain(self, seconds):
        """After the disarm: continue queued events; an execute hit taken by another thread at the same moment is
        continued here (its breakpoint is cleared, so the instruction just runs). True when the game exited."""
        t0 = time.time()
        while time.time() - t0 < seconds:
            if not D.k32.WaitForDebugEvent(self.ev, 50):
                continue
            code, pid, tid = struct.unpack_from('<III', self.ev.raw, 0)
            cont = D.DBG_CONTINUE
            if code == D.EXIT_PROCESS_DEBUG_EVENT:
                D.k32.ContinueDebugEvent(pid, tid, D.DBG_CONTINUE)
                self.log('EXIT ' + D.kv(hits=self.hits, during='drain'))
                return True
            if code in (D.LOAD_DLL_DEBUG_EVENT, D.CREATE_PROCESS_DEBUG_EVENT):
                hfile = struct.unpack_from('<Q', self.ev.raw, 16)[0]
                if hfile:
                    D.k32.CloseHandle(hfile)
            if code == D.CREATE_THREAD_DEBUG_EVENT:
                self.threads[tid] = struct.unpack_from('<Q', self.ev.raw, 16)[0]
            if code == D.EXCEPTION_DEBUG_EVENT:
                exc = struct.unpack_from('<I', self.ev.raw, 16)[0]
                if exc == D.EXCEPTION_SINGLE_STEP:
                    h = self.threads.get(tid)
                    if h is not None and self.ctx.get(h, D.CONTEXT_ALL) and self.addr_kind.get(self.ctx.q(D.OFF_RIP)):
                        self.late += 1
                        self.log('LATE_HIT ' + D.kv(tid=tid, during='drain'))
                        self.set_dr(h, False)
                    else:
                        cont = D.DBG_EXCEPTION_NOT_HANDLED
                elif exc not in (D.EXCEPTION_BREAKPOINT, D.STATUS_WX86_BREAKPOINT):
                    cont = D.DBG_EXCEPTION_NOT_HANDLED
            D.k32.ContinueDebugEvent(pid, tid, cont)
        return False


# ---- process / run folder ------------------------------------------------------------------------------------------

def find_game(a, log):
    t0 = time.time()
    while time.time() - t0 < a.wait_process_sec:
        if a.stop_file and os.path.exists(a.stop_file):
            log('NO_GAME ' + D.kv(why='stop_file'))
            return None
        for pid in a1mem.find_pids():
            try:
                p = a1mem.Proc(pid)
            except OSError:
                continue
            ok = p.pe_identity()
            ts, soi = p.timestamp, p.size_of_image
            p.close()
            if ok or a.allow_unknown_exe:
                return pid
            log('IGNORED ' + D.kv(pid=pid, timestamp='%x' % (ts or 0), size_of_image='%x' % (soi or 0)))
        time.sleep(1.0)
    return None


def remove(path):
    if path and os.path.exists(path):
        os.remove(path)


def pid_alive(pid):
    h = D.k32.OpenProcess(0x00100000 | 0x1000, False, pid)     # SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION
    if not h:
        return False
    try:
        return D.k32.WaitForSingleObject(h, 0) == 0x102
    finally:
        D.k32.CloseHandle(h)


def cleanup(out_dir):
    pidf = os.path.join(out_dir, 'running.pid')
    stop = os.path.join(out_dir, 'stop')
    print('gai3 goalpass cleanup: no mod is installed by this kit; nothing in the game folder to remove.')
    pid = None
    if os.path.exists(pidf):
        try:
            pid = int(open(pidf).read().split()[0])
        except (ValueError, IndexError):
            pid = None
    if not pid or not pid_alive(pid):
        print('no goalpass debugger is running.')
        remove(pidf)
        remove(stop)
        return 0
    open(stop, 'w').close()
    print('goalpass debugger pid %d: stop file written, waiting for it to clear its breakpoints and detach ...' % pid)
    t0 = time.time()
    while time.time() - t0 < 30 and pid_alive(pid):
        time.sleep(0.2)
    alive = pid_alive(pid)
    print('debugger stopped.' if not alive else 'debugger still running after 30 s (pid %d): stop it by hand' % pid)
    if not alive:
        remove(stop)
    return 1 if alive else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', help='log file (log-only mode)')
    ap.add_argument('--out-dir', help='run mode: make <dir>\\<stamp>-goalpass[-label]\\ and parse at the end')
    ap.add_argument('--label', default='')
    ap.add_argument('--pid', type=int)
    ap.add_argument('--wait-process-sec', type=float, default=1800)
    ap.add_argument('--arm-file')
    ap.add_argument('--arm-now', action='store_true', help='arm at once (the default without --arm-file)')
    ap.add_argument('--stop-file')
    ap.add_argument('--stop-galactic-frame', type=int, default=700)
    ap.add_argument('--max-hits', type=int, default=3000000)
    ap.add_argument('--allow-unknown-exe', action='store_true')
    ap.add_argument('--non-interactive', action='store_true', help='accepted for run_goalpass.cmd; no prompts anyway')
    ap.add_argument('--cleanup', action='store_true')
    ap.add_argument('--image-base', type=lambda s: int(s, 0), default=0, help=argparse.SUPPRESS)  # selftest fake
    a = ap.parse_args()
    a.no_gmm = False
    if a.cleanup:
        if not a.out_dir:
            ap.error('--cleanup needs --out-dir')
        return cleanup(a.out_dir)
    if not a.out and not a.out_dir:
        ap.error('give --out LOG or --out-dir DIR')
    if not a.arm_file:
        a.arm_now = True
    run = None
    pidf = None
    if a.out_dir:
        os.makedirs(a.out_dir, exist_ok=True)
        stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
        run = os.path.join(a.out_dir, stamp + '-goalpass' + ('-' + a.label if a.label else ''))
        os.makedirs(run)
        a.out = os.path.join(run, 'trace.log')
        a.stop_file = os.path.abspath(a.stop_file or os.path.join(a.out_dir, 'stop'))
        remove(a.stop_file)
        if a.arm_file:
            remove(a.arm_file)
        pidf = os.path.join(a.out_dir, 'running.pid')
        with open(pidf, 'w') as f:
            f.write('%d\n' % os.getpid())
        with open(os.path.join(a.out_dir, 'latest.txt'), 'w') as f:
            f.write('run=%s\nlog=%s\nstop=%s\narm=%s\n' % (run, a.out, a.stop_file, a.arm_file or '-'))
        print('gai3 goalpass: run folder', run, flush=True)
        print('stop file: %s (create it to stop early); stops by itself at galactic mode frame %d' %
              (a.stop_file, a.stop_galactic_frame), flush=True)
    log = D.Log(a.out)
    log('GOALPASS 1 ' + D.kv(t=time.strftime('%Y-%m-%dT%H:%M:%S'), sites='%x,%x,%x' % (RVA_TARGETED, RVA_GLOBAL,
                                                                                     RVA_COMMIT),
                             stop_galactic_frame=a.stop_galactic_frame, arm='file' if a.arm_file else 'now'))
    rc = 0
    try:
        if a.pid is None:
            print('waiting up to %d s for StarWarsG.exe ...' % a.wait_process_sec, flush=True)
            a.pid = find_game(a, log)
            if a.pid is None:
                log('NO_GAME')
                print('no StarWarsG.exe found')
                rc = 2
        if a.pid is not None:
            print('attaching to pid %d' % a.pid, flush=True)
            tr = GoalTracer(a, log)
            try:
                hits = tr.run()
            except OSError as e:
                log('ATTACH_FAIL ' + D.kv(err=str(e).replace(' ', '_')))
                print('attach failed:', e, flush=True)
                rc = 4
            else:
                print('goalpass_dbg: %d hits (T %d, G %d, C %d), stop: %s' % (
                    hits, tr.hits_kind['T'], tr.hits_kind['G'], tr.hits_kind['C'], tr.stop_why or 'game exit'),
                    flush=True)
                if tr.refused:
                    rc = 3
    finally:
        log.close()
        remove(pidf)
    if run:
        import goalpass_parse
        try:
            prc = goalpass_parse.parse_to(a.out, run)
        except Exception as e:  # noqa: BLE001
            print('parse failed:', e)
            prc = 20
        if prc == 0:
            for line in open(os.path.join(run, 'report.txt')):
                if line.startswith(('stop:', 'hits:', 'passes:', 'E check')):
                    print(line.rstrip())
            print('report:', os.path.join(run, 'report.txt'))
        rc = rc or prc
        remove(a.stop_file)
        print('GOALPASS_DONE exit=%d run=%s' % (rc, run), flush=True)
    return rc


if __name__ == '__main__':
    sys.exit(main())
