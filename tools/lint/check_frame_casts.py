"""ref4 t03 frame-cast lint: sim code turns data-driven seconds into frame counts only through alamo/core/frames.hpp.

    python tools/lint/check_frame_casts.py [--root DIR]
    python tools/lint/check_frame_casts.py --selftest

A plain static_cast of "seconds x frames per second" to an integer is undefined behaviour when the float is negative,
huge or NaN, and MSVC and clang really give different frame counts there (9632d392 / a2b08a39 split the aol_space mod
golden on clang). engine/core/include/alamo/core/frames.hpp has one function per rounding form the original uses
(frames_trunc, frames_round, frames_ceil) with the x86 range behaviour, the same result as the plain cast in range.

Rule: a static_cast to an integer type (int8..int64_t, uint*_t, int, unsigned, long, short, size_t) whose argument
multiplies by a frames-per-second factor (the literal 30.0 / 30.0f, or a name containing fps, FPS, FramesPerSecond or
TicksPerSecond) in floating point (a float literal, static_cast<float|double>, std::round / lround / ceil / floor / trunc)
is a finding, unless that product sits inside frames_trunc / frames_round / frames_ceil / trunc_i32. A line ending in the
comment `// frame-cast-ok: <reason>` is skipped (an integer product the rule misreads; give the reason).
Scanned: engine/ C++ (.cpp .hpp .h .inl .cc) except engine/core (the helpers' home) and the code outside the sim
(CLAUDE.md): render, audio, video, ui and engine/apps/game (the windowed game's presentation: story presenter and camera,
movie playback, session setup); tests, testdata and third_party are skipped. Comments and string literals are ignored.
Exit 1 on a finding, else 0.
"""
import re
import sys
from pathlib import Path

CPP_EXTS = {'.cpp', '.hpp', '.h', '.inl', '.cc'}
SKIP_DIRS = {'tests', 'testdata', 'third_party'}
SKIP_PATHS = {('core',), ('render',), ('audio',), ('video',), ('ui',), ('apps', 'game')}
CAST = re.compile(r'static_cast\s*<\s*(?:std::)?(?:u?int(?:8|16|32|64)_t|unsigned(?:\s+int)?|int|long(?:\s+long)?|short|'
                  r'size_t)\s*>\s*\(')
FPS = re.compile(r'(?<![\w.])30\.0*f?(?![\w.])|\b\w*(?:fps|Fps|FPS|FramesPerSecond|TicksPerSecond)\w*\b')
FLOATY = re.compile(r'(?<![\w.])\d+\.\d*(?:[eE][-+]?\d+)?f?(?![\w.])|static_cast\s*<\s*(?:float|double)\s*>|'
                    r'\bstd::(?:round|lround|llround|ceil|floor|trunc)\b')
HELPER = re.compile(r'\b(?:frames_trunc|frames_round|frames_ceil|trunc_i32)\s*\(')
OK_MARK = '// frame-cast-ok:'


