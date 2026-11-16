#!/usr/bin/env python3
"""q6t33_parse.py - report of q6 t33 / t35 runs: what held the galactic end screen after the elimination.

  q6t33_parse.py RUN_DIR [RUN_DIR ...] [--out FILE]

Per run folder (q6t33_run.py output) it reads gates.log (q6t33_gates.py samples), records.log (the plot's Lua records)
and shots\\shots.csv, and per campaign played gives:
  - the Lua event (TAKE / AIGIVE) frame, the Star Destroyer's planet before / after, the heartbeats' last frame;
  - the victory monitor: the gframe it was declared (countdown set), ready (hold < 1, countdown < 1) and the end screen
    shown (RVA 0xA61E08), and the frames in between;
  - every gate interval from 3 s before the declare to the end screen: message box, Bink handle, hold counter, GUI movie
    (and its name), hint text, encyclopedia pop-up, countdown skip;
  - a verdict: which gate was closed for the whole of ready -> end screen.
The last section is one table over all runs (variant, frames ready -> end, the gate, its cause).
"""
import argparse
import csv
import os
import re
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from q6t33_gates import parse_line  # noqa: E402

REC_RE = re.compile(r'Q6T\|([A-Z0-9_]+)\|(.*)\|END')
WATCH = ('msgbox', 'bink', 'binkp', 'mov', 'hint', 'pop', 'skip', 'chg', 'b79', 'g26a4', 'g2908', 'g2a34', 'movie')


def read_gates(path):
    out = []
    if not os.path.isfile(path):
        return out
    for ln in open(path, encoding='ascii', errors='replace'):
        if ln.startswith('G|'):
            out.append(parse_line(ln))
    return out


def read_records(path):
    recs = []
    if not os.path.isfile(path):
        return recs
    for ln in open(path, encoding='ascii', errors='replace'):
        m = REC_RE.search(ln)
        if not m:
            continue
        d = {'kind': m.group(1)}
        for kv in m.group(2).split('|'):
            if '=' in kv:
                k, v = kv.split('=', 1)
                d.setdefault(k, v)
        try:
            d['n'] = int(d.get('n', 0))
            d['f'] = int(d['f']) if d.get('f', '?') != '?' else None
        except ValueError:
            d['f'] = None
        recs.append(d)
    return recs


def sessions(recs):
    """Lua records grouped by plot state (sid), in record order."""
    by = {}
    for r in recs:
        by.setdefault(r.get('sid', '?'), []).append(r)
    out = []
    for sid, rs in by.items():
        rs.sort(key=lambda r: r['n'])
        out.append({'sid': sid, 'variant': rs[0].get('var', '?'), 'recs': rs})
    return out


def declares(gates):
    """Indexes of the samples where the galactic victory monitor turns declared."""
    idx, prev = [], 0
    for i, g in enumerate(gates):
        d = g.get('decl') if g.get('mt') == 0 and g.get('mon') == 1 else 0
        if d is None:   # a failed read: keep the previous state
            continue
        if d == 1 and prev != 1:
            idx.append(i)
        prev = d
    return idx


def first(gates, lo, pred, hi=None):
    for i in range(lo, len(gates) if hi is None else hi):
        if pred(gates[i]):
            return i
    return None


def intervals(gates, lo, hi, key):
    """[(value, gf_from, gf_to)] of field `key` over samples lo..hi-1 (only non-zero / non-'-' values)."""
    out, cur, start = [], None, None
    for i in range(lo, hi):
        v = gates[i].get(key)
        on = v not in (0, None, '-')
        if on and v != cur:
            if cur is not None:
                out.append((cur, start, gates[i]['gf']))
            cur, start = v, gates[i]['gf']
        elif not on and cur is not None:
            out.append((cur, start, gates[i]['gf']))
            cur = None
    if cur is not None:
        out.append((cur, start, '%s+' % gates[hi - 1]['gf']))
    return out


