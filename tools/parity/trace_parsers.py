"""trace_parsers.py - the parsers of the Q4 parity harness (ref3 t02, 2026-11-19): the original game's Q2 state traces
(Q2TRACE 1 reader, metrics, production queue, Lua probe output) and the port's outputs (ALAMO_CRC 1 log, --ai-log).

Until ref3 they lived in the Q2 probe kit (tools/probe_mod/q2_traces/analysis/q2_parity.py and q2trace.py) and
parity_harness.py reached into the kit for them. Now this module is the seam: parity_harness.py, story_battle.py,
tools/analysis/a2g_port_vs_retail.py and the kit (q2_parity.py's comparison, q2trace.py) import it. Standard library only.

Metric dicts: empty_metrics() names every key; trace_metrics / queue_metrics / lua_metrics fill the retail side,
port_metrics the port side, both keyed by tick = mode frame (q2_parity.py's docstring has the field list).
"""
import collections
import math
import os
import re
import struct

LCG_MUL, LCG_ADD = 0x41C64E6D, 0xBDF
MASK64 = (1 << 64) - 1
P1, P2, P5 = 0x9E3779B185EBCA87, 0xC2B2AE3D27D4EB4F, 0x27D4EB2F165667C5
SAMPLE_EVERY = 1800  # ticks between planets-owned / object-count samples (the port's "# t=" cadence, 60 s)
ORBIT_SNAP = 1e-3    # position change below this is "not moving"
NOT_PRODUCTION = ("GALACTIC_FLEET", "PARTICLES", "ART_MODEL", "GALACTIC_SQUADRON", "EXPLOSION")

# Unit-name tokens -> faction, to name the trace's player indices (the trace has no faction names).
FACTION_TOKENS = {
    "Empire": ("STORMTROOPER", "IMPERIAL", "TIE_", "E_GROUND", "EMPIRE_", "STAR_DESTROYER", "AT_ST", "AT_AT", "VEERS",
               "TARTAN", "VICTORY_DESTROYER", "ACCUSER", "SLAVE_I", "BOBA_FETT"),
    "Rebel": ("REBEL", "R_GROUND", "X-WING", "XWING", "Y-WING", "ALLIANCE", "CORELLIAN_", "MARAUDER", "T2B", "HAN_SOLO",
              "CHEWBACCA", "OBI_WAN", "MILLENNIUM", "GALLOFREE", "PLEX"),
    "Underworld": ("UNDERWORLD", "U_GROUND", "STARVIPER", "SKIPRAY", "VENGEANCE", "KEDALBE", "INTERCEPTOR4", "CRUSADER",
                   "DESTROYER_DROID", "MERC"),
}


# ---- hashing (same as q2trace.py / engine/core StateHasher) -------------------------------------------------
def _round(h, w):
    h = (h + w * P2) & MASK64
    h = ((h << 31) | (h >> 33)) & MASK64
    return (h * P1) & MASK64


def _final(h, n):
    h ^= n
    h ^= h >> 33
    h = (h * 0xFF51AFD7ED558CCD) & MASK64
    h ^= h >> 33
    h = (h * 0xC4CEB9FE1A85EC53) & MASK64
    h ^= h >> 33
    return h


def rng_digest(state, draws):
    """StateHasher digest of SimRng::hash (u32 state, u64 draw count): engine/core sim_rng.cpp."""
    b = struct.pack("<IQ", state & 0xFFFFFFFF, draws & MASK64)
    h = _round(P5, int.from_bytes(b[:8], "little"))
    h = _round(h, int.from_bytes(b[8:].ljust(8, b"\0"), "little"))
    return _final(h, 12)


def lcg(s):
    return (s * LCG_MUL + LCG_ADD) & 0xFFFFFFFF


def lcg_steps(a, b, limit):
    s = a
    for i in range(limit + 1):
        if s == b:
            return i
        s = lcg(s)
    return None


def f32(h):
    return struct.unpack("<f", struct.pack("<I", int(h, 16)))[0]