def strip_code(text):
    """Comments and string / char literals blanked (newlines kept, so offsets and line numbers stay)."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith('//', i):
            j = text.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(''.join('\n' if ch == '\n' else ' ' for ch in text[i:j]))
            i = j
        elif c in '"\'':
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            j = min(j + 1, n)
            out.append(c + ' ' * max(0, j - i - 2) + (c if j - i >= 2 else ''))
            i = j
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def balanced(text, open_at):
    """Index just past the ')' matching the '(' at open_at (len(text) when unbalanced)."""
    depth = 0
    for k in range(open_at, len(text)):
        if text[k] == '(':
            depth += 1
        elif text[k] == ')':
            depth -= 1
            if depth == 0:
                return k + 1
    return len(text)


def without_helpers(arg):
    """arg with every frames_* / trunc_i32 call (and its argument) removed."""
    while True:
        m = HELPER.search(arg)
        if not m:
            return arg
        end = balanced(arg, m.end() - 1)
        arg = arg[:m.start()] + arg[end:]


def findings_in(text):
    """[(line, cast text)] of the casts the rule flags in one source text."""
    code = strip_code(text)
    lines = text.split('\n')
    out = []
    for m in CAST.finditer(code):
        end = balanced(code, m.end() - 1)
        arg = without_helpers(code[m.end():end - 1])
        if '*' not in arg or not FPS.search(arg) or not FLOATY.search(arg):
            continue
        line = code.count('\n', 0, m.start()) + 1
        last = code.count('\n', 0, end) + 1
        if any(OK_MARK in lines[k - 1] for k in range(line, last + 1)):
            continue
        out.append((line, ' '.join(code[m.start():end].split())))
    return out


def scanned(root):
    eng = root / 'engine'
    for p in sorted(eng.rglob('*')):
        if p.suffix not in CPP_EXTS or not p.is_file():
            continue
        rel = p.relative_to(eng).parts
        if rel[:1] in SKIP_PATHS or rel[:2] in SKIP_PATHS or SKIP_DIRS.intersection(rel[:-1]):
            continue
        yield p


def check(root):
    bad = []
    for p in scanned(root):
        for line, cast in findings_in(p.read_text(encoding='utf-8', errors='replace')):
            bad.append(f'{p.relative_to(root).as_posix()}:{line}: {cast}')
    return bad


def selftest():
    flagged = [
        'uint32_t a = static_cast<uint32_t>(30.0f * secs);',
        'int b = static_cast<int32_t>(static_cast<float>(kAiFps) * sleep);',
        'auto c = static_cast<int64_t>(static_cast<double>(kFramesPerSecond) *\n    max_wait);',
        'int d = static_cast<int32_t>(std::round(30.0f * cooldown));',
        'uint64_t e = static_cast<uint64_t>(s * 30.0f);',
        'unsigned f = static_cast<unsigned>(delay * static_cast<float>(cfg_.fps) + 0.5f);',
        'int g = static_cast<int>(std::lround(30.0 * static_cast<double>(t)));',
    ]
    clean = [
        'uint32_t a = static_cast<uint32_t>(frames_trunc(30.0f * secs));',
        'int b = frames_trunc(static_cast<float>(kAiFps) * sleep);',
        'uint64_t e = uint64_t{static_cast<uint32_t>(frames_trunc(s * 30.0f))};',
        'uint32_t h = static_cast<uint32_t>(trunc_i32(static_cast<float>(cfg_.fps) * x + 0.5f));',
        'int i = static_cast<int32_t>(n) / (kAiFps * 10);',          # integer arithmetic
        'int64_t j = static_cast<int64_t>(kAiFps) * 30;',              # the cast's argument has no product
        'uint32_t k = static_cast<uint32_t>(frame_count * 30u);',     # integer product
        'float l = static_cast<float>(fps) * 30.0f;',                  # not an integer cast
        'int m = static_cast<int>(x * 30.0f);  // frame-cast-ok: selftest marker',
        '// int n = static_cast<int>(30.0f * secs);',
        'const char* o = "static_cast<int>(30.0f * secs)";',
        'int p = static_cast<int>(130.0f * w);',                       # 130, not 30
    ]
    fails = []
    for s in flagged:
        if len(findings_in(s)) != 1:
            fails.append('not flagged: ' + s)
    for s in clean:
        if findings_in(s):
            fails.append('flagged: ' + s)
    two = findings_in('x\ny = static_cast<int>(30.0f * a) + static_cast<int>(b * 30.0f);\n')
    if [ln for ln, _ in two] != [2, 2]:
        fails.append('line numbers / two casts on one line: %r' % two)
    for f in fails:
        print('check_frame_casts selftest: ' + f)
    print('check_frame_casts selftest: ' + ('ok' if not fails else 'FAILED'))
    return 0 if not fails else 1


def main(argv):
    if '--selftest' in argv:
        return selftest()
    root = Path(argv[argv.index('--root') + 1]) if '--root' in argv else Path(__file__).resolve().parents[2]
    bad = check(root)
    for b in bad:
        print(b)
    print(f'frame casts: {len(bad)} finding(s); use alamo/core/frames.hpp (tools/lint/check_frame_casts.py)' if bad
          else 'frame casts: ok')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
