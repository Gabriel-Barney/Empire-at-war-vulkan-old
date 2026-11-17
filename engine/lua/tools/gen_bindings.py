#!/usr/bin/env python3
"""Generate engine/lua/src/binding_table.inc from docs/re/lua_api.json and the engine source.

One row per Lua binding the original registers (143 globals + 323 wrapper methods). The table is
checked in; rerun after lua_api.json changes or after a binding's effect changes:

    toolchain\\venv\\Scripts\\python.exe -I engine\\lua\\tools\\gen_bindings.py

Row: { kind, owner class (methods) / impl class (globals), name, call sites in the RE corpora, effect, consumer }.
Globals without a recovered class get the class whose methods they expose (GameRandom ->
LuaGameRandom, ...) or "" (a plain function / host-set value).

Effect column (docs/MOD_COMPAT_DESIGN.md §9.1, roadmap mcx0 t04), found from the code:
  recorded     the binding appends a lua::PresentationOp (its op_name() in src/presentation.cpp names it); consumer =
               the first alamo_game source (engine/apps/game, ui, render, audio, video; tests excluded) that handles
               that op (`PresentationOp::X`, `Op::X` or `P::X`, or a comment "binding-consumer: <Name or Class.Name>"
               where the host reads the op's results indirectly), "" when none does
  noop-retail / noop-gap / partial / full
               an annotation "// binding-effect: <Name or Class.Name> <state> <spec or reason>" in engine/lua/src;
               the consumer column then holds the spec or reason
  full         every other binding (it does what retail does as far as the port knows)
A recorded binding with no consumer that is invisible by design takes the annotation state "recorded" with its reason;
its consumer column then reads "invisible: <reason>". tools/lint/check_tag_ledger.py checks that the committed table
matches the code (render()).
"""
import json
import os
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
SRC = ROOT / "docs" / "re" / "lua_api.json"
OUT = ROOT / "engine" / "lua" / "src" / "binding_table.inc"
LUA_SRC = ROOT / "engine" / "lua" / "src"
PRESENTATION_HPP = ROOT / "engine" / "lua" / "include" / "alamo" / "lua" / "presentation.hpp"
PRESENTATION_CPP = LUA_SRC / "presentation.cpp"
GAME_DIRS = ["engine/apps/game", "engine/ui", "engine/render", "engine/audio", "engine/video"]
EFFECTS = {"full": "Full", "recorded": "Recorded", "noop-retail": "NoopRetail", "noop-gap": "NoopGap", "partial": "Partial"}
ANNOTATION = re.compile(r"binding-effect:\s*([A-Za-z0-9_]+(?:\.[A-Za-z0-9_]+)?)\s+(full|recorded|noop-retail|noop-gap|partial)"
                        r"\s+(.+)")

# Globals whose registration class was not recovered but whose methods are known (LUA_API.md).
GLOBAL_CLASS_OVERRIDES = {
    "GameRandom": "LuaGameRandom",
    "GetCurrentTime": "LuaGetTime",
    "ThreadValue": "ThreadValue",
}


