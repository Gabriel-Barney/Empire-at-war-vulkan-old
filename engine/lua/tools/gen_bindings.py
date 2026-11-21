#!/usr/bin/env python3
"""Generate engine/lua/src/binding_table.inc from docs/re/lua_api.json and the engine source.

One row per Lua binding the original registers (143 globals + 323 wrapper methods). The table is
checked in; rerun after lua_api.json changes or after a binding's effect changes:

    toolchain\\venv\\Scripts\\python.exe -I engine\\lua\\tools\\gen_bindings.py

Row: { kind, owner class (methods) / impl class (globals), name, call sites in the RE corpora, effect, consumer, hooks }.
hooks (ref5 t04): the bridge hooks ("Struct.hook", space separated) the binding reads, found from the code (binding_hook_reads);
src/bridge_hooks.inc lists every hook of the three hook structs with an is-set test.
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
  partial      also, without an annotation: a binding whose implementing function or the comment block right above it
               carries "[inferred]" (behaviour the port guessed; inferred_sites); consumer = "[inferred] <file:line>"
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


REG_ENTRY = re.compile(r'\{\s*([GM])\s*,\s*(?:"([^"]*)"|([A-Za-z_]\w*))\s*,\s*"([A-Za-z0-9_]+)"\s*,\s*([A-Za-z_]\w*)\s*\}')
FUNC_DEF = re.compile(r"^(?:static\s+)?int\s+([A-Za-z_]\w*)\s*\(\s*Call\s*&")
CLASS_CONST = re.compile(r'constexpr\s+const\s+char\s*\*\s*([A-Za-z_]\w*)\s*=\s*"([^"]+)"')
MACRO_DEF = re.compile(r"#define\s+([A-Za-z_]\w*)\(K\)")


def inferred_sites():
    """{binding key ("Name" / "Class.Name"): "file:line"} of the bindings whose implementing function, or the comment block
    right above it, carries an "[inferred]" marker (behaviour the port guessed, MOD_COMPAT_DESIGN.md §9.1: partial).
    Registration entries {G|M, class, name, fn}; a class given as a constant or as the K of a registration macro is
    resolved from the same file. An entry whose class is not resolved counts for the name's rows no explicit entry claims."""
    out, unresolved = {}, {}
    claimed = set()
    for path in sorted(LUA_SRC.glob("*.cpp")):
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        text = "\n".join(lines)
        consts = dict(CLASS_CONST.findall(text))
        # Function spans: definition line to the first column-0 "}" (or the same line for a one-line body), with the
        # comment lines directly above.
        marks = {}
        for i, line in enumerate(lines):
            m = FUNC_DEF.match(line)
            if not m:
                continue
            start = i
            while start > 0 and lines[start - 1].lstrip().startswith("//"):
                start -= 1
            end = i
            if line.count("{") == 0 or line.count("{") != line.count("}"):
                while end + 1 < len(lines) and lines[end].rstrip() != "}":
                    end += 1
            for j in range(start, end + 1):
                if "[inferred]" in lines[j]:
                    marks[m.group(1)] = f"{path.name}:{j + 1}"
                    break
        # Registration macros: the classes each one is instantiated with.
        macro_classes, macro_spans = {}, []
        for i, line in enumerate(lines):
            m = MACRO_DEF.search(line)
            if not m:
                continue
            end = i
            while end + 1 < len(lines) and lines[end].rstrip().endswith("\\"):
                end += 1
            macro_spans.append((i, end, m.group(1)))
            macro_classes[m.group(1)] = re.findall(re.escape(m.group(1)) + r'\("([^"]+)"\)', text)
        for i, line in enumerate(lines):
            for m in REG_ENTRY.finditer(line):
                kind, lit, const, name, fn = m.groups()
                if kind == "G":
                    classes = [None]
                elif lit is not None:
                    classes = [lit]
                elif const in consts:
                    classes = [consts[const]]
                else:
                    span = next((s for s in macro_spans if s[0] <= i <= s[1]), None)
                    classes = macro_classes.get(span[2], []) if span else []
                    if not classes:
                        classes = [""]
                for cls in classes:
                    key = name if kind == "G" else (f"{cls}.{name}" if cls else None)
                    if key is not None:
                        claimed.add(key)
                        if fn in marks:
                            out.setdefault(key, marks[fn])
                    elif fn in marks:
                        unresolved.setdefault(name, marks[fn])
    return out, unresolved, claimed


