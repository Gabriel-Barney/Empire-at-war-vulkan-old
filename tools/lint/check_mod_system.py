"""ums4 t03 mod-system lint (Stage 10, docs/MODS.md 3.1.2 / 3.2, docs/DECISIONS.md "Unified mod system refactor"): the
mod environment has one owner per question, so the transition shims stay gone.

    python tools/lint/check_mod_system.py [--root DIR] [--quiet]
    python tools/lint/check_mod_system.py --selftest

Rules:
  mod_dirs   No call of Vfs::mod_dirs() (`.mod_dirs()` / `->mod_dirs()`) outside engine/vfs. Code that asks which mods
             are mounted, or which mod serves a file, reads the layer table (vfs::Layer, Vfs::layers / layer_of) or
             ModEnvironment::mounted(); engine/vfs itself builds those from the search list.
  modinfo    No modinfo.json reader outside the mod catalog (engine/vfs/src/mod_registry.cpp: vfs::ModCatalog,
             describe_mod_folder): no string literal that is a modinfo.json file name or path ("modinfo.json",
             "<dir>/modinfo.json", "-modinfo.json") in engine/ or launcher/src code. Everyone else reads descriptors
             (the catalog, ModEnvironment::mounted()) or alamo_mod_check's JSON. A literal with a space in it (a message
             such as "named in modinfo.json") is text, not a file name.
  shim       None of the removed transition names comes back in engine/ code: list_installed_mods, resolve_stack,
             scan_installed_mods, ModRegistry, ResolvedStack (ums4 t01; their replacements: the ModCatalog rows,
             vfs::resolve, scan_mod_catalog, ModCatalog, ModStack).
Scanned: engine/ (C++: .cpp .hpp .h .inl .cc) and launcher/src (.rs). Comments are ignored. Tests may write fixture
files, so test code is not scanned: engine/**/tests, engine/**/testdata, and in Rust everything from a `#[cfg(test)]`
line to the end of its file (the test module sits last, the crate's convention). Exit 1 on a finding, else 0.
"""
import re
import sys
from pathlib import Path

CPP_EXTS = {'.cpp', '.hpp', '.h', '.inl', '.cc'}
SKIP_DIRS = {'tests', 'testdata', 'third_party'}
MOD_DIRS_CALL = re.compile(r'(?:\.|->)\s*mod_dirs\s*\(\s*\)')
STRING = re.compile(r'"((?:\\.|[^"\\])*)"')
CHAR = re.compile(r"'(?:\\.|[^'\\])'")
RUST_QUOTE_CHAR = re.compile(r"b?'\"'")
MODINFO_NAME = re.compile(r'^[^\s"]*modinfo\.json$', re.IGNORECASE)
CATALOG = 'engine/vfs/src/mod_registry.cpp'
SHIM_NAMES = re.compile(r'\b(list_installed_mods|resolve_stack|scan_installed_mods|ModRegistry|ResolvedStack)\b')


def strip_comments(text, rust):
    """The text with // and /* */ comments blanked (string literals kept, raw strings left alone: none hold either)."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if rust and (text.startswith("'\"'", i) or text.startswith("b'\"'", i)):  # the char '"' (not a string)
            k = 4 if c == 'b' else 3
            out.append(text[i:i + k])
            i += k
        elif not rust and text.startswith('R"', i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == '_')):
            d = text.find('(', i + 2)  # a raw string R"delim( ... )delim": kept whole, as one string literal
            delim = text[i + 2:d] if d >= 0 else ''
            j = text.find(')' + delim + '"', d + 1) if d >= 0 else -1
            j = n if j < 0 else j + len(delim) + 2
            out.append('"' + text[d + 1:j - len(delim) - 2].replace('"', '') + '"' if d >= 0 else text[i:j])
            i = j
        elif c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif c == "'" and not rust:  # a C++ char literal ('"' would otherwise open a string)
            j = i + 1
            while j < n and text[j] != "'" and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith('//', i):
            j = text.find('\n', i)
            j = n if j < 0 else j
            i = j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append('\n' * text.count('\n', i, j))
            i = j
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def scan_text(rel, text, rust):
    """Findings (rel, line, rule, text) of one file's code."""
    if rust:
        k = text.find('#[cfg(test)]')
        if k >= 0:
            text = text[:k]
    code = strip_comments(text, rust)
    found = []
    vfs = rel.startswith('engine/vfs/')
    for no, line in enumerate(code.splitlines(), 1):
        if not rust and not vfs and MOD_DIRS_CALL.search(line):
            found.append((rel, no, 'mod_dirs', line.strip()))
        if not rust and SHIM_NAMES.search(STRING.sub('""', line)):
            found.append((rel, no, 'shim', line.strip()))
        if rel != CATALOG:
            # A char literal such as '"' would open a string (Rust: only that one; 'a is a lifetime there).
            line = RUST_QUOTE_CHAR.sub("''", line) if rust else CHAR.sub("''", line)
            for m in STRING.finditer(line):
                if MODINFO_NAME.match(m.group(1)):
                    found.append((rel, no, 'modinfo', line.strip()))
                    break
    return found


