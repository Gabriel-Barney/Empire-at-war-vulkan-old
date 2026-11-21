// skai t15 (docs/re/SKAI_T15_PLAN_RETEST.md §3.5): which goal-system rule ends retail's land plans at a commit.
// Loaded into StarWarsG.exe (FoC Steam x64 1.121) by frida_logger.py. Read-only: Interceptor.attach on function entries,
// memory reads, no writes, no calls into the game. Every event is sent as one JSON object; the logger writes JSONL.
//
// Events (f = global logic frame, FrameSynchronizer +0; mf = the current mode's frame, GameModeManager +0x38 -> +0x10;
// ra = return address as an RVA, i.e. which function called):
//   retest_enter / retest_leave  0x6c47c0 PP re-validation: pp, plandef (PP +0x50), valid byte before (PP +0x4c),
//                                test-contrast byte (PP +0x4d), running (PP +0x100); leave: ret (al)
//   plan_end      0x5f6a50 (plan): plan, plandef (plan +0x60), goal (plan +0x88), ra, bt (the next 4 return RVAs)
//   goal_release  0x693750 (goal): goal, pp (goal +0x68), ra
//   budget_release 0x614880 (gs, goal): goal, ra
//   goal_dtor     0x691840 (goal): goal, pp, ra
//   purge         0x69c750 (Purge_Goals): ra
//   resolve_enter / resolve_leave 0x6c8970 Resolve_Category (gs, proposed set, cat = args[2]), maint_enter / maint_leave
//                 0x722a30 maintenance (goal set, cat = args[1]): brackets
//   resolve_goals 0x6c8970 entry, land goal systems only (vtable 0x8be308): player, cat, and per goal of the category's
//                 active set (gs +0x38) and proposed set (args[1]): g (pointer), type (name), tgt, d (+0x50), d0 (+0x54),
//                 st (+0x74), eff (0x6927f0 hysteresis), plan (PP +0x50 -> +0x40) (docs/re/SKAI_T15_RESOLVE_HOOK_RE.md)
//   resolve_result 0x6c8970 leave: survivors (gs +0x38 [cat] pointers), accepted (entry proposals now in state 1)
//   activation    0x58a5d0 (learning, goal, ok): goal, ok, ra (which Resolve phase accepted / which test rejected)
// Addresses: docs/re/SKAI_T15_PLAN_RETEST.md, docs/re/A1_READINGS.md §28, tools/probe_mod/q2_traces/reader/q2_layout.hpp.
'use strict';

const EXE = 'StarWarsG.exe';
const TIMESTAMP = 0x67002393;  // PE TimeDateStamp of the 1.121 Steam build (q2_layout.hpp kFocTimeDateStamp)
const RVA = {
  retest: 0x6c47c0, planEnd: 0x5f6a50, goalRelease: 0x693750, budgetRelease: 0x614880, goalDtor: 0x691840,
  purge: 0x69c750, resolve: 0x6c8970, maint: 0x722a30, activation: 0x58a5d0,
  landGoalSystemVt: 0x8be308, unitTargetVt: 0x8ce4a0, regionTargetVt: 0x8ce548,
  frameSync: 0xB0A320, gmm: 0xB153E0,
};

const mod = Process.getModuleByName(EXE);
const base = mod.base;

function rva(p) {
  try {
    const d = p.sub(base);
    return d.compare(ptr(mod.size)) < 0 ? '0x' + d.toString(16) : p.toString();
  } catch (e) {
    return String(p);
  }
}
function safe(fn, dflt) {
  try { return fn(); } catch (e) { return dflt; }
}
function frames() {
  const f = safe(() => base.add(RVA.frameSync).readU32(), -1);
  const mf = safe(() => base.add(RVA.gmm).add(0x38).readPointer().add(0x10).readU32(), -1);
  return { f: f, mf: mf };
}
function emit(kind, extra, ctx) {
  const o = frames();
  o.k = kind;
  if (ctx) {
    o.ra = rva(ctx.returnAddress);
  }
  for (const key in extra) o[key] = extra[key];
  send(o);
}
function backtrace(ctx, n) {
  return safe(() => Thread.backtrace(ctx.context, Backtracer.FUZZY).slice(0, n).map(rva), []);
}

