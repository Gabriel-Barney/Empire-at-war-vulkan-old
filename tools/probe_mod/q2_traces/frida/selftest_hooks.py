"""Offline selftest of frida/skai_t15_hooks.js (skai t15), no game needed.

1. Compile: frida compiles the hook script in a throwaway local process (a sleeping Python child). A broken copy must
   fail, so the check is known to work.
2. Helpers at run time: the script is loaded into that child with its module name swapped for the child's own exe. The
   build-stamp check then fails, so no hook is attached ('error' message, expected). A test body appended to the
   script builds fake goal sets, goals, targets and strings in the child's memory, with the layout of
   docs/re/SKAI_T15_RESOLVE_HOOK_RE.md, and checks setGoals / goalInfo / effDesire against the values written.

    toolchain\\re\\venv\\Scripts\\python.exe -I tools\\probe_mod\\q2_traces\\frida\\selftest_hooks.py
"""
import json
import os
import struct
import subprocess
import sys
import threading

import frida

HERE = os.path.dirname(os.path.abspath(__file__))

TEST_BODY = r"""
;(function () {
  const keep = [];  // frida frees Memory.alloc blocks whose handles are collected: keep every one
  function str(text) {  // MSVC std::string, inline when <= 15 chars
    const s = Memory.alloc(0x20);
    if (text.length <= 15) {
      s.writeUtf8String(text);
      s.add(0x18).writeU64(15);
    } else {
      const heap = Memory.allocUtf8String(text);
      keep.push(heap);
      s.writePointer(heap);
      s.add(0x18).writeU64(text.length);
    }
    s.add(0x10).writeU64(text.length);
    keep.push(s);
    return s;
  }
  function goal(typeName, target, d, d0, st, planName) {
    const g = Memory.alloc(0xa0);
    const typ = Memory.alloc(0x40);
    const name = str(typeName);
    Memory.copy(typ, name, 0x20);
    const pair = Memory.alloc(0x10);
    pair.writePointer(typ);
    g.add(0x48).writePointer(pair);
    g.add(0x50).writeFloat(d);
    g.add(0x54).writeFloat(d0);
    g.add(0x58).writePointer(target);
    g.add(0x74).writeS32(st);
    if (planName) {
      const pp = Memory.alloc(0x60);
      const def = Memory.alloc(0x60);
      Memory.copy(def.add(0x40), str(planName), 0x20);
      pp.add(0x50).writePointer(def);
      g.add(0x68).writePointer(pp);
      keep.push(pp, def);
    }
    keep.push(g, typ, name, pair);
    return g;
  }
  function goalSet(byCat) {  // set +0x48 -> list sentinel; node +0 next, +0x10 cat, +0x18 -> vector
    const set = Memory.alloc(0xa0);
    const head = Memory.alloc(0x20);
    set.add(0x48).writePointer(head);
    let prev = head;
    for (const [cat, goals] of byCat) {
      const node = Memory.alloc(0x20);
      const vec = Memory.alloc(0x18);
      const arr = Memory.alloc(Math.max(8, goals.length * 8));
      goals.forEach((g, i) => arr.add(i * 8).writePointer(g));
      vec.writePointer(arr);
      vec.add(8).writePointer(arr.add(goals.length * 8));
      node.add(0x10).writeS32(cat);
      node.add(0x18).writePointer(vec);
      prev.writePointer(node);
      prev = node;
      keep.push(node, vec, arr);
    }
    prev.writePointer(head);
    keep.push(set, head);
    return set;
  }
  const unit = Memory.alloc(0x40);
  unit.writePointer(base.add(RVA.unitTargetVt));
  unit.add(0x30).writeU32(792);
  const zone = Memory.alloc(0x40);
  Memory.copy(zone.add(0x10), str('New Zone 86 with a long name'), 0x20);
  const region = Memory.alloc(0x40);
  region.writePointer(base.add(RVA.regionTargetVt));
  region.add(0x28).writePointer(zone);
  const g1 = goal('Destroy_Structure', unit, 15.001, 15.001, 2, 'DestroyStructure');
  const g2 = goal('Clear_Infantry', region, 17.5, 17.5, 0, '');
  const g3 = goal('Land_Scout_Area', ptr(0), 50.0, 3.0, 1, '');
  const set = goalSet([[1, [g3]], [4, [g1, g2]]]);
  send({ k: 'test',
         cat4: setGoals(set, 4).map(goalInfo),
         cat1: setGoals(set, 1).map(goalInfo),
         cat9: setGoals(set, 9).length,
         names: [g1.toString(), g2.toString(), g3.toString()] });
})();
"""


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def eff(d, st):
    if st < 1 or not (f32(0.01) < d < 40):
        return d
    t = f32(f32(d - f32(0.01)) / f32(40 - f32(0.01)))
    return f32(d + f32(f32(1 - t) * f32(1.6)))