def faction_of_types(names):
    score = collections.Counter()
    for n in names:
        for fac, toks in FACTION_TOKENS.items():
            if any(t in n for t in toks):
                score[fac] += 1
    return score.most_common(1)[0][0] if score else None


def parse_map(text):
    out = {}
    for part in (text or "").split(","):
        if "=" in part:
            k, v = part.split("=", 1)
            out[int(k)] = v.strip()
    return out


def empty_metrics(source):
    return {"source": source, "scenario": {}, "factions": {}, "rng_draws": None, "rng_pause_draws": None,
            "fiscal_cycles": None, "owners_initial": None, "owner_changes": None, "planets_owned": None,
            "fleet_moves": None, "fleet_hops": None, "production": None, "object_counts": None, "credits": None,
            "credits_final": None, "perception": None, "lua_owners": None, "conflicts": None, "notes": []}


# ---- trace side ------------------------------------------------------------------------------------------------
def trace_metrics(path, factions_override=None, max_tick=None):
    """Metrics of a Q2TRACE 1 file (the original game, or a port run written in the same format)."""
    m = empty_metrics(os.path.abspath(path))
    types, meta, header = {}, {}, {}
    with open(path, "r", encoding="ascii") as fh:
        for line in fh:
            if line.startswith("T "):
                p = line.split()
                types[int(p[1])] = p[4]
    objs = {}          # id -> [owner, type_name, x, y]
    seen = set()
    planets = set()
    players = {}       # index -> credits
    draws, pause_draws = {}, []
    cycles, cycle_info, cycle_windows, cycle_prev_m = [], None, [], 0
    prev_rng, prev_m = None, None
    last_cycle = None
    fleet_state = {}   # id -> (parked_pos, moving_since, from_planet)
    hops = []
    production = []
    owner_changes = []
    owners_now = {}
    owners_initial = None
    planets_owned, object_counts, credits = {}, {}, {}
    first_obj_tick = None
    new_objs = []
    planet_names, credit_samples, player_flags = {}, {}, {}
    mframe = 0
    in_header = True
    pending_sample = None

    def planet_pos():
        return {objs[i][1]: (objs[i][2], objs[i][3]) for i in planets if i in objs}

    def nearest(x, y, pp):
        best = min(((math.hypot(x - a, y - b), n) for n, (a, b) in pp.items()), default=(None, None))
        return best[1]

    def faction(idx):
        return m["factions"].get(idx, "p%d" % idx)

    def planet_of(i):
        for _ in range(6):  # object -> squadron -> fleet -> planet
            o = objs.get(i)
            if o is None:
                return None
            if i in planets:
                return o[1]
            i = o[4]
        return None

    def end_of_frame(tick):
        nonlocal owners_initial
        if not objs:
            return
        # New objects of this tick, grouped into one production batch per (owner, planet).
        batch = {}
        for tk, i in new_objs:
            o = objs.get(i)
            if o is not None:
                batch.setdefault((o[0], planet_of(i)), collections.Counter())[o[1]] += 1
        for (own, at), cnt in sorted(batch.items(), key=lambda kv: (kv[0][0], str(kv[0][1]))):
            production.append((tick, own, at, dict(sorted(cnt.items()))))
        new_objs.clear()
        pp = planet_pos()
        own = {objs[i][1]: objs[i][0] for i in planets if i in objs}
        # Initial owners come from the first frame that has planets (a run sampled from tick 0 has only the galaxy art
        # model at tick 0; taking that empty map turned every tick-1 owner into a "change").
        if owners_initial is None and own:
            owners_initial = dict(own)
            owners_now.update(own)
        for name, o in sorted(own.items()):
            # q4hf t03: a run whose tick-0 sample holds one planet only (20261108-204711: CORELLIA with a placeholder
            # owner) gets the rest at tick 1. A planet's first sight, and the setup on tick 1, are initial owners.
            if owners_initial is not None and (name not in owners_now or tick <= 1):
                owners_initial[name] = o
                owners_now[name] = o
            if owners_now.get(name) != o:
                owner_changes.append((tick, name, owners_now.get(name), o))
                owners_now[name] = o
        # Fleet hops: parked -> moving -> parked somewhere else.
        live = set()
        for i, o in objs.items():
            if o[1] != "GALACTIC_FLEET":
                continue
            live.add(i)
            st = fleet_state.get(i)
            pos = (o[2], o[3])
            if st is None:
                fleet_state[i] = [pos, None, nearest(pos[0], pos[1], pp), None]
                continue
            moved = math.hypot(pos[0] - st[0][0], pos[1] - st[0][1]) > ORBIT_SNAP
            if moved and st[1] is None:
                st[1] = tick  # first tick of the hop (departure)
            elif not moved and st[1] is not None:
                to = nearest(pos[0], pos[1], pp)
                if to != st[2]:
                    hops.append((st[1], o[0], st[2], to, tick, i))
                st[1] = None
                st[2] = to
            st[0] = pos
        for i in [k for k in fleet_state if k not in live]:
            del fleet_state[i]
        for i in planets:
            if i in objs:
                planet_names[i] = objs[i][1]
        if players and (not credit_samples or tick % SAMPLE_EVERY == 0) and tick not in credit_samples:
            credit_samples[tick] = dict(sorted(players.items()))
        if tick % SAMPLE_EVERY == 0 and tick not in planets_owned:
            cnt = collections.Counter(own.values())
            planets_owned[tick] = dict(cnt)
            oc = collections.Counter((o[0], o[1]) for o in objs.values())
            object_counts[tick] = dict(oc)

    with open(path, "r", encoding="ascii") as fh:
        if fh.readline().strip() != "Q2TRACE 1":
            raise ValueError("%s: not a Q2TRACE 1 file" % path)
        for line in fh:
            p = line.split()
            if not p:
                continue
            t = p[0]
            if in_header:
                if t == "meta":
                    meta[p[1]] = " ".join(p[2:])
                elif t == "end_header":
                    in_header = False
                else:
                    header[t] = p[1:]
                continue
            if t == "F":
                g, mf, rng = int(p[1]), int(p[3]), int(p[4], 16)
                if pending_sample is not None:
                    end_of_frame(pending_sample)
                if max_tick is not None and mf > max_tick:
                    pending_sample = None
                    break
                if prev_rng is not None:
                    n = lcg_steps(prev_rng, rng, 2_000_000)
                    if mf == prev_m:
                        if n:
                            pause_draws.append((g, mf, n))
                    else:
                        draws[(prev_m + 1, mf)] = n
                prev_rng, prev_m = rng, mf
                mframe = mf
                if len(p) >= 11:
                    cyc = int(p[10])
                    if cycle_info is None:
                        cycle_info = {"first_last_cycle_frame": int(p[8]), "first_next_cycle_frame": int(p[9]),
                                      "first_cycle": cyc}
                    if last_cycle is not None and cyc != last_cycle:
                        cycles.append(mf)
                        cycle_windows.append((cycle_prev_m + 1, mf))
                        if players:
                            credits[mf] = {k: v for k, v in sorted(players.items())}
                    last_cycle = cyc
                    cycle_prev_m = mf
                pending_sample = mf
            elif t == "K":
                pass  # keyframe lines overwrite the state
            elif t == "O":
                i = int(p[1])
                ent = [int(p[2]), types.get(int(p[3]), "?%s" % p[3]), f32(p[4]), f32(p[5]), int(p[13])]
                if int(p[11]) & 2:  # dead
                    objs.pop(i, None)
                    continue
                objs[i] = ent
                if i not in seen:
                    seen.add(i)
                    # The start tick is that of the first object that is not art / fleet / particles: a run sampled
                    # from tick 0 sees the galaxy art model a tick before the starting forces, which then all looked
                    # like production.
                    if any(x in ent[1] for x in NOT_PRODUCTION):
                        pass
                    elif first_obj_tick is None:
                        first_obj_tick = mframe
                    elif mframe > first_obj_tick:
                        new_objs.append((mframe, i))
            elif t == "X":
                objs.pop(int(p[1]), None)
            elif t == "G":
                planets.add(int(p[1]))
            elif t == "C":
                players[int(p[1])] = f32(p[2])
                player_flags[int(p[1])] = (int(p[4]), int(p[5])) if len(p) >= 6 else (0, 0)
            elif t == "end":
                break
        if pending_sample is not None:
            end_of_frame(pending_sample)
    # Factions of the trace players (from what they own at the start).
    if factions_override:
        m["factions"] = dict(factions_override)
    else:
        # Objects of the first sample that names at least two factions (rebuilding from 'seen' is not possible, use
        # the counts). A run sampled from tick 0 (20261108-170824) has a tick-0 sample holding only the galaxy art
        # model, which names nobody; the first sample with objects then left every player unnamed.
        first = None
        for tk in sorted(object_counts):
            by_owner = collections.defaultdict(list)
            for (own, name), n in object_counts[tk].items():
                by_owner[own].extend([name] * n)
            named = {}
            for own, names in by_owner.items():
                f = faction_of_types(names)
                if f and f not in named.values():
                    named[own] = f
            if len(named) >= 2:
                first = tk
                m["factions"].update(named)
                break
    fac = faction
    m["scenario"] = {"campaign": meta.get("campaign") or meta.get("skirmish") or meta.get("label"),
                     "seed": header.get("seed", [None])[0], "ticks": mframe, "first_tick_with_objects": first_obj_tick,
                     "mod": meta.get("mod"), "lua_probe": meta.get("lua_probe"), "scenario": meta.get("scenario"),
                     "label": meta.get("label")}
    m["rng_draws"] = [(a, b, n) for (a, b), n in sorted(draws.items())]
    m["rng_pause_draws"] = pause_draws
    m["fiscal_cycles"] = {"cycle_ticks": cycles, "cycle_windows": cycle_windows, **cycle_info} if cycle_info else None
    if owners_initial is not None:
        m["owners_initial"] = {k: fac(v) for k, v in sorted(owners_initial.items())}
        m["owner_changes"] = [(tk, n, fac(a) if a is not None else None, fac(b)) for tk, n, a, b in owner_changes]
        m["planets_owned"] = {tk: {fac(k): v for k, v in sorted(d.items())} for tk, d in sorted(planets_owned.items())}
        m["fleet_hops"] = [(d, fac(o), a, b, arr) for d, o, a, b, arr, _ in hops]
        m["fleet_moves"] = [(d, fac(o), b) for d, o, a, b, arr, _ in hops]
    # C lines carry the PlayerList index. In galactic it equals the object owner id; in skirmish it does not (the human
    # is list index 0, its objects owner 3), so there the human / AI flags and the skirmish meta name the factions.
    cmap = {}
    sk = meta.get("skirmish") or ""
    hm, am = re.search(r"human (\w+)", sk), re.search(r"AI (\w+)", sk)
    if hm and am:
        ais = [k for k, (h, a) in player_flags.items() if a]
        for k, (h, a) in player_flags.items():
            if h:
                cmap[k] = hm.group(1)
        if len(ais) == 1:
            cmap[ais[0]] = am.group(1)

    def cfac(k):
        return cmap.get(k, "slot%d" % k) if cmap else fac(k)
    m["production"] = [(tk, fac(o), at, cnt) for tk, o, at, cnt in production]
    m["production_source"] = "new objects"
    m["object_counts"] = {tk: {"%s:%s" % (fac(o), n): c for (o, n), c in sorted(d.items())}
                          for tk, d in sorted(object_counts.items())} or None
    m["planet_names"] = dict(planet_names)
    m["type_names"] = types
    if credit_samples:
        m["credits_series"] = {tk: {cfac(k): v for k, v in d.items()} for tk, d in sorted(credit_samples.items())}
    if credits:
        m["credits"] = {tk: {cfac(k): v for k, v in d.items()} for tk, d in credits.items()}
    if players:
        m["credits_final"] = {cfac(k): v for k, v in sorted(players.items())}
    return m


