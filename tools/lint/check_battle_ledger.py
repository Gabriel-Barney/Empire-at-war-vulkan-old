"""Battle save ledger lint (roadmap ref2 t01).

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

Usage: check_battle_ledger.py [--selftest] [--list]
  --list      print the members the parser found with their ledger kind
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

SKIP_STARTS = ("using ", "friend ", "static ", "template", "typedef ", "enum ", "public:", "private:", "protected:",
               "struct ", "class ", "union ", "#", "explicit ", "virtual ", "constexpr ", "inline ", "operator")


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
    """The text between the braces of `class <name> {` (comments already stripped)."""
    m = re.search(r"\bclass\s+" + re.escape(name) + r"\s*(?:final\s*)?\{", text)
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
                if ")" in head or re.match(r"^(struct|class|enum|union)\b", head.strip()):
                    rest = body[i + 1:].lstrip()
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


def data_members(header_text):
    text = re.sub(r"(?m)^[ 	]*#.*$", "", strip_comments(header_text))  # #ifdef blocks: both sides are members
    body = class_body(text, "TacticalBattle")
    if body is None:
        return None
    names = []
    for raw in top_statements(body):
        s = re.sub(r"^(public|private|protected)\s*:\s*", "", raw)
        while True:  # several access labels in a row
            t = re.sub(r"^(public|private|protected)\s*:\s*", "", s)
            if t == s:
                break
            s = t
        if not s or s.startswith(SKIP_STARTS) or s.startswith(("mutable static", "alignas")) and "static" in s:
            continue
        if re.match(r"^(struct|class|enum|union)\b", s):
            continue
        if s.endswith("}") and "(" in s[:s.find("{")]:
            continue  # inline function definition
        if s.startswith("mutable "):
            s = s[len("mutable "):]
        # Function declarations: a `(` at depth 0 that follows a name (and is not inside an initializer).
        decl = s
        # Cut initializers per declarator below; first decide declaration vs function.
        first = split_top(decl)[0]
        lhs = re.split(r"\s=\s|\{", first, maxsplit=1)[0]
        if re.search(r"[A-Za-z0-9_>\]]\s*\(", lhs) and not re.search(r"std::function\s*<|\(\*", lhs):
            continue
        parts = split_top(decl)
        for k, p in enumerate(parts):
            p = re.split(r"\s=\s|=|\{", p, maxsplit=1)[0].strip()
            p = re.sub(r"\[[^\]]*\]\s*$", "", p).strip()  # array extent
            m = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*$", p)
            if not m or m.group(1) == "operator":
                continue
            if k == 0 and not re.search(r"[\s\*&>]", p):
                continue  # a lone word (a macro use)
            names.append(m.group(1))
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


def selftest():
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
    print("check_battle_ledger selftest: ok")
    return 0


def main(argv):
    if "--selftest" in argv:
        return selftest()
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
            print(f"{k:8} {h:6} {n}")
        return 0
    errors = check(header, ledger, save)
    for e in errors:
        print(f"battle ledger: {e}")
    if errors:
        print(f"battle ledger: {len(errors)} problem(s); the rules are in tools/lint/check_battle_ledger.py")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
