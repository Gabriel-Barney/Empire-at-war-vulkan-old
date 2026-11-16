#!/usr/bin/env python3
"""mov2_t07_analyse.py - report of a mov2 t07 run: stop distances, re-order braking and MovingVsMoving holds, frame
exact from track.log (mov2_t07_track.py), keyed by the probe's records (records.log) and config.json.

  mov2_t07_analyse.py RUN_DIR [--out FILE]

Frames are global logic frames (gf). The Lua records carry g = NOW (GetCurrentTime.Frame() x 30), the same clock; the
report prints, per order, the frames from g to the mover's first state / speed change as a check.

STOP (move 'stop'): cruise speed, the brake start (last frame at cruise before the speed falls to 0), frames and
  distance from the brake start to rest, frames in End Move (state 0x14) and 0x15, where it rests against its goal
  (along the move / to the side), for squads also when the last member came to rest after the container.
REORDER (moves 'same', 'turn90', 'reverse'): from the re-order frame: speed then, lowest speed before the new path is
  walked, frames to that low, distance run on along the old heading, frames in 0x14, whether the state left 0x13 at
  all (MOVEMENT 3.5.2 G: an aligned new path keeps state and speed; one not aligned coasts in End Move at the
  deceleration, then starts from 0x16), frames to rest.
MVM: per tank the holds (runs of >= 3 frames at speed 0 while more than 20 from its goal), the longest hold, the
  states seen in it, arrival frame, the closest approach of the two.
"""
import argparse
import json
import math
import os
import re
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from mov2_t07_track import parse_line  # noqa: E402

REC_RE = re.compile(r'M7Q\|([A-Z0-9_]+)\|(.*)\|END')
STATES = {0x0: 'start', 0xf: 'turn', 0x10: 'turn', 0x11: 'turn', 0x12: 'startmove', 0x13: 'move', 0x14: 'endmove',
          0x15: 'arrive', 0x16: 'stopped'}


def read_records(path):
    out = []
    for ln in open(path, encoding='ascii', errors='replace') if os.path.isfile(path) else []:
        m = REC_RE.search(ln)
        if not m:
            continue
        d = {'rk': m.group(1)}
        for kv in m.group(2).split('|'):
            if '=' in kv:
                k, v = kv.split('=', 1)
                d.setdefault(k, v)
        for k in ('n', 'f', 'g'):
            try:
                d[k] = int(d[k])
            except (KeyError, ValueError):
                d[k] = None
        out.append(d)
    out.sort(key=lambda r: r['n'] or 0)
    return out


def read_track(path):
    by = {}
    for ln in open(path, encoding='ascii', errors='replace') if os.path.isfile(path) else []:
        if ln.startswith('T|'):
            d = parse_line(ln)
            by.setdefault((d['k'], d['r'], d['id']), []).append(d)
    for v in by.values():
        v.sort(key=lambda d: d['gf'])
    return by


class Frame:
    def __init__(self, cfg):
        sq = cfg['sq']
        self.sx, self.sy = sq['site']['x'], sq['site']['y']
        self.ux, self.uy, self.vx, self.vy = sq['ux'], sq['uy'], sq['vx'], sq['vy']

    def local(self, x, y):
        dx, dy = x - self.sx, y - self.sy
        return dx * self.ux + dy * self.uy, dx * self.vx + dy * self.vy


def uv(s):
    return tuple(float(x) for x in s.split(',')) if s and s != '?' else None


def window(rows, g0, g1):
    return [r for r in rows if g0 <= r['gf'] <= g1]


def path_len(rows):
    return sum(math.hypot(b['x'] - a['x'], b['y'] - a['y']) for a, b in zip(rows, rows[1:]))


def st_count(rows, st):
    return sum(1 for r in rows if r['st'] == st)


def states_seq(rows):
    seq = []
    for r in rows:
        name = STATES.get(r['st'], hex(r['st']) if r['st'] is not None else '?')
        if not seq or seq[-1][0] != name:
            seq.append([name, r['gf'], r['gf']])
        else:
            seq[-1][2] = r['gf']
    return seq