def queue_metrics(path, m, max_tick=None):
    """Production queues from the reader's production.q2q ("Q2QUEUES 1"): one entry per queued item, keyed by
    (planet, queue, type, player, start frame, tag). Replaces the new-object production of the trace with queue starts,
    so it lines up with the port's build orders (same type names), and adds build frames / cost / completion."""
    f = m["factions"]
    names, pnames = m.get("type_names") or {}, m.get("planet_names") or {}
    entries, live = [], {}
    with open(path, "r", encoding="ascii") as fh:
        if fh.readline().strip() != "Q2QUEUES 1":
            raise ValueError("%s: not Q2QUEUES 1" % path)
        for line in fh:
            p = line.split()
            if not p or p[0] != "Q":
                continue
            mf = int(p[2])
            if max_tick is not None and mf > max_tick:
                break
            planet, queue = int(p[3]), int(p[4])
            for e in p[6:]:
                q = e.split(":")
                if len(q) < 10:
                    continue
                # One queued item = (planet, queue, type, player, tag) while it stays listed. Its start frame can
                # move (a stalled item restarts every tick), so the start is not part of the key; a gap longer than a
                # few missed samples starts a new item.
                key = (planet, queue, int(q[0]), int(q[1]), q[7])
                ent = live.get(key)
                if ent is not None and mf - ent["last"] > 12:
                    ent = None
                if ent is None:
                    ent = live[key] = {"seen": mf, "start": int(q[2]), "planet": pnames.get(planet, str(planet)),
                                       "queue": "space" if queue else "ground", "type": names.get(int(q[0]), q[0]),
                                       "player": int(q[1]), "frames": int(q[3]), "seconds": f32(q[4]),
                                       "cost": f32(q[6]), "created": None, "last": mf, "restarts": 0, "last_start": int(q[2])}
                    entries.append(ent)
                elif int(q[2]) != ent["last_start"]:
                    ent["restarts"] += 1
                    ent["last_start"] = int(q[2])
                ent["last"] = mf
                if ent["created"] is None and int(q[9]) != 0x3FFFFF:
                    ent["created"] = mf
    out = sorted(entries, key=lambda e: (e["start"], e["player"], e["planet"], e["type"]))
    for e in out:
        e["faction"] = f.get(e["player"], "p%d" % e["player"])
        e["done"] = e["created"] if e["created"] is not None else e["last"] + 1
    m["queue_entries"] = out
    m["production"] = [(e["start"], e["faction"], e["planet"], {e["type"]: 1}) for e in out]
    m["production_source"] = "queue starts"
    return m


