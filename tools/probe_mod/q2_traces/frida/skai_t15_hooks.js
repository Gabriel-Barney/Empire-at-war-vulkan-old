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
//   resolve_enter / resolve_leave 0x6c8970 Resolve_Category, maint_enter / maint_leave 0x722a30 maintenance: brackets
// Addresses: docs/re/SKAI_T15_PLAN_RETEST.md, docs/re/A1_READINGS.md §28, tools/probe_mod/q2_traces/reader/q2_layout.hpp.
'use strict';

const EXE = 'StarWarsG.exe';
const TIMESTAMP = 0x67002393;  // PE TimeDateStamp of the 1.121 Steam build (q2_layout.hpp kFocTimeDateStamp)
const RVA = {
  retest: 0x6c47c0, planEnd: 0x5f6a50, goalRelease: 0x693750, budgetRelease: 0x614880, goalDtor: 0x691840,
  purge: 0x69c750, resolve: 0x6c8970, maint: 0x722a30,
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

  for (const [name, at] of [['resolve', RVA.resolve], ['maint', RVA.maint]]) {
    Interceptor.attach(base.add(at), {
      onEnter(args) {
        emit(name + '_enter', { cat: args[1].toInt32() }, this);
      },
      onLeave(retval) {
        emit(name + '_leave', {}, null);
      },
    });
  }
}
