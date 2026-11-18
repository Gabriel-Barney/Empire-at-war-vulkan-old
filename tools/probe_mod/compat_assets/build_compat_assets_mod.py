#!/usr/bin/env python3
"""build_compat_assets_mod.py - builds the compat_assets probe mod (Mods\\CompatAssetsProbe) from the install.

  build_compat_assets_mod.py [--game DIR] [--out MODDIR] [--only KEY,KEY] [--skip KEY,KEY] [--map NAME] [--list]

Roadmap mcx2 t02, docs/MOD_COMPAT_DESIGN.md sec. 10.2 (decision D5: draw what retail draws, show nothing where retail
shows nothing). One land skirmish: the probe spawns, one slot at a time, a unit whose model (or animation, or particle
system) is a deliberately broken copy of a shipped one, points the camera at it and holds; the runner takes a screenshot
of each slot. The slots, in the order they spawn (control first, the ones most likely to crash retail last):

%(slots)s

Files of the mod (MODPATH=Mods\\CompatAssetsProbe):
  Data\\Art\\Models\\CAP_*.ALO / CAP_*_<ANIM>_<nn>.ALA   broken copies of EV_AT-ST, EI_TROOPER and P_FIRE_MED01
  Data\\XML\\CAP_Probe_Objects.xml                      one variant type per slot (Variant_Of_Existing_Type)
  Data\\XML\\GameObjectFiles.xml                        the shipped list plus CAP_Probe_Objects.xml
  Data\\Scripts\\FreeStore\\BusyTacticalFreeStore.lua    shipped script + generated CA_CFG + compat_assets_probe.lua
  Data\\Text\\MasterTextFile_English.dat                shipped texts + one line per slot
  CAP_PROBE_MARKER.txt                                 marker: cleanup deletes the folder only when it is there
Nothing from the game is stored in the repository.
"""
import argparse
import math
import os
import re
import shutil
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
PROBE_MOD = os.path.dirname(HERE)
ROOT = os.path.normpath(os.path.join(PROBE_MOD, '..', '..'))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(PROBE_MOD, 'common'))
sys.path.insert(0, PROBE_MOD)
sys.path.insert(0, os.path.join(PROBE_MOD, 's78n_t14_squads'))
sys.path.insert(0, os.path.join(PROBE_MOD, 'oi_abilities'))
import alo_chunks as A  # noqa: E402
import ted_owners  # noqa: E402
from ted_owners import MegArchive  # noqa: E402
from build_probe import dat_read, dat_bytes  # noqa: E402

MOD_NAME = 'CompatAssetsProbe'
MARKER = 'CAP_PROBE_MARKER.txt'
DEFAULT_GAME = ted_owners.DEFAULT_GAME
BS = '\\'
MODELS = 'DATA\\ART\\MODELS\\'
FREESTORE = 'DATA\\SCRIPTS\\FREESTORE\\BUSYTACTICALFREESTORE.LUA'
MASTERTEXT = 'DATA\\TEXT\\MASTERTEXTFILE_ENGLISH.DAT'
OBJFILES = 'DATA\\XML\\GAMEOBJECTFILES.XML'
DEFAULT_MAP = '_MP_LAND_GEONOSIS'
# [offline only] the corridor build_s78n_t14_mod found on Geonosis (2026-11-10): open, flat, far from both starts
FALLBACK_SITE = {'x': 2000.0, 'y': 800.0, 'z': -34.7, 'theta0': 165.0}

ATST = 'EV_AT-ST'
TROOPER = 'EI_TROOPER'
FIRE = 'P_FIRE_MED01'
WALK = 'MOVE_00'  # the AT-ST walk cycle, used as every IDLE of the animation slots so a played animation shows