def main():
    src = open(os.path.join(HERE, "skai_t15_hooks.js"), encoding="utf-8").read()
    child = subprocess.Popen([sys.executable, "-I", "-c", "import time; time.sleep(120)"])
    fails = []
    try:
        session = frida.attach(child.pid)
        for name, text, want_ok in (("hooks", src, True), ("broken", src + "\n}{", False)):
            try:
                session.create_script(text, name=name).unload()
                ok = True
            except Exception:  # frida raises its own error types for a syntax error
                ok = False
            print("compile %-6s: %s" % (name, "ok" if ok else "error"))
            if ok != want_ok:
                fails.append("compile " + name)
        exe = os.path.basename(sys.executable)
        test_src = src.replace("const EXE = 'StarWarsG.exe';", "const EXE = %s;" % json.dumps(exe)) + TEST_BODY
        msgs = []
        done = threading.Event()

        def on_message(msg, data):
            msgs.append(msg)
            if msg.get("type") == "error" or msg.get("payload", {}).get("k") == "test":
                done.set()

        script = session.create_script(test_src, name="runtime")
        script.on("message", on_message)
        script.load()
        done.wait(20)
        script.unload()
        session.detach()
    finally:
        child.kill()
    payloads = [m.get("payload", {}) for m in msgs if m.get("type") == "send"]
    errors = [m for m in msgs if m.get("type") == "error"]
    if errors:
        fails.append("script error: %s" % errors[0].get("description"))
    if not any(p.get("k") == "error" and "unexpected" in p.get("what", "") for p in payloads):
        fails.append("the build-stamp check did not refuse a non-game process")
    test = next((p for p in payloads if p.get("k") == "test"), None)
    if test is None:
        fails.append("no test result")
    else:
        g1, g2, g3 = test["cat4"] + test["cat1"] if len(test["cat4"]) == 2 and len(test["cat1"]) == 1 else (None,) * 3
        if g1 is None:
            fails.append("setGoals: wrong counts %r" % ([len(test["cat4"]), len(test["cat1"])],))
        else:
            want = [
                (g1, "Destroy_Structure", "obj:792", f32(15.001), 2, "DestroyStructure", eff(f32(15.001), 2)),
                (g2, "Clear_Infantry", "zone:New Zone 86 with a long name", f32(17.5), 0, "", f32(17.5)),
                (g3, "Land_Scout_Area", "global", f32(50.0), 1, "", f32(50.0)),
            ]
            for got, typ, tgt, d, st, plan, e in want:
                checks = [got["type"] == typ, got["tgt"] == tgt, abs(got["d"] - d) < 1e-6, got["st"] == st,
                          got["plan"] == plan, abs(got["eff"] - e) < 1e-6]
                if not all(checks):
                    fails.append("goalInfo %s: %r" % (typ, got))
            if abs(eff(f32(15.001), 2) - 15.001) < 0.5:
                fails.append("effDesire expected value looks wrong")
        if test["cat9"] != 0:
            fails.append("setGoals: a missing category returned goals")
    print("selftest_hooks: " + ("OK" if not fails else "FAILED: " + "; ".join(fails)))
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
