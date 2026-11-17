"""alo_chunks.py - a small reader / writer for the Alamo chunk tree (ALO models, ALO particle systems, ALA animations)
and the defect makers the compat_assets probe uses (roadmap mcx2 t02, docs/MOD_COMPAT_DESIGN.md sec. 10.2).

Chunk layout (docs/formats): u32 id, u32 size; bit 31 of size set = the chunk holds child chunks, else data. A data
chunk may hold mini-chunks: u8 id, u8 size, data. Sizes are recomputed on write, so a tree edited here stays a
well-formed file unless a defect maker breaks it on purpose (overrun_chunk writes a size by hand).
Standard library only; no retail data is stored in the repository: the builder reads the shipped files from the install.
"""
import struct


class Chunk:
    __slots__ = ('id', 'children', 'data')

    def __init__(self, cid, children=None, data=b''):
        self.id = cid
        self.children = children  # list of Chunk, or None for a data chunk
        self.data = data

    def find(self, cid):
        return [c for c in (self.children or []) if c.id == cid]

    def first(self, cid):
        f = self.find(cid)
        return f[0] if f else None

    def __repr__(self):
        if self.children is None:
            return 'Chunk(0x%x, %d bytes)' % (self.id, len(self.data))
        return 'Chunk(0x%x, %d children)' % (self.id, len(self.children))


def parse(buf, start=0, end=None):
    """The chunks in buf[start:end]; a child list is parsed when the size's bit 31 is set."""
    end = len(buf) if end is None else end
    out = []
    pos = start
    while pos + 8 <= end:
        cid, size = struct.unpack_from('<II', buf, pos)
        has_children = bool(size & 0x80000000)
        size &= 0x7FFFFFFF
        body = pos + 8
        if body + size > end:
            raise ValueError('chunk 0x%x at %d overruns its parent' % (cid, pos))
        if has_children:
            out.append(Chunk(cid, parse(buf, body, body + size)))
        else:
            out.append(Chunk(cid, None, bytes(buf[body:body + size])))
        pos = body + size
    return out


def body_bytes(c):
    if c.children is None:
        return c.data
    return b''.join(write_one(k) for k in c.children)


def write_one(c):
    body = body_bytes(c)
    size = len(body) | (0x80000000 if c.children is not None else 0)
    return struct.pack('<II', c.id, size) + body


def write(chunks):
    return b''.join(write_one(c) for c in chunks)


def minis(data):
    """[(id, bytes)] of a data chunk that holds mini-chunks."""
    out = []
    pos = 0
    while pos + 2 <= len(data):
        mid, size = data[pos], data[pos + 1]
        out.append((mid, bytes(data[pos + 2:pos + 2 + size])))
        pos += 2 + size
    return out


def pack_minis(items):
    return b''.join(struct.pack('<BB', mid, len(d)) + d for mid, d in items)


def walk(chunks, path=()):
    """Yields (path of parent ids, chunk) depth-first."""
    for c in chunks:
        yield path, c
        if c.children is not None:
            yield from walk(c.children, path + (c.id,))


# ---- model helpers ------------------------------------------------------------------------------------------------

def bones(tree):
    """The 0x202 bone chunks in file order."""
    sk = next((c for c in tree if c.id == 0x200), None)
    return [] if sk is None else sk.find(0x202)


def bone_parent(b):
    t = b.first(0x206) or b.first(0x205)
    return struct.unpack_from('<i', t.data, 0)[0]


def set_bone_parent(b, parent):
    t = b.first(0x206) or b.first(0x205)
    t.data = struct.pack('<i', parent) + t.data[4:]


def bone_name(b):
    n = b.first(0x203)
    return n.data.split(b'\0')[0].decode('latin-1') if n else ''


def submeshes(tree):
    """[(mesh chunk, submesh data chunk 0x10000)]."""
    out = []
    for m in (c for c in tree if c.id == 0x400):
        for sd in m.find(0x10000):
            out.append((m, sd))
    return out


