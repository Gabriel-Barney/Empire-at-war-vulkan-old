"""Q2 parity: per-metric comparison of a Q2 reference trace (original game) with a port run of the same scenario.

Standard library only. Used by `q2trace.py compare` (tools/probe_mod/q2_traces/q2trace.py); can also run alone:

    toolchain\\venv\\Scripts\\python.exe -I tools\\probe_mod\\q2_traces\\analysis\\q2_parity.py
        <trace.q2t | trace run dir> --port-crc <port.crc> [--port-ai-log <ai.txt>] [--port-trace <port.q2t>]
        [--lua <lua_*.txt>] [--port-factions 0=Rebel,1=Empire,5=Underworld] [--json out.json]

Both sides are reduced to the same metric set, keyed by **tick = mode frame** (the number of ticks of the mode done;
trace `F` field 4, the port's galactic frame after that many `tick_once()` calls; port CRC record t = tick t + 1):

  rng_draws       synced-RNG draws per tick. Trace: LCG steps between the states sampled at consecutive mode frames
                  (draws on global frames where the mode frame did not advance, i.e. galactic pauses, are reported
                  apart). Port: recovered from the `rng` system digest of the CRC log (StateHasher(u32 state, u64 draws),
                  stepped forward from the seed), so no engine change is needed. Tick 1 includes the galaxy build.
  fiscal_cycles   ticks where the fiscal-cycle counter increments (trace F fields). Port: only with --port-trace.
  owners          initial planet owners and every owner change (trace O owner of planet objects; port AI log
                  `<frame> owner <Planet> a -> b`); planets owned per faction every 1800 ticks (port `# t=` lines).
  fleet moves     trace: every GALACTIC_FLEET hop (depart tick, from, to, arrive tick) from positions; port: the AI's
                  `order move fleet <id> -> <Planet>` (the order tick, which is the departure tick).
  production      trace: objects that appear after the start (completion, by type and owner; fleets/particles left
                  out); port: the AI's `order build <Type> at <Planet>` (order tick). Compared by type sequence per
                  faction (tick offsets = build time, reported but not a divergence by themselves).
  object counts   per (faction, type) every 1800 ticks. Trace only unless --port-trace.
  credits         trace C lines (reader v2) and Lua `PLAYER <f> credits <x>` per cycle; port: final credits from the
                  AI-log summary (and per tick with --port-trace).
  perception      Lua `PERCEPTION <ai> <planet> <eq>=<v>` (trace only; the port has no output for it yet).

Factions name players on both sides (player indices differ): trace players by the unit types they own at the start,
port players by the AI template names and the `human` field of the AI log; override with --port-factions /
--trace-factions.
"""
import argparse
import collections
import json
import math
import os
import re
import sys

# The parsers (trace / queue / Lua / CRC / AI log) live in tools/parity/trace_parsers.py (ref3 t02); the names stay
# importable from here for the kit's scripts.
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "parity"))
from trace_parsers import (  # noqa: E402,F401
    AI_LINE, FACTION_TOKENS, LCG_ADD, LCG_MUL, NOT_PRODUCTION, ORBIT_SNAP, SAMPLE_EVERY, SUMMARY_CREDITS,
    empty_metrics, f32, faction_of_types, lcg, lcg_steps, lua_metrics, parse_map, port_metrics, port_rng_draws,
    port_tactical_start, queue_metrics, read_crc_columns, read_crc_v1, rng_digest, summary_credits, trace_metrics)


# ---- comparison ------------------------------------------------------------------------------------------------
class Result:
    def __init__(self, metric, status, first_tick=None, detail="", module=None):
        self.metric, self.status, self.first_tick, self.detail, self.module = metric, status, first_tick, detail, module

    def as_dict(self):
        return {"metric": self.metric, "status": self.status, "first_tick": self.first_tick, "detail": self.detail,
                "module": self.module}