def seq_text(seq, g0):
    return ' '.join('%s+%d..%d' % (n, a - g0, b - g0) for n, a, b in seq)


def first_change(rows, g0):
    """Frames from g0 to the first frame whose state or speed differs from the frame at / before g0."""
    base = None
    for r in rows:
        if r['gf'] <= g0:
            base = r
        elif base is not None and (r['st'] != base['st'] or abs((r['v'] or 0) - (base['v'] or 0)) > 1e-6):
            return r['gf'] - g0
    return None


def rest_frame(rows, g0, eps=1e-4):
    """First frame after g0 from which the speed stays 0 to the end of `rows`."""
    last_moving = None
    for r in rows:
        if r['gf'] >= g0 and (r['v'] or 0) > eps:
            last_moving = r['gf']
    if last_moving is None:
        return g0
    nxt = [r['gf'] for r in rows if r['gf'] > last_moving]
    return nxt[0] if nxt else None


def stop_stats(rows, g0, g_end, F, goal, start):
    w = window(rows, g0, g_end)
    if len(w) < 3:
        return None
    vmax = max(r['v'] or 0 for r in w)
    rest = rest_frame(w, g0)
    peaks = [i for i, r in enumerate(w) if (r['v'] or 0) >= vmax - 1e-5 and (rest is None or r['gf'] < rest)]
    if vmax <= 1e-4 or not peaks:
        return None   # no motion in the tracked frames (the tracker found the ids after the unit had stopped)
    i_peak = max(peaks)
    brake = w[i_peak]['gf']
    br = window(w, brake, rest if rest is not None else g_end)
    end = br[-1]
    u, v = F.local(end['x'], end['y'])
    du, dv = goal[0] - start[0], goal[1] - start[1]
    L = math.hypot(du, dv) or 1.0
    along = ((u - goal[0]) * du + (v - goal[1]) * dv) / L
    side = (-(u - goal[0]) * dv + (v - goal[1]) * du) / L
    return {'vmax': vmax, 'brake_gf': brake, 'rest_gf': rest, 'brake_frames': (rest - brake) if rest else None,
            'brake_dist': path_len(br), 'endmove': st_count(br, 0x14), 'arrive': st_count(br, 0x15),
'past_goal': along, 'side': side, 'seq': seq_text(states_seq(w), g0),
            'first_change': first_change(rows, g0)}


def reorder_stats(rows, gr, g_end, F, old_dir):
    w = window(rows, gr - 1, g_end)
    if len(w) < 3:
        return None
    v_at = w[0]['v'] or 0
    rest = rest_frame(w, gr)
    seq = states_seq(window(w, gr, g_end))
    left_move = any(n != 'move' for n, a, b in seq if b < (rest or g_end))
    # the low point: the lowest speed before the speed rises again (the new path walked)
    lo_i, lo_v = 0, v_at
    for i, r in enumerate(w):
        if (r['v'] or 0) < lo_v - 1e-6:
            lo_i, lo_v = i, (r['v'] or 0)
        elif (r['v'] or 0) > lo_v + 0.05:
            break
    lo = w[lo_i]
    run_on = 0.0
    if old_dir is not None:
        for a, b in zip(w[:lo_i], w[1:lo_i + 1]):
            run_on += (b['x'] - a['x']) * old_dir[0] + (b['y'] - a['y']) * old_dir[1]
    return {'v_at': v_at, 'v_low': lo_v, 'to_low': lo['gf'] - gr, 'run_on': run_on,
            'endmove': st_count(window(w, gr, lo['gf'] + 1), 0x14), 'left_move': left_move,
            'rest_after': (rest - gr) if rest else None, 'seq': seq_text(seq, gr), 'first_change': first_change(rows, gr)}


def holds(rows, g0, goal, F, min_len=3, eps=1e-4, near=20.0):
    out, cur = [], None
    for r in rows:
        if r['gf'] < g0:
            continue
        u, v = F.local(r['x'], r['y'])
        far = math.hypot(u - goal[0], v - goal[1]) > near
        if (r['v'] or 0) <= eps and far:
            if cur is None:
                cur = [r['gf'], r['gf'], set()]
            cur[1] = r['gf']
            cur[2].add(STATES.get(r['st'], hex(r['st']) if r['st'] is not None else '?'))
        elif cur is not None:
            out.append(cur)
            cur = None
    if cur is not None:
        out.append(cur)
    return [(a - g0, b - a + 1, '/'.join(sorted(s))) for a, b, s in out if b - a + 1 >= min_len]


