"""mcx5 t05 limit lint: every integer limit in engine/ sim code says what kind of limit it is (docs/MOD_COMPAT_DESIGN.md §8,
P5: no port limit below retail).

    python tools/lint/check_limits.py [--engine DIR] [--list] [--quiet]
    python tools/lint/check_limits.py --selftest

A limit is an integer `constexpr` (any integer type, optional static / inline / const) or an integer `#define` whose
name says it bounds something: it contains Max / MAX, Limit / LIMIT, Cap / CAP (as a word part: kCap, kFpsCap),
Capacity or Slots / SLOTS. Each one carries a tag in a comment on its line, or in the own-line comments directly above
it:
    // limit: retail <docs/re section and / or address>   a size retail has (the citation is required)
    // limit: data <where>                                 read from data (GameConstants, the type); the constant is a default
    // limit: dynamic <why>                                the container grows; the constant is an initial size or a step
    // limit: guard <what it protects>                     a sanity bound far above any real data; refuses, never clamps
Scope (sim code, CLAUDE.md): engine/ without render, audio, video, ui, the Phase 3 renderer modules (assets, rhi,
materials, shaderxlat) and the game app's presentation host (apps/game/app); tests, testdata, tools and bench folders
are never scanned. Exit 1 on an untagged limit or a malformed tag; 0 otherwise. --list prints every limit with its tag.
"""
import re
import sys
from pathlib import Path

SCOPE = ['core', 'jobs', 'data', 'galactic', 'objects', 'anim', 'commands', 'tactical', 'ai', 'lua', 'vfs', 'terrain',
         'apps/sim', 'apps/headless', 'apps/game/src', 'apps/game/rules', 'apps/game/include', 'apps/crash']
SKIP_DIRS = {'tests', 'testdata', 'tools', 'bench', 'benches', 'third_party', 'generated'}
EXTS = {'.cpp', '.hpp', '.h', '.inl', '.cc'}
KINDS = ('retail', 'data', 'dynamic', 'guard')

INT_TYPE = r'(?:std::)?(?:u?int(?:8|16|32|64)_t|size_t|ptrdiff_t|int|unsigned(?:\s+int|\s+long)?|long(?:\s+long)?|short|uint)'
CONSTEXPR = re.compile(r'^\s*(?:\[\[[^\]]*\]\]\s*)?(?:(?:static|inline|const)\s+)*constexpr\s+(?:(?:static|inline|const)\s+)*'
                       + INT_TYPE + r'\s+([A-Za-z_]\w*)\s*(?:=|\{)')
DEFINE = re.compile(r'^\s*#\s*define\s+([A-Za-z_]\w*)\s+\(?\s*-?(?:0[xX][0-9a-fA-F]+|\d+)[uUlL]*')
LIMIT_NAME = re.compile(r'(Max|MAX|Limit|LIMIT|Capacity|CAPACITY|Slots|SLOTS|Cap(?![a-z])|CAP(?![A-Z]))')
TAG = re.compile(r'\blimit:\s*(\w+)\s*(.*)$')


def is_limit_name(name):
    return LIMIT_NAME.search(name) is not None


def strip_strings(line):
    """Blank raw strings, string literals and char literals, so a '//' or tag text inside them is not a comment."""
    line = re.sub(r'(?:u8|u|U|L)?R"([^()\\ \t]{0,16})\(.*?\)\1"', 'R""', line)
    line = re.sub(r'"(?:\\.|[^"\\])*"', '""', line)
    return re.sub(r"'(?:\\.|[^'\\])+'", "''", line)


def comment_of(line):
    """The // comment of a line (outside string literals), or None."""
    s = strip_strings(line)
    k = s.find('//')
    return None if k < 0 else s[k + 2:]


def find_tag(lines, i):
    """The tag text for the limit on line i: its own comment, else the own-line // comments directly above."""
    own = comment_of(lines[i])
    if own is not None and TAG.search(own):
        return TAG.search(own)
    j = i - 1
    while j >= 0 and lines[j].lstrip().startswith('//'):
        m = TAG.search(lines[j].lstrip()[2:])
        if m:
            return m
        j -= 1
    return None