def cmp_rng(ref, port):
    if ref["rng_draws"] is None or port["rng_draws"] is None:
        return Result("rng_draws", "n/a", detail="missing on the %s side" % ("trace" if ref["rng_draws"] is None else "port"))
    pd = {a: n for a, b, n in port["rng_draws"]}
    port_last = max(pd) if pd else 0
    checked = equal = 0
    first = None
    first_detail = ""
    span_bad = 0
    rows = []
    ref_total = port_total = 0
    for a, b, n in ref["rng_draws"]:
        if b > port_last or n is None:
            continue
        pn = sum(pd.get(t, 0) for t in range(a, b + 1))
        checked += 1
        ref_total += n
        port_total += pn
        if pn == n:
            equal += 1
        else:
            if a != b:
                span_bad += 1
            if first is None:
                first = a
                first_detail = "tick %s: trace %d draws, port %d" % (a if a == b else "%d..%d" % (a, b), n, pn)
        if len(rows) < 12:
            rows.append("%s:%d/%d" % (a if a == b else "%d-%d" % (a, b), n, pn))
    pause = sum(n for _, _, n in ref.get("rng_pause_draws") or [])
    hist_r = collections.Counter(n for a, b, n in ref["rng_draws"] if a == b and b <= port_last)
    hist_p = collections.Counter(n for a, b, n in port["rng_draws"])
    detail = ("%d/%d ticks equal; total draws trace %d, port %d (+%d trace draws on paused frames); "
              "first ticks (trace/port) %s; most common per-tick counts trace %s, port %s" % (
                  equal, checked, ref_total, port_total, pause, " ".join(rows),
                  hist_r.most_common(4), hist_p.most_common(4)))
    if checked == 0:
        return Result("rng_draws", "n/a", detail="no tick on both sides; " + detail)
    if first is None:
        return Result("rng_draws", "match", detail=detail)
    return Result("rng_draws", "DIVERGES", first, first_detail + "; " + detail, "engine/galactic + engine/ai (draw order)")


PORT_FIRST_CYCLE_FRAME = 0   # engine/galactic: the frame-0 cycle is bookkeeping only (GalacticSim::start, q2f t02), first payment 1350
PORT_CYCLE_PERIOD = 1350     # econ::fiscal_period_frames(45 s)


def cycle_rule_check(c, first=PORT_FIRST_CYCLE_FRAME, period=PORT_CYCLE_PERIOD):
    """The trace against the port's cycle rule (cycle on sim frame first + k*period; the mode frame seen after it is one
    more). Each trace change is a window (previous sample + 1 .. sample) because samples can be missed."""
    bad = []
    if c.get("first_next_cycle_frame") != first + period or c.get("first_last_cycle_frame") != first:
        bad.append("first sample: last %s next %s (port rule %d / %d)" % (c.get("first_last_cycle_frame"),
                                                                         c.get("first_next_cycle_frame"), first, first + period))
    for k, (lo, hi) in enumerate(c.get("cycle_windows") or [], 1):
        want = first + k * period + 1
        if not lo <= want <= hi:
            bad.append("cycle %d seen in ticks %d..%d, port rule %d" % (k + 1, lo, hi, want))
    return bad


def cmp_cycles(ref, port):
    if ref["fiscal_cycles"] and not port["fiscal_cycles"]:
        c = ref["fiscal_cycles"]
        bad = cycle_rule_check(c)
        d = ("port run has no cycle output; trace vs the port's rule (empty cycle on frame 0, then a paid one every %d): %s; trace "
             "cycle ticks %s" % (PORT_CYCLE_PERIOD, "; ".join(bad) if bad else "all %d cycles agree" % (len(c["cycle_ticks"]) + 1),
                                 c["cycle_ticks"][:12]))
        return Result("fiscal_cycles", "DIVERGES" if bad else "rule-match", None, d,
                      "engine/galactic (economy)" if bad else "engine/apps (headless: no cycle output)")
    if not ref["fiscal_cycles"] or not port["fiscal_cycles"]:
        side = "trace" if not ref["fiscal_cycles"] else "port"
        d = "missing on the %s side" % side
        if ref["fiscal_cycles"]:
            c = ref["fiscal_cycles"]
            d += "; trace: first next_cycle_frame %s, cycle ticks %s" % (c.get("first_next_cycle_frame"), c["cycle_ticks"][:12])
        return Result("fiscal_cycles", "n/a", detail=d, module="engine/apps (headless: no cycle output)" if side == "port" else None)
    r, p = ref["fiscal_cycles"]["cycle_ticks"], port["fiscal_cycles"]["cycle_ticks"]
    # A trace change is seen on the first sample after the cycle: with missed samples it is a window (previous sample + 1
    # .. sample), and the port tick only has to fall inside it.
    win = ref["fiscal_cycles"].get("cycle_windows") or [(t, t) for t in r]
    n = min(len(r), len(p))
    for i in range(n):
        lo, hi = win[i] if i < len(win) else (r[i], r[i])
        if not lo <= p[i] <= hi:
            return Result("fiscal_cycles", "DIVERGES", min(r[i], p[i]), "cycle %d: trace tick %s, port %d" % (
                i + 1, r[i] if lo == hi else "%d..%d" % (lo, hi), p[i]), "engine/galactic (economy)")
    return Result("fiscal_cycles", "match", detail="%d cycle ticks agree (inside the trace's sample windows) %s" % (n, r[:6]))