# ---- ref5 t04: the bridge hooks each binding reads ----------------------------------------------------------------------
# The three hook structs between the bindings and the sim (nullable std::function members). A binding row names the hooks
# its implementing function, or a lua/src function it calls (BINDING_HOOK_DEPTH levels down), reads as `->hook` / `.hook`;
# hook names are unique across the structs. bridge_hooks.inc lists every hook with a "is it set" test, which the host test
# (alamo_sim_battle_story_tests) uses against the real story, AI and object-script bridges.
INCLUDE = ROOT / "engine" / "lua" / "include" / "alamo" / "lua"
HOOKS_OUT = ROOT / "engine" / "lua" / "src" / "bridge_hooks.inc"
HOOK_STRUCTS = (("ObjectBridge", "object_bridge.hpp", "class ObjectBridge {"),
                ("RemainingScriptHooks", "script_remaining.hpp", "struct RemainingScriptHooks {"),
                ("TacticalScriptHooks", "script_gaps.hpp", "struct TacticalScriptHooks {"))
BINDING_HOOK_DEPTH = 1
FUNC_HEAD = re.compile(r"^(?:static\s+|inline\s+)?[A-Za-z_][\w:<>,\s\*&]*?\b([A-Za-z_]\w*)\s*\([^;]*$")


def _strip(text):
    text = re.sub(r"//[^\n]*", "", text)
    return re.sub(r'"(?:\\.|[^"\\])*"', '""', text)


def bridge_hooks():
    """[(struct, hook, optional)] in declaration order; raises on a name declared twice. A member whose comment block (the
    lines since the previous declaration, or its own line) says "hook-optional: <why>" has a retail-correct path without
    the hook (a speed-up, an AI-lane-only write, a fallback the binding's own effect states): binding rows leave it out. A
    name that is also a std::function member of another engine/lua header (FogScriptHooks.lane_write, ...) is ambiguous by
    name and is not attributed to a binding either."""
    out, seen = [], {}
    for struct, header, opener in HOOK_STRUCTS:
        text = (INCLUDE / header).read_text(encoding="utf-8")
        i = text.index(opener) + len(opener)
        depth, j = 1, i
        while depth:
            depth += {"{": 1, "}": -1}.get(text[j], 0)
            j += 1
        raw = text[i:j - 1]
        body = re.sub(r"//[^\n]*", lambda m: " " * len(m.group(0)), raw)  # same offsets, comments blanked
        last = 0
        for m in re.finditer(r"std::function<", body):
            k, depth = m.end(), 1
            while depth:
                depth += {"<": 1, ">": -1}.get(body[k], 0)
                k += 1
            name = re.match(r"\s*([A-Za-z_]\w*)\s*;", body[k:])
            if not name:
                continue
            end = k + name.end()
            start = max(body.rfind(";", 0, m.start()), body.rfind("{", 0, m.start()), body.rfind("}", 0, m.start()), last)
            line_end = raw.find("\n", end)
            optional = "hook-optional:" in raw[start:line_end if line_end >= 0 else len(raw)]
            last = end
            if name.group(1) in seen:
                raise SystemExit(f"hook {name.group(1)} declared in {seen[name.group(1)]} and {struct}")
            seen[name.group(1)] = struct
            out.append((struct, name.group(1), optional))
    return out


def ambiguous_hook_names():
    """Hook names that are also std::function members declared outside the three hook structs (any engine/lua header)."""
    counts = {}
    for path in sorted(INCLUDE.glob("*.hpp")):
        text = re.sub(r"//[^\n]*", "", path.read_text(encoding="utf-8"))
        for m in re.finditer(r"std::function<", text):
            k, depth = m.end(), 1
            while depth and k < len(text):
                depth += {"<": 1, ">": -1}.get(text[k], 0)
                k += 1
            name = re.match(r"\s*([A-Za-z_]\w*)\s*;", text[k:])
            if name:
                counts[name.group(1)] = counts.get(name.group(1), 0) + 1
    return {n for n, c in counts.items() if c > 1}


def lua_functions():
    """{(file name, function name): body text} of the top-level functions in engine/lua/src (comments and strings blanked)."""
    funcs = {}
    for path in sorted(LUA_SRC.glob("*.cpp")):
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        i = 0
        while i < len(lines):
            line = lines[i]
            m = FUNC_HEAD.match(line)
            if m and not line.startswith((" ", "\t", "}", "#", "/")) and m.group(1) not in ("if", "for", "while", "switch", "return"):
                j = i
                while j < len(lines) and "{" not in lines[j] and not lines[j].rstrip().endswith(";"):
                    j += 1
                if j < len(lines) and "{" in lines[j] and not lines[j].rstrip().endswith(";"):
                    end = j
                    if not (lines[j].count("{") == lines[j].count("}") and lines[j].rstrip().endswith("}")):
                        while end + 1 < len(lines) and lines[end].rstrip() != "}":
                            end += 1
                    key = (path.name, m.group(1))
                    funcs[key] = funcs.get(key, "") + _strip("\n".join(lines[i:end + 1]))
                    i = end + 1
                    continue
            i += 1
    return funcs