def lua_metrics(paths, m):
    """Adds the Lua probe output (Q2LUA 1, tools/probe_mod/q2_traces/mod: CYCLE / PLAYER / PLANET / PERCEPTION lines)."""
    credits, owners, perception = {}, {}, {}
    for path in paths:
        cycle = None
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            for line in fh:
                p = line.split()
                if not p:
                    continue
                if p[0] == "CYCLE" and len(p) >= 2:
                    cycle = int(p[1])
                elif p[0] == "PLAYER" and len(p) >= 4 and cycle is not None:
                    try:
                        credits.setdefault(cycle, {})[p[1]] = float(p[3])
                    except ValueError:
                        credits.setdefault(cycle, {})[p[1]] = None
                elif p[0] == "PLANET" and len(p) >= 4 and cycle is not None:
                    owners.setdefault(cycle, {})[p[1].upper()] = p[3]
                elif p[0] == "PERCEPTION" and len(p) >= 3 and cycle is not None:
                    for kv in p[3:]:
                        if "=" in kv:
                            k, v = kv.split("=", 1)
                            try:
                                val = float(v)
                            except ValueError:
                                val = v
                            perception.setdefault(cycle, {})["%s|%s|%s" % (p[1], p[2].upper(), k)] = val
    if credits:
        m["lua_credits"] = credits
    if owners:
        m["lua_owners"] = owners
    if perception:
        m["perception"] = perception
    return m


