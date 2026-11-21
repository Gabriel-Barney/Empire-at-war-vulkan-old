#!/usr/bin/env python3
"""compat_assets_analyse.py - turns the compat_assets probe records (CAQ|...|END) into a per-slot table.

  compat_assets_analyse.py RECORDS.log [--config config.json] [--out report.txt] [--json report.json]

Per slot: whether the variant type exists (Find_Object_Type), how it spawned, how many objects were alive when the
camera arrived and when it left, and whether the game stopped while the slot was spawning or on screen (the last SHOW
or SPAWN_BEGIN without a HOLD_END, and no DONE). What retail DREW is read from the screenshots (out\\<run>\\shots\\,
the on-screen line "CAP SLOT <n> <KEY>" names each slot); the report lists the questions to answer from them. The
verdict per defect class goes into docs/re/MCX2_T02_ASSET_SALVAGE.md.
"""
import argparse
import json
import os
import sys

FIELDS_SKIP = {'sid', 'n', 'f'}


def parse_line(line):
    line = line.strip()
    if not line.startswith('CAQ|') or not line.endswith('|END'):
        return None
    parts = line[4:-4].split('|')
    rec = {'kind': parts[0]}
    for p in parts[1:]:
        if '=' in p:
            k, v = p.split('=', 1)
            rec[k] = v
    return rec


def load(path):
    out = []
    with open(path, encoding='ascii', errors='replace') as f:
        for line in f:
            r = parse_line(line)
            if r:
                out.append(r)
    out.sort(key=lambda r: int(r.get('n', '0')) if r.get('n', '0').isdigit() else 0)
    return out


def analyse(recs, cfg=None):
    slots = {}
    order = []
    for r in recs:
        key = r.get('key')
        if key and key not in slots:
            slots[key] = {'key': key, 'slot': r.get('slot')}
            order.append(key)
        if r['kind'] == 'SPAWN_BEGIN':
            slots[key]['begin_f'] = r.get('f')
        elif r['kind'] == 'SPAWN':
            slots[key].update({'how': r.get('how'), 'n': r.get('objs'), 'alive_at_spawn': r.get('alive'),
                               'ids': r.get('ids')})
        elif r['kind'] == 'SHOW':
            slots[key]['show_f'] = r.get('f')
        elif r['kind'] == 'HOLD_END':
            slots[key]['alive_at_end'] = r.get('alive')
            slots[key]['end_f'] = r.get('f')
    done = any(r['kind'] == 'DONE' for r in recs)
    stopped_in = None
    if not done and order:
        last = slots[order[-1]]
        if 'end_f' not in last:
            stopped_in = last['key']
    planned = [p['key'] for p in (cfg or {}).get('slots', [])]
    never = [k for k in planned if k not in slots]
    return {'records': len(recs), 'armed': any(r['kind'] == 'ARM' for r in recs), 'done': done,
            'slots': [slots[k] for k in order], 'stopped_in': stopped_in, 'never_reached': never,
            'apierr': [r for r in recs if r['kind'] in ('APIERR', 'ERR')]}


def render(res, cfg=None):
    labels = (cfg or {}).get('labels', {})
    notes = (cfg or {}).get('notes', {})
    out = ['compat_assets probe report (roadmap mcx2 t02)', '',
           'records %d, armed %s, probe finished %s' % (res['records'], res['armed'], res['done']), '']
    out.append('%-7s %-18s %4s %6s %6s  %s' % ('slot', 'spawn', 'objs', 'alive0', 'alive1', 'defect'))
    for s in res['slots']:
        out.append('%-7s %-18s %4s %6s %6s  %s' % (s['key'], s.get('how', '?'), s.get('n', '?'),
                                                   s.get('alive_at_spawn', '?'), s.get('alive_at_end', '-'),
                                                   labels.get(s['key'], '')))
        if notes.get(s['key']):
            out.append('        built: %s' % notes[s['key']])
    if res['stopped_in']:
        out += ['', 'THE GAME STOPPED while slot %s was spawning or on screen (no HOLD_END, no DONE). Retail does not '
                    'survive this defect, or something else ended the run: rerun with --skip %s to see the rest.' %
                (res['stopped_in'], res['stopped_in'])]
    if res['never_reached']:
        out += ['', 'never reached: %s' % ', '.join(res['never_reached'])]
    for r in res['apierr']:
        out.append('API/ERR: %s' % ' '.join('%s=%s' % (k, v) for k, v in r.items() if k not in FIELDS_SKIP))
    out += ['', 'From the screenshots (shots\\, the line "CAP SLOT <n> <KEY>" names the slot), for each slot: is the unit',
            'drawn like the control (CTRL, SKCTRL, PCTRL), drawn differently (how), or not drawn? For ANZ / ANG / ANS:',
            'does the AT-ST walk in place like CTRL, stand in one pose, or stand in a broken pose?',
            'Record the verdicts in docs/re/MCX2_T02_ASSET_SALVAGE.md.']
    return '\n'.join(out)


def report(records_path, out_txt=None, out_json=None, cfg_path=None):
    cfg = None
    if cfg_path and os.path.exists(cfg_path):
        with open(cfg_path) as f:
            cfg = json.load(f)
    recs = load(records_path) if os.path.exists(records_path) else []
    res = analyse(recs, cfg)
    text = render(res, cfg)
    if out_txt:
        with open(out_txt, 'w') as f:
            f.write(text + '\n')
    if out_json:
        with open(out_json, 'w') as f:
            json.dump(res, f, indent=1)
    return text, res


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('records')
    ap.add_argument('--config')
    ap.add_argument('--out')
    ap.add_argument('--json')
    a = ap.parse_args()
    text, res = report(a.records, a.out, a.json, a.config)
    print(text)
    return 0 if res['records'] else 10


if __name__ == '__main__':
    sys.exit(main())