def collision_trees(tree):
    return [c for _, c in walk(tree) if c.id == 0x1200]


# ---- defect makers (each returns a short description of what it changed) ------------------------------------------

def drop_connections(tree):
    """No connections chunk 0x600 (audit: 5 mod files)."""
    n = len(tree)
    tree[:] = [c for c in tree if c.id != 0x600]
    if len(tree) == n:
        raise ValueError('model has no 0x600 chunk to drop')
    return 'removed the connections chunk 0x600'


def drop_mesh_info(tree):
    """A mesh without its info chunk 0x402 (audit: 2). The first mesh that has one."""
    for m in (c for c in tree if c.id == 0x400):
        if m.first(0x402) is not None:
            name = m.first(0x401).data.split(b'\0')[0].decode('latin-1') if m.first(0x401) else '?'
            m.children = [k for k in m.children if k.id != 0x402]
            return 'removed info chunk 0x402 of mesh %s' % name
    raise ValueError('no mesh with an info chunk')


def bad_collision_child(tree):
    """A collision-tree inner node whose child index points past the node list (audit: 8)."""
    for col in collision_trees(tree):
        nodes = col.first(0x1202)
        if nodes is None or len(nodes.data) < 10:
            continue
        data = bytearray(nodes.data)
        count = len(data) // 10
        for i in range(count):
            n_tris, index = struct.unpack_from('<HH', data, i * 10 + 6)
            if n_tris == 0:  # inner node: index = first child
                struct.pack_into('<H', data, i * 10 + 8, count + 5)
                nodes.data = bytes(data)
                return 'collision node %d of %d: child index set to %d' % (i, count, count + 5)
    raise ValueError('no collision tree with an inner node')


def bad_skin_index(tree):
    """One skinned vertex whose bone index is outside its submesh's bone map (audit: 1)."""
    for _, sd in submeshes(tree):
        bmap = sd.first(0x10006)
        vb = sd.first(0x10007) or sd.first(0x10005)
        info = sd.first(0x10001)
        if bmap is None or vb is None or info is None or not bmap.data:
            continue
        nverts = struct.unpack_from('<I', info.data, 0)[0]
        if not nverts:
            continue
        stride = len(vb.data) // nverts
        off = 112 if stride == 144 else 96 if stride == 128 else None
        if off is None:
            continue
        data = bytearray(vb.data)
        nmap = len(bmap.data) // 4
        struct.pack_into('<I', data, off, nmap + 7)  # vertex 0, influence 0
        vb.data = bytes(data)
        return 'vertex 0 bone index set to %d (bone map holds %d)' % (nmap + 7, nmap)
    raise ValueError('no skinned submesh with a bone map')


def overrun_chunk(tree):
    """Returns the file bytes with the last data chunk of the last mesh claiming 4096 bytes more than its parent
    holds (synthetic: a chunk that overruns its parent inside the model). The tree is not changed."""
    buf = bytearray(write(tree))
    target = None
    pos = 0
    while pos + 8 <= len(buf):
        cid, size = struct.unpack_from('<II', buf, pos)
        size &= 0x7FFFFFFF
        if cid == 0x400:
            target = (pos, pos + 8 + size)
        pos += 8 + size
    if target is None:
        raise ValueError('no mesh chunk')

    def leaves(a, b):
        p = a
        while p + 8 <= b:
            cid, size = struct.unpack_from('<II', buf, p)
            kids = bool(size & 0x80000000)
            size &= 0x7FFFFFFF
            if kids:
                yield from leaves(p + 8, p + 8 + size)
            else:
                yield p, cid, size
            p += 8 + size
    last = None
    for last in leaves(target[0] + 8, target[1]):
        pass
    if last is None:
        raise ValueError('mesh without data chunks')
    p, cid, size = last
    struct.pack_into('<I', buf, p + 4, size + 0x1000)
    return bytes(buf), 'chunk 0x%x at %d: size %d -> %d (overruns its mesh)' % (cid, p, size, size + 0x1000)


