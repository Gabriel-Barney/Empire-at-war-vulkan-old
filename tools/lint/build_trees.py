#!/usr/bin/env python
"""Build-tree names and the exe lookup: the one place that knows them (ref3 t03, 2026-11-19).

Every Python tool imports this module and every build script asks it, so a tree is named in one place:

  agent tree   build-<tree>-<compiler>   build_agent.cmd; <tree> defaults to `wt` (one tree per worktree):
                                         build-wt-msvc, build-wt-clang
  main tree    build-<kind>              build_msvc.cmd, build_clang.cmd, build_asan.cmd, build_coverage.cmd,
                                         build_fuzz.cmd: build-msvc, build-clang, build-asan, build-cov, build-fuzz
  module tree  build-<module>            build_module.cmd

Python:   sys.path.insert(0, <repo>/tools/lint); import build_trees
          build_trees.agent_tree('msvc')            -> 'build-wt-msvc'
          build_trees.main_tree('msvc')             -> 'build-msvc'
          build_trees.find_exe('alamo_headless.exe') -> first <repo>/<tree>/bin/<exe> that exists, or None
Scripts:  for /f "delims=" %%t in ('python -I tools\\lint\\build_trees.py name agent wt msvc') do set "B=%%t"

  python -I tools/lint/build_trees.py name agent [tree] <msvc|clang>   print an agent tree name
  python -I tools/lint/build_trees.py name main <msvc|clang|asan|cov|fuzz>
  python -I tools/lint/build_trees.py name module <module>
  python -I tools/lint/build_trees.py exe <exe name> [--main-first] [--msvc] [--root DIR]
                                         print the exe's path (relative to --root, default the repo), exit 1 if none
  python -I tools/lint/build_trees.py bin [--main-first] [--exe NAME]    print the bin folder holding NAME
  python -I tools/lint/build_trees.py check                             lint: no tree name spelled outside this module
  python -I tools/lint/build_trees.py --selftest

Exe lookup order: the worktree's agent trees first (build-wt-msvc, then build-msvc, build-wt-clang, build-clang);
--main-first puts the main MSVC tree first (benchmarks run in the main checkout, where build-msvc is the full build);
--msvc leaves the clang trees out (benchmark baselines, the mod corpus and the probe kits use MSVC exes).

`check` scans tools/**/*.py, tools/**/*.cmd and the root *.cmd scripts for a tree name written as a string (Python) or
on a non-`rem` line (.cmd), outside this file. Docstrings and comments may still name trees; code asks this module.
"""
from __future__ import annotations

import io
import os
import re
import sys
import tempfile
import tokenize

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PREFIX = 'build-'
DEFAULT_TREE = 'wt'
COMPILERS = ('msvc', 'clang')
MAIN_KINDS = ('msvc', 'clang', 'asan', 'cov', 'fuzz')
# Trees of the main checkout that a worktree clean never prunes (the full builds the gate scripts make).
MAIN_TREES_KEPT = frozenset(PREFIX + k for k in ('msvc', 'clang', 'asan'))


def agent_tree(compiler: str, tree: str = DEFAULT_TREE) -> str:
    """build_agent.cmd's tree for <compiler>: build-<tree>-<compiler>."""
    return f'{PREFIX}{tree}-{compiler}'


def main_tree(kind: str) -> str:
    """The fixed tree of a main build script: build-msvc, build-clang, build-asan, build-cov, build-fuzz."""
    if kind not in MAIN_KINDS:
        raise ValueError(f'unknown main tree kind {kind!r} (one of {", ".join(MAIN_KINDS)})')
    return PREFIX + kind


def module_tree(module: str) -> str:
    """build_module.cmd's tree for one module."""
    return PREFIX + module


# The worktree's default trees, MSVC before clang (what a tool means by "the build" when given none).
WARM_TREE = agent_tree('msvc')
# clangd reads the MSVC tree's database since 2026-11-21 (clang builds run nightly only; clangd takes cl commands).
CLANGD_TREE = agent_tree('msvc')
TIDY_TREE = agent_tree('clang')
EXE_ORDER = (agent_tree('msvc'), main_tree('msvc'), agent_tree('clang'), main_tree('clang'))
MAIN_FIRST_ORDER = (main_tree('msvc'), agent_tree('msvc'), main_tree('clang'), agent_tree('clang'))
# The trees land_on_dev.cmd accepts a passing fast-tier stamp from.
LAND_TREES = (agent_tree('msvc'), main_tree('msvc'))


def is_tree_name(name: str) -> bool:
    """A build-* folder name (case-insensitive, as Windows folders are)."""
    return name.lower().startswith(PREFIX)


def exe_order(main_first: bool = False, msvc_only: bool = False) -> tuple[str, ...]:
    order = MAIN_FIRST_ORDER if main_first else EXE_ORDER
    return tuple(t for t in order if t.endswith('-msvc')) if msvc_only else order


