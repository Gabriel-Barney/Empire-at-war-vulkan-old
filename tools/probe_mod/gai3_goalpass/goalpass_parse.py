#!/usr/bin/env python3
"""goalpass_parse.py - goalpass_dbg.py log -> report.txt, passes.tsv, evals.tsv (roadmap gai3 t06; BKG1 B.8).

  goalpass_parse.py trace.log [--out-dir DIR] [--all-modes]

Each AI player has its own galactic goal system (rdi, `gs`); passes are grouped per goal system and labelled with its
owner player id (and faction when the PLAYER line found one). A pass ends at a Commit hit (k=C):
  passes.tsv  one row per Commit: owner, faction, gs, pass, commit gf / mf, E logged by Commit ([rdi+0x188] before
              the reset), evals seen since that goal system's previous Commit (T + G hits), delta = E - seen, quota
              ([rdi+0x178], the quota the pass ran at), first / last eval mode frame, note. The first pass after
              arming is `first`: retail seeds E at Init (B.2) and hits before arming are missing, so its delta need not
              be 0. Every later pass must have delta 0 (the "E check").
  evals.tsv   every evaluation in order: owner, gs, pass, seq, hit n, gf, mf, kind (T targeted / G global), source and
              target cursors, E before the increment, goal type name (or gt:<ptr>), function, planet name (or
              idx:<n> / po:<ptr>), planet index byte.
  picks.tsv   every Weighted_Random_Pick call (PICK lines, Dr3; gai3 t05): mode frame, caller RVA, entry count, total
              weight, entries (planet names in list order; FindTarget moves its best to the end) and their weights.
Names come from the GTYPE / PLANET / FUNC lines (std::string at goal type +0x00, planet type +0xF8; else the first
`cands` entry; else the pointer). For a fallback by source order, report.txt lists every (owner, source cursor) ->
goal type pointer seen: the source list is 0x60f6c0's (goal type, function) order (B.3).
By default only hits in the galactic mode (mt 0) count; --all-modes keeps the others (main-menu battles).
"""
import argparse
import os
import sys
import urllib.parse
from collections import OrderedDict, defaultdict

sys.dont_write_bytecode = True


def unesc(s):
    return urllib.parse.unquote(s)


def parse_kv(rest):
    d = {}
    for tok in rest.split(' '):
        if '=' in tok:
            k, v = tok.split('=', 1)
            d[k] = v
    return d


def as_int(v):
    if v in (None, '', 'None', '-'):
        return None
    try:
        return int(v)
    except ValueError:
        return None


def ident_like(s):
    return bool(s) and len(s) >= 2 and all(c.isalnum() or c in '_-.' for c in s)


def pick_name(d):
    n = unesc(d.get('name', '-'))
    if n != '-' and ident_like(n):
        return n, 'name'
    for c in d.get('cands', '-').split(';'):
        if ':' in c and c != '-':
            t = unesc(c.split(':', 1)[1])
            if ident_like(t):
                return t, 'cand ' + c.split(':', 1)[0]
    return None, None


def read_log(path):
    L = dict(head={}, attach={}, exe={}, sites=[], arms=[], stop={}, exit={}, detach={}, hits=[], gsys=OrderedDict(),
             players=OrderedDict(), gtypes=OrderedDict(), funcs=OrderedDict(), planets=OrderedDict(), late=0, picks=[],
             other=defaultdict(int))
    for line in open(path, encoding='ascii', errors='replace'):
        line = line.rstrip('\n')
        tag, _, rest = line.partition(' ')
        d = parse_kv(rest)
        if tag == 'GOALPASS':
            L['head'] = d
        elif tag == 'ATTACH':
            L['attach'] = d
        elif tag == 'EXE':
            L['exe'] = d
        elif tag == 'SITE':
            L['sites'].append(d)
        elif tag in ('ARM', 'DISARM'):
            L['arms'].append((tag, d))
        elif tag == 'HIT':
            L['hits'].append(d)
        elif tag == 'PICK':
            L['picks'].append(d)
        elif tag == 'GSYS':
            L['gsys'][d['gs']] = d
        elif tag == 'PLAYER':
            L['players'][d['ptr']] = d
        elif tag == 'GTYPE':
            L['gtypes'][d['ptr']] = d
        elif tag == 'FUNC':
            L['funcs'][d['ptr']] = d
        elif tag == 'PLANET':
            L['planets'][d['ptr']] = d
        elif tag == 'LATE_HIT':
            L['late'] += 1
        elif tag == 'STOP':
            L['stop'] = d
        elif tag == 'EXIT':
            L['exit'] = d
        elif tag == 'DETACH':
            L['detach'] = d
        elif tag:
            L['other'][tag] += 1
    return L