def files(root):
    for base, exts, rust in ((root / 'engine', CPP_EXTS, False), (root / 'launcher' / 'src', {'.rs'}, True)):
        if not base.is_dir():
            continue
        for p in sorted(base.rglob('*')):
            if p.suffix.lower() not in exts or not p.is_file():
                continue
            if any(part in SKIP_DIRS for part in p.relative_to(root).parts[:-1]):
                continue
            yield p, rust


def scan(root):
    found = []
    for p, rust in files(root):
        rel = p.relative_to(root).as_posix()
        found += scan_text(rel, p.read_text(encoding='utf-8', errors='replace'), rust)
    return found


def selftest():
    cases = [
        # (path, rust, text, expected rules)
        ('engine/apps/game/src/a.cpp', False, 'auto d = v.mod_dirs();\n', ['mod_dirs']),
        ('engine/apps/game/src/a.cpp', False, 'if (p->mod_dirs ().empty()) {}\n', ['mod_dirs']),
        ('engine/apps/game/src/a.cpp', False, '// v.mod_dirs() in a comment\n/* p->mod_dirs() */\n', []),
        ('engine/apps/game/src/a.cpp', False, 'r.mod_dirs = x;  // a field, not the call\n', []),
        ('engine/vfs/src/mod_environment.cpp', False, 'for (auto& d : vfs.mod_dirs()) {}\n', []),
        ('engine/apps/game/src/a.cpp', False, 'std::ifstream in(fs::path(dir) / "modinfo.json");\n', ['modinfo']),
        ('engine/apps/game/src/a.cpp', False, 'read(dir + "\\\\ModInfo.json");\n', ['modinfo']),
        ('engine/apps/game/src/a.cpp', False, 'n.ends_with("-modinfo.json");\n', ['modinfo']),
        ('engine/apps/game/src/a.cpp', False, 'msg += ", named in modinfo.json";\n', []),
        ('engine/apps/game/src/a.cpp', False, "char q = '\"'; auto s = \"modinfo.json\";\n", ['modinfo']),
        ('engine/vfs/src/mod_registry.cpp', False, 'read(dir / "modinfo.json", base);\n', []),
        ('launcher/src/bisect.rs', True, 'let t = std::fs::read_to_string(p.join("modinfo.json"));\n', ['modinfo']),
        ('launcher/src/mods.rs', True, '/// Name from `modinfo.json`.\nfn f() {}\n#[cfg(test)]\nmod tests { write(&r.join("modinfo.json")); }\n', []),
        ('launcher/src/x.rs', True, 'let v = v.mod_dirs();\n', []),  # Rust has no Vfs
        ('launcher/src/x.rs', True, "let q = '\"'; let p = dir.join(\"modinfo.json\"); // x\n", ['modinfo']),
        ('launcher/src/x.rs', True, "let q = b'\"'; // \"modinfo.json\" in a comment\n", []),
        ('engine/apps/game/src/a.cpp', False, 'auto p = dir / R"(modinfo.json)";\n', ['modinfo']),
        ('engine/apps/game/src/a.cpp', False, 'auto j = R"x({"a": "b"})x"; auto p = dir / "modinfo.json";\n', ['modinfo']),
        ('engine/apps/game/src/a.cpp', False, 'auto j = R"({"text": "see // here"})"; int x = 1;\n', []),
        ('engine/apps/game/src/a.cpp', False, 'auto rows = game::list_installed_mods(c);\n', ['shim']),
        ('engine/vfs/src/a.cpp', False, 'using ModRegistry = ModCatalog;\n', ['shim']),
        ('engine/apps/game/src/a.cpp', False, 'auto s = resolve_mod_stack(g, p, l);  // not resolve_stack\n', []),
    ]
    bad = 0
    for rel, rust, text, want in cases:
        got = [f[2] for f in scan_text(rel, text, rust)]
        if got != want:
            bad += 1
            print(f'selftest FAIL {rel}: {text!r} -> {got}, expected {want}')
    print(f'check_mod_system selftest: {len(cases) - bad} / {len(cases)} cases pass')
    return 1 if bad else 0


def main(argv):
    if '--selftest' in argv:
        return selftest()
    root = Path(__file__).resolve().parents[2]
    if '--root' in argv:
        root = Path(argv[argv.index('--root') + 1]).resolve()
    found = scan(root)
    for rel, no, rule, line in found:
        print(f'{rel}:{no}: {rule}: {line}')
    if found:
        print(f'check_mod_system: {len(found)} finding(s): Vfs::mod_dirs() outside engine/vfs, a modinfo.json reader '
              f'outside {CATALOG} or a removed shim name (docs/MODS.md 3.2)')
        return 1
    if '--quiet' not in argv:
        print('check_mod_system: 0 findings (no Vfs::mod_dirs() call outside engine/vfs, no modinfo.json reader outside '
              'the mod catalog, no removed shim name)')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