def junk_trailer(tree):
    """Returns the file bytes of the intact model followed by ten empty 0x0 chunks and one chunk whose size runs far
    past the end of the file: the shape of Thrawn's Revenge Defiler.alo (audit sec. 3 'chunk overruns parent': the
    model is complete, a tool appended junk after the connections chunk)."""
    body = write(tree)
    tail = b''.join(struct.pack('<II', 0, 0) for _ in range(10))
    tail += struct.pack('<II', 0x1000000, 21626881) + bytes(range(256)) * 2
    return body + tail, 'appended 10 empty chunks and chunk 0x1000000 claiming 21626881 bytes (%d present)' % 512


def spill_mesh(tree):
    """One mesh chunk 0x400 written as an EMPTY data chunk, its children following at top level: the shape of
    Thrawn's Revenge passive_z-95_headhunter.alo (audit 'mesh without info chunk'). Picks the smallest mesh that is not
    the first one."""
    meshes = [i for i, c in enumerate(tree) if c.id == 0x400]
    if len(meshes) < 2:
        raise ValueError('need two meshes')
    i = min(meshes[1:], key=lambda k: len(write_one(tree[k])))
    m = tree[i]
    name = m.first(0x401).data.split(b'\0')[0].decode('latin-1') if m.first(0x401) else '?'
    tree[i:i + 1] = [Chunk(0x400, None, b'')] + list(m.children)
    return 'mesh %s (top-level chunk %d) written as an empty 0x400 with its %d children spilled to top level' % (
        name, i, len(m.children))