def cmp_owners(ref, port):
    out = []
    if ref["owners_initial"] and port["owners_initial"]:
        diff = {k: (v, port["owners_initial"].get(k)) for k, v in ref["owners_initial"].items() if port["owners_initial"].get(k) != v}
        out.append(Result("owners_initial", "DIVERGES" if diff else "match", 0 if diff else None,
                          str(diff) if diff else "%d planets" % len(ref["owners_initial"]), "engine/galactic (campaign setup)" if diff else None))
    elif ref["owners_initial"]:
        out.append(Result("owners_initial", "n/a", detail="port: no initial owners in the AI log (trace: %s)" % ref["owners_initial"]))
    if ref["owner_changes"] is None or port["owner_changes"] is None:
        out.append(Result("owner_changes", "n/a", detail="missing on one side"))
    else:
        r, p = ref["owner_changes"], port["owner_changes"]
        first, det = None, ""
        for i in range(max(len(r), len(p))):
            a = r[i] if i < len(r) else None
            b = p[i] if i < len(p) else None
            if a is None or b is None or a[1:] != b[1:] or abs(a[0] - b[0]) > 30:
                first = min(x[0] for x in (a, b) if x)
                det = "change %d: trace %s, port %s" % (i + 1, a, b)
                break
        if first is None:
            out.append(Result("owner_changes", "match", detail="%d changes" % len(r)))
        else:
            out.append(Result("owner_changes", "DIVERGES", first, det + "; trace %d changes, port %d" % (len(r), len(p)),
                              "engine/ai (galactic AI) + engine/apps/sim (conflicts)"))
    if ref["planets_owned"] and port["planets_owned"]:
        first, det, n = None, "", 0
        for tk in sorted(set(ref["planets_owned"]) & set(port["planets_owned"])):
            n += 1
            a = {k: v for k, v in ref["planets_owned"][tk].items() if v and k in FACTION_TOKENS}
            b = {k: v for k, v in port["planets_owned"][tk].items() if v and k in FACTION_TOKENS}
            if a != b and first is None:
                first, det = tk, "tick %d: trace %s, port %s" % (tk, a, b)
        out.append(Result("planets_owned", "DIVERGES" if first is not None else "match", first,
                          det or "%d samples equal (every %d ticks)" % (n, SAMPLE_EVERY),
                          "engine/ai (galactic AI)" if first is not None else None))
    return out


def per_faction(seq, idx_fac=1):
    d = collections.defaultdict(list)
    for x in seq:
        d[x[idx_fac]].append(x)
    return d