def arrival(rows, g0, goal, F, near=20.0):
    for r in rows:
        if r['gf'] >= g0:
            u, v = F.local(r['x'], r['y'])
            if math.hypot(u - goal[0], v - goal[1]) <= near and (r['v'] or 0) <= 1e-4:
                return r['gf'] - g0
    return None


def report(run_dir, out=None):
    cfg = json.load(open(os.path.join(run_dir, 'config.json')))
    F = Frame(cfg)
    recs = read_records(os.path.join(run_dir, 'records.log'))
    track = read_track(os.path.join(run_dir, 'track.log'))
    lines = ['mov2 t07 run %s' % run_dir, 'corridor: %s' % cfg.get('site_note'),
             'records %d, tracked objects %d, track lines %d' % (len(recs), len(track), sum(len(v) for v in track.values()))]
    states = [r['rk'] for r in recs if r['rk'] in ('ARM', 'SKIP', 'DONE', 'ERR', 'APIERR')]
    lines.append('script: %s' % (' '.join(states) or 'no ARM / DONE record'))
    trials = {}
    for r in recs:
        k = r.get('k')
        if k is not None:
            trials.setdefault(k, []).append(r)
    summary = []
    for k in sorted(trials, key=lambda x: int(x)):
        rs = trials[k]
        tr = next((r for r in rs if r['rk'] == 'TRIAL'), {})
        name, kind = tr.get('name', '?'), tr.get('kind', '?')
        lines.append('-' * 100)
        lines.append('TRIAL %s %s (%s)' % (k, name, kind))
        objs = {key: v for key, v in track.items() if key[0] == k}
        if not objs:
            lines.append('  no track lines (the tracker did not find the ids: see track.log "# found" / "# lost")')
        mover = [v for key, v in objs.items() if key[1] in ('c', 'u')]
        mover = mover[0] if mover else []
        members = [v for key, v in objs.items() if key[1] == 'm']
        if kind in ('squad', 'unit'):
            orders = [r for r in rs if r['rk'] == 'ORDER']
            reords = {r.get('mi'): r for r in rs if r['rk'] == 'REORD'}
            for i, o in enumerate(orders):
                g0 = o['g']
                g_end = orders[i + 1]['g'] - 1 if i + 1 < len(orders) else (mover[-1]['gf'] if mover else g0)
                mv = o.get('move')
                start, goal = uv(o.get('from')), uv(o.get('to'))
                if g0 is None or not mover or start is None:
                    lines.append('  %s: no NOW frame or no track' % mv)
                    continue
                lag = mover[0]['gf'] - g0
                if lag > 0 and mover[0]['gf'] <= g_end:
                    lines.append('  %s: the track starts %d frames after the order (tracker found the ids late)' % (mv, lag))
                elif mover[0]['gf'] > g_end:
                    lines.append('  %s: not tracked (the track starts %d frames after the order, after this move)' % (
                        mv, lag))
                    continue
                if mv == 'stop':
                    s = stop_stats(mover, g0, g_end, F, goal, start)
                    if s is None:
                        lines.append('  stop: too few track lines, or no motion in them (tracked too late)')
                        continue
                    mem = ''
                    if members and s['rest_gf']:
                        last = max((rest_frame(window(m, g0, g_end), g0) or g_end) for m in members)
                        mem = '; last member at rest %+d frames after the container' % (last - s['rest_gf'])
                    lines.append('  STOP order g=%s (first change +%s): cruise %.4f/frame; brake from gf %s: %s frames, '
                                 '%.2f units to rest (End Move %d, 0x15 %d frames); rests %+.2f along past the goal, '
                                 '%+.2f to the side%s' % (g0, s['first_change'], s['vmax'], s['brake_gf'],
                                                          s['brake_frames'], s['brake_dist'], s['endmove'], s['arrive'],
                                                          s['past_goal'], s['side'], mem))
                    lines.append('    states: %s' % s['seq'])
                    summary.append((name, 'stop', s['vmax'], s['brake_frames'], s['brake_dist'], s['endmove'],
                                    '%+.1f' % s['past_goal']))
                else:
                    ro = reords.get(str(o.get('mi')))
                    if ro is None or ro['g'] is None:
                        lines.append('  %s: no REORD record' % mv)
                        continue
                    d = (goal[0] - start[0], goal[1] - start[1])
                    L = math.hypot(*d) or 1.0
                    ux, uy = d[0] / L, d[1] / L
                    old_dir = (ux * F.ux + uy * F.vx, ux * F.uy + uy * F.vy)
                    s = reorder_stats(mover, ro['g'], g_end, F, old_dir)
                    if s is None:
                        lines.append('  %s: too few track lines' % mv)
                        continue
                    lines.append('  REORDER %s at g=%s (first change +%s): speed %.4f -> low %.4f after %d frames, ran on '
                                 '%.2f along the old heading, End Move %d frames, state left Move: %s; at rest +%s' % (
                                     mv, ro['g'], s['first_change'], s['v_at'], s['v_low'], s['to_low'], s['run_on'],
                                     s['endmove'], 'YES' if s['left_move'] else 'NO (aligned: speed kept)',
                                     s['rest_after']))
                    lines.append('    states: %s' % s['seq'])
                    summary.append((name, mv, s['v_at'], s['to_low'], s['run_on'], s['endmove'],
                                    'low %.3f' % s['v_low']))
        elif kind == 'mvm':
            o = next((r for r in rs if r['rk'] == 'ORDER'), None)
            if o is None or o['g'] is None:
                lines.append('  no ORDER record')
                continue
            g0 = o['g']
            spec = next((t for t in cfg['sq']['trials'] if t['name'] == name), None)
            rows = {key[1]: v for key, v in objs.items()}
            lag = min((v[0]['gf'] for v in rows.values() if v), default=g0) - g0
            if lag > 0:
                lines.append('  the track starts %d frames after the order (tracker found the ids late; holds before '
                             'that are not seen)' % lag)
            for role in ('a', 'b'):
                r = rows.get(role)
                if not r or spec is None:
                    lines.append('  %s: no track' % role)
                    continue
                goal = spec[role]['to']
                hs = holds(r, g0, goal, F)
                longest = max((h[1] for h in hs), default=0)
                arr = arrival(r, g0, goal, F)
                lines.append('  %s %s: holds %s; longest %d frames; arrived +%s; states %s' % (
                    role, spec[role]['type'], ', '.join('+%d x%d (%s)' % h for h in hs) or 'none', longest, arr,
                    seq_text(states_seq(window(r, g0, g0 + 3000)), g0)[:400]))
                summary.append((name, 'mvm_' + role, None, longest, None, len(hs), 'arr +%s' % arr))
            ra, rb = rows.get('a'), rows.get('b')
            if ra and rb:
                bb = {x['gf']: x for x in rb}
                dmin = min((math.hypot(x['x'] - bb[x['gf']]['x'], x['y'] - bb[x['gf']]['y']), x['gf'] - g0)
                           for x in ra if x['gf'] in bb and x['gf'] >= g0)
                lines.append('  closest approach %.2f at +%d' % dmin)
    lines.append('=' * 100)
    lines.append('SUMMARY (frames are logic frames; distances in world units)')
    lines.append('%-14s %-8s %9s %8s %9s %7s  %s' % ('trial', 'what', 'v', 'frames', 'distance', 'endmv', 'note'))
    for name, what, v, fr, dist, em, note in summary:
        lines.append('%-14s %-8s %9s %8s %9s %7s  %s' % (name, what, '%.4f' % v if v is not None else '-', fr,
                                                       '%.2f' % dist if dist is not None else '-', em, note))
    text = '\n'.join(lines) + '\n'
    if out:
        with open(out, 'w') as f:
            f.write(text)
    return text


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('run')
    ap.add_argument('--out')
    a = ap.parse_args()
    print(report(a.run, a.out))


if __name__ == '__main__':
    main()