// skai t15 Resolve candidate dump (docs/re/SKAI_T15_RESOLVE_HOOK_RE.md [H]); every read is guarded and bounded.
const MAX_LIST = 64;
function stdString(p, sizeOff, capOff) {
  return safe(() => {
    const size = p.add(sizeOff).readU64().toNumber();
    const cap = p.add(capOff).readU64().toNumber();
    const data = cap > 15 ? p.readPointer() : p;
    return data.readUtf8String(Math.min(size, 120));
  }, '?');
}
// A goal set's vector of goal pointers for category `cat`: std::list head (sentinel) at set +0x48; node +0x00 next,
// +0x10 category (int), +0x18 -> vector (+0 begin, +8 end).
function setGoals(set, cat) {
  return safe(() => {
    const head = set.add(0x48).readPointer();
    let node = head.readPointer();
    for (let i = 0; i < MAX_LIST && !node.isNull() && !node.equals(head); i++, node = node.readPointer()) {
      if (node.add(0x10).readS32() !== cat) continue;
      const vec = node.add(0x18).readPointer();
      const begin = vec.readPointer();
      const n = Math.min(MAX_LIST, Math.max(0, Math.floor(vec.add(8).readPointer().sub(begin).toInt32() / 8)));
      const out = [];
      for (let k = 0; k < n; k++) out.push(begin.add(k * 8).readPointer());
      return out;
    }
    return [];
  }, []);
}
// 0x6927f0 in float steps: state >= 1 and 0.01 < d < 40 -> d + (1 - (d - 0.01) / (40 - 0.01)) * 1.6.
function effDesire(d, st) {
  const f = Math.fround;
  if (st < 1 || !(d > f(0.01) && d < 40)) return d;
  return f(d + f(f(1 - f(f(d - f(0.01)) / f(40 - f(0.01)))) * f(1.6)));
}
function targetText(t) {
  if (t.isNull()) return 'global';
  return safe(() => {
    const vt = t.readPointer();
    if (vt.equals(base.add(RVA.unitTargetVt))) return 'obj:' + t.add(0x30).readU32();
    if (vt.equals(base.add(RVA.regionTargetVt))) return 'zone:' + stdString(t.add(0x28).readPointer().add(0x10), 0x10, 0x18);
    return 'tgt:' + rva(vt);
  }, '?');
}
function goalInfo(g) {
  const d = safe(() => g.add(0x50).readFloat(), NaN);
  const st = safe(() => g.add(0x74).readS32(), -1);
  return {
    g: g.toString(),
    type: safe(() => stdString(g.add(0x48).readPointer().readPointer(), 0x10, 0x18), '?'),
    tgt: safe(() => targetText(g.add(0x58).readPointer()), '?'),
    d: d,
    d0: safe(() => g.add(0x54).readFloat(), NaN),
    st: st,
    eff: effDesire(d, st),
    plan: safe(() => {
      const pp = g.add(0x68).readPointer();
      return pp.isNull() ? '' : stdString(pp.add(0x50).readPointer().add(0x40), 0x10, 0x18);
    }, '?'),
  };
}
function goalSystemPlayer(gs) {
  return safe(() => gs.add(0x28).readPointer().add(0x28).readPointer().add(0x28).readPointer().add(0x4c).readS32(), -1);
}