def cmp_fleet(ref, port):
    if ref["fleet_moves"] is None or port["fleet_moves"] is None:
        return [Result("fleet_moves", "n/a", detail="missing on one side")]
    out = []
    rf, pf = per_faction(ref["fleet_moves"]), per_faction(port["fleet_moves"])
    for fac in sorted(set(rf) | set(pf)):
        if fac not in FACTION_TOKENS:
            continue
        r, p = rf.get(fac, []), pf.get(fac, [])
        first, det = None, ""
        for i in range(max(len(r), len(p))):
            a = r[i] if i < len(r) else None
            b = p[i] if i < len(p) else None
            if a is None or b is None or a[2] != b[2] or abs(a[0] - b[0]) > 30:
                first = min(x[0] for x in (a, b) if x)
                det = "move %d: trace %s, port %s" % (i + 1, a and "tick %d -> %s" % (a[0], a[2]), b and "tick %d -> %s" % (b[0], b[2]))
                break
        rt = collections.Counter(x[2] for x in r)
        pt = collections.Counter(x[2] for x in p)
        summ = "trace %d departures (by target %s), port %d (%s)" % (len(r), dict(rt.most_common(5)), len(p), dict(pt.most_common(5)))
        out.append(Result("fleet_moves[%s]" % fac, "DIVERGES" if first is not None else "match", first,
                          (det + "; " if det else "") + summ, "engine/ai (galactic AI plans)" if first is not None else None))
    if ref.get("fleet_hops"):
        hops = ref["fleet_hops"]
        durs = collections.Counter((h[2], h[3], h[4] - h[0]) for h in hops)
        out.append(Result("fleet_hops (trace info)", "info", detail="%d hops; e.g. %s" % (
            len(hops), ", ".join("%s->%s %d ticks" % k for k, _ in durs.most_common(6)))))
    return out


def cmp_production(ref, port):
    """Production batches per faction: (tick, faction, planet, {type: n}). The trace sees completions (new objects, by
    member type), the port AI log sees build orders (by company type), so the batch sequence is compared by planet and the
    tick of the first batch; type names are listed, not matched."""
    if ref["production"] is None or port["production"] is None:
        d = "missing on the %s side" % ("trace" if ref["production"] is None else "port")
        if ref["production"]:
            d += "; trace (%s) %d events, first: %s" % (ref.get("production_source"), len(ref["production"]), "; ".join(
                "tick %d %s %s" % (x[0], x[1], ",".join("%s x%d" % kv for kv in x[3].items())) for x in ref["production"][:6]))
        return [Result("production", "n/a", detail=d)]
    out = []
    what = "build orders" if port.get("production_orders") else "batches"
    pprod = list(port["production"])
    if ref.get("production_source") == "queue starts" and port.get("starbase_orders"):
        # The queues show starbase upgrades, so the port's starbase orders are compared too.
        pprod = sorted(pprod + [(tk, f, at, {t: 1}) for tk, f, at, t in port["starbase_orders"]], key=lambda x: x[0])
    rf, pf = per_faction(ref["production"]), per_faction(pprod)

    def fmt(x):
        return x and "tick %d at %s %s" % (x[0], x[2], ",".join("%s%s" % (t, "" if n == 1 else " x%d" % n) for t, n in x[3].items()))
    for fac in sorted(set(rf) | set(pf)):
        if fac not in FACTION_TOKENS:
            continue
        r, p = rf.get(fac, []), pf.get(fac, [])
        first, det = None, ""
        for i in range(max(len(r), len(p))):
            a = r[i] if i < len(r) else None
            b = p[i] if i < len(p) else None
            if a is None or b is None or a[2] != b[2] or (ref.get("production_source") == "queue starts" and set(a[3]) != set(b[3])):
                first = min(x[0] for x in (a, b) if x)
                det = "item %d: trace %s; port %s" % (i + 1, fmt(a), fmt(b))
                break
        rt, pt = collections.Counter(), collections.Counter()
        for x in r:
            rt.update(x[3])
        for x in p:
            pt.update(x[3])
        summ = "trace %d %s %s; port %d %s %s" % (len(r), ref.get("production_source", "batches"), dict(rt.most_common(6)), len(p),
                                                  what, dict(pt.most_common(6)))
        out.append(Result("production[%s]" % fac, "DIVERGES" if first is not None else "match", first,
                          (det + "; " if det else "") + summ, "engine/ai (galactic AI build plans) + engine/galactic (production)"
                          if first is not None else None))
    return out