def analyse(L, all_modes=False):
    gt_name = {p: (pick_name(d)[0] or 'gt:' + p) for p, d in L['gtypes'].items()}
    fn_name = {p: (pick_name(d)[0] or 'fn:' + p) for p, d in L['funcs'].items()}
    po_name = {}
    for p, d in L['planets'].items():
        n = pick_name(d)[0]
        po_name[p] = n or ('idx:%s' % d.get('index') if d.get('index') not in (None, 'None') else 'po:' + p)
    pl_by_id = {}
    for d in L['players'].values():
        pl_by_id[d.get('id')] = d
    passes, evals, open_passes = [], [], []
    cur = defaultdict(list)             # gs -> evals since its previous Commit
    npass = defaultdict(int)
    ignored = defaultdict(int)
    src_map = OrderedDict()             # (own, src) -> set of gt pointers
    for h in L['hits']:
        mt = as_int(h.get('mt'))
        if not all_modes and mt != 0:
            ignored[(mt, h['k'])] += 1
            continue
        gs, k = h['gs'], h['k']
        own = h.get('own')
        fac = pl_by_id.get(own, {}).get('fname', '-')
        if k in ('T', 'G'):
            cur[gs].append(h)
            src_map.setdefault((own, as_int(h.get('src'))), set()).add(h.get('gt'))
            continue
        if k != 'C':
            continue
        npass[gs] += 1
        ev = cur.pop(gs, [])
        e = as_int(h.get('e'))
        nt = sum(1 for x in ev if x['k'] == 'T')
        ng = len(ev) - nt
        first = npass[gs] == 1
        delta = None if e is None else e - len(ev)
        note = 'first' if first else ('ok' if delta == 0 else 'MISMATCH')
        passes.append(OrderedDict(owner=own, faction=fac, gs=gs, pass_=npass[gs], commit_n=h['n'], commit_gf=h.get('gf'),
                                  commit_mf=h.get('mf'), E=e, evals_seen=len(ev), targeted=nt, global_=ng, delta=delta,
                                  quota=h.get('q'), first_eval_mf=ev[0].get('mf') if ev else '',
                                  last_eval_mf=ev[-1].get('mf') if ev else '', note=note))
        for i, x in enumerate(ev):
            evals.append(eval_row(x, own, fac, gs, npass[gs], i + 1, gt_name, fn_name, po_name))
    for gs, ev in cur.items():
        if not ev:
            continue
        own = ev[0].get('own')
        fac = pl_by_id.get(own, {}).get('fname', '-')
        p = npass[gs] + 1
        open_passes.append((own, fac, gs, p, len(ev), ev[0].get('mf'), ev[-1].get('mf')))
        for i, x in enumerate(ev):
            r = eval_row(x, own, fac, gs, p, i + 1, gt_name, fn_name, po_name)
            r['pass_'] = '%d(open)' % p
            evals.append(r)
    return dict(passes=passes, evals=evals, open=open_passes, ignored=ignored, gt_name=gt_name, po_name=po_name,
                fn_name=fn_name, src_map=src_map, picks=pick_rows(L, po_name))


def eval_row(x, own, fac, gs, p, seq, gt_name, fn_name, po_name):
    gt, fn, po = x.get('gt', ''), x.get('fn', ''), x.get('po', '')
    planet = ''
    if x['k'] == 'T':
        planet = po_name.get(po) or ('idx:%s' % x.get('pi') if x.get('pi') not in (None, 'None') else 'po:' + po)
    return OrderedDict(owner=own, faction=fac, gs=gs, pass_=p, seq=seq, n=x['n'], gf=x.get('gf'), mf=x.get('mf'),
                       kind=x['k'], src=x.get('src'), tgt=x.get('tgt') if x['k'] == 'T' else '', e_before=x.get('e'),
                       goal_type=gt_name.get(gt, 'gt:' + gt), gt_ptr=gt, function=fn_name.get(fn, 'fn:' + fn),
                       planet=planet, planet_index=x.get('pi', '') if x['k'] == 'T' else '', planet_ptr=po)


