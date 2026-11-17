"""Tag ledger generator (docs/MOD_COMPAT_DESIGN.md §7.2, roadmap mcx0 t02).

One row per (class, tag) of retail's XML descriptor tables (docs/re/xml_field_tables.md, through the generated
engine/objects/src/field_schema_table.inc; class = the row's segment owner), with the port's state:

  unread    the port's loader for the class does not keep the tag, and no engine code reads it
  stored    the port keeps the value (a generic store: the typed object view, its sub-object fields, GameConstants,
            the campaign's raw list) but no engine code reads it
  consumed  engine code reads it: a string literal naming the tag in engine source outside the descriptor tables,
            tests, tools and the load report (every typed read goes through an accessor taking the tag name:
            GameObjectTypes::get_*(t, "Tag"), GameConstants::get*("Tag"), XmlNode::child*("Tag"), CampaignDef::raw_value),
            a name the code builds from such literals ("Corruption_" + n + "_Type": built_tag_reads),
            or an annotation "// ledger: consumes <Class>.<Tag>" next to an indirect read (a column filled from the
            tag, a struct member set by a loader)
  verified  consumed, and a test carries "// ledger: verifies <Class>.<Tag>" (a test that checks the tag's effect
            against a docs/re spec or a probe)

A literal naming a tag that several classes list counts for each of them (72 of the 2,764 tag names).

Other columns: code (parser type code), consumers (how many files read it, and the first), spec (the first docs/re
page that names the tag in backticks), base_uses (how often base FoC's XML in extracted/foc_data_text.zip writes an
element of that name, any class), tier (1 for every row until a row says otherwise: TIER_OVERRIDES).
corpus_uses / corpus_mods are filled by the corpus runner (mcx7), not here.

Outputs (both generated, never edited by hand):
  engine/objects/generated/tag_ledger.inc   rows compiled into alamo_objects (objects/tag_ledger.hpp)
  docs/status/tag-ledger.md                 the human view

    python engine/objects/tools/gen_tag_ledger.py            write both
    python engine/objects/tools/gen_tag_ledger.py --allow-regress Class.Tag ...
                                                             write although a row moves consumed -> stored / unread
tools/lint/check_tag_ledger.py imports this module and checks that the committed ledger matches the code.
"""
import argparse
import os
import re
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
ENGINE = os.path.join(ROOT, "engine")
SCHEMA = os.path.join(ENGINE, "objects", "src", "field_schema_table.inc")
OUT_INC = os.path.join(ENGINE, "objects", "generated", "tag_ledger.inc")
OUT_MD = os.path.join(ROOT, "docs", "status", "tag-ledger.md")
DOCS_RE = os.path.join(ROOT, "docs", "re")
FOC_ZIP = os.path.join(ROOT, "extracted", "foc_data_text.zip")

STATES = ("unread", "stored", "consumed", "verified")
STATE_ENUM = {"unread": "Unread", "stored": "Stored", "consumed": "Consumed", "verified": "Verified"}

# Classes whose port loader keeps every tag of an entry, read or not (so an unread tag there is "stored"):
#   * the typed object view keeps every descriptor field of GameObjectType / SpaceProp / HeroUnit, and the raw store
#     keeps every element below a type, so the ability / action sub-object fields are kept too;
#   * data::GameConstants keeps every child of <GameConstants>; CampaignDef::raw every child of a campaign.
# Every other class has a loader that reads named tags only.
GENERIC_STORE = {"GameObjectType", "SpaceProp", "HeroUnit", "GameConstants", "Campaign", "Ability_Base"}
SUB_OBJECT_OWNER = re.compile(r"(_Ability|_Action)$|^(Unit_Ability|Hero_Clash|Destroy_Unit)$")

TIER_OVERRIDES = {}  # (class, tag) -> tier, for rows that are distribution-level only (tier 2)

# Source files that name tags without reading them for the game: the descriptor tables themselves, reports, tests,
# tools, generated tables, vendored code.
EXCLUDED_PARTS = ("/tests/", "/tools/", "/third_party/", "/generated/", "/test_support/")
EXCLUDED_FILES = {
    "engine/objects/src/field_schema_table.inc",
    "engine/objects/src/sub_object_names.inc",
    "engine/objects/src/behavior_classes.inc",
    "engine/objects/src/coverage.cpp",
    "engine/lua/src/binding_table.inc",
    "engine/apps/game/src/mod_check.cpp",
}
SOURCE_EXT = (".cpp", ".hpp", ".h", ".inc")
ANNOTATION = re.compile(r"ledger:\s*(consumes|verifies)\s+([A-Za-z0-9_]+)\.([A-Za-z0-9_\-\.]+)")
ROW = re.compile(r'\{"([^"]+)", (0x[0-9a-fA-F]+), (0x[0-9a-fA-F]+), "([^"]*)", (\d+)')