def held_by(gates, lo, hi):
    """Gates closed at every gframe of lo..hi-1 (the ready -> end-screen stretch). A gate counts as closed at a gframe when
    any sample of that gframe has it closed: the advisor hologram restarts its Light_Loop movie between two queued
    messages, and the poller can catch the handle at 0 for a few ms inside one gframe (the main loop did not see it)."""
    if hi <= lo:
        return []
    names = []
    for key, test in (('bink', lambda g: g.get('bink') == 1), ('msgbox', lambda g: g.get('msgbox') == 1),
                      ('hold', lambda g: (g.get('hold') or 0) > 0)):
        closed = {}
        for i in range(lo, hi):
            gf = gates[i]['gf']
            closed[gf] = closed.get(gf, False) or test(gates[i])
        if closed and all(closed.values()):
            names.append(key)
    return names


def starts(gates, lo, hi, key):
    """How often field `key` (re)starts over samples lo..hi-1: a 0 -> on change, counted once per gframe."""
    n, prev_on, last_gf = 0, None, None
    for i in range(lo, hi):
        on = gates[i].get(key) not in (0, None, '-')
        if on and prev_on is False and gates[i]['gf'] != last_gf:
            n += 1
            last_gf = gates[i]['gf']
        elif on and prev_on is None:
            n += 1
            last_gf = gates[i]['gf']
        prev_on = on
    return n


def live(g):
    """The galactic mode is still up in this sample (the monitor reads; gframe running)."""
    return g.get('mt') == 0 and g.get('mon') == 1 and g.get('gf')


def any_on(gates, lo, hi, key):
    return any(gates[i].get(key) not in (0, None, '-') for i in range(lo, hi))


def analyse_decl(gates, di, back_frames=90):
    g0 = gates[di]
    gf0 = g0['gf']
    lo = di
    while lo > 0 and gates[lo - 1]['gf'] is not None and gf0 - gates[lo - 1]['gf'] <= back_frames:
        lo -= 1
    ri = first(gates, di, lambda g: g.get('decl') == 1 and (g.get('cd') or 0) < 1 and (g.get('hold') or 0) < 1)
    # The end: the end-screen byte, or the galactic mode going away, whichever comes first. Retail sets the byte only
    # after the mode is torn down (and the end credits Bink started), when the gframe reads 0 again, so the end gframe
    # is the last gframe of the live mode and the time is the poller's wall clock.
    ei = first(gates, di, lambda g: g.get('ends') == 1 or not live(g))
    li = None if ei is None else (ei if live(gates[ei]) else ei - 1)
    while li is not None and li > di and not live(gates[li]):
        li -= 1
    stop = ei if ei is not None else min(len(gates), di + 20000)
    hi = min(len(gates), (ei + 1) if ei is not None else stop)
    a = {'decl_gf': gf0, 'decl_mf': g0.get('mf'), 'decl_cd': g0.get('cd'), 'decl_hold': g0.get('hold'),
         'ready_gf': gates[ri]['gf'] if ri is not None else None,
         'end_gf': gates[li]['gf'] if li is not None else None,
         'end_how': None if ei is None else ('end-screen byte' if gates[ei].get('ends') == 1 else 'galactic mode left'),
         'ends_w': next((g['w'] for g in gates[di:] if g.get('ends') == 1), None),
         'decl_w': g0.get('w'), 'ready_w': gates[ri].get('w') if ri is not None else None,
         'end_w': gates[ei].get('w') if ei is not None else None,
         'cursor': (g0.get('cx'), g0.get('cy'))}
    a['decl_to_ready'] = (a['ready_gf'] - gf0) if a['ready_gf'] is not None else None
    a['ready_to_end'] = (a['end_gf'] - a['ready_gf']) if a['end_gf'] is not None and a['ready_gf'] is not None else None
    a['decl_to_end'] = (a['end_gf'] - gf0) if a['end_gf'] is not None else None
    a['ready_to_end_s'] = (a['end_w'] - a['ready_w']) if a['end_w'] is not None and a['ready_w'] is not None else None
    a['advisor_msgs'] = starts(gates, di, ei if ei is not None else hi, 'hint')
    a['iv'] = {k: intervals(gates, lo, hi, k) for k in WATCH}
    a['iv']['hold'] = intervals(gates, lo, hi, 'hold')
    if ri is not None and ei is not None:
        # up to (not including) the end gframe: at that gframe every gate opened and the main loop moved on
        ej = first(gates, ri, lambda g: g['gf'] == a['end_gf'], li + 1)
        a['held'] = held_by(gates, ri, ej if ej is not None else li + 1)
        a['hint_in_wait'] = any_on(gates, ri, ei, 'hint')
        a['pop_in_wait'] = any_on(gates, ri, ei, 'pop')
        movies = sorted({gates[i].get('movie') for i in range(ri, ei)} - {'-', None})
        a['movies_in_wait'] = movies
    else:
        a['held'] = None
    a['verdict'] = verdict(a)
    return a


