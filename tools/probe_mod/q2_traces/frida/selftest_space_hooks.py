"""Offline selftest of frida/skai_t22_space_hooks.js (skai t22), no game needed.

The same two steps as selftest_hooks.py (skai t15), on the space script:
1. Compile in a throwaway local process (a broken copy must fail).
2. Run the helpers in that process (the build-stamp check refuses it, so nothing is hooked) on fake memory with the
   layout of docs/re/SKAI_T15_RESOLVE_HOOK_RE.md: setGoals / goalInfo as in skai t15, plus what the space script adds:
   a space grid cell target (region target with no hint zone) gives 'cell:<x>,<y>' from its centre, and
   goalSystemMode tells a land, a space and another goal system apart by vtable.

    toolchain\\re\\venv\\Scripts\\python.exe -I tools\\probe_mod\\q2_traces\\frida\\selftest_space_hooks.py
"""
import json
import os
import subprocess
import sys
import threading

import frida

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import selftest_hooks as t15  # noqa: E402  (TEST_BODY builders, f32, eff)

SPACE_BODY = r"""
;(function () {
  const keep = [];
  function goal(typeName, target, d, st) {
    const g = Memory.alloc(0xa0);
    const typ = Memory.alloc(0x40);
    typ.writeUtf8String(typeName);  // inline std::string (<= 15 chars)
    typ.add(0x10).writeU64(typeName.length);
    typ.add(0x18).writeU64(15);
    const pair = Memory.alloc(0x10);
    pair.writePointer(typ);
    g.add(0x48).writePointer(pair);
    g.add(0x50).writeFloat(d);
    g.add(0x54).writeFloat(d);
    g.add(0x58).writePointer(target);
    g.add(0x74).writeS32(st);
    keep.push(g, typ, pair);
    return g;
  }
  const cell = Memory.alloc(0x40);
  cell.writePointer(base.add(RVA.regionTargetVt));
  cell.add(0x1c).writeFloat(-3600.0);
  cell.add(0x20).writeFloat(400.0);
  cell.add(0x28).writePointer(ptr(0));
  const scout = goal('Space_Scout', cell, 10.0, 0);
  const gsLand = Memory.alloc(0x10);
  gsLand.writePointer(base.add(RVA.landGoalSystemVt));
  const gsSpace = Memory.alloc(0x10);
  gsSpace.writePointer(base.add(RVA.spaceGoalSystemVt));
  const gsOther = Memory.alloc(0x10);
  gsOther.writePointer(base.add(0x8be298));
  keep.push(cell, gsLand, gsSpace, gsOther);
  send({ k: 'space_test', scout: goalInfo(scout),
         modes: [goalSystemMode(gsLand), goalSystemMode(gsSpace), goalSystemMode(gsOther)] });
})();
"""


def main():
    src = open(os.path.join(HERE, "skai_t22_space_hooks.js"), encoding="utf-8").read()
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
        test_src = src.replace("const EXE = 'StarWarsG.exe';", "const EXE = %s;" % json.dumps(exe)) + t15.TEST_BODY + SPACE_BODY
        msgs = []
        done = threading.Event()

        def on_message(msg, data):
            msgs.append(msg)
            if msg.get("type") == "error" or msg.get("payload", {}).get("k") == "space_test":
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
    land = next((p for p in payloads if p.get("k") == "test"), None)
    if land is None or len(land.get("cat4", [])) != 2 or land["cat4"][1].get("tgt") != "zone:New Zone 86 with a long name":
        fails.append("skai t15 readers changed: %r" % (land,))
    test = next((p for p in payloads if p.get("k") == "space_test"), None)
    if test is None:
        fails.append("no space test result")
    else:
        sc = test["scout"]
        if not (sc.get("type") == "Space_Scout" and sc.get("tgt") == "cell:-3600,400" and abs(sc.get("d") - 10.0) < 1e-6
                and sc.get("eff") == t15.eff(t15.f32(10.0), 0)):
            fails.append("space cell goal: %r" % (sc,))
        if test["modes"] != ["land", "space", ""]:
            fails.append("goalSystemMode: %r" % (test["modes"],))
    print("selftest_space_hooks: " + ("OK" if not fails else "FAILED: " + "; ".join(fails)))
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