# ---- port side -----------------------------------------------------------------------------------------------
def read_crc_v1(path):
    """An ALAMO_CRC 1 log (alamo_headless --crc-out, q2trace.py to-q1): (systems, parity systems, records), records a list
    of (tick, combined digest, parity, [system digests]) in file order. The one CRC v1 reader of the tools."""
    systems, parity, recs = [], [], []
    with open(path, "r", encoding="ascii") as fh:
        if fh.readline().strip() != "ALAMO_CRC 1":
            raise ValueError("%s: not ALAMO_CRC 1" % path)
        for line in fh:
            p = line.split()
            if not p or p[0] == "#":
                continue
            if p[0] == "systems":
                systems = p[2:]
            elif p[0] == "parity":
                parity = p[2:]
            elif p[0][0].isdigit():
                recs.append((int(p[0]), int(p[1], 16), int(p[2], 16), [int(x, 16) for x in p[3:]]))
    return systems, parity, recs


def read_crc_columns(path):
    """(systems, [(tick, [system digests])]) of an ALAMO_CRC 1 log (read_crc_v1 without the combined / parity columns)."""
    systems, _, recs = read_crc_v1(path)
    return systems, [(tick, cols) for tick, _, _, cols in recs]


def port_tactical_start(crc_path):
    """First CRC record with a tactical mode on the stack (the `spatial` digest hashes `top_tactical() != nullptr`
    first, so it leaves its galactic-only value when the battle is pushed). None if the run never goes tactical."""
    systems, recs = read_crc_columns(crc_path)
    if "spatial" not in systems or not recs:
        return None
    k = systems.index("spatial")
    base = recs[0][1][k]
    for tick, cols in recs:
        if cols[k] != base:
            return tick
    return None


