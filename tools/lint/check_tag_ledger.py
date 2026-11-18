"""Ledger lint (docs/MOD_COMPAT_DESIGN.md §7.2 and §9.1, roadmap mcx0 t03 / t04).

Checks that the generated ledgers match the code, without writing anything:
  tag ledger      engine/objects/generated/tag_ledger.inc (engine/objects/tools/gen_tag_ledger.py)
                  - every row's state is what the code gives now (a changed consumer count alone is not a failure:
                    the generator refreshes it whenever the ledger is regenerated);
                  - no consumed / verified row without a consumer;
                  - no row moves from consumed / verified to stored / unread (a reader was removed);
                  - every "// ledger: consumes|verifies <Class>.<Tag>" annotation names a row and sits next to code
                    (consumes) or in a test that names the tag (verifies);
                  - every RETAIL_UNUSED entry (roadmap mcx4) names a row nothing reads and cites an existing docs/re
                    page that names the tag; such a row is retail_unused, settled like a consumed one.
  binding ledger  engine/lua/src/binding_table.inc (engine/lua/tools/gen_bindings.py): the effect and consumer
                  columns are what the code gives now.
A failure prints what differs and the command that regenerates the ledger. Runs in build_agent.cmd (before the build),
in build_gate.cmd (tools/lint/gate_summary.py) and as the ctest entry alamo_objects_tag_ledger_lint.

Usage: check_tag_ledger.py [--selftest | --gaps]
  --gaps  list the base-FoC honour gaps still open (tier 1, base_uses > 0, state unread / stored: the mcx4 exit test
          needs none) from the committed ledger, and exit 1 when there are any
Exit: 0 in step, 1 differences or annotation errors, 2 a ledger file is missing.
"""
import importlib.util
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
GEN_TAGS = os.path.join(ROOT, "engine", "objects", "tools", "gen_tag_ledger.py")
GEN_BINDINGS = os.path.join(ROOT, "engine", "lua", "tools", "gen_bindings.py")


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def check_tags(gen):
    if not os.path.isfile(gen.OUT_INC):
        print(f"tag ledger: {os.path.relpath(gen.OUT_INC, ROOT)} is missing; run python engine/objects/tools/gen_tag_ledger.py")
        return 2
    with open(gen.OUT_INC, encoding="utf-8") as f:
        committed = gen.parse_inc(f.read())
    ledger, problems = gen.build(with_extras=False)
    errors = [f"annotation: {p}" for p in problems]
    errors += [f"regression: {b}" for b in gen.regressions(committed, ledger, set())]
    for key, (state, n, _) in sorted(committed.items()):
        if state in ("consumed", "verified") and n == 0:
            errors.append(f"{key[0]}.{key[1]}: {state} with no consumer")
    now = {(r["class"], r["tag"].lower()): r for r in ledger}
    stale = []
    for key in sorted(set(now) | set(committed)):
        r, c = now.get(key), committed.get(key)
        if r is None or c is None:
            stale.append(f"{key[0]}.{key[1]}: {'no longer a descriptor row' if r is None else 'new row'}")
        elif r["state"] != c[0]:
            stale.append(f"{r['class']}.{r['tag']}: ledger {c[0]} ({c[1]} files), code {r['state']} ({r['consumers']} files"
                         + (f", first {r['consumer']}" if r["consumer"] else "") + ")")
    for e in errors:
        print("tag ledger: " + e)
    for s in stale[:40]:
        print("tag ledger: stale: " + s)
    if len(stale) > 40:
        print(f"tag ledger: ... {len(stale) - 40} more stale rows")
    if stale and not errors:
        print("tag ledger: regenerate with: python engine/objects/tools/gen_tag_ledger.py")
    if errors or stale:
        return 1
    print(f"tag ledger: {len(ledger)} rows in step with the code")
    return 0