def cmp_object_counts(ref, port):
    if not ref["object_counts"] or not port["object_counts"]:
        info = ""
        if ref["object_counts"]:
            last = max(ref["object_counts"])
            tot = collections.Counter()
            for k, v in ref["object_counts"][last].items():
                tot[k.split(":")[0]] += v
            info = "; trace at tick %d: %s objects by faction" % (last, dict(tot))
        return Result("object_counts", "n/a", detail="missing on the port side (needs a port state trace)" + info,
                      module="engine/apps (headless: no object dump)")
    first, det = None, ""
    for tk in sorted(set(ref["object_counts"]) & set(port["object_counts"])):
        a, b = ref["object_counts"][tk], port["object_counts"][tk]
        if a != b:
            diff = {k: (a.get(k, 0), b.get(k, 0)) for k in sorted(set(a) | set(b)) if a.get(k, 0) != b.get(k, 0)}
            first, det = tk, "tick %d: (trace, port) %s" % (tk, dict(list(diff.items())[:8]))
            break
    return Result("object_counts", "DIVERGES" if first is not None else "match", first, det, "engine/galactic" if first is not None else None)


def cmp_credits(ref, port):
    out = []
    rc = ref.get("credits") or {}
    lc = ref.get("lua_credits") or {}
    if not rc and not lc and not ref.get("credits_final"):
        out.append(Result("credits", "n/a", detail="the trace has no credits (reader v1 found no player vector; no Lua probe); "
                          "port final: %s" % port.get("credits_final")))
        return out
    if rc and port.get("credits"):
        first, det = None, ""
        for tk in sorted(set(rc) & set(port["credits"])):
            a, b = rc[tk], port["credits"][tk]
            bad = {f: (a.get(f), b.get(f)) for f in a if f in FACTION_TOKENS and b.get(f) is not None and abs(a[f] - b[f]) > 0.5}
            if bad:
                first, det = tk, "cycle tick %d: (trace, port) %s" % (tk, bad)
                break
        out.append(Result("credits_per_cycle", "DIVERGES" if first is not None else "match", first, det,
                          "engine/galactic (economy)" if first is not None else None))
    elif (rc or lc) and not (ref.get("credits_series") and port.get("credits_series")):
        src = rc or lc
        out.append(Result("credits_per_cycle", "n/a", detail="port has no per-cycle credits (needs a port state trace); "
                          "trace first cycles: %s" % dict(list(sorted(src.items()))[:3]), module="engine/apps (headless)"))
    series = ref.get("credits_series")
    if series and not port.get("credits_series"):
        pts = sorted(series.items())
        show = [pts[0]] + pts[1:][-3:] if len(pts) > 4 else pts
        out.append(Result("credits_series", "n/a", detail="port has no credits over time; trace %s" % "; ".join(
            "tick %d %s" % (tk, {f: round(v, 1) for f, v in d.items() if f in FACTION_TOKENS}) for tk, d in show)))
    elif series and port.get("credits_series"):
        first, det, n = None, "", 0
        for tk in sorted(set(series) & set(port["credits_series"])):
            a, b = series[tk], port["credits_series"][tk]
            n += 1
            bad = {f: (round(a[f], 1), round(b[f], 1)) for f in a if f in FACTION_TOKENS and b.get(f) is not None and abs(a[f] - b[f]) > 0.5}
            if bad and first is None:
                first, det = tk, "tick %d: (trace, port) %s" % (tk, bad)
        out.append(Result("credits_series", "DIVERGES" if first is not None else "match", first, det or "%d samples equal" % n,
                          "engine/galactic (economy)" if first is not None else None))
    if ref.get("credits_final") and port.get("credits_final"):
        a, b = ref["credits_final"], port["credits_final"]
        bad = {f: (a.get(f), b.get(f)) for f in a if f in FACTION_TOKENS and b.get(f) is not None and abs(a[f] - b[f]) > 0.5}
        out.append(Result("credits_final", "DIVERGES" if bad else "match", ref["scenario"].get("ticks") if bad else None,
                          str(bad or a), "engine/galactic (economy) + engine/ai" if bad else None))
    return out