def binding_hook_reads():
    """{binding key ("Name" / "Class.Name"): sorted ["Struct.hook"]}: the hooks each registered binding reads."""
    ambiguous = ambiguous_hook_names()
    hooks = {h: s for s, h, optional in bridge_hooks() if not optional and h not in ambiguous}
    funcs = lua_functions()
    access = re.compile(r"(?:->|\.)\s*([A-Za-z_]\w*)\b")
    called = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
    files_of = {}
    for f, name in funcs:
        files_of.setdefault(name, []).append(f)

    def resolve(f, name):
        """A call from file `f`: that file's own definition (an anonymous-namespace helper), else every other file's."""
        if (f, name) in funcs:
            return [(f, name)]
        return [(g, name) for g in files_of.get(name, [])]

    direct, calls = {}, {}
    for (f, name), body in funcs.items():
        direct[(f, name)] = {m.group(1) for m in access.finditer(body) if m.group(1) in hooks}
        calls[(f, name)] = {k for m in called.finditer(body) if m.group(1) != name for k in resolve(f, m.group(1))}

    def reads(fn):
        seen, frontier, out = {fn}, [fn], set(direct.get(fn, ()))
        for _ in range(BINDING_HOOK_DEPTH):
            nxt = []
            for f in frontier:
                for g in sorted(calls.get(f, ())):
                    if g not in seen:
                        seen.add(g)
                        nxt.append(g)
                        out |= direct.get(g, set())
            frontier = nxt
        return out

    out = {}
    for path in sorted(LUA_SRC.glob("*.cpp")):
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        text = "\n".join(lines)
        consts = dict(CLASS_CONST.findall(text))
        macro_spans, macro_classes = [], {}
        for i, line in enumerate(lines):
            m = MACRO_DEF.search(line)
            if not m:
                continue
            end = i
            while end + 1 < len(lines) and lines[end].rstrip().endswith("\\"):
                end += 1
            macro_spans.append((i, end, m.group(1)))
            macro_classes[m.group(1)] = re.findall(re.escape(m.group(1)) + r'\("([^"]+)"\)', text)
        for i, line in enumerate(lines):
            for m in REG_ENTRY.finditer(line):
                kind, lit, const, name, fn = m.groups()
                if kind == "G":
                    classes = [None]
                elif lit is not None:
                    classes = [lit]
                elif const in consts:
                    classes = [consts[const]]
                else:
                    span = next((s for s in macro_spans if s[0] <= i <= s[1]), None)
                    classes = macro_classes.get(span[2], []) if span else []
                hs = set()
                for k in resolve(path.name, fn):
                    hs |= reads(k)
                if not hs:
                    continue
                for cls in classes:
                    key = name if cls is None else f"{cls}.{name}"
                    out.setdefault(key, set()).update(f"{hooks[h]}.{h}" for h in hs)
    return {k: sorted(v) for k, v in out.items()}


def render_hooks():
    lines = ["// GENERATED by engine/lua/tools/gen_bindings.py from the hook structs (object_bridge.hpp, script_remaining.hpp,",
             "// script_gaps.hpp) - do not edit. Row: struct, hook, is-set test (the argument points at that struct)."]
    for struct, hook, _ in bridge_hooks():
        lines.append(f"{{{c_str(struct)}, {c_str(hook)}, [](const void* p) {{ return static_cast<bool>(static_cast<const {struct}*>(p)->{hook}); }}}},")
    return "\n".join(lines) + "\n"


def render():
    """(binding_table.inc text, annotation problems), computed from lua_api.json and the code."""
    rows, n_globals, n_methods = table_rows()
    ops = presentation_ops()
    consumers = op_consumers()
    notes, problems = annotations()
    inferred, inferred_by_name, claimed = inferred_sites()
    hook_reads = binding_hook_reads()
    keys = set()
    lines = [
        "// GENERATED by engine/lua/tools/gen_bindings.py from docs/re/lua_api.json and the engine source - do not edit.",
        f"// {n_globals} globals + {n_methods} methods. Row: kind, class, name, RE call sites, effect, consumer, hooks read.",
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
        else:
            site = inferred.get(key)
            if site is None and kind == "Method" and key not in claimed:
                site = inferred_by_name.get(name)
            if site is not None:
                effect, consumer = "partial", "[inferred] " + site
        note = notes.get(key)
        if note is not None:
            effect = note[0]
            if effect == "recorded":
                if not consumer:
                    consumer = "invisible: " + note[1]
            else:
                consumer = note[1]
        hooks = " ".join(hook_reads.get(key, []))
        lines.append(f"{{BindingKind::{kind}, {c_str(cls)}, {c_str(name)}, {calls}, BindingEffect::{EFFECTS[effect]}, "
                     f"{c_str(consumer)}, {c_str(hooks)}}},")
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
    HOOKS_OUT.write_text(render_hooks(), encoding="utf-8", newline="\n")
    print(f"wrote {OUT} and {HOOKS_OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
