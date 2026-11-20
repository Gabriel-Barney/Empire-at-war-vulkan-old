"""Save ledger lint (roadmap ref2 t01: TacticalBattle; ref4 t02: every saved module in TABLE below).

Every saved class in TABLE has a ledger header that lists each of its data members once. Two rule sets:

Generic classes (MovementSystem, CombatSystem, AbilitySystem, GalacticSim, ConflictManager, StorySession, StoryModel,
CampaignBattles, BattleAi, GalacticSimTokenResolver): the ledger macro takes four kinds,
  SAVED(member, hashed)      written by a save function, restored by a read function and walked by a hash function;
  SAVED(member, unhashed)    written and restored, not in the CRC (the reason goes in the comment of the line);
  REBUILT(member, "why")     set again from the same inputs when a load rebuilds the owner (setup, host, map load);
  DERIVED(member, "why")     recomputed from saved state: a cache, scratch, the records of one tick, a host callback;
  CHECKED(member, "why")     not restored, but the save / read functions read it (a fingerprint the read checks, the
                             object store or rules they look ids up in).
The check fails when a member is missing, listed twice or stale; when a save function names a REBUILT or DERIVED member
(mark it SAVED or CHECKED); when a SAVED member is not named by a save function or by a read function; when a
SAVED(.., hashed) member is not named by a hash function, or a SAVED(.., unhashed) one is (fix the ledger). A function
is named in TABLE by `Class::name` (a regex such as `hash_\\w+` matches several); the check reads every definition of it
in the entry's source files and in the class header, and the member names those bodies mention (`\\bname\\b`). A member
written by a helper (a free function handed `*this`'s fields) is named in the function that hands it over.

TacticalBattle (the first ledger) keeps its own rules, described here:
Every data member of TacticalBattle (engine/apps/sim/include/alamo/sim/tactical_battle.hpp) is declared once in the
ledger engine/apps/sim/src/tactical_battle_ledger.hpp as SAVED (written by state_io or a module save part), REBUILT (set
again by the rebuilt battle's setup from the same inputs) or DERIVED (recomputed from saved state: caches, per-tick
scratch, host-fed gates). This check fails when
  - a member is missing from the ledger, or listed twice, or the ledger names something that is not a member;
  - state_io (tactical_battle_save.cpp) touches a member the ledger does not mark SAVED, or a SAVED entry of kind
    "state" is not touched by state_io;
  - a SAVED entry of kind "part" (a module system written by write_part) is not named in write_part.
The battle itself checks the state_io side at run time too (TacticalBattle::check_save_ledger, a probe archive: every
field state_io walks lies inside a SAVED member and every SAVED "state" member is walked), from the save-and-continue
test (alamo_sim_save_continue_tests).

Scope: each class's own non-static data members (a record's fields are not covered: a field added to PlanetState, Unit,
... must be added to its save / hash by hand, or the record walked by one visit function). The parser reads
declarations, not C++: --selftest holds the forms it must find, the generic rules on a synthetic class, and plants a
member in every class of TABLE (the real files) expecting exactly one new "missing" error.
Usage: check_battle_ledger.py [--selftest] [--list [Class]] [--only Class]
  --list      print the members the parser found with their ledger kind (and, for generic classes, which save / read /
              hash functions name them)
Exit: 0 in step, 1 differences, 2 a file is missing. Runs in build_agent.cmd (before the build) and as the ctest entry
alamo_sim_battle_ledger_lint.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
HEADER = os.path.join(ROOT, "engine", "apps", "sim", "include", "alamo", "sim", "tactical_battle.hpp")
LEDGER = os.path.join(ROOT, "engine", "apps", "sim", "src", "tactical_battle_ledger.hpp")
SAVE = os.path.join(ROOT, "engine", "apps", "sim", "src", "tactical_battle_save.cpp")

# Statements that declare no instance data. `static` data members are shared by every battle, not battle state.
SKIP_STARTS = ("using ", "friend ", "static ", "static_assert", "template", "typedef ", "#", "explicit ", "virtual ",
               "constexpr ", "inline ", "operator", "~")


def strip_comments(text):
    """Comments and string / char literals out (literals become empty), line breaks kept."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            seg = text[i:n if j < 0 else j + 2]
            out.append("\n" * seg.count("\n"))
            i = n if j < 0 else j + 2
        elif c == "'" and i > 0 and text[i - 1].isdigit():
            out.append(c)  # a digit separator (1'000), not a char literal
            i += 1
        elif c == '"' or c == "'":
            if c == '"' and i > 0 and text[i - 1] == "R":  # raw string R"(...)"
                k = text.find("(", i)
                delim = text[i + 1:k]
                end = text.find(")" + delim + '"', k)
                i = n if end < 0 else end + len(delim) + 2
                out.append('""')
                continue
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(c + c)
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def class_body(text, name):
    """The text between the braces of `class <name> [final] [: bases] {` (comments already stripped)."""
    m = re.search(r"\b(?:class|struct)\s+" + re.escape(name) + r"\s*(?:final\s*)?(?::[^;{]*)?\{", text)
    if not m:
        return None
    depth, i = 1, m.end()
    while i < len(text) and depth:
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
        i += 1
    return text[m.end():i - 1]