def port_rng_draws(crc_path, seed=0, limit=500_000, offset=0):
    """Per-tick synced-RNG draws from the port CRC log's `rng` system digest (state, draw count). `offset` = the record
    that becomes mode frame 1 (a battle pushed mid-run); earlier records are summed into tick 1's count."""
    systems, recs = read_crc_columns(crc_path)
    if "rng" not in systems:
        return None, "port CRC log has no rng system"
    k_rng = systems.index("rng")
    s, d = seed & 0xFFFFFFFF, 0
    out = []
    for tick, cols in recs:
        want = cols[k_rng]
        ss, k = s, 0
        while k <= limit and rng_digest(ss, d + k) != want:
            ss = lcg(ss)
            k += 1
        if k > limit:
            return out, "rng digest of record %d not reached within %d draws (seed %d)" % (tick, limit, seed)
        out.append((tick + 1, tick + 1, k))  # record t = after tick t -> mode frame t + 1
        s, d = ss, d + k
    if offset:
        pre = sum(n for a, _, n in out if a <= offset)
        out = [(offset + 1 - offset, offset + 1 - offset, pre + next((n for a, _, n in out if a == offset + 1), 0))] + [
            (a - offset, a - offset, n) for a, _, n in out if a > offset + 1]
    return out, None


AI_LINE = re.compile(r"^(\d+) p(\d+) (.*)$")
PLAN_LINE = re.compile(r"^plan (\d+) (\S+) (for|ended) ?(.*)$")


def ai_log_records(path):
    """(frame, player, text) of every `<frame> p<player> <text>` line of an alamo_headless --ai-log file."""
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            m = AI_LINE.match(line.strip())
            if m:
                yield int(m.group(1)), int(m.group(2)), m.group(3)


def ai_log_plans(path):
    """(frame, player, plan id, plan name, 'for' | 'ended', rest) of the AI log's `plan N NAME for|ended ...` lines."""
    for frame, player, text in ai_log_records(path):
        m = PLAN_LINE.match(text)
        if m:
            yield frame, player, int(m.group(1)), m.group(2), m.group(3), m.group(4)