# key, label, base type, element, what is broken. Order = spawn order.
SLOTS = [
    ('CTRL', 'control: intact AT-ST copy, walk cycle as idle', 'AT_ST_Walker', 'GroundVehicle', 'none'),
    ('NOCONN', 'model without connections chunk 0x600 (5 mod files)', 'AT_ST_Walker', 'GroundVehicle', 'drop_connections'),
    ('NOINFO', 'mesh without info chunk 0x402 (synthetic)', 'AT_ST_Walker', 'GroundVehicle', 'drop_mesh_info'),
    ('SPILL', 'empty mesh chunk, children at top level (z-95 shape, 2 mod files)', 'AT_ST_Walker', 'GroundVehicle',
     'spill_mesh'),
    ('COLL', 'collision tree child index out of bounds (8 mod files)', 'AT_ST_Walker', 'GroundVehicle',
     'bad_collision_child'),
    ('TRAIL', 'junk chunks after the model, last one overruns the file (Defiler shape, 1 mod file)', 'AT_ST_Walker',
     'GroundVehicle', 'junk_trailer'),
    ('BORDER', 'bone listed before its parent (hierarchy unchanged)', 'AT_ST_Walker', 'GroundVehicle', 'bone_order'),
    ('ANZ', 'animation: rotation block size 0 with rotation data (Venator shape)', 'AT_ST_Walker', 'GroundVehicle',
     'ala_stride0'),
    ('ANG', 'animation: garbage block sizes, no block data (RobedVibroFighter shape, ~50 mod files)', 'AT_ST_Walker',
     'GroundVehicle', 'ala_garbage_blocks'),
    ('SKCTRL', 'control: intact stormtrooper copy', 'Squad_Stormtrooper', 'GroundInfantry', 'none'),
    ('SKIN', 'skin bone index outside the bone map (1 mod file)', 'Squad_Stormtrooper', 'GroundInfantry',
     'bad_skin_index'),
    ('PCTRL', 'control: intact fire particle copy', 'Environmental_Fire_Damage_Particles', 'Particle', 'none'),
    ('PBAD', 'particle emitter property 4 bytes too long (synthetic)', 'Environmental_Fire_Damage_Particles',
     'Particle', 'particle_property_size'),
    ('ANS', 'animation: rotation data holds half the declared frames (synthetic, reads past the data)',
     'AT_ST_Walker', 'GroundVehicle', 'ala_rotation_short'),
    ('OVR', 'chunk inside a mesh overruns its parent (synthetic)', 'AT_ST_Walker', 'GroundVehicle', 'overrun_chunk'),
    ('EMPTY', 'empty .ALO file (2 mod files)', 'AT_ST_Walker', 'GroundVehicle', 'empty'),
]
ANIM_DEFECTS = {'ala_stride0', 'ala_garbage_blocks', 'ala_rotation_short'}
BYTES_DEFECTS = {'overrun_chunk', 'junk_trailer'}

CFG = {
    'session': 'mcx2_t02',
    'ai_faction': 'REBEL',       # the AI whose free store runs the probe
    'human_faction': 'Empire',   # the driver's side; the slots are spawned for this player
    'start_frame': 300,          # 10 s into the battle
    'hold': 270,                 # frames the camera stays on a slot (9 s; the runner shoots at 4 s and 7 s)
    'gap': 30,                   # frames between slots
    'spacing': 90.0,             # distance between slots along the corridor
    'overview': 300,             # frames of the closing overview (camera on the middle of the row)
}


def slot_doc():
    return '\n'.join('  %-7s %s' % (k, label) for k, label, _, _, _ in SLOTS)


__doc__ = __doc__ % {'slots': slot_doc()}