def top_statements(body):
    """Statements at depth 0 of a class body: a nested brace block is kept inside its statement, a function body or a
    nested type's body ends the statement (`void f() { ... }` has no `;`)."""
    stmts, cur, depth, i = [], [], 0, 0
    while i < len(body):
        c = body[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                cur.append(c)
                s = "".join(cur).strip()
                # A brace block that closes a statement-level body (function, nested type) when the statement is not an
                # initializer: `x_{...}` / `= {...}` keep going to the `;`.
                head = s[:s.find("{")]
                rest = body[i + 1:].lstrip()
                is_type = re.match(r"^(struct|class|enum|union)\b", head.strip())
                if is_type and not rest.startswith(";"):
                    i += 1  # `struct {...} x_;`: the declarator follows the body, the statement ends at its `;`
                    continue
                if ")" in head or is_type:
                    if rest.startswith(";"):
                        i = body.index(";", i)
                    stmts.append(s)
                    cur = []
                    i += 1
                    continue
                i += 1
                continue
        elif c == ";" and depth == 0:
            stmts.append("".join(cur).strip())
            cur = []
            i += 1
            continue
        cur.append(c)
        i += 1
    return [" ".join(s.split()) for s in stmts if s.strip()]


def split_top(s, sep=","):
    parts, depth, cur = [], 0, []
    for c in s:
        if c in "<([{":
            depth += 1
        elif c in ">)]}":
            depth -= 1
        if c == sep and depth == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(c)
    parts.append("".join(cur))
    return parts


TYPE_SYNTAX = ("decltype", "alignas", "noexcept", "sizeof", "alignof")


def cut_init(p):
    """A declarator without its initializer: the text before the first top-level `=` (not `==`, `<=`, ...) or `{`."""
    depth = 0
    for k, c in enumerate(p):
        if c in "<([":
            depth += 1
        elif c in ">)]":
            depth -= 1
        elif depth == 0 and c == "{":
            return p[:k]
        elif depth == 0 and c == "=" and (k == 0 or p[k - 1] not in "=<>!") and p[k + 1:k + 2] != "=":
            return p[:k]
    return p


def is_function(lhs):
    """True for a function declaration: the first top-level `(` (outside template arguments) follows a name that is not
    type syntax (decltype, alignas, ...). `int (*fp_)(int)` (a `(` after no name) is a pointer member."""
    depth, k = 0, 0
    while k < len(lhs):
        c = lhs[k]
        if c == "<":
            depth += 1
        elif c == ">":
            depth -= 1
        elif c == "(" and depth == 0:
            m = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*$", lhs[:k])
            if m and m.group(1) in TYPE_SYNTAX:
                level, k = 1, k + 1  # skip the group
                while k < len(lhs) and level:
                    level += {"(": 1, ")": -1}.get(lhs[k], 0)
                    k += 1
                continue
            if lhs[k + 1:].lstrip()[:1] in ("*", "&"):
                return False  # `int (*fp_)(int)`: a pointer declarator, not a parameter list
            return bool(m)
        k += 1
    return False


def declarator_name(p):
    p = cut_init(p).strip()
    m = re.search(r"\(\s*[\*&]+\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)\s*[\(\[]", p)  # (*fp_)(args) / (&r_)[n]
    if m:
        return m.group(1)
    p = re.sub(r"(\[[^\]]*\]\s*)+$", "", p).strip()  # array extents
    m = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*$", p)
    return m.group(1) if m and m.group(1) != "operator" else None


def data_members(header_text, cls="TacticalBattle"):
    text = re.sub(r"(?m)^[ \t]*#.*$", "", strip_comments(header_text))  # #ifdef blocks: both sides are members
    body = class_body(text, cls)
    if body is None:
        return None
    names = []
    for raw in top_statements(body):
        s = raw
        while True:  # access labels (several in a row)
            t = re.sub(r"^(public|private|protected)\s*:\s*", "", s)
            if t == s:
                break
            s = t
        s = re.sub(r"^mutable\s+", "", s)
        if not s or s.startswith(SKIP_STARTS):
            continue
        tm = re.match(r"^(struct|class|union|enum(?:\s+class|\s+struct)?)\b", s)
        if tm:
            if "{" in s:  # a nested type; with a declarator after its body (`struct {...} x_;`) also a member
                s = s[s.rfind("}") + 1:].strip()
                if not s:
                    continue
                s = "T " + s
            else:  # an elaborated type (`struct lua_State* L_;`) or a forward declaration (`struct X;`)
                s = s[tm.end():].strip()
        if s.endswith("}") and is_function(s[:s.find("{")]):
            continue  # inline function definition
        parts = split_top(s)
        if is_function(cut_init(parts[0])):
            continue
        for k, p in enumerate(parts):
            name = declarator_name(p)
            if not name:
                continue
            if k == 0 and not re.search(r"[\s\*&>)]", cut_init(p).strip()):
                continue  # a lone word (a forward declaration, a macro use)
            names.append(name)
    return names


LEDGER_RE = re.compile(r"\b(SAVED|REBUILT|DERIVED)\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,\s*(state|part|[A-Za-z_]+)?")


def ledger_entries(text):
    entries = []
    body = strip_comments(text)
    for m in re.finditer(r"\b(SAVED|REBUILT|DERIVED)\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*(?:,\s*([A-Za-z_]+))?", body):
        if m.group(2) in ("m", "member", "name"):
            continue  # the macro's own parameter list
        entries.append((m.group(1), m.group(2), m.group(3) or ""))
    return entries


def state_io_body(save_text):
    t = strip_comments(save_text)
    m = re.search(r"void\s+TacticalBattle::state_io\s*\(", t)
    if not m:
        return None
    i = t.index("{", m.end())
    depth, j = 1, i + 1
    while depth and j < len(t):
        depth += {"{": 1, "}": -1}.get(t[j], 0)
        j += 1
    return t[i:j]


def write_part_body(save_text):
    t = strip_comments(save_text)
    m = re.search(r"void\s+TacticalBattle::write_part\s*\(", t)
    if not m:
        return None
    i = t.index("{", m.end())
    depth, j = 1, i + 1
    while depth and j < len(t):
        depth += {"{": 1, "}": -1}.get(t[j], 0)
        j += 1
    return t[i:j]


def chunk_writer(header_text, name):
    """A save chunk of its own: an inline `void write_<x>(ByteWriter& w) const { ... name ... }` of TacticalBattle."""
    t = strip_comments(header_text)
    return any(re.search(r"\b" + re.escape(name) + r"\b", m.group(1))
               for m in re.finditer(r"\bvoid\s+write_\w+\s*\([^)]*\)\s*const\s*\{([^{}]*)\}", t))


def check(header_text, ledger_text, save_text):
    errors = []
    members = data_members(header_text)
    if members is None:
        return ["tactical_battle.hpp: class TacticalBattle not found"]
    entries = ledger_entries(ledger_text)
    seen = {}
    for kind, name, how in entries:
        if name in seen:
            errors.append(f"ledger: {name} is listed twice ({seen[name]} and {kind})")
        seen[name] = kind
        if kind == "SAVED" and how not in ("state", "part", "chunk"):
            errors.append(f"ledger: SAVED({name}, ...) needs `state` (state_io), `part` (write_part) or `chunk`")
    mset = set(members)
    dup = sorted({n for n in members if members.count(n) > 1})
    for n in dup:
        errors.append(f"tactical_battle.hpp: member {n} declared twice (parser)")
    for n in members:
        if n not in seen:
            errors.append(f"ledger: TacticalBattle::{n} is missing; add it as SAVED, REBUILT or DERIVED "
                          f"(engine/apps/sim/src/tactical_battle_ledger.hpp)")
    for name in seen:
        if name not in mset:
            errors.append(f"ledger: {name} is not a TacticalBattle data member (renamed or removed?)")
    sio = state_io_body(save_text)
    wp = write_part_body(save_text)
    if sio is None or wp is None:
        errors.append("tactical_battle_save.cpp: state_io or write_part not found")
        return errors
    touched = set(re.findall(r"\bb\.([A-Za-z_][A-Za-z0-9_]*)", sio))
    for name in sorted(touched):
        if name not in mset:
            continue  # a member function call (b.foo()) is not state
        kind = seen.get(name)
        if kind != "SAVED":
            errors.append(f"state_io: writes {name}, which the ledger marks {kind or 'nothing'} (mark it SAVED, state)")
    for kind, name, how in entries:
        if kind != "SAVED":
            continue
        if how == "state" and name not in touched:
            errors.append(f"state_io: SAVED({name}, state) is not written by state_io")
        if how == "part" and not re.search(r"\b" + re.escape(name) + r"\b", wp):
            errors.append(f"write_part: SAVED({name}, part) is not written by write_part")
        if how == "chunk" and not chunk_writer(header_text, name):
            errors.append(f"SAVED({name}, chunk): no inline TacticalBattle::write_*() const in tactical_battle.hpp writes it")
    return errors


# ---------------------------------------------------------------------------------------------- generic classes (ref4 t02)

class Entry:
    """One saved class: its header, its ledger header, the source files that define its functions, and the regexes of
    its save, read and hash function names (`Class::<name>`)."""

    def __init__(self, cls, header, ledger, sources, save, read, hash_fns):
        self.cls, self.header, self.ledger = cls, header, ledger
        self.sources, self.save, self.read, self.hash = sources, save, read, hash_fns


ENG = "engine/"
TABLE = [
    Entry("MovementSystem", ENG + "tactical/movement/include/alamo/tactical/movement/movement_system.hpp",
          ENG + "tactical/movement/src/movement_ledger.hpp", [ENG + "tactical/movement/src"],
          save=["write"], read=["read"], hash_fns=[r"hash_\w+", "register_crc"]),
    Entry("CombatSystem", ENG + "tactical/combat/include/alamo/tactical/combat/combat_system.hpp",
          ENG + "tactical/combat/src/combat_ledger.hpp", [ENG + "tactical/combat/src"],
          save=["write"], read=["read"], hash_fns=[r"hash_\w+", r"register_\w*crc"]),
    Entry("AbilitySystem", ENG + "tactical/abilities/include/alamo/tactical/abilities/ability_system.hpp",
          ENG + "tactical/abilities/src/ability_ledger.hpp", [ENG + "tactical/abilities/src"],
          save=["write"], read=["read"], hash_fns=[r"hash\w*", "register_crc"]),
    Entry("GalacticSim", ENG + "galactic/include/alamo/galactic/campaign.hpp",
          ENG + "galactic/src/galactic_ledger.hpp", [ENG + "galactic/src"],
          save=["write"], read=["read"], hash_fns=[r"hash_\w+", "register_crc"]),
    Entry("ConflictManager", ENG + "galactic/include/alamo/galactic/conflicts.hpp",
          ENG + "galactic/src/conflicts_ledger.hpp", [ENG + "galactic/src"],
          save=["write"], read=["read"], hash_fns=[r"hash\w*", "register_crc"]),
    Entry("StorySession", ENG + "apps/sim/include/alamo/sim/story_session.hpp",
          ENG + "apps/sim/src/story_session_ledger.hpp", [ENG + "apps/sim/src"],
          save=[r"write\w*"], read=[r"read\w*"], hash_fns=[r"hash\w*", "register_crc"]),
]


def source_texts(entry):
    """Comment-stripped text of every .cpp / .hpp under the entry's source folders (or the named files), and the
    header's."""
    paths = []
    for s in entry.sources:
        p = os.path.join(ROOT, s)
        if os.path.isdir(p):
            paths += [os.path.join(p, f) for f in sorted(os.listdir(p)) if f.endswith((".cpp", ".hpp", ".inl"))]
        else:
            paths.append(p)
    paths.append(os.path.join(ROOT, entry.header))
    out = []
    for p in paths:
        with open(p, encoding="utf-8") as f:
            out.append(strip_comments(f.read()))
    return out


def matching_paren(t, k):
    """The index after the `)` that closes the `(` before t[k]."""
    depth = 1
    while k < len(t) and depth:
        depth += {"(": 1, ")": -1}.get(t[k], 0)
        k += 1
    return k


def brace_block(t, i):
    """t[i] is `{`: the block up to its matching `}`."""
    depth, j = 1, i + 1
    while depth and j < len(t):
        depth += {"{": 1, "}": -1}.get(t[j], 0)
        j += 1
    return t[i:j]


QUALS = re.compile(r"\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?(?:final\s*)?(?:->\s*[^{;]+)?\{")


def function_bodies(texts, cls, name_res, class_text=None):
    """Every definition of `cls::<name>(...) {...}` with <name> matching one of name_res, out of class in `texts` and
    inline in the class body `class_text`: a list of (name, body)."""
    names = "(?:" + "|".join(name_res) + ")"
    out = []
    qualified = re.compile(r"\b" + re.escape(cls) + r"::(" + names + r")\s*\(")
    inline = re.compile(r"(?<![\w.>:])(" + names + r")\s*\(")
    for t, pat in [(t, qualified) for t in texts] + ([(class_text, inline)] if class_text else []):
        for m in pat.finditer(t):
            if not re.fullmatch(names, m.group(1)):
                continue
            k = matching_paren(t, m.end())
            q = QUALS.match(t, k)
            if not q:
                continue
            out.append((m.group(1), brace_block(t, q.end() - 1)))
    return out


def generic_entries(ledger_text):
    entries = []
    for m in re.finditer(r"\b(SAVED|REBUILT|DERIVED|CHECKED)\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*(?:,\s*([A-Za-z_]+))?",
                         strip_comments(ledger_text)):
        if m.group(2) in ("m", "member", "name"):
            continue  # a macro's own parameter list
        entries.append((m.group(1), m.group(2), m.group(3) or ""))
    return entries


def named(bodies, mset):
    """Members of mset each body mentions: {member: [function names]}."""
    out = {}
    for fn, body in bodies:
        for w in set(re.findall(r"\b[A-Za-z_][A-Za-z0-9_]*\b", body)) & mset:
            out.setdefault(w, []).append(fn)
    return out


def generic_usage(entry, header_text, texts):
    members = data_members(header_text, entry.cls)
    if members is None:
        return None, None, None
    stripped = re.sub(r"(?m)^[ \t]*#.*$", "", strip_comments(header_text))
    body = class_body(stripped, entry.cls)
    mset = set(members)
    use, found = {}, {}
    for key, fns in (("save", entry.save), ("read", entry.read), ("hash", entry.hash)):
        bodies = function_bodies(texts, entry.cls, fns, body)
        found[key] = bool(bodies)
        use[key] = named(bodies, mset)
    return members, use, found


def check_generic(entry, header_text, ledger_text, texts):
    """The generic rules (module docstring) for one class; a list of error strings."""
    c = entry.cls
    members, use, found = generic_usage(entry, header_text, texts)
    if members is None:
        return [f"{entry.header}: class {c} not found"]
    errors = []
    for key, fns in (("save", entry.save), ("read", entry.read), ("hash", entry.hash)):
        if not found[key]:
            errors.append(f"{c}: no {key} function ({', '.join(fns)}) found in {', '.join(entry.sources)}")
    entries = generic_entries(ledger_text)
    seen = {}
    for kind, name, how in entries:
        if name in seen:
            errors.append(f"{c} ledger: {name} is listed twice ({seen[name][0]} and {kind})")
        seen[name] = (kind, how)
        if kind == "SAVED" and how not in ("hashed", "unhashed"):
            errors.append(f"{c} ledger: SAVED({name}, ...) needs `hashed` or `unhashed`")
    mset = set(members)
    for n in sorted({n for n in members if members.count(n) > 1}):
        errors.append(f"{c}: member {n} declared twice (parser)")
    for n in members:
        if n not in seen:
            errors.append(f"{c} ledger: {c}::{n} is missing; add it as SAVED, REBUILT, DERIVED or CHECKED ({entry.ledger})")
    for name in seen:
        if name not in mset:
            errors.append(f"{c} ledger: {name} is not a {c} data member (renamed or removed?)")
    for name, (kind, how) in seen.items():
        if name not in mset:
            continue
        saved_by, read_by, hashed_by = use["save"].get(name), use["read"].get(name), use["hash"].get(name)
        if kind == "SAVED":
            if not saved_by:
                errors.append(f"{c}: SAVED({name}) is not named by a save function ({', '.join(entry.save)})")
            if not read_by:
                errors.append(f"{c}: SAVED({name}) is not named by a read function ({', '.join(entry.read)}): "
                              f"written but not restored")
            if how == "hashed" and not hashed_by:
                errors.append(f"{c}: SAVED({name}, hashed) is not named by a hash function ({', '.join(entry.hash)})")
            if how == "unhashed" and hashed_by:
                errors.append(f"{c}: SAVED({name}, unhashed) is named by {', '.join(sorted(set(hashed_by)))} "
                              f"(mark it hashed)")
        elif kind in ("REBUILT", "DERIVED") and saved_by:
            errors.append(f"{c}: {', '.join(sorted(set(saved_by)))} names {name}, which the ledger marks {kind} "
                          f"(mark it SAVED, or CHECKED when the save only reads it)")
    return errors


def plant(header_text, cls):
    """The header with one more data member in class `cls` (right after its opening brace)."""
    m = re.search(r"\b(?:class|struct)\s+" + re.escape(cls) + r"\s*(?:final\s*)?(?::[^;{]*)?\{", header_text)
    return header_text[:m.end()] + "\n    int planted_member_ = 0;\n" + header_text[m.end():]


def read_text(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        return f.read()


def run_entry(entry, header_override=None):
    for p in (entry.header, entry.ledger):
        if not os.path.isfile(os.path.join(ROOT, p)):
            return [f"{p} is missing"], 2
    header = header_override if header_override is not None else read_text(entry.header)
    return check_generic(entry, header, read_text(entry.ledger), source_texts(entry)), 0


def selftest_generic():
    header = """
class Thing final : public Base {
public:
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
    void hash(StateHasher& h) const { h.u32(count_); hash_more(h); }
private:
    void hash_more(StateHasher& h) const;
    const Store* store_ = nullptr;
    int count_ = 0;
    std::vector<int> names_;
    int cache_ = 0;
    int setup_ = 0;
};
"""
    src = """
void Thing::write(ByteWriter& w) const { w.u32(store_->size()); w.i32(count_); put(w, names_); }
bool Thing::read(ByteReader& r) { if (r.u32() != store_->size()) return false; count_ = r.i32(); get(r, names_); cache_ = 0; return true; }
void Thing::hash_more(StateHasher& h) const { (void)h; }
"""
    ledger = """
#define THING_LEDGER(SAVED, REBUILT, DERIVED, CHECKED) \\
    CHECKED(store_, "the store the ids are looked up in") \\
    SAVED(count_, hashed) \\
    SAVED(names_, unhashed) \\
    DERIVED(cache_, "a cache") \\
    REBUILT(setup_, "setup")
"""
    e = Entry("Thing", "h", "l", [], ["write"], ["read"], [r"hash\w*"])

    def run(hd=header, s=src, ld=ledger):
        return check_generic(e, hd, ld, [strip_comments(s), strip_comments(hd)])

    assert run() == [], run()
    r = run(hd=plant(header, "Thing"))
    assert len(r) == 1 and "planted_member_ is missing" in r[0], r
    r = run(s=src.replace("get(r, names_);", ""))  # written but not restored
    assert len(r) == 1 and "SAVED(names_) is not named by a read" in r[0], r
    r = run(s=src.replace("put(w, names_);", "put(w, names_); w.i32(cache_);"))  # writes a derived member
    assert len(r) == 1 and "names cache_" in r[0] and "DERIVED" in r[0], r
    r = run(hd=header.replace("h.u32(count_);", ""))  # a hashed member no hash function names
    assert len(r) == 1 and "SAVED(count_, hashed)" in r[0], r
    r = run(s=src.replace("(void)h;", "h.u32(names_.size());"))  # an unhashed member the hash names
    assert len(r) == 1 and "SAVED(names_, unhashed)" in r[0] and "hash_more" in r[0], r
    r = run(ld=ledger.replace("    REBUILT(setup_, \"setup\")\n", "    REBUILT(setup_, \"setup\") \\\n    SAVED(gone_, hashed)\n"))
    assert len(r) == 1 and "gone_ is not a Thing data member" in r[0], r
    print("check_battle_ledger selftest (generic rules): ok")


def selftest_planted():
    """A member planted in each TABLE class (the real files) gives exactly one new error: it is missing."""
    for entry in TABLE:
        base, code = run_entry(entry)
        if code:
            raise AssertionError(base)
        got, _ = run_entry(entry, plant(read_text(entry.header), entry.cls))
        new = [x for x in got if x not in base]
        assert len(new) == 1 and "planted_member_ is missing" in new[0], (entry.cls, new)
    hb = read_text(os.path.relpath(HEADER, ROOT))
    with open(LEDGER, encoding="utf-8") as f:
        lb = f.read()
    with open(SAVE, encoding="utf-8") as f:
        sb = f.read()
    base = check(hb, lb, sb)
    new = [x for x in check(plant(hb, "TacticalBattle"), lb, sb) if x not in base]
    assert len(new) == 1 and "planted_member_ is missing" in new[0], ("TacticalBattle", new)
    print(f"check_battle_ledger selftest (planted member in {len(TABLE) + 1} classes): ok")


def selftest():
    selftest_battle()
    selftest_generic()
    selftest_planted()
    return 0


def selftest_battle():
    header = """
class TacticalBattle {
public:
    struct Inner { int not_a_member_ = 0; };
    void f(int x);
    int g() const { return a_; }
    static constexpr int kN = 3;
    std::function<void(int)> cb_;
    void write_groups(ByteWriter& w) const { groups_.write(w); }
private:
    Groups groups_;
    template <class Ar, class Self>
    static void state_io(Ar& a, Self& b);
    int a_ = 0, b_ = 1;
    std::vector<std::pair<int, std::string>> hints_;
    float arr_[4] = {};
    std::unique_ptr<X> sys_;
#ifdef HAVE_X
    mutable std::unique_ptr<X> own_;
#endif
    TacticalBattle& operator=(const TacticalBattle&) = delete;
    int c_{3};
};
"""
    ledger = """
#define ALAMO_TACTICAL_BATTLE_LEDGER(SAVED, REBUILT, DERIVED) \\
    SAVED(groups_, chunk) \
    SAVED(a_, state, "x") \\
    SAVED(b_, state, "x") \\
    SAVED(arr_, state, "") \\
    SAVED(sys_, part, "") \\
    REBUILT(hints_, "map load") \\
    DERIVED(cb_, "host") \\
    DERIVED(own_, "cache") \
    DERIVED(c_, "cache")
"""
    save = """
void TacticalBattle::state_io(Ar& a, Self& b) { a(b.a_, b.b_); a(b.arr_[0]); (void)b.g(); }
void TacticalBattle::write_part(SavePart p, ByteWriter& w) const { put_system(w, sys_.get()); }
"""
    got = data_members(header)
    want = ["cb_", "groups_", "a_", "b_", "hints_", "arr_", "sys_", "own_", "c_"]
    assert got == want, got
    assert check(header, ledger, save) == [], check(header, ledger, save)
    # a member missing from the ledger
    e = check(header.replace("int c_{3};", "int c_{3};\n    bool d_ = false;"), ledger, save)
    assert len(e) == 1 and "d_ is missing" in e[0], e
    # state_io writes a member the ledger marks REBUILT
    e = check(header, ledger, save.replace("a(b.arr_[0]);", "a(b.arr_[0], b.hints_);"))
    assert len(e) == 1 and "hints_" in e[0] and "REBUILT" in e[0], e
    # a SAVED state member that state_io does not write
    e = check(header, ledger, save.replace("a(b.a_, b.b_);", "a(b.a_);"))
    assert len(e) == 1 and "SAVED(b_, state)" in e[0], e
    # a SAVED part member write_part does not name, a stale entry, a duplicate
    e = check(header, ledger + "    SAVED(gone_, state, \"\")\n    DERIVED(a_, \"x\")\n",
              save.replace("sys_.get()", "nullptr"))
    assert any("SAVED(sys_, part)" in x for x in e) and any("gone_ is not" in x for x in e) and \
        any("a_ is listed twice" in x for x in e), e
    # declaration forms (review of ref2 t01): every member found, nothing else
    edge = """
class TacticalBattle {
    decltype(x) d_;
    alignas(16) float b_[4];
    std::move_only_function<void()> cb_;
    std::unique_ptr<F, decltype(&free)> p_;
    int x_=f(3);
    int (*fp_)(int) = nullptr;
    struct lua_State* L_;
    class X* q_;
    struct { int a; } in_;
    enum M { A, B } m_ = A;
    struct Fwd;
    struct Def { int z_; };
    static int shared_;
    int n_ = 1'000;
    void f(int a);
    int g() const { return 1; }
    bool eq_ = (1 == 2), le_ = 1 <= 2;
    TacticalBattle& operator=(const TacticalBattle&) = delete;
    ~TacticalBattle();
    void h(int* a) const;
};
"""
    got = data_members(edge)
    want = ["d_", "b_", "cb_", "p_", "x_", "fp_", "L_", "q_", "in_", "m_", "n_", "eq_", "le_"]
    assert got == want, got
    print("check_battle_ledger selftest: ok")
    return 0


def main(argv):
    if "--selftest" in argv:
        return selftest()
    only = argv[argv.index("--only") + 1] if "--only" in argv else None
    if "--list" in argv:
        k = argv.index("--list")
        only = argv[k + 1] if k + 1 < len(argv) and not argv[k + 1].startswith("--") else only
    status = 0
    total = 0
    if only in (None, "TacticalBattle"):
        for p in (HEADER, LEDGER, SAVE):
            if not os.path.isfile(p):
                print(f"battle ledger: {os.path.relpath(p, ROOT)} is missing")
                return 2
        with open(HEADER, encoding="utf-8") as f:
            header = f.read()
        with open(LEDGER, encoding="utf-8") as f:
            ledger = f.read()
        with open(SAVE, encoding="utf-8") as f:
            save = f.read()
        if "--list" in argv:
            kinds = {n: (k, h) for k, n, h in ledger_entries(ledger)}
            for n in data_members(header) or []:
                k, h = kinds.get(n, ("?", ""))
                print(f"TacticalBattle {k:8} {h:6} {n}")
        else:
            errors = check(header, ledger, save)
            for e in errors:
                print(f"battle ledger: {e}")
            total += len(errors)
    for entry in TABLE:
        if only not in (None, entry.cls):
            continue
        if "--list" in argv:
            members, use, _ = generic_usage(entry, read_text(entry.header), source_texts(entry))
            kinds = {n: (k, h) for k, n, h in generic_entries(read_text(entry.ledger))}                 if os.path.isfile(os.path.join(ROOT, entry.ledger)) else {}
            for n in members or []:
                k, h = kinds.get(n, ("?", ""))
                flags = "".join(c if use[key].get(n) else "-" for key, c in (("save", "S"), ("read", "R"), ("hash", "H")))
                print(f"{entry.cls} {k:8} {h:9} {flags} {n}")
            continue
        errors, code = run_entry(entry)
        if code:
            print(f"save ledger: {errors[0]}")
            status = 2
            continue
        for e in errors:
            print(f"save ledger: {e}")
        total += len(errors)
    if total:
        print(f"save ledger: {total} problem(s); the rules are in tools/lint/check_battle_ledger.py")
        return 1
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