def scan_text(text):
    """[(line, name, kind or None, detail, error)] for every limit in the text."""
    out = []
    lines = text.splitlines()
    in_block = False
    for i, line in enumerate(lines):
        code = line
        if in_block:
            end = code.find('*/')
            if end < 0:
                continue
            code = code[end + 2:]
            in_block = False
        if '/*' in code and '*/' not in code[code.find('/*'):]:
            in_block = True
            code = code[:code.find('/*')]
        m = CONSTEXPR.match(code) or DEFINE.match(code)
        if not m or not is_limit_name(m.group(1)):
            continue
        name = m.group(1)
        t = find_tag(lines, i)
        if not t:
            out.append((i + 1, name, None, '', 'untagged limit (add // limit: retail|data|dynamic|guard ...)'))
            continue
        kind, detail = t.group(1), t.group(2).strip()
        if kind not in KINDS:
            out.append((i + 1, name, kind, detail, f'unknown limit kind "{kind}" (one of {", ".join(KINDS)})'))
        elif kind == 'retail' and not detail:
            out.append((i + 1, name, kind, detail, 'retail limit without a citation (docs/re section or address)'))
        else:
            out.append((i + 1, name, kind, detail, None))
    return out


def files_in(engine):
    for sub in SCOPE:
        root = engine / sub
        if not root.is_dir():
            continue
        for p in sorted(root.rglob('*')):
            if p.suffix not in EXTS or not p.is_file():
                continue
            rel = p.relative_to(engine)
            if any(part in SKIP_DIRS for part in rel.parts[:-1]):
                continue
            yield p


def run(engine, list_all=False, quiet=False):
    engine = Path(engine)
    bad = total = 0
    kinds = {k: 0 for k in KINDS}
    files = list(files_in(engine))
    for p in files:
        for line, name, kind, detail, err in scan_text(p.read_text(encoding='utf8', errors='replace')):
            total += 1
            rel = p.relative_to(engine.parent).as_posix()
            if err:
                bad += 1
                print(f'{rel}:{line}: {name}: {err}')
            else:
                kinds[kind] += 1
                if list_all:
                    print(f'{rel}:{line}: {name}: {kind} {detail}')
    if bad:
        print(f'check_limits: {bad} of {total} limit(s) untagged or malformed in {len(files)} files '
              '(docs/MOD_COMPAT_DESIGN.md §8)')
        return 1
    if not quiet:
        print(f'check_limits: OK ({total} limits in {len(files)} files: ' +
              ', '.join(f'{kinds[k]} {k}' for k in KINDS) + ')')
    return 0


SELFTEST = '''\
constexpr int kMaxA = 5;                 // limit: retail AI_GALACTIC §2 (0x123)
// A long comment above the limit,
// limit: guard far above real data
inline constexpr uint32_t kMaxB = 1u << 20;
static constexpr size_t kSlotsC = 3;      // no tag here
constexpr int kCountD = 7;               // not a limit name
constexpr int kMaxE = 9;                 // limit: retail
constexpr int kMaxF = 9;                 // limit: wild guess
#define ALAMO_MAX_G 64
#define ALAMO_MAX_H 64                   // limit: dynamic initial size
constexpr float kMaxI = 1.0f;            // float: not an integer limit
constexpr uint32_t kFpsCap = 200;        // limit: data FPSCAP=
constexpr uint32_t kCapture = 1;         // "Cap" inside a word: not a limit
/* constexpr int kMaxJ = 1; */
const char* s = "constexpr int kMaxK = 1;";
constexpr int kMaxL = 2; auto r = R"(// limit: guard fake)";
'''


def selftest():
    got = [(line, name, err is None) for line, name, _, _, err in scan_text(SELFTEST)]
    want = [(1, 'kMaxA', True), (4, 'kMaxB', True), (5, 'kSlotsC', False), (7, 'kMaxE', False), (8, 'kMaxF', False),
            (9, 'ALAMO_MAX_G', False), (10, 'ALAMO_MAX_H', True), (12, 'kFpsCap', True), (16, 'kMaxL', False)]
    if got != want:
        print('check_limits selftest: FAILED')
        print('  want', want)
        print('  got ', got)
        return 1
    print(f'check_limits selftest: OK ({len(want)} limits, {sum(1 for w in want if not w[2])} findings)')
    return 0


def main(argv):
    if '--selftest' in argv:
        return selftest()
    engine = Path(__file__).resolve().parents[2] / 'engine'
    if '--engine' in argv:
        engine = Path(argv[argv.index('--engine') + 1])
    return run(engine, list_all='--list' in argv, quiet='--quiet' in argv)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