def find_tree(root: str | None = None, main_first: bool = False, exe: str | None = None,
              msvc_only: bool = False) -> str | None:
    """The first tree (absolute path) under root whose bin/ holds exe (any bin/ when exe is None). msvc_only: never a
    clang tree (benchmarks compare with MSVC baselines; the corpus and probes run the shipped compiler's exes)."""
    root = root or REPO
    for t in exe_order(main_first, msvc_only):
        b = os.path.join(root, t, 'bin')
        if os.path.isfile(os.path.join(b, exe)) if exe else os.path.isdir(b):
            return os.path.join(root, t)
    return None


def find_exe(exe: str, root: str | None = None, main_first: bool = False, msvc_only: bool = False) -> str | None:
    """The first <root>/<tree>/bin/<exe> that exists, in exe_order(); None when no tree has it."""
    t = find_tree(root, main_first, exe, msvc_only)
    return os.path.join(t, 'bin', exe) if t else None


# ---------------------------------------------------------------------------------------------------------------------
# Lint: tree names stay in this module.

NAME_RE = re.compile(r'(?i)\bbuild-(?:wt-)?(?:msvc|clang|asan|cov|fuzz)\b|\bbuild-[%!]')
PY_LITERAL_RE = re.compile(r'(?i)^build-(?:wt-)?(?:msvc|clang|asan|cov|fuzz)(?:[\\/].*)?$')
SELF = os.path.normcase(os.path.abspath(__file__))


def py_findings(text: str) -> list[int]:
    """Lines of a Python source that hold a string literal naming a build tree."""
    out = []
    try:
        toks = list(tokenize.generate_tokens(io.StringIO(text).readline))
    except (tokenize.TokenError, SyntaxError):
        return out
    fstring_middle = getattr(tokenize, 'FSTRING_MIDDLE', None)   # Python 3.12+: f-string text is its own token
    for t in toks:
        if fstring_middle is not None and t.type == fstring_middle:
            if PY_LITERAL_RE.match(t.string) or re.match(r'(?i)^build-(?:wt-)?(?:msvc|clang|asan|cov|fuzz)(?:[\\/]|$)',
                                                         t.string):
                out.append(t.start[0])
            continue
        if t.type != tokenize.STRING:
            continue
        body = re.sub(r'^[rRbBuUfF]*', '', t.string)
        q = 3 if body[:3] in ('"""', "'''") else 1
        if PY_LITERAL_RE.match(body[q:-q]):
            out.append(t.start[0])
    return out


def cmd_findings(text: str) -> list[int]:
    """Lines of a batch file, outside rem / :: comments, that spell a build-tree name."""
    out = []
    for i, line in enumerate(text.splitlines(), 1):
        s = line.strip().lstrip('@').strip()
        if s.lower().startswith('rem ') or s.lower() == 'rem' or s.startswith('::'):
            continue
        if NAME_RE.search(line):
            out.append(i)
    return out


def scan_files(root: str) -> list[str]:
    files = [os.path.join(root, f) for f in sorted(os.listdir(root)) if f.lower().endswith('.cmd')]
    for d, dirs, names in os.walk(os.path.join(root, 'tools')):
        dirs[:] = sorted(x for x in dirs if x not in ('__pycache__', 'out', 'work') and not is_tree_name(x))
        files += [os.path.join(d, n) for n in sorted(names) if n.endswith(('.py', '.cmd'))]
    return files


def check(root: str | None = None) -> int:
    root = root or REPO
    bad = []
    for f in scan_files(root):
        if os.path.normcase(os.path.abspath(f)) == SELF:
            continue
        try:
            text = open(f, encoding='utf-8', errors='replace').read()
        except OSError:
            continue
        lines = py_findings(text) if f.endswith('.py') else cmd_findings(text)
        rel = os.path.relpath(f, root).replace('\\', '/')
        bad += [f'{rel}:{n}' for n in lines]
    for b in bad:
        print(f'{b}: build-tree name spelled out; ask tools/lint/build_trees.py (agent_tree, main_tree, find_exe)')
    print(f'build_trees check: {len(bad)} finding(s)' if bad else 'build_trees check: ok')
    return 1 if bad else 0


# ---------------------------------------------------------------------------------------------------------------------