def pick_rows(L, po_name):
    """One row per Weighted_Random_Pick call (Dr3): entries named through the PLANET lines, weights = cum steps."""
    rows = []
    for d in L['picks']:
        names, weights, prev = [], [], 0.0
        for e in (d.get('ents') or '-').split(','):
            if e in ('', '-'):
                continue
            parts = e.split('/')
            if len(parts) != 3:
                continue
            po, cum = parts[1], float(parts[2])
            names.append(po_name.get(po) or 'po:' + po)
            weights.append('%.4g' % (cum - prev))
            prev = cum
        rows.append(OrderedDict(n=d.get('n'), gf=d.get('gf'), mt=d.get('mt'), mf=d.get('mf'), ret=d.get('ret'),
                                cnt=d.get('cnt'), total='%.6g' % prev, entries=' '.join(names),
                                weights=' '.join(weights)))
    return rows


PICK_COLS = ['n', 'gf', 'mt', 'mf', 'ret', 'cnt', 'total', 'entries', 'weights']


def write_tsv(path, rows, cols):
    with open(path, 'w', newline='\n') as f:
        f.write('\t'.join(c.rstrip('_') for c in cols) + '\n')
        for r in rows:
            f.write('\t'.join('' if r.get(c) is None else str(r.get(c)) for c in cols) + '\n')


PASS_COLS = ['owner', 'faction', 'gs', 'pass_', 'commit_n', 'commit_gf', 'commit_mf', 'E', 'evals_seen', 'targeted',
             'global_', 'delta', 'quota', 'first_eval_mf', 'last_eval_mf', 'note']
EVAL_COLS = ['owner', 'faction', 'gs', 'pass_', 'seq', 'n', 'gf', 'mf', 'kind', 'src', 'tgt', 'e_before', 'goal_type',
             'gt_ptr', 'function', 'planet', 'planet_index', 'planet_ptr']