def port_metrics(crc_path=None, ai_log=None, port_trace=None, factions_override=None, seed=0, max_tick=None, tactical=False):
    if port_trace:
        m = trace_metrics(port_trace, factions_override, max_tick)
        m["source"] = os.path.abspath(port_trace)
    else:
        m = empty_metrics(os.path.abspath(crc_path or ai_log or "?"))
    if crc_path and m["rng_draws"] is None:
        offset = 0
        if tactical:
            start = port_tactical_start(crc_path)
            if start is None:
                m["notes"].append("port run never pushed a tactical mode; RNG ticks are the run's own")
            else:
                offset = start
                m["notes"].append("port battle starts at CRC record %d: port tick 1 = that record; the draws of the %d "
                                  "records before it (galaxy, fleet hop, battle setup) are counted into tick 1" % (start, start))
                m["scenario"]["tactical_start_record"] = start
        draws, err = port_rng_draws(crc_path, seed, offset=offset)
        m["rng_draws"] = [x for x in draws if max_tick is None or x[1] <= max_tick] if draws else draws
        m["rng_pause_draws"] = []
        if err:
            m["notes"].append(err)
    if not ai_log:
        return m
    fac_template, human_idx, human_fac = {}, None, None
    owner_changes, moves, builds, owned, conflicts = [], [], [], {}, []
    summary = None
    with open(ai_log, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if line.startswith("# galactic-ai campaign"):
                p = line.split()
                if "campaign" in p:
                    m["scenario"]["campaign"] = p[p.index("campaign") + 1]
                if "seed" in p:
                    m["scenario"]["seed"] = p[p.index("seed") + 1]
                if "ticks" in p:
                    m["scenario"]["ticks"] = int(p[p.index("ticks") + 1])
                if "human" in p:
                    human_fac = p[p.index("human") + 1]
                continue
            if line.startswith("# galactic-ai ") and "planets:" in line:
                summary = line
                hm = re.search(r"human p(\d+)", line)
                if hm:
                    human_idx = int(hm.group(1))
                continue
            if line.startswith("# t="):
                tm = re.match(r"# t=(\d+)s planets((?: \d+=\d+)+)", line)
                if tm:
                    tick = int(tm.group(1)) * 30
                    owned[tick] = {int(a): int(b) for a, b in (kv.split("=") for kv in tm.group(2).split())}
                continue
            if line.startswith("conflict "):
                cm = re.match(r"conflict tick (\d+) gframe (\d+) (\S+) (\S+) (\S+) vs (\S+): (.*)$", line)
                if cm:
                    conflicts.append((int(cm.group(1)), cm.group(4).upper(), cm.group(5), cm.group(6), cm.group(7)))
                continue
            om = re.match(r"^(\d+) owner (\S+) (-?\d+) -> (-?\d+)$", line)
            if om:
                owner_changes.append((int(om.group(1)), om.group(2).upper(), int(om.group(3)), int(om.group(4))))
                continue
            am = AI_LINE.match(line)
            if not am:
                continue
            frame, pl, text = int(am.group(1)), int(am.group(2)), am.group(3)
            if text.startswith("templates "):
                fac_template[pl] = text.split()[1]
            elif text.startswith("order move fleet "):
                moves.append((frame, pl, text.split("-> ")[-1].upper()))
            elif text.startswith("order build "):
                w = text.split()
                builds.append((frame, pl, w[2].upper(), w[4].upper() if len(w) > 4 else None))
            elif text.startswith("human ") or " human p" in text:
                pass
    # Factions of the port players.
    facs = {}
    for pl, tmpl in fac_template.items():
        f = next((k for k in FACTION_TOKENS if k.lower() in tmpl.lower()), None)
        if f:
            facs[pl] = f
    if human_idx is not None and human_fac:
        facs[human_idx] = human_fac
    if factions_override:
        facs.update(factions_override)
    m["factions"] = facs

    def fac(i):
        return facs.get(i, "p%d" % i)

    def keep(tk):
        return max_tick is None or tk <= max_tick
    m["owner_changes"] = [(tk, n, fac(a), fac(b)) for tk, n, a, b in owner_changes if keep(tk)]
    m["planets_owned"] = {tk: {fac(k): v for k, v in sorted(d.items()) if v} for tk, d in sorted(owned.items()) if keep(tk)}
    m["fleet_moves"] = [(tk, fac(pl), to) for tk, pl, to in moves if keep(tk)]
    # Starbase levels are not sync-store objects in the original's galactic mode (the vanilla trace has no *STAR_BASE*
    # type at all), so the trace cannot see them: kept apart from the compared production.
    m["production"] = [(tk, fac(pl), at, {t: 1}) for tk, pl, t, at in builds if keep(tk) and "STAR_BASE" not in t]
    m["starbase_orders"] = [(tk, fac(pl), at, t) for tk, pl, t, at in builds if keep(tk) and "STAR_BASE" in t]
    m["production_orders"] = True
    m["conflicts"] = [c for c in conflicts if keep(c[0])]
    if summary:
        pm = re.search(r"planets:((?: \d+=\d+/-?[\d.]+)+)", summary)
        if pm:
            m["credits_final"] = {fac(int(a)): float(c) for a, b, c in
                                  (re.match(r"(\d+)=(\d+)/(-?[\d.]+)", kv).groups() for kv in pm.group(1).split())
                                  if fac(int(a)) in FACTION_TOKENS}
        m["scenario"]["summary"] = summary[2:]
    return m


SUMMARY_CREDITS = re.compile(r"planets:((?: \d+=\d+/-?[\d.]+)+)")


def summary_credits(text):
    """{player index: credits} from an alamo_headless galactic-ai summary line ('planets: 0=4/8338 1=4/13668 ...')."""
    mm = SUMMARY_CREDITS.search(text)
    if not mm:
        return None
    return {int(a): float(c) for a, c in re.findall(r"(\d+)=\d+/(-?[\d.]+)", mm.group(1))}


# ---- Q2TRACE 1 reader (was q2trace.py) ----------------------------------------------------------------------------------------
class Frame:
    __slots__ = ("g", "mtype", "mframe", "rng", "pause", "stack", "flags", "cycle", "s", "key", "lines")

    def __init__(self, parts):
        self.g, self.mtype, self.mframe = int(parts[1]), int(parts[2]), int(parts[3])
        self.rng = int(parts[4], 16)
        self.pause, self.stack, self.flags = int(parts[5]), int(parts[6]), int(parts[7])
        self.cycle = tuple(int(x) for x in parts[8:11]) if len(parts) >= 11 else None
        self.s = None
        self.key = False
        self.lines = []


def parse(path):
    """A Q2TRACE 1 file: (meta, header, frames, types, end reason)."""
    meta, header = {}, {}
    frames, types, end_reason = [], {}, None
    cur = None
    cut = False
    with open(path, "r", encoding="ascii", newline="\n") as fh:
        first = fh.readline().rstrip("\n")
        if first != "Q2TRACE 1":
            raise ValueError("not a Q2TRACE 1 file: %r" % first)
        in_header = True
        for ln, line in enumerate(fh, 2):
            if not line.endswith("\n"):
                # storyb t09: a reader stopped mid-write (killed, PC off) leaves a cut last line. Drop it and, below,
                # the frame it belonged to, so validate / to-q1 still give a (partial) summary of everything before.
                cut = True
                break
            line = line.rstrip("\n")
            if not line:
                continue
            p = line.split(" ")
            tag = p[0]
            if in_header:
                if tag == "meta":
                    meta[p[1]] = " ".join(p[2:])
                elif tag == "end_header":
                    in_header = False
                else:
                    header[tag] = p[1:]
                continue
            if tag == "F":
                cur = Frame(p)
                frames.append(cur)
            elif tag == "S":
                cur.s = p[1:]
            elif tag == "K":
                cur.key = True
            elif tag == "M":
                meta[p[1]] = " ".join(p[2:])
            elif tag in ("O", "X", "G", "C", "P"):
                cur.lines.append(p)
            elif tag == "start":
                header["start"] = p[1:]
            elif tag == "T":
                types[int(p[1])] = (int(p[2], 16), int(p[3], 16), " ".join(p[4:]))
            elif tag == "E":
                end_reason = " ".join(p[1:])
            elif tag == "end":
                break
            else:
                raise ValueError("line %d: unknown tag %r" % (ln, tag))
    if cut and end_reason is None:
        if frames:
            frames.pop()  # its object lines may be incomplete
        meta["truncated"] = "trace cut mid-line (reader stopped while writing); the last sampled frame was dropped"
    return meta, header, frames, types, end_reason