// Identity check: refuse to hook another build.
const peOff = base.add(0x3c).readU32();
const stamp = base.add(peOff + 8).readU32();
if (stamp !== TIMESTAMP) {
  send({ k: 'error', what: 'unexpected StarWarsG.exe build', stamp: '0x' + stamp.toString(16) });
} else {
  send({ k: 'hello', base: base.toString(), size: mod.size, stamp: '0x' + stamp.toString(16) });

  Interceptor.attach(base.add(RVA.retest), {
    onEnter(args) {
      const pp = args[0];
      this.pp = pp;
      emit('retest_enter', {
        pp: pp.toString(),
        plandef: safe(() => pp.add(0x50).readPointer().toString(), '?'),
        valid: safe(() => pp.add(0x4c).readU8(), -1),
        test_contrast: safe(() => pp.add(0x4d).readU8(), -1),
        running: safe(() => pp.add(0x100).readU8(), -1),
      }, this);
    },
    onLeave(retval) {
      emit('retest_leave', { pp: this.pp.toString(), ret: retval.toInt32() & 0xff,
                             valid: safe(() => this.pp.add(0x4c).readU8(), -1) }, null);
    },
  });

  Interceptor.attach(base.add(RVA.planEnd), {
    onEnter(args) {
      const plan = args[0];
      emit('plan_end', {
        plan: plan.toString(),
        plandef: safe(() => plan.add(0x60).readPointer().toString(), '?'),
        goal: safe(() => plan.add(0x88).readPointer().toString(), '?'),
        bt: backtrace(this, 5),
      }, this);
    },
  });

  Interceptor.attach(base.add(RVA.goalRelease), {
    onEnter(args) {
      const g = args[0];
      emit('goal_release', { goal: g.toString(), pp: safe(() => g.add(0x68).readPointer().toString(), '?') }, this);
    },
  });

  Interceptor.attach(base.add(RVA.budgetRelease), {
    onEnter(args) {
      emit('budget_release', { goal: args[1].toString() }, this);
    },
  });

  Interceptor.attach(base.add(RVA.goalDtor), {
    onEnter(args) {
      const g = args[0];
      emit('goal_dtor', { goal: g.toString(), pp: safe(() => g.add(0x68).readPointer().toString(), '?') }, this);
    },
  });

  Interceptor.attach(base.add(RVA.purge), {
    onEnter(args) {
      emit('purge', {}, this);
    },
  });

  Interceptor.attach(base.add(RVA.maint), {
    onEnter(args) {
      emit('maint_enter', { cat: args[1].toInt32() }, this);
    },
    onLeave(retval) {
      emit('maint_leave', {}, null);
    },
  });

  // Resolve_Category (gs, proposed set, cat): skai t15 fix, the category is args[2] (args[1] is the proposed set).
  Interceptor.attach(base.add(RVA.resolve), {
    onEnter(args) {
      const gs = args[0];
      const cat = args[2].toInt32();
      this.gs = gs;
      this.cat = cat;
      this.land = safe(() => gs.readPointer().equals(base.add(RVA.landGoalSystemVt)), false);
      emit('resolve_enter', { cat: cat, gs: gs.toString(), land: this.land ? 1 : 0 }, this);
      if (!this.land) return;
      const active = setGoals(gs.add(0x38), cat);
      const proposed = setGoals(args[1], cat);
      this.proposed = proposed;
      emit('resolve_goals', {
        player: goalSystemPlayer(gs), cat: cat,
        active: active.map(goalInfo), proposed: proposed.map(goalInfo),
      }, null);
    },
    onLeave(retval) {
      emit('resolve_leave', { cat: this.cat }, null);
      if (!this.land) return;
      const survivors = setGoals(this.gs.add(0x38), this.cat).map(g => g.toString());
      // Proposals stay alive until Commit clears the set after the last category (SKAI_T15_RESOLVE_HOOK_RE §1).
      const accepted = this.proposed.filter(g => safe(() => g.add(0x74).readS32(), -1) === 1).map(g => g.toString());
      emit('resolve_result', { player: goalSystemPlayer(this.gs), cat: this.cat, survivors: survivors, accepted: accepted }, null);
    },
  });

  // Activation record (learning, goal, ok): only Resolve calls it; ra names the phase / failed test.
  Interceptor.attach(base.add(RVA.activation), {
    onEnter(args) {
      emit('activation', { goal: args[1].toString(), ok: args[2].toInt32() & 0xff }, this);
    },
  });
}