def report_lines(L, A, log_path):
    out = []
    w = out.append
    w('gai3 goalpass report (BKG1 B.8) for %s' % log_path)
    h = L['head']
    w('started: %s  stop_galactic_frame=%s  arm=%s' % (h.get('t'), h.get('stop_galactic_frame'), h.get('arm')))
    e = L['exe']
    w('exe: base=%s timestamp=%s size_of_image=%s known=%s' % (e.get('base'), e.get('timestamp'), e.get('size_of_image'),
                                                              e.get('known')))
    for s in L['sites']:
        w('site %s rva=%s bytes=%s ok=%s' % (s.get('k'), s.get('rva'), s.get('bytes'), s.get('ok')))
    st = L['stop']
    end = ('stop why=%s' % st.get('why')) if st else ('game exit code=%s' % L['exit'].get('code') if L['exit'] else 'none')
    w('stop: %s; detach ok=%s; late hits=%d' % (end, L['detach'].get('ok', '-'), L['late']))
    kinds = defaultdict(int)
    for x in L['hits']:
        kinds[x['k']] += 1
    w('hits: %d (T %d, G %d, C %d); Weighted_Random_Pick calls (PICK, Dr3): %d' % (
        len(L['hits']), kinds['T'], kinds['G'], kinds['C'], len(L['picks'])))
    if A['ignored']:
        w('ignored (not galactic mode, use --all-modes): ' +
          ', '.join('mt=%s %s:%d' % (mt, k, n) for (mt, k), n in sorted(A['ignored'].items(), key=str)))
    gal = [x for x in L['hits'] if as_int(x.get('mt')) == 0]
    if gal:
        w('galactic hits: mode frames %s..%s, global frames %s..%s' % (gal[0].get('mf'), gal[-1].get('mf'),
                                                                       gal[0].get('gf'), gal[-1].get('gf')))
    w('')
    w('players (PLAYER lines; owner = [[[gs+0x28]+0x28]+0x28]+0x4C [M]):')
    for d in L['players'].values():
        w('  id=%s index=%s human=%s faction=%s (%s) ptr=%s' % (d.get('id'), d.get('index'), d.get('human'),
                                                              d.get('fname'), d.get('faction'), d.get('ptr')))
    w('goal systems (GSYS lines):')
    for d in L['gsys'].values():
        w('  gs=%s own=%s chain=%s first seen mt=%s mf=%s' % (d['gs'], d.get('own'), d.get('chain'), d.get('mt'),
                                                             d.get('mf')))
    w('')
    P = A['passes']
    w('passes: %d committed, %d open at the stop' % (len(P), len(A['open'])))
    later = [p for p in P if p['note'] != 'first']
    bad = [p for p in later if p['note'] != 'ok']
    w('E check (E logged by Commit == evaluations seen, every pass after the first): %s (%d passes, %d mismatches)' % (
        ('OK' if later else 'NO DATA') if not bad else 'FAILED', len(later), len(bad)))
    byown = OrderedDict()
    for p in P:
        byown.setdefault((p['owner'], p['faction'], p['gs']), []).append(p)
    for (own, fac, gs), ps in byown.items():
        w('')
        w('owner %s %s gs=%s: %d passes' % (own, fac, gs, len(ps)))
        w('  pass commit_mf commit_gf      E  seen  (T/G)  delta quota  evals_mf   note')
        for p in ps:
            w('  %4d %9s %9s %6s %5d (%d/%d) %6s %5s  %s-%s  %s' % (p['pass_'], p['commit_mf'], p['commit_gf'], p['E'],
                                                                 p['evals_seen'], p['targeted'], p['global_'],
                                                                 p['delta'], p['quota'], p['first_eval_mf'],
                                                                 p['last_eval_mf'], p['note']))
    for own, fac, gs, p, n, m0, m1 in A['open']:
        w('open pass: owner %s %s gs=%s pass %d: %d evals, mf %s-%s' % (own, fac, gs, p, n, m0, m1))
    w('')
    w('goal types (GTYPE: name = std::string at +0x00; flags +0x20, category +0x28, reach +0x2C):')
    for ptr, d in L['gtypes'].items():
        n, how = pick_name(d)
        w('  %s  %-40s flags=%s cat=%s reach=%s  (%s)' % (ptr, n or '-', d.get('flags'), d.get('category'),
                                                        d.get('reach'), how or 'no name: pointer only'))
    w('functions (FUNC, name from cands only):')
    for ptr, d in L['funcs'].items():
        n, how = pick_name(d)
        w('  %s  %s  (%s)' % (ptr, n or '-', how or 'pointer only'))
    w('planets (PLANET: name = type +0xF8 std::string, type = object +0x298; index byte +0x335 [M]):')
    for ptr, d in L['planets'].items():
        n, how = pick_name(d)
        w('  %s  idx=%-4s %-28s (%s)' % (ptr, d.get('index'), n or '-', how or 'pointer only'))
    w('')
    w('source order (owner, source cursor +0x17C -> goal type; the fallback when names are missing):')
    for (own, src), gts in sorted(A['src_map'].items(), key=lambda kv: (str(kv[0][0]), kv[0][1] or -1)):
        w('  own=%s src=%s -> %s' % (own, src, ', '.join('%s (%s)' % (g, A['gt_name'].get(g, '?')) for g in sorted(gts))))
    w('')
    w('Weighted_Random_Pick calls (gai3 t05 / a2m t14.4: FindTarget candidates in list order, best moved to the end; '
      'a Rebel slice pick at mode frame ~97 lists the Rebel Enemy planets):')
    for r in A['picks']:
        w('  n=%s mt=%s mf=%s ret=%s cnt=%s total=%s | %s | w %s' % (r['n'], r['mt'], r['mf'], r['ret'], r['cnt'],
                                                                     r['total'], r['entries'], r['weights']))
    if not A['picks']:
        w('  none')
    if L['other']:
        w('')
        w('other lines: ' + ', '.join('%s:%d' % kv for kv in sorted(L['other'].items())))
    return out


def parse_to(log_path, out_dir, all_modes=False):
    L = read_log(log_path)
    A = analyse(L, all_modes)
    os.makedirs(out_dir, exist_ok=True)
    write_tsv(os.path.join(out_dir, 'passes.tsv'), A['passes'], PASS_COLS)
    write_tsv(os.path.join(out_dir, 'evals.tsv'), A['evals'], EVAL_COLS)
    write_tsv(os.path.join(out_dir, 'picks.tsv'), A['picks'], PICK_COLS)
    with open(os.path.join(out_dir, 'report.txt'), 'w', newline='\n') as f:
        f.write('\n'.join(report_lines(L, A, log_path)) + '\n')
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('log')
    ap.add_argument('--out-dir', help='default: the log folder')
    ap.add_argument('--all-modes', action='store_true')
    a = ap.parse_args()
    out = a.out_dir or os.path.dirname(os.path.abspath(a.log))
    rc = parse_to(a.log, out, a.all_modes)
    print('wrote', os.path.join(out, 'report.txt'), 'passes.tsv', 'evals.tsv')
    return rc


if __name__ == '__main__':
    sys.exit(main())