class Source:
    """Shipped files: loose FoC > FoC MEGs (patches first) > loose EaW > EaW MEGs, models.meg included."""

    def __init__(self, game):
        self.gf = ted_owners.GameFiles(game)
        self.idx = []
        for sub in ('corruption', 'GameData'):
            d = os.path.join(game, sub, 'Data')
            if not os.path.isdir(d):
                continue
            have = {f.lower(): f for f in os.listdir(d)}
            for n in ('64patch.meg', 'patch2.meg', 'patch.meg', 'models.meg'):
                if n in have:
                    self.idx.append((sub + ':' + have[n], os.path.join(d, have[n])))
        self._open = {}

    def _entries(self, i):
        if i not in self._open:
            a = MegArchive(self.idx[i][1])
            self._open[i] = (a, {e.name.upper().replace('/', BS): e for e in a.entries})
        return self._open[i]

    def read(self, name):
        name = name.upper().replace('/', BS)
        if name.startswith(MODELS):
            loose = os.path.join(self.gf.game, 'corruption', name)
            if os.path.isfile(loose):
                return open(loose, 'rb').read(), 'loose:corruption'
            for i in range(len(self.idx)):
                a, idx = self._entries(i)
                e = idx.get(name)
                if e is not None:
                    return a.data[e.offset:e.offset + e.size], self.idx[i][0]
            return None, None
        return self.gf.read(name)

    def need(self, name):
        data, src = self.read(name)
        if data is None:
            raise SystemExit('shipped %s not found' % name)
        return data, src

    def models_like(self, prefix):
        out = set()
        for i in range(len(self.idx)):
            _, idx = self._entries(i)
            out.update(n[len(MODELS):] for n in idx if n.startswith(MODELS + prefix))
        return sorted(out)


def crlf(text):
    return text.replace('\r\n', '\n').replace('\n', '\r\n').encode('latin-1')


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)


def prepare(out):
    if os.path.exists(out):
        if not os.path.exists(os.path.join(out, MARKER)):
            raise SystemExit('refusing to overwrite %s: not a compat_assets probe folder' % out)
        shutil.rmtree(out)
    os.makedirs(out)
    write(os.path.join(out, MARKER), b'compat_assets probe mod (tools/probe_mod/compat_assets); removed by '
                                     b'cleanup_compat_assets.cmd\r\n')


def base_model(key):
    return {'SKCTRL': TROOPER, 'SKIN': TROOPER, 'PCTRL': FIRE, 'PBAD': FIRE}.get(key, ATST)


def model_name(key):
    return 'CAP_%s' % key


def slot_files(src, key, defect):
    """{file name: bytes} of one slot and the note of what was broken."""
    base = base_model(key)
    raw, origin = src.need(MODELS + base + '.ALO')
    name = model_name(key)
    files = {}
    note = 'intact copy of %s.ALO (%s)' % (base, origin)
    if defect == 'empty':
        files[name + '.ALO'] = b''
        note = 'empty file'
    elif defect in BYTES_DEFECTS:
        files[name + '.ALO'], note = getattr(A, defect)(A.parse(raw))
    elif defect == 'none' or defect in ANIM_DEFECTS:
        files[name + '.ALO'] = raw
    else:
        tree = A.parse(raw)
        note = getattr(A, defect)(tree)
        files[name + '.ALO'] = A.write(tree)
    if base == ATST:
        walk, _ = src.need(MODELS + '%s_%s.ALA' % (ATST, WALK))
        for anim in src.models_like(ATST + '_'):
            if not anim.endswith('.ALA'):
                continue
            tail = anim[len(ATST) + 1:-4]
            if tail.startswith('IDLE_'):
                continue
            files['%s_%s.ALA' % (name, tail)], _ = src.need(MODELS + anim)
        idle = walk
        if defect in ANIM_DEFECTS:
            tree = A.parse(walk)
            note = getattr(A, defect)(tree) + ' (%s_%s.ALA used as IDLE_00)' % (ATST, WALK)
            idle = A.write(tree)
        files['%s_IDLE_00.ALA' % name] = idle
    elif base == TROOPER:
        for anim in src.models_like(TROOPER + '_'):
            if anim.endswith('.ALA'):
                files['%s_%s' % (name, anim[len(TROOPER) + 1:])], _ = src.need(MODELS + anim)
    return files, note