def cmp_lua(ref, port):
    out = []
    if ref.get("lua_owners"):
        cyc = ref["lua_owners"]
        if port.get("lua_owners"):
            first = None
            for c in sorted(set(cyc) & set(port["lua_owners"])):
                if cyc[c] != port["lua_owners"][c]:
                    first = c
                    break
            out.append(Result("lua_owners", "DIVERGES" if first is not None else "match",
                              None if first is None else first * 1350, "cycle %s" % first, "engine/ai" if first is not None else None))
        else:
            out.append(Result("lua_owners", "n/a", detail="port has no per-cycle owners; trace has %d cycles" % len(cyc)))
    if ref.get("perception"):
        out.append(Result("perception", "n/a", detail="trace has %d cycles x %d values; the port has no perception dump yet" % (
            len(ref["perception"]), len(next(iter(ref["perception"].values())))), module="engine/ai (perception equations)"))
    return out


def compare_metrics(ref, port):
    res = [cmp_rng(ref, port), cmp_cycles(ref, port)]
    res += cmp_owners(ref, port)
    res += cmp_fleet(ref, port)
    res += cmp_production(ref, port)
    res.append(cmp_object_counts(ref, port))
    res += cmp_credits(ref, port)
    res += cmp_lua(ref, port)
    if ref.get("queue_entries"):
        q = ref["queue_entries"]
        by_type = {}
        for e in q:
            by_type.setdefault(e["type"], set()).add((e["frames"], e["cost"]))
        stalled = [e for e in q if e["restarts"] > 30]
        res.append(Result("production queues (trace info)", "info", detail="%d queued items; build frames / cost by type: %s%s" % (
            len(q), "; ".join("%s %s" % (t, sorted(v)[:3]) for t, v in sorted(by_type.items())),
            "; stalled items (start frame reset every tick): %s" % "; ".join(
                "%s %s@%s ticks %d-%d (%d restarts)" % (e["faction"], e["type"], e["planet"], e["seen"], e["last"], e["restarts"])
                for e in stalled[:6]) if stalled else "")))
    if port.get("starbase_orders") and ref.get("production_source") != "queue starts":
        res.append(Result("starbase orders (port info)", "info", detail="not visible in the trace (no starbase objects): %s" % (
            ", ".join("%s %d %s@%s" % (f, tk, t, at) for tk, f, at, t in port["starbase_orders"][:8]))))
    if port.get("conflicts") is not None:
        c = port["conflicts"]
        res.append(Result("conflicts (port info)", "info", detail="%d conflicts; first %s" % (len(c), c[:2])))
    return res


def report(ref, port, results):
    lines = ["Q2 parity report",
             "  trace: %s (%s, seed %s, %s ticks)" % (ref["source"], ref["scenario"].get("campaign"), ref["scenario"].get("seed"),
                                                   ref["scenario"].get("ticks")),
             "  port:  %s (%s, seed %s, %s ticks)" % (port["source"], port["scenario"].get("campaign"), port["scenario"].get("seed"),
                                                   port["scenario"].get("ticks")),
             "  factions: trace %s; port %s" % (ref["factions"], port["factions"]),
             "  tick = mode frame (ticks of the mode done); port CRC record t = tick t+1", ""]
    for n in ref["notes"] + port["notes"]:
        lines.append("  NOTE " + n)
    w = max(len(r.metric) for r in results)
    for r in results:
        ft = "" if r.first_tick is None else " first tick %s" % r.first_tick
        mod = "" if not r.module else " [%s]" % r.module
        lines.append("%-*s  %-8s%s%s" % (w, r.metric, r.status, ft, mod))
        if r.detail:
            lines.append("%-*s    %s" % (w, "", r.detail))
    div = [r for r in results if r.status == "DIVERGES"]
    lines.append("")
    lines.append("%d metrics compared: %d match, %d match the port's rule only, %d diverge, %d n/a" % (
        sum(1 for r in results if r.status in ("match", "DIVERGES", "rule-match")), sum(1 for r in results if r.status == "match"),
        sum(1 for r in results if r.status == "rule-match"), len(div), sum(1 for r in results if r.status == "n/a")))
    return "\n".join(lines)