def gaps(gen):
    """The mcx4 exit test's open rows, from the committed ledger (tag_ledger.inc carries tier and base uses)."""
    if not os.path.isfile(gen.OUT_INC):
        print("tag ledger: tag_ledger.inc is missing")
        return 2
    row = re.compile(
        r'\{"([^"]*)", "([^"]*)", 0x[0-9a-f]+, LedgerState::(\w+), \d+, "[^"]*", "([^"]*)", (\d+), (\d+)\}')
    open_rows = []
    with open(gen.OUT_INC, encoding="utf-8") as f:
        for line in f:
            m = row.match(line)
            if m and m.group(3) in ("Unread", "Stored") and int(m.group(5)) > 0 and m.group(6) == "1":
                open_rows.append((-int(m.group(5)), m.group(1), m.group(2), m.group(3).lower(), m.group(4)))
    for uses, cls, tag, state, spec in sorted(open_rows):
        print(f"gap: {cls}.{tag} ({state}, base uses {-uses}, spec {spec or '-'})")
    print(f"tag ledger: {len(open_rows)} base-FoC honour gaps open (tier 1, base_uses > 0, unread / stored)")
    return 1 if open_rows else 0


def check_bindings(gen):
    if not hasattr(gen, "render"):
        return 0
    if not os.path.isfile(gen.OUT):
        print("binding ledger: engine/lua/src/binding_table.inc is missing")
        return 2
    with open(gen.OUT, encoding="utf-8") as f:
        committed = f.read()
    text, problems = gen.render()
    for p in problems:
        print("binding ledger: annotation: " + p)
    if text != committed:
        old, new = committed.splitlines(), text.splitlines()
        diff = [f"- {a}\n  + {b}" for a, b in zip(old, new) if a != b][:20]
        for d in diff:
            print("binding ledger: stale:\n  " + d)
        print("binding ledger: regenerate with: python engine/lua/tools/gen_bindings.py")
        return 1
    if problems:
        return 1
    print("binding ledger: in step with the code")
    return 0


def selftest(gen):
    bad = 0
    lits, comments, code = gen.scan_source('x = get("Max_Speed"); // "Mass" in a comment\n/* "Tactical_Health" */ R"(\"Damage\")";\n'
                                           "// ledger: consumes GameObjectType.Mass\ny = c.mass;\n")
    names = [s for _, s in lits]
    if names != ["Max_Speed"]:
        print("selftest: literals", names)
        bad += 1
    if not any("ledger: consumes" in c for _, c in comments) or 4 not in code:
        print("selftest: comments / code lines", comments, sorted(code))
        bad += 1
    old = {("GameObjectType", "mass"): ("consumed", 1, "engine/x.cpp")}
    reg = gen.regressions(old, [{"class": "GameObjectType", "tag": "Mass", "state": "stored"}], set())
    if len(reg) != 1:
        print("selftest: regression not found", reg)
        bad += 1
    if gen.regressions(old, [{"class": "GameObjectType", "tag": "Mass", "state": "stored"}], {"gameobjecttype.mass"}):
        print("selftest: --allow-regress ignored")
        bad += 1
    # RETAIL_UNUSED (mcx4): a listed row nothing reads becomes retail_unused; a read one, an unknown row or a citation
    # that is no docs/re page naming the tag is an error.
    rows = [("GameConstants", "Progressive_Taxation", 0), ("GameConstants", "Income_Redistribution", 0)]
    page = "docs/re/GALACTIC_ECONOMY.md"
    st = gen.compute_states(rows, {}, {}, {("GameConstants", "progressive_taxation"): page})
    if st.get(("GameConstants", "progressive_taxation")) != "retail_unused" or \
            st.get(("GameConstants", "income_redistribution")) != "stored":
        print("selftest: retail_unused state", st)
        bad += 1
    if gen.retail_unused_problems(rows, {}, {("GameConstants", "Progressive_Taxation"): page + " Q2.2"}):
        print("selftest: a good RETAIL_UNUSED entry was refused")
        bad += 1
    probs = gen.retail_unused_problems(rows, {("GameConstants", "income_redistribution"): {"engine/x.cpp"}}, {
        ("GameConstants", "Income_Redistribution"): page,
        ("GameConstants", "No_Such_Tag"): page,
        ("GameConstants", "Progressive_Taxation"): "docs/re/NO_SUCH_PAGE.md",
    })
    if len(probs) != 3:
        print("selftest: RETAIL_UNUSED problems", probs)
        bad += 1
    print("selftest:", "ok" if not bad else f"{bad} failed")
    return 1 if bad else 0


def main(argv):
    gen = load(GEN_TAGS, "gen_tag_ledger")
    if "--selftest" in argv:
        return selftest(gen)
    if "--gaps" in argv:
        return gaps(gen)
    rc = check_tags(gen)
    rc_b = check_bindings(load(GEN_BINDINGS, "gen_bindings"))
    return max(rc, rc_b)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