def objects_xml(slots):
    lines = ['<?xml version="1.0" ?>', '<CAP_Probe_Objects>']
    for key, label, base_type, elem, _ in slots:
        lines.append('\t<!-- %s: %s -->' % (key, label.replace('--', '-')))
        lines.append('\t<%s Name="CAP_%s">' % (elem, key))
        lines.append('\t\t<Variant_Of_Existing_Type>%s</Variant_Of_Existing_Type>' % base_type)
        lines.append('\t\t<Land_Model_Name>%s.ALO</Land_Model_Name>' % model_name(key))
        if elem == 'Particle':
            lines.append('\t\t<Particle_Lifetime_Frames>100000</Particle_Lifetime_Frames>')
            lines.append('\t\t<Scale_Factor>3.0</Scale_Factor>')
        lines.append('\t</%s>' % elem)
    lines.append('</CAP_Probe_Objects>')
    return crlf('\n'.join(lines) + '\n')


def object_files(src):
    shipped, _ = src.need(OBJFILES)
    text = shipped.decode('latin-1')
    i = text.rfind('</')
    if i < 0:
        raise SystemExit('GameObjectFiles.xml: no closing tag')
    return crlf(text[:i] + '\t<File>CAP_Probe_Objects.xml</File>\n' + text[i:])


def texts(slots):
    out = [('TEXT_CAP_ARMED', 'COMPAT ASSETS PROBE armed: %d slots. Give no orders, do not pause.' % len(slots)),
           ('TEXT_CAP_DONE', 'COMPAT ASSETS PROBE done. Esc > Quit, then quit the game.'),
           ('TEXT_CAP_OVERVIEW', 'COMPAT ASSETS PROBE: overview of every slot (left = first)')]
    for i, (key, label, _, _, _) in enumerate(slots):
        out.append(('TEXT_CAP_SLOT_%s' % key, 'CAP SLOT %d %s: %s' % (i + 1, key, label)))
    return out


def texts_dat(src, pairs):
    mt, _ = src.need(MASTERTEXT)
    tmp = os.path.join(os.environ.get('TEMP', HERE), 'cap_mt_%d.tmp' % os.getpid())
    with open(tmp, 'wb') as f:
        f.write(mt)
    try:
        keys = {k for k, _ in pairs}
        base = [kv for kv in dat_read(tmp) if kv[0] not in keys]
    finally:
        os.remove(tmp)
    return dat_bytes(base + list(pairs))


def site(game, map_name, offline):
    if offline:
        return dict(FALLBACK_SITE, note='fallback corridor (offline)')
    import build_s78n_t14_mod as s78
    p, theta0, far, note = s78.site_for(game, map_name, False)
    return {'x': p['x'], 'y': p['y'], 'z': p.get('z', 0.0), 'theta0': theta0,
            'note': '%s; %.0f from the nearest start' % (note, far)}


def lua_value(v):
    if isinstance(v, bool):
        return 'true' if v else 'false'
    if isinstance(v, (int, float)):
        return repr(float(v)) if isinstance(v, float) else str(v)
    if isinstance(v, str):
        return '"%s"' % v.replace('\\', '\\\\').replace('"', '\\"')
    if isinstance(v, (list, tuple)):
        return '{ ' + ', '.join(lua_value(x) for x in v) + ' }'
    return '{ ' + ', '.join('%s = %s' % (k, lua_value(v[k])) for k in sorted(v)) + ' }'


def config(game, offline, slots, map_name=DEFAULT_MAP):
    s = site(game, map_name, offline)
    th = math.radians(s['theta0'])
    pts = []
    for i, (key, _, _, _, _) in enumerate(slots):
        d = 60.0 + i * CFG['spacing']
        pts.append({'key': key, 'x': round(s['x'] + d * math.cos(th), 1), 'y': round(s['y'] + d * math.sin(th), 1),
                    'z': s['z']})
    cfg = dict(CFG)
    cfg.update({'map': map_name, 'site_note': s['note'], 'slots': pts})
    return cfg