def verdict(a):
    if a['end_gf'] is None:
        return 'NO END SCREEN seen in the gate log (quit early, or the end flag address is wrong)'
    if a['ready_gf'] is None:
        return 'monitor never ready (hold / countdown), end screen anyway: check the monitor pointer'
    w = a['ready_to_end']
    if w <= 2:
        return 'NOT HELD: end screen %d gframe(s) after ready (the 30-frame countdown only)' % w
    held = a['held'] or []
    cause = []
    if 'bink' in held:
        src = []
        if a.get('hint_in_wait'):
            src.append('advisor hint text shown')
        if a.get('pop_in_wait'):
            src.append('encyclopedia pop-up shown')
        cause.append('Bink open (GUI movie %s; %s; %s advisor message(s) from declare to end)' % (
            '/'.join(a.get('movies_in_wait') or ['?']), ', '.join(src) or 'no hint / pop-up flag', a.get('advisor_msgs')))
    if 'msgbox' in held:
        cause.append('message box up')
    if 'hold' in held:
        cause.append('victory monitor hold counter > 0')
    if not cause:
        return 'HELD %d gframes by NO single gate seen (gates changed during the wait, or another check): see intervals' % w
    return 'HELD %d gframes after ready: %s' % (w, ' + '.join(cause))


def lua_summary(s):
    rs = s['recs']
    ev = next((r for r in rs if r['kind'] in ('TAKE', 'AIGIVE')), None)
    start = next((r for r in rs if r['kind'] == 'START'), None)
    hbs = [r for r in rs if r['kind'] == 'HB']
    out = {'variant': s['variant'], 'sid': s['sid'], 'event': ev, 'start': start,
           'last_hb_f': hbs[-1]['f'] if hbs else None, 'done': any(r['kind'] == 'DONE' for r in rs)}
    if ev:
        after = [r for r in hbs if r['f'] is not None and ev['f'] is not None and r['f'] >= ev['f']]
        out['sd_after'] = [(r['f'] - ev['f'], r.get('sd'), r.get('sdp'), r.get('gm')) for r in after]
        out['hb_after'] = len(after)
    return out