def c_str(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def table_rows():
    api = json.loads(SRC.read_text(encoding="utf-8"))["api"]
    globals_ = [e for e in api if e["kind"] == "global"]
    methods = [e for e in api if e["kind"] == "method"]
    rows = []
    for e in sorted(globals_, key=lambda e: e["name"]):
        cls = e.get("class") or ""
        if cls in ("?",):
            cls = ""
        cls = GLOBAL_CLASS_OVERRIDES.get(e["name"], cls)
        rows.append(("Global", cls, e["name"], int(e.get("calls_total") or 0)))
    for e in sorted(methods, key=lambda e: (e["owner"], e["name"])):
        rows.append(("Method", e["owner"], e["name"], int(e.get("calls_total_any_receiver") or 0)))
    return rows, len(globals_), len(methods)


def presentation_ops():
    """{binding key ("Name" / "Class.Name"): PresentationOp enumerator}, from presentation.hpp and op_name()."""
    hpp = PRESENTATION_HPP.read_text(encoding="utf-8")
    body = hpp[hpp.index("enum class PresentationOp"):]
    body = body[body.index("{") + 1:body.index("Count")]
    body = re.sub(r"//[^\n]*", "", body)
    enums = [e.strip() for e in body.split(",") if e.strip()]
    cpp = PRESENTATION_CPP.read_text(encoding="utf-8")
    table = cpp[cpp.index("kNames[] = {"):]
    table = table[:table.index("};")]
    names = re.findall(r'"([^"]+)"', table)
    if len(names) != len(enums):
        raise SystemExit(f"op_name table has {len(names)} names, PresentationOp {len(enums)} enumerators")
    return dict(zip(names, enums))


def op_consumers():
    """{PresentationOp enumerator: [alamo_game files handling it]}."""
    pat = re.compile(r"\b(?:PresentationOp|Op|P)::([A-Za-z0-9_]+)")
    note = re.compile(r"binding-consumer:\s*([A-Za-z0-9_]+(?:\.[A-Za-z0-9_]+)?)")
    ops = presentation_ops()
    out = {}
    for d in GAME_DIRS:
        base = ROOT / d
        if not base.is_dir():
            continue
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = sorted(x for x in dirnames if x not in ("tests", "tools"))
            rel_dir = pathlib.Path(dirpath).relative_to(ROOT).as_posix()
            for name in sorted(filenames):
                if not name.endswith((".cpp", ".hpp", ".h")):
                    continue
                text = (pathlib.Path(dirpath) / name).read_text(encoding="utf-8", errors="replace")
                for m in pat.finditer(text):
                    out.setdefault(m.group(1), set()).add(f"{rel_dir}/{name}")
                for m in note.finditer(text):  # an indirect consumer (a request list the host reads)
                    if m.group(1) in ops:
                        out.setdefault(ops[m.group(1)], set()).add(f"{rel_dir}/{name}")
                for m in note.finditer(text):  # an indirect consumer (a request list the host reads)
                    if m.group(1) in ops:
                        out.setdefault(ops[m.group(1)], set()).add(f"{rel_dir}/{name}")
    return {k: sorted(v) for k, v in out.items()}


def annotations():
    out, problems = {}, []
    for path in sorted(LUA_SRC.glob("*.cpp")):
        for ln, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            if "binding-effect:" not in line:
                continue
            m = ANNOTATION.search(line)
            if not m:
                problems.append(f"{path.name}:{ln}: malformed binding-effect annotation")
                continue
            out[m.group(1)] = (m.group(2), m.group(3).strip(), f"{path.name}:{ln}")
    return out, problems


def render():
    """(binding_table.inc text, annotation problems), computed from lua_api.json and the code."""
    rows, n_globals, n_methods = table_rows()
    ops = presentation_ops()
    consumers = op_consumers()
    notes, problems = annotations()
    keys = set()
    lines = [
        "// GENERATED by engine/lua/tools/gen_bindings.py from docs/re/lua_api.json and the engine source - do not edit.",
        f"// {n_globals} globals + {n_methods} methods. Row: kind, class, name, RE call sites, effect, consumer.",
    ]
    for kind, cls, name, calls in rows:
        key = name if kind == "Global" else f"{cls}.{name}"
        keys.add(key)
        effect, consumer = "full", ""
        op = ops.get(key)
        if op is not None:
            effect = "recorded"
            files = consumers.get(op, [])
            consumer = files[0] if files else ""
        note = notes.get(key)
        if note is not None:
            effect = note[0]
            if effect == "recorded":
                if not consumer:
                    consumer = "invisible: " + note[1]
            else:
                consumer = note[1]
        lines.append(f"{{BindingKind::{kind}, {c_str(cls)}, {c_str(name)}, {calls}, BindingEffect::{EFFECTS[effect]}, "
                     f"{c_str(consumer)}}},")
    for key, (_, _, where) in sorted(notes.items()):
        if key not in keys:
            problems.append(f"{where}: binding-effect names {key}, which is not in the binding table")
    return "\n".join(lines) + "\n", problems


def main() -> int:
    text, problems = render()
    for p in problems:
        print("annotation:", p)
    if problems:
        return 1
    OUT.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