def selftest() -> int:
    bad = []

    def expect(c, msg):
        if not c:
            bad.append(msg)

    expect(agent_tree('msvc') == 'build-wt-msvc' and agent_tree('clang', 'n1') == 'build-n1-clang', 'agent names')
    expect(main_tree('asan') == 'build-asan' and module_tree('lua') == 'build-lua', 'main / module names')
    expect(EXE_ORDER == ('build-wt-msvc', 'build-msvc', 'build-wt-clang', 'build-clang'), 'exe order')
    expect(is_tree_name('Build-X') and not is_tree_name('tools'), 'is_tree_name')
    try:
        main_tree('nope')
        bad.append('unknown main kind accepted')
    except ValueError:
        pass
    with tempfile.TemporaryDirectory() as td:
        expect(find_exe('a.exe', td) is None, 'no tree, no exe')
        for t in ('build-msvc', 'build-wt-clang'):
            os.makedirs(os.path.join(td, t, 'bin'))
            open(os.path.join(td, t, 'bin', 'a.exe'), 'w').close()
        expect(find_exe('a.exe', td) == os.path.join(td, 'build-msvc', 'bin', 'a.exe'), 'msvc before clang')
        os.makedirs(os.path.join(td, 'build-wt-msvc', 'bin'))
        expect(find_exe('a.exe', td) == os.path.join(td, 'build-msvc', 'bin', 'a.exe'), 'tree without the exe skipped')
        expect(find_tree(td) == os.path.join(td, 'build-wt-msvc'), 'any bin when no exe given')
        open(os.path.join(td, 'build-wt-msvc', 'bin', 'a.exe'), 'w').close()
        expect(find_exe('a.exe', td).endswith(os.path.join('build-wt-msvc', 'bin', 'a.exe')), 'worktree tree first')
        expect(find_exe('a.exe', td, main_first=True).endswith(os.path.join('build-msvc', 'bin', 'a.exe')), 'main first')
        # lint
        os.makedirs(os.path.join(td, 'tools', 'x'))
        with open(os.path.join(td, 'tools', 'x', 'ok.py'), 'w') as f:
            f.write('"""Usage: [--tree build-wt-msvc]"""\nimport build_trees\nT = build_trees.agent_tree("msvc")\n'
                    'X = "build-time"\n# build-msvc in a comment\n')
        with open(os.path.join(td, 'ok.cmd'), 'w') as f:
            f.write('@echo off\nrem Dir build-msvc\\\n:: build-clang\nset B=%T%\n')
        from contextlib import redirect_stdout
        with redirect_stdout(io.StringIO()):
            expect(check(td) == 0, 'clean files pass')
        expect(find_exe('a.exe', td, msvc_only=True).endswith(os.path.join('build-wt-msvc', 'bin', 'a.exe')), 'msvc only')
        with open(os.path.join(td, 'tools', 'x', 'bad.py'), 'w') as f:
            f.write('A = "x"\nT = ("build-wt-msvc", "build-msvc")\nP = r"build-msvc\\bin\\a.exe"\n')
        if getattr(tokenize, 'FSTRING_MIDDLE', None) is not None:
            expect(py_findings('E = "x"\nP = f"build-msvc/bin/{E}"\n') == [2], 'f-string finding')
        with open(os.path.join(td, 'bad.cmd'), 'w') as f:
            f.write('@echo off\ncmake --build build-clang\nset B=build-!ATREE!-!ACOMP!\n')
        expect(py_findings(open(os.path.join(td, 'tools', 'x', 'bad.py')).read()) == [2, 2, 3], 'py findings')
        expect(cmd_findings(open(os.path.join(td, 'bad.cmd')).read()) == [2, 3], 'cmd findings')
        with redirect_stdout(io.StringIO()):
            expect(check(td) == 1, 'spelled names fail')
    for b in bad:
        print('FAIL: ' + b)
    print('build_trees selftest: ' + ('FAILED' if bad else 'ok'))
    return 1 if bad else 0


def main(argv: list[str]) -> int:
    if argv == ['--selftest']:
        return selftest()
    if argv[:1] == ['check']:
        return check()
    if argv[:2] == ['name', 'agent'] and len(argv) in (3, 4):
        print(agent_tree(argv[-1], argv[2] if len(argv) == 4 else DEFAULT_TREE))
        return 0
    if argv[:2] == ['name', 'main'] and len(argv) == 3:
        print(main_tree(argv[2]))
        return 0
    if argv[:2] == ['name', 'module'] and len(argv) == 3:
        print(module_tree(argv[2]))
        return 0
    if argv[:1] in (['exe'], ['bin']):
        rest = argv[1:]
        main_first = '--main-first' in rest
        msvc_only = '--msvc' in rest
        rest = [a for a in rest if a not in ('--main-first', '--msvc')]
        root = None
        if '--root' in rest:
            i = rest.index('--root')
            root = rest[i + 1]
            rest = rest[:i] + rest[i + 2:]
        exe = None
        if argv[0] == 'exe':
            if len(rest) != 1:
                print(__doc__)
                return 2
            exe = rest[0]
        elif '--exe' in rest:
            exe = rest[rest.index('--exe') + 1]
        t = find_tree(root, main_first, exe, msvc_only)
        if not t:
            return 1
        out = os.path.join(t, 'bin', exe) if argv[0] == 'exe' else os.path.join(t, 'bin')
        print(os.path.relpath(out, root or REPO) if root is None else out)
        return 0
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