def resolve_trace(path):
    """A run folder or a trace file -> (trace.q2t, [lua_*.txt])."""
    if not os.path.isdir(path) and os.path.basename(path).lower() == "trace.q2t":
        path = os.path.dirname(os.path.abspath(path))
    if os.path.isdir(path):
        lua = sorted(os.path.join(path, f) for f in os.listdir(path) if f.lower().startswith("lua_") and f.lower().endswith(".txt"))
        return os.path.join(path, "trace.q2t"), lua
    return path, []


def run(trace, port_crc=None, port_ai_log=None, port_trace=None, lua=None, port_factions=None, trace_factions=None,
        json_out=None, seed=None, max_tick=None, quiet=False, port_credits=None):
    tpath, lua_auto = resolve_trace(trace)
    ref = trace_metrics(tpath, trace_factions)
    qpath = os.path.join(os.path.dirname(os.path.abspath(tpath)), "production.q2q")
    if os.path.exists(qpath):
        queue_metrics(qpath, ref)
    lua_files = list(lua or []) + [f for f in lua_auto if f not in (lua or [])]
    if lua_files:
        lua_metrics(lua_files, ref)
    if seed is None:
        seed = int(ref["scenario"].get("seed") or "0", 16)
    tick_cap = max_tick if max_tick is not None else ref["scenario"].get("ticks")
    tactical = ref["scenario"].get("scenario") == "skirmish"
    port = port_metrics(port_crc, port_ai_log, port_trace, port_factions, seed, tick_cap, tactical)
    if port_credits:  # {tick: {player index: credits}} from short port runs (q2trace.py compare --port-exe)
        port["credits_series"] = {tk: {port["factions"].get(i, "p%d" % i): v for i, v in d.items()}
                                  for tk, d in sorted(port_credits.items())}
    results = compare_metrics(ref, port)
    text = report(ref, port, results)
    if not quiet:
        print(text)
    if json_out:
        with open(json_out, "w", encoding="utf-8", newline="\n") as fh:
            json.dump({"trace": ref["scenario"], "port": port["scenario"], "trace_factions": ref["factions"],
                       "port_factions": port["factions"], "results": [r.as_dict() for r in results],
                       "trace_fleet_hops": ref.get("fleet_hops"), "trace_production": ref.get("production"),
                       "port_production_orders": port.get("production_orders"), "trace_owner_changes": ref.get("owner_changes"),
                       "trace_fiscal_cycles": ref.get("fiscal_cycles"), "trace_rng_first": (ref["rng_draws"] or [])[:40],
                       "port_rng_first": (port["rng_draws"] or [])[:40]}, fh, indent=1, sort_keys=True, default=str)
            fh.write("\n")
    return results, (1 if any(r.status == "DIVERGES" for r in results) else 0)


def add_args(ap):
    ap.add_argument("trace", help="trace.q2t or its run folder (lua_*.txt there are read too)")
    ap.add_argument("--port-crc", help="port CRC log (alamo_headless --crc-out)")
    ap.add_argument("--port-ai-log", help="port AI log (alamo_headless --ai-log)")
    ap.add_argument("--port-trace", help="port run written as Q2TRACE 1 (when the port can emit one)")
    ap.add_argument("--lua", action="append", help="Lua probe output (lua_*.txt); repeatable")
    ap.add_argument("--port-factions", help="port player index -> faction, e.g. 0=Rebel,1=Empire,5=Underworld")
    ap.add_argument("--trace-factions", help="trace player index -> faction, e.g. 0=Rebel,1=Empire,6=Underworld")
    ap.add_argument("--seed", type=lambda s: int(s, 0), help="port session seed (default: the trace's seed line)")
    ap.add_argument("--max-tick", type=int, help="compare up to this tick (default: the trace's last mode frame)")
    ap.add_argument("--json", help="write the results as JSON")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    add_args(ap)
    a = ap.parse_args(argv)
    _, rc = run(a.trace, a.port_crc, a.port_ai_log, a.port_trace, a.lua, parse_map(a.port_factions), parse_map(a.trace_factions),
                a.json, a.seed, a.max_tick)
    return rc


if __name__ == "__main__":
    sys.exit(main())