def lua_config(cfg):
    keys = ['session', 'ai_faction', 'human_faction', 'start_frame', 'hold', 'gap', 'overview']
    body = {k: cfg[k] for k in keys}
    body['slots'] = [{'key': p['key'], 'type': 'CAP_' + p['key'], 'x': p['x'], 'y': p['y'], 'z': p['z']}
                     for p in cfg['slots']]
    return '-- generated by build_compat_assets_mod.py\nCA_CFG = %s\n' % lua_value(body)


def freestore(src, cfg):
    shipped, _ = src.need(FREESTORE)
    text = shipped.decode('latin-1').replace('\r\n', '\n').rstrip('\n')
    probe = open(os.path.join(HERE, 'compat_assets_probe.lua'), encoding='ascii').read()
    return crlf(text + '\n\n' + lua_config(cfg) + '\n' + probe)


def pick(only=None, skip=None):
    keys = [s[0] for s in SLOTS]
    for k in (only or []) + (skip or []):
        if k not in keys:
            raise SystemExit('unknown slot %s (slots: %s)' % (k, ', '.join(keys)))
    out = [s for s in SLOTS if (not only or s[0] in only) and s[0] not in (skip or [])]
    if not out:
        raise SystemExit('no slot left')
    return out


def build(game, out, only=None, skip=None, map_name=DEFAULT_MAP, offline=False):
    """Build Mods\\CompatAssetsProbe into `out`. Returns the config (slots, notes, files)."""
    slots = pick(only, skip)
    src = Source(game)
    prepare(out)
    files = []

    def put(rel, data):
        write(os.path.join(out, rel), data)
        files.append(rel)

    notes = {}
    for key, _, _, _, defect in slots:
        fs, note = slot_files(src, key, defect)
        notes[key] = note
        for n, data in sorted(fs.items()):
            put('Data\\Art\\Models\\' + n, data)
    put('Data\\XML\\CAP_Probe_Objects.xml', objects_xml(slots))
    put('Data\\XML\\GameObjectFiles.xml', object_files(src))
    cfg = config(game, offline, slots, map_name)
    put('Data\\Scripts\\FreeStore\\BusyTacticalFreeStore.lua', freestore(src, cfg))
    put('Data\\Text\\MasterTextFile_English.dat', texts_dat(src, texts(slots)))
    cfg['notes'] = notes
    cfg['labels'] = {k: label for k, label, _, _, _ in slots}
    cfg['files'] = files
    return cfg


def describe(cfg):
    out = ['compat_assets probe: %d slots on %s (%s)' % (len(cfg['slots']), cfg['map'], cfg['site_note'])]
    for p in cfg['slots']:
        out.append('  %-7s at (%.0f, %.0f): %s' % (p['key'], p['x'], p['y'], cfg['notes'][p['key']]))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game', default=DEFAULT_GAME)
    ap.add_argument('--out')
    ap.add_argument('--only', default='')
    ap.add_argument('--skip', default='')
    ap.add_argument('--map', default=DEFAULT_MAP)
    ap.add_argument('--offline-site', action='store_true', help='use the stored Geonosis corridor (no map parse)')
    ap.add_argument('--list', action='store_true')
    a = ap.parse_args()
    if a.list:
        print(slot_doc())
        return 0
    out = a.out or os.path.join(a.game, 'corruption', 'Mods', MOD_NAME)
    split = lambda s: [x.strip().upper() for x in s.split(',') if x.strip()]  # noqa: E731
    cfg = build(a.game, out, split(a.only), split(a.skip), a.map, a.offline_site)
    print('\n'.join(describe(cfg)))
    print('%d files in %s' % (len(cfg['files']), out))
    return 0


if __name__ == '__main__':
    sys.exit(main())