def bone_order(tree):
    """Moves one non-root bone that has a child to the end of the skeleton, so a child comes before its parent; every
    bone reference (parents, connections 0x602 mini 3, proxies 0x603 mini 6, skin bone maps 0x10006) is remapped, so
    the hierarchy is the same, only the order differs (design sec. 10.2: 'bone parent not before child')."""
    bl = bones(tree)
    n = len(bl)
    parents = [bone_parent(b) for b in bl]
    mover = next((i for i in range(1, n) if parents[i] >= 0 and any(p == i for p in parents)), None)
    if mover is None:
        raise ValueError('no non-root bone with a child')
    order = [i for i in range(n) if i != mover] + [mover]  # new index -> old index
    new_of = {old: new for new, old in enumerate(order)}
    sk = next(c for c in tree if c.id == 0x200)
    others = [c for c in sk.children if c.id != 0x202]
    for b in bl:
        p = bone_parent(b)
        set_bone_parent(b, new_of[p] if p >= 0 else p)
    sk.children = others + [bl[old] for old in order]
    for _, c in walk(tree):
        if c.id == 0x602 or c.id == 0x603:
            key = 3 if c.id == 0x602 else 6
            items = minis(c.data)
            items = [(mid, struct.pack('<I', new_of[struct.unpack('<I', d)[0]]) if mid == key and len(d) == 4 and
                      struct.unpack('<I', d)[0] in new_of else d) for mid, d in items]
            c.data = pack_minis(items)
        elif c.id == 0x10006:
            vals = struct.unpack('<%dI' % (len(c.data) // 4), c.data)
            c.data = struct.pack('<%dI' % len(vals), *[new_of.get(v, v) for v in vals])
    child = next(i for i in range(n) if parents[i] == mover)
    return 'bone %d (%s) moved to %d; its child %s is now %d, before it' % (
        mover, bone_name(bl[mover]), n - 1, bone_name(bl[child]), new_of[child])


# ---- animation ---------------------------------------------------------------------------------------------------

def ala_info(tree):
    root = tree[0]
    info = root.first(0x1001)
    vals = {}
    for mid, d in minis(info.data):
        if len(d) == 4:
            vals[mid] = struct.unpack('<I', d)[0]
    return {'frames': vals.get(1, 0), 'bones': vals.get(3, 0), 'rot_block': vals.get(0x0B), 'trans_block':
            vals.get(0x0C), 'scale_block': vals.get(0x0D),
            'rot_data': len(root.first(0x1009).data) // 2 if root.first(0x1009) else 0,
            'trans_data': len(root.first(0x100A).data) // 2 if root.first(0x100A) else 0}


def ala_rotation_short(tree, keep=0.5):
    """Rotation block data holds fewer frames than declared (the frames after `keep` are cut)."""
    root = tree[0]
    rot = root.first(0x1009)
    i = ala_info(tree)
    if rot is None or not i['rot_block']:
        raise ValueError('not a block-layout animation with rotation data')
    frames = max(1, int(i['frames'] * keep))
    rot.data = rot.data[:frames * i['rot_block'] * 2]
    return 'rotation data cut to %d of %d frames (%d of %d int16)' % (frames, i['frames'], frames * i['rot_block'],
                                                                     i['frames'] * i['rot_block'])


def ala_rotation_long(tree, extra_frames=3):
    """Rotation block data holds more than declared (trailing frames repeated)."""
    root = tree[0]
    rot = root.first(0x1009)
    i = ala_info(tree)
    if rot is None or not i['rot_block']:
        raise ValueError('not a block-layout animation with rotation data')
    frame = rot.data[-i['rot_block'] * 2:]
    rot.data = rot.data + frame * extra_frames
    return 'rotation data grown by %d frames' % extra_frames


def _set_info_minis(tree, values):
    info = tree[0].first(0x1001)
    items = [(mid, values.get(mid, d)) for mid, d in minis(info.data)]
    info.data = pack_minis(items)


def ala_stride0(tree):
    """The rotation block size declared 0 while the rotation data and the bones' rotation indices stay (the shape of
    Thrawn's Revenge Prop_Venator_Idle_00.ALA: info 0x0B = 0, 0x1009 holds frames x 4 int16)."""
    i = ala_info(tree)
    if not i['rot_block'] or not i['rot_data']:
        raise ValueError('not a block-layout animation with rotation data')
    _set_info_minis(tree, {0x0B: struct.pack('<I', 0)})
    return 'rotation block size %d -> 0, %d int16 of rotation data kept' % (i['rot_block'], i['rot_data'])


def ala_garbage_blocks(tree):
    """Block sizes that are garbage while no bone indexes a block and no block data exists (the shape of Thrawn's
    Revenge RobedVibroFighter_*.ALA: info 0x0B-0x0D hold float bit patterns, no 0x1009-0x100B, every bone index
    0xFFFF): every track is the bone's default value."""
    root = tree[0]
    root.children = [c for c in root.children if c.id not in (0x1009, 0x100A, 0x100B)]
    _set_info_minis(tree, {0x0B: bytes.fromhex('e08090a7'), 0x0C: bytes.fromhex('94600f25'),
                           0x0D: bytes.fromhex('a860ac3d')})
    n = 0
    for b in root.find(0x1002):
        for c in b.find(0x1003):
            c.data = pack_minis([(mid, b'\xff\xff' if mid in (0x0E, 0x0F, 0x10) and len(d) == 2 else d)
                                 for mid, d in minis(c.data)])
            n += 1
    return 'block data removed, %d bones set to no track, block sizes set to garbage' % n


# ---- particles ---------------------------------------------------------------------------------------------------

def particle_property_size(tree, prop=0x09):
    """One emitter property mini-chunk one u32 longer than its type (design sec. 10.2: 'particle spawn-shape / track
    size mismatch'; formats: 'emitter property X has size N'). Every emitter of the system gets it."""
    if not tree or tree[0].id != 0x900:
        raise ValueError('not a particle system')
    done = 0
    for _, c in walk(tree):
        if c.id == 0x700:
            for p in c.find(0x02):
                items = minis(p.data)
                new = []
                for mid, d in items:
                    if mid == prop and len(d) == 4:
                        d = d + b'\0\0\0\0'
                        done += 1
                    new.append((mid, d))
                p.data = pack_minis(new)
    if not done:
        raise ValueError('no emitter has property 0x%x' % prop)
    return 'emitter property 0x%02x grown from 4 to 8 bytes in %d emitter(s)' % (prop, done)