def report_run(run_dir):
    gates = read_gates(os.path.join(run_dir, 'gates.log'))
    recs = read_records(os.path.join(run_dir, 'records.log'))
    ses = [lua_summary(s) for s in sessions(recs)]
    decl = [analyse_decl(gates, i) for i in declares(gates)]
    shots = os.path.join(run_dir, 'shots', 'shots.csv')
    nshots = sum(1 for _ in csv.DictReader(open(shots))) if os.path.isfile(shots) else 0
    lines = ['=' * 100, 'RUN %s' % run_dir,
             '  gate lines %d, Lua records %d, plot sessions %d, declares %d, shots %d' % (
                 len(gates), len(recs), len(ses), len(decl), nshots)]
    rows = []
    used = set()
    for s in ses:
        ev = s['event']
        lines.append('-' * 100)
        lines.append('CAMPAIGN %s (sid %s)%s' % (s['variant'], s['sid'], ', DONE: ran 120 s, never ended' if s['done']
                                                  else ''))
        if s['start']:
            lines.append('  START %s sd=%s gm=%s msg=%s' % (s['start'].get('owned'), s['start'].get('sd'),
                                                          s['start'].get('gm'), s['start'].get('msg')))
        if not ev:
            lines.append('  no event record (quit before the event?)')
            continue
        lines.append('  EVENT %s at f=%s (Lua mode frame), %s; sd=%s sdp=%s' % (ev['kind'], ev['f'], ev.get('ok'),
                                                                             ev.get('sd'), ev.get('sdp')))
        lines.append('  heartbeats after the event: %d, last at f=%s (%s frames after it)' % (
            s.get('hb_after', 0), s['last_hb_f'], (s['last_hb_f'] - ev['f']) if s['last_hb_f'] and ev['f'] else '?'))
        moves = []
        last = None
        for df, sd, sdp, gm in s.get('sd_after', []):
            if (sd, sdp, gm) != last:
                moves.append('+%d: sd=%s sdp=%s gm=%s' % (df, sd, sdp, gm))
                last = (sd, sdp, gm)
        if moves:
            lines.append('  Star Destroyer / game mode after the event: ' + '; '.join(moves))
        # the declare that belongs to this event: the first whose mode frame is within 10 of the event
        a = None
        for k, d in enumerate(decl):
            if k not in used and d['decl_mf'] is not None and ev['f'] is not None and abs(d['decl_mf'] - ev['f']) <= 10:
                a = d
                used.add(k)
                break
        if a is None:
            lines.append('  NO DECLARE matched in gates.log (monitor pointer wrong, or the poller was not running)')
            rows.append((s['variant'], ev['f'], None, None, None, 'no declare in gate log'))
            continue
        lines.append('  monitor declared gf=%s (mf=%s, countdown %s, hold %s); ready gf=%s (+%s); end screen gf=%s '
                     '(+%s after ready, +%s after declare)' % (
                         a['decl_gf'], a['decl_mf'], a['decl_cd'], a['decl_hold'], a['ready_gf'], a['decl_to_ready'],
                         a['end_gf'], a['ready_to_end'], a['decl_to_end']))
        lines.append('  end by %s; ready -> end %s s wall clock; end-screen byte set at w=%s (declare w=%s); '
                     'advisor messages (hint starts) from declare to end: %s' % (
                         a['end_how'], None if a['ready_to_end_s'] is None else round(a['ready_to_end_s'], 2),
                         a['ends_w'], a['decl_w'], a['advisor_msgs']))
        lines.append('  cursor at declare: %s' % (a['cursor'],))
        for k in ('bink', 'msgbox', 'hold', 'mov', 'movie', 'hint', 'pop', 'binkp', 'skip', 'chg', 'b79', 'g26a4',
                  'g2908', 'g2a34'):
            iv = a['iv'].get(k)
            if iv:
                lines.append('    %-6s %s' % (k, ', '.join('%s gf %s..%s' % (v, x, y) for v, x, y in iv)))
        lines.append('  VERDICT: ' + a['verdict'])
        rows.append((s['variant'], ev['f'], a['decl_to_ready'], a['ready_to_end'], a['decl_to_end'], a['verdict']))
    for k, d in enumerate(decl):
        if k not in used:
            lines.append('-' * 100)
            lines.append('DECLARE without a plot session: gf=%s mf=%s -> %s' % (d['decl_gf'], d['decl_mf'], d['verdict']))
    return '\n'.join(lines), rows


def report_runs(run_dirs, out=None):
    parts, table = [], []
    for r in run_dirs:
        text, rows = report_run(r)
        parts.append(text)
        table += rows
    parts.append('=' * 100)
    parts.append('SUMMARY (gframes; the port shows the end screen 30 frames after the declare)')
    parts.append('%-12s %7s %8s %8s %8s  %s' % ('variant', 'ev f', 'decl>rdy', 'rdy>end', 'decl>end', 'verdict'))
    for v, f, dr, re_, de, ver in table:
        parts.append('%-12s %7s %8s %8s %8s  %s' % (v, f, dr, re_, de, ver))
    text = '\n'.join(parts) + '\n'
    if out:
        with open(out, 'w') as fh:
            fh.write(text)
    return text


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('runs', nargs='+')
    ap.add_argument('--out')
    a = ap.parse_args()
    print(report_runs(a.runs, a.out))


if __name__ == '__main__':
    main()