def rel(path):
    return os.path.relpath(path, ROOT).replace("\\", "/")


def schema_rows():
    """[(class, tag, code)] in document order, first row of each (class, tag lower)."""
    out, seen = [], set()
    with open(SCHEMA, encoding="utf-8") as f:
        for line in f:
            m = ROW.match(line)
            if not m:
                continue
            tag, code, owner = m.group(1), int(m.group(2), 16), m.group(4)
            key = (owner, tag.lower())
            if key in seen:
                continue
            seen.add(key)
            out.append((owner, tag, code))
    return out


def scan_source(text, concat=None):
    """(literals, comments, code_lines): string literals (not raw strings), comment texts with their line, and the
    set of line numbers that hold code outside comments. `concat` (optional list) gets (literal, after '+', before '+')
    for every literal, for tag names the code builds by concatenation (built_tag_reads)."""
    if concat is None:
        concat = []
    lits, comments, code_lines = [], [], set()
    i, n, line = 0, len(text), 1
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            i += 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            comments.append((line, text[i:j]))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            comments.append((line, text[i:j]))
            line += text.count("\n", i, j)
            i = j
        elif c == "R" and text.startswith('R"', i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
            k = text.find("(", i + 2)
            delim = text[i + 2:k] if k >= 0 else ""
            end = text.find(")" + delim + '"', k + 1) if k >= 0 else -1
            j = n if end < 0 else end + len(delim) + 2
            code_lines.add(line)
            line += text.count("\n", i, j)
            i = j
        elif c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            if c == '"':
                lits.append((line, text[i + 1:j]))
                k = i - 1
                while k >= 0 and text[k] in " \t\r\n":
                    k -= 1
                m = j + 1
                while m < n and text[m] in " \t\r\n":
                    m += 1
                concat.append((text[i + 1:j], k >= 0 and text[k] == "+", m < n and text[m] == "+"))
            code_lines.add(line)
            i = j + 1
        else:
            if not c.isspace():
                code_lines.add(line)
            i += 1
    return lits, comments, code_lines


def built_tag_reads(concat, by_tag):
    """Tags (lower case) a file reads under a name it builds: "Corruption_" + n + "_Type", "Starting_Spawned_Units_Tech_"
    + n, faction + "_Victory_Conditions". A tag counts when it starts with a literal followed by '+' (4+ characters) and
    the rest is digits or ends with a literal of the file preceded by '+'; or ends with a literal preceded by '+' that
    starts with '_' and has 11+ characters; or is exactly a literal of the file followed by such a suffix literal
    (read("Tractor_Beam") ... p + "_Texture")."""
    prefixes = {s.lower() for s, _, after in concat if after and len(s) >= 4}
    suffixes = {s.lower() for s, before, _ in concat if before and len(s) >= 3}
    long_suffixes = {x for x in suffixes if x.startswith("_") and len(x) >= 11}
    if not prefixes and not suffixes:
        return set()
    out = set()
    words = {s.lower() for s, _, _ in concat if len(s) >= 4}
    for w in words:
        for x in suffixes:
            if x.startswith("_") and w + x in by_tag:
                out.add(w + x)
    for tag in by_tag:
        for p in prefixes:
            if tag.startswith(p) and len(tag) > len(p):
                rest = tag[len(p):]
                if rest.isdigit() or any(tag.endswith(x) and len(tag) >= len(p) + len(x) for x in suffixes):
                    out.add(tag)
                    break
        else:
            if any(tag.endswith(x) and len(tag) > len(x) for x in long_suffixes):
                out.add(tag)
    return out


def engine_sources():
    for dirpath, dirnames, filenames in os.walk(ENGINE):
        dirnames.sort()
        for name in sorted(filenames):
            if name.endswith(SOURCE_EXT):
                yield os.path.join(dirpath, name)


def find_consumers(rows):
    """Returns (consumers, verifiers, problems): consumers[(class, tag lower)] = set of files; verifiers likewise from
    test annotations; problems = annotation errors (unknown row, no code next to it)."""
    by_tag = {}
    for cls, tag, _ in rows:
        by_tag.setdefault(tag.lower(), []).append(cls)
    known = {(cls, tag.lower()) for cls, tag, _ in rows}
    consumers, verifiers, problems = {}, {}, []
    for path in engine_sources():
        r = rel(path)
        is_test = "/tests/" in r
        excluded = r in EXCLUDED_FILES or any(p in r for p in EXCLUDED_PARTS)
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read()
        if "ledger:" not in text and excluded:
            continue
        concat = []
        lits, comments, code_lines = scan_source(text, concat)
        if not excluded:
            for _, s in lits:
                for cls in by_tag.get(s.lower(), ()):
                    consumers.setdefault((cls, s.lower()), set()).add(r)
            for tag in built_tag_reads(concat, by_tag):
                for cls in by_tag[tag]:
                    consumers.setdefault((cls, tag), set()).add(r)
        for ln, body in comments:
            for m in ANNOTATION.finditer(body):
                kind, cls, tag = m.group(1), m.group(2), m.group(3)
                key = (cls, tag.lower())
                where = f"{r}:{ln}"
                if key not in known:
                    problems.append(f"{where}: '{m.group(0)}' names no ledger row ({cls}.{tag})")
                    continue
                if kind == "consumes":
                    if excluded:
                        problems.append(f"{where}: a consumes annotation in a file the scan skips (tests, tools, tables)")
                        continue
                    if not any(l in code_lines for l in (ln, ln + 1, ln + 2)):
                        problems.append(f"{where}: no code on the annotation's line or the two after it")
                        continue
                    consumers.setdefault(key, set()).add(r)
                else:
                    if not is_test:
                        problems.append(f"{where}: a verifies annotation outside a tests/ folder")
                        continue
                    if tag.lower() not in text.lower().replace(m.group(0).lower(), ""):
                        problems.append(f"{where}: the test names {tag} only in the annotation")
                        continue
                    verifiers.setdefault(key, set()).add(r)
    return consumers, verifiers, problems


def compute_states(rows, consumers, verifiers):
    out = {}
    for cls, tag, _ in rows:
        key = (cls, tag.lower())
        if key in consumers:
            st = "verified" if key in verifiers else "consumed"
        elif cls in GENERIC_STORE or SUB_OBJECT_OWNER.search(cls):
            st = "stored"
        else:
            st = "unread"
        out[key] = st
    return out


def spec_index():
    idx = {}
    if not os.path.isdir(DOCS_RE):
        return idx
    tick = re.compile(r"`([A-Za-z][A-Za-z0-9_]*)`")
    for name in sorted(os.listdir(DOCS_RE)):
        if not name.endswith(".md") or name == "xml_field_tables.md":
            continue
        with open(os.path.join(DOCS_RE, name), encoding="utf-8", errors="replace") as f:
            for m in tick.finditer(f.read()):
                idx.setdefault(m.group(1).lower(), "docs/re/" + name)
    return idx


def base_uses():
    counts = {}
    if not os.path.isfile(FOC_ZIP):
        return counts
    el = re.compile(rb"<([A-Za-z_][A-Za-z0-9_]*)[\s>/]")
    with zipfile.ZipFile(FOC_ZIP) as z:
        for name in sorted(z.namelist()):
            if not name.lower().endswith(".xml"):
                continue
            for m in el.finditer(z.read(name)):
                k = m.group(1).decode("ascii").lower()
                counts[k] = counts.get(k, 0) + 1
    return counts


def build(with_extras=True):
    rows = schema_rows()
    consumers, verifiers, problems = find_consumers(rows)
    states = compute_states(rows, consumers, verifiers)
    spec = spec_index() if with_extras else {}
    uses = base_uses() if with_extras else {}
    ledger = []
    for cls, tag, code in rows:
        key = (cls, tag.lower())
        files = sorted(consumers.get(key, ()))
        ledger.append({
            "class": cls, "tag": tag, "code": code, "state": states[key], "consumers": len(files),
            "consumer": files[0] if files else "", "spec": spec.get(tag.lower(), ""),
            "base_uses": uses.get(tag.lower(), 0), "tier": TIER_OVERRIDES.get(key, 1),
        })
    return ledger, problems


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def render_inc(ledger):
    lines = [
        "// GENERATED by engine/objects/tools/gen_tag_ledger.py from docs/re/xml_field_tables.md and the engine source.",
        "// Do not edit; tools/lint/check_tag_ledger.py checks it. Row: {class, tag, code, state, consumer files, first",
        f"// consumer, spec, base FoC uses, tier}}. {len(ledger)} rows.",
    ]
    for r in ledger:
        lines.append("{%s, %s, 0x%x, LedgerState::%s, %d, %s, %s, %d, %d}," % (
            c_str(r["class"]), c_str(r["tag"]), r["code"], STATE_ENUM[r["state"]], r["consumers"], c_str(r["consumer"]),
            c_str(r["spec"]), r["base_uses"], r["tier"]))
    return "\n".join(lines) + "\n"


def parse_inc(text):
    """{(class, tag lower): (state, consumers, consumer)} of a written tag_ledger.inc."""
    row = re.compile(r'\{"([^"]*)", "([^"]*)", 0x[0-9a-f]+, LedgerState::(\w+), (\d+), "([^"]*)"')
    inv = {v: k for k, v in STATE_ENUM.items()}
    out = {}
    for line in text.splitlines():
        m = row.match(line)
        if m:
            out[(m.group(1), m.group(2).lower())] = (inv.get(m.group(3), m.group(3)), int(m.group(4)), m.group(5))
    return out


def render_md(ledger):
    classes = {}
    for r in ledger:
        c = classes.setdefault(r["class"], {s: 0 for s in STATES})
        c[r["state"]] += 1
    tot = {s: sum(c[s] for c in classes.values()) for s in STATES}
    out = [
        "# Tag ledger",
        "",
        "Generated by `engine/objects/tools/gen_tag_ledger.py` (roadmap mcx0 t02); do not edit. Design:",
        "`docs/MOD_COMPAT_DESIGN.md` §7.2. One row per (class, tag) of retail's descriptor tables",
        "(`docs/re/xml_field_tables.md`; class = segment owner). The compiled copy is",
        "`engine/objects/generated/tag_ledger.inc`; `tools/lint/check_tag_ledger.py` keeps both in step with the code.",
        "",
        "States: **unread** (the port does not keep it), **stored** (kept, nothing reads it), **consumed** (engine code",
        "reads it), **verified** (consumed, and a test checks its effect). An `unread` or `stored` tag that a mod sets is an",
        "honour gap in `alamo_mod_check` report v4. Work order (§7.2): tier 1 with the most corpus mods, then base FoC uses.",
        "",
        f"Totals: {len(ledger)} rows; unread {tot['unread']}, stored {tot['stored']}, consumed {tot['consumed']}, "
        f"verified {tot['verified']}.",
        "",
        "## Per class",
        "",
        "| Class | Rows | Unread | Stored | Consumed | Verified |",
        "|---|---:|---:|---:|---:|---:|",
    ]
    for cls in sorted(classes, key=lambda c: (-sum(classes[c].values()), c)):
        c = classes[cls]
        out.append(f"| {cls} | {sum(c.values())} | {c['unread']} | {c['stored']} | {c['consumed']} | {c['verified']} |")
    gaps = [r for r in ledger if r["state"] in ("unread", "stored") and r["base_uses"] > 0]
    gaps.sort(key=lambda r: (-r["base_uses"], r["class"], r["tag"].lower()))
    out += [
        "",
        "## Honour gaps base FoC uses",
        "",
        "Rows not consumed whose tag base FoC's XML writes (base uses count the element name in any class). These are",
        "base-FoC parity gaps as well as mod gaps (`mcx4`).",
        "",
        "| Class | Tag | Code | State | Base uses | Spec |",
        "|---|---|---|---|---:|---|",
    ]
    for r in gaps:
        out.append(f"| {r['class']} | {r['tag']} | 0x{r['code']:x} | {r['state']} | {r['base_uses']} | {r['spec'] or '-'} |")
    out += ["", "## Every row", "", "Per class: `Tag` (state, consumer files).", ""]
    for cls in sorted(classes):
        items = [f"`{r['tag']}` ({r['state'][0]}{'' if not r['consumers'] else ' ' + str(r['consumers'])})"
                 for r in ledger if r["class"] == cls]
        out.append(f"- **{cls}**: " + ", ".join(items))
    return "\n".join(out) + "\n"


def regressions(old, ledger, allowed):
    bad = []
    for r in ledger:
        key = (r["class"], r["tag"].lower())
        prev = old.get(key)
        if prev and prev[0] in ("consumed", "verified") and r["state"] in ("stored", "unread"):
            if f"{r['class']}.{r['tag']}".lower() not in allowed:
                bad.append(f"{r['class']}.{r['tag']}: {prev[0]} -> {r['state']} (was read in {prev[2]})")
    return bad


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--allow-regress", nargs="*", default=[], metavar="CLASS.TAG",
                    help="rows allowed to move from consumed / verified to stored / unread")
    a = ap.parse_args(argv)
    ledger, problems = build()
    for p in problems:
        print("annotation:", p)
    old = {}
    if os.path.isfile(OUT_INC):
        with open(OUT_INC, encoding="utf-8") as f:
            old = parse_inc(f.read())
    bad = regressions(old, ledger, {x.lower() for x in a.allow_regress})
    if bad:
        print("refused: these rows lose their consumer (pass --allow-regress Class.Tag if that is intended):")
        for b in bad:
            print("  " + b)
        return 1
    if problems:
        return 1
    os.makedirs(os.path.dirname(OUT_INC), exist_ok=True)
    with open(OUT_INC, "w", encoding="utf-8", newline="\n") as f:
        f.write(render_inc(ledger))
    with open(OUT_MD, "w", encoding="utf-8", newline="\n") as f:
        f.write(render_md(ledger))
    counts = {s: sum(1 for r in ledger if r["state"] == s) for s in STATES}
    print(f"wrote {rel(OUT_INC)} and {rel(OUT_MD)}: {len(ledger)} rows, " + ", ".join(f"{k} {v}" for k, v in counts.items()))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
