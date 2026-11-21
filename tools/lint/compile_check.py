#!/usr/bin/env python3
"""Edit-time compile check on MSVC (wr3 t02; replaces the string-literal edit hook, which it runs first).

After an edit to a C++ file the translation unit is compiled syntax-only (MSVC /Zs) with the exact flags of the
checkout's agent tree (build-wt-msvc: its compile_commands.json, so MSVC's /W4 /permissive- apply). Since 2026-11-21
clang checks only in the nightly gate (docs/DECISIONS.md, "Clang runs nightly only"); ALAMO_COMPILE_CHECK_CLANG=1 adds
clang -fsyntax-only from build-wt-clang (configure it with build_agent.cmd clang all configure), both compilers then
running at once. A header edit checks up to two of its
includers from the tree's dependency log (the same-stem source first, then the smallest one of the same module). A
source file the build does not know (missing from a CMakeLists, or the tree not reconfigured) is reported as such:
that is what later shows up as "Cannot open include file" or an unresolved symbol. It never blocks an edit: findings go
to stderr with exit 2 (Claude Code shows them to the agent), anything unexpected exits 0 silently.

  compile_check.py <file> ...     check these files, print findings, exit 1 when there are any
  compile_check.py --hook         PostToolUse hook: the edited file from the hook JSON on stdin
  compile_check.py --repro        the common compile errors reproduced on both compilers (acceptance, wr3 t02; needs
                                  both trees)
  compile_check.py --selftest

Tree missing: configure it once per checkout with build_agent.cmd msvc all configure. The MSVC environment
(setup_x64.bat) is captured once into <tree>/msvc_env.json.
"""

from __future__ import annotations

import concurrent.futures
import json
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import build_trees  # noqa: E402

SOURCES = {".c", ".cc", ".cpp", ".cxx"}
HEADERS = {".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp"}
DIAG = re.compile(r"(?:\b(?:fatal error|error|warning) [A-Z]+\d+\b|: (?:fatal error|error|warning):)")
NOISE = re.compile(r"^\s*$|^[^:]*\.c(?:pp|c|xx)?\s*$")  # MSVC echoes the file name
ALL_COMPILERS = ("msvc", "clang")
# The edit check runs MSVC only; clang is opt-in (it checks nightly, docs/DECISIONS.md "Clang runs nightly only").
COMPILERS = ALL_COMPILERS if os.environ.get("ALAMO_COMPILE_CHECK_CLANG") == "1" else ("msvc",)
MAX_LINES = 14


def key(path: str) -> str:
    return os.path.normcase(os.path.normpath(path))


def tree_dir(compiler: str) -> str:
    return os.path.join(ROOT, build_trees.agent_tree(compiler, "wt"))


def load_commands(tree: str) -> dict[str, dict]:
    try:
        with open(os.path.join(tree, "compile_commands.json"), encoding="utf-8") as f:
            entries = json.load(f)
    except (OSError, ValueError):
        return {}
    return {key(e["file"] if os.path.isabs(e["file"]) else os.path.join(e["directory"], e["file"])): e for e in entries}


def syntax_only(compiler: str, command: str) -> str:
    """The tree's compile command turned into a syntax-only run that writes nothing."""
    if compiler == "msvc":
        command = re.sub(r'\s/F[od](?:"[^"]*"|\S+)', "", command)
        return command.replace(" /FS", "") + " /Zs"
    command = re.sub(r'\s-o\s+(?:"[^"]*"|\S+)', "", command)
    command = re.sub(r'\s-M[DT]?[DF]?\s+(?:"[^"]*"|\S+)', "", command) if " -MF" in command else command
    return re.sub(r"\s-c(?=\s)", "", command) + " -fsyntax-only"


def msvc_env(tree: str) -> dict | None:
    setup = os.path.join(ROOT, "toolchain", "msvc", "setup_x64.bat")
    cache = os.path.join(tree, "msvc_env.json")
    if not os.path.isfile(setup):
        return None
    stamp = os.path.getmtime(setup)
    try:
        with open(cache, encoding="utf-8") as f:
            c = json.load(f)
        if c.get("stamp") == stamp:
            return c["env"]
    except (OSError, ValueError, KeyError):
        pass
    p = subprocess.run(f'cmd /d /c ""{setup}" >nul && set"', capture_output=True, text=True, errors="replace")
    env = dict(line.split("=", 1) for line in p.stdout.splitlines() if "=" in line and not line.startswith("="))
    if "INCLUDE" not in env:
        return None
    try:
        with open(cache, "w", encoding="utf-8") as f:
            json.dump({"stamp": stamp, "env": env}, f)
    except OSError:
        pass
    return env


def includers(header: str, tree: str, commands: dict[str, dict], limit: int = 2) -> list[str]:
    """Up to `limit` sources that include the header, from the tree's ninja dependency log."""
    ninja = os.path.join(ROOT, "toolchain", "venv", "Scripts", "ninja.exe")
    if not os.path.isfile(ninja) or not os.path.isfile(os.path.join(tree, ".ninja_deps")):
        return []
    out = subprocess.run([ninja, "-C", tree, "-t", "deps"], capture_output=True, text=True, errors="replace").stdout
    want = key(header)
    hits: list[tuple[str, int]] = []
    obj, count, found = None, 0, False

    def flush():
        if obj and found:
            hits.append((obj, count))

    for line in out.splitlines():
        if not line.startswith(" "):
            flush()
            m = re.match(r"^(.*?): #deps (\d+)", line)
            obj, count, found = (m.group(1), int(m.group(2)), False) if m else (None, 0, False)
        elif obj and not found and line.strip() and key(os.path.join(tree, line.strip())) == want:
            found = True
    flush()
    by_obj = {}
    for src, e in commands.items():
        m = re.search(r'\s(?:/Fo|-o\s+)("?)([^"\s]+)\1', e.get("command", ""))
        if m:
            by_obj[key(os.path.join(tree, m.group(2)))] = src
    srcs = [(by_obj[key(os.path.join(tree, o))], n) for o, n in hits if key(os.path.join(tree, o)) in by_obj]
    stem = os.path.splitext(os.path.basename(header))[0].lower()
    rel = os.path.relpath(header, ROOT).replace("\\", "/").split("/")
    module = "/".join(rel[:2]) if rel[0] == "engine" else ""

    def rank(s: tuple[str, int]):
        name = os.path.splitext(os.path.basename(s[0]))[0].lower()
        srel = os.path.relpath(s[0], ROOT).replace("\\", "/")
        return (name != stem, not (module and srel.lower().startswith(module.lower() + "/")), s[1])

    return [s for s, _ in sorted(srcs, key=rank)[:limit]]


def diagnostics(text: str) -> list[str]:
    lines = text.splitlines()
    out = []
    for i, line in enumerate(lines):
        if DIAG.search(line):
            out.append(line.strip())
            # clang prints the source line and a caret under it
            if ": error:" in line or ": warning:" in line:
                out.extend(l.rstrip() for l in lines[i + 1:i + 3] if l.strip() and not DIAG.search(l))
    return out


def run(compiler: str, entry: dict, env: dict | None) -> tuple[int, list[str], float]:
    t0 = time.time()
    try:
        p = subprocess.run(syntax_only(compiler, entry["command"]), cwd=entry["directory"], env=env, capture_output=True,
                           text=True, errors="replace", timeout=60)
    except (OSError, subprocess.TimeoutExpired) as exc:
        return 0, [f"(not run: {exc})"], time.time() - t0
    return p.returncode, diagnostics(p.stdout + p.stderr), time.time() - t0


def check(paths: list[str]) -> tuple[list[str], float]:
    """Findings (lines) for the edited files, and the wall time."""
    t0 = time.time()
    findings: list[str] = []
    try:
        import check_string_literals as csl
        for p in paths:
            try:
                text = open(p, encoding="utf-8", errors="replace").read()
            except OSError:
                continue
            for line, col, kind in csl.find_bad(text):
                findings.append(f"{os.path.relpath(p, ROOT)}:{line}:{col}: unterminated {kind} literal")
    except (ImportError, AttributeError, ValueError):
        pass
    if findings:
        return findings, time.time() - t0
    jobs = []
    trees = {c: tree_dir(c) for c in COMPILERS}
    cmds = {c: load_commands(t) for c, t in trees.items()}
    known = [c for c in COMPILERS if cmds[c]]
    if not known:
        return [], time.time() - t0
    env: dict[str, dict | None] = {"msvc": msvc_env(trees["msvc"]) if cmds["msvc"] else None, "clang": None}
    for p in paths:
        ext = os.path.splitext(p)[1].lower()
        if ext in SOURCES:
            units = [p]
            if not any(key(p) in cmds[c] for c in known):
                rel = os.path.relpath(p, ROOT)
                if rel.replace("\\", "/").startswith("engine/"):
                    findings.append(f"{rel}: not in the build ({', '.join(os.path.basename(trees[c]) for c in known)}): "
                                    "list it in its CMakeLists.txt, then reconfigure (build_agent.cmd <compiler> all configure)")
                continue
        elif ext in HEADERS:
            c0 = known[0]
            units = includers(p, trees[c0], cmds[c0])
        else:
            continue
        for u in units:
            for c in known:
                e = cmds[c].get(key(u))
                if e and (c != "msvc" or env["msvc"]):
                    jobs.append((c, u, e))
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(lambda j: (j, run(j[0], j[2], env[j[0]])), jobs))
    for (c, u, _), (code, diags, _secs) in results:
        if diags or code:
            shown = diags[:MAX_LINES] or [f"exit {code}, no diagnostic lines"]
            more = f" (+{len(diags) - MAX_LINES} more lines)" if len(diags) > MAX_LINES else ""
            findings.append(f"[{c}] {os.path.relpath(u, ROOT)}{more}:")
            findings.extend("  " + d for d in shown)
    return findings, time.time() - t0


def hook() -> int:
    try:
        data = json.loads(sys.stdin.read() or "{}")
        path = (data.get("tool_input") or {}).get("file_path") or ""
        ext = os.path.splitext(path)[1].lower()
        if not path or ext not in SOURCES | HEADERS or not os.path.isfile(path):
            return 0
        if not key(path).startswith(key(ROOT) + os.sep):
            return 0
        findings, secs = check([path])
    except Exception:  # never get in the way of an edit
        return 0
    if findings:
        sys.stderr.write(f"compile check ({secs:.1f} s, syntax only, {' + '.join(COMPILERS)}) found problems after this edit:\n"
                         + "\n".join(findings) + "\n")
        return 2
    return 0


# The fifteen most frequent compile errors of the build records (2026-11-14 to 21) that a syntax-only run can see, plus
# the conversion warning both compilers must flag (rule 1). Link errors (LNK1104 locked exe, LNK2019 / LNK1120) are
# outside a syntax check; a source missing from its CMakeLists, the usual LNK2019 cause, is the "not in the build" case.
REPROS = [
    ("C1083", '#include "alamo/no_such_header.hpp"\n'),
    ("C2039", "namespace r { struct A {}; inline int f(A a) { return a.nope; } }\n"),
    ("C2065", "namespace r { inline int f() { return undeclared_x; } }\n"),
    ("C3861", "namespace r { inline int f() { return undeclared_fn(1); } }\n"),
    ("C2027", "namespace r { struct B; inline void f(B* b) { b->x = 1; } }\n"),
    ("C2143", "namespace r { inline int f() { int x = 1 return x; } }\n"),
    ("C4430", "namespace r { const cx = 3; }\n"),
    ("C2664", "namespace r { void take(int*); inline void f() { long l = 0; take(&l); } }\n"),
    ("C2737", "namespace r { constexpr int cx; }\n"),
    ("C2146", "namespace r { unknown_type value; }\n"),
    ("C2440", "namespace r { inline void f() { void* v = nullptr; int* q = v; (void)q; } }\n"),
    ("C2059", "namespace r { int z = ); }\n"),
    ("C2653", "namespace r { int w = NoSuchNs::v; }\n"),
    ("C2248", "namespace r { class P { int s = 0; }; inline int f(P p) { return p.s; } }\n"),
    ("C2001", None),  # caught before compiling by the string-literal pass (needs a literal broken over a line)
    ("C4244", "namespace r { inline int f(long long v) { return v; } }\n"),
]


def repro() -> int:
    """Each REPROS snippet compiled with the flags of a real tactical source on both compilers: MSVC must report the
    code, clang an error or warning. Prints the median time per check."""
    src = os.path.join(ROOT, "engine", "tactical", "movement", "src", "movement_system.cpp")
    trees = {c: tree_dir(c) for c in ALL_COMPILERS}
    cmds = {c: load_commands(t).get(key(src)) for c, t in trees.items()}
    if not all(cmds.values()):
        print("compile_check repro: configure build-wt-msvc and build-wt-clang first")
        return 1
    env = {"msvc": msvc_env(trees["msvc"]), "clang": None}
    tmp = os.path.join(trees["msvc"], "tmp", "compile_check_repro")
    os.makedirs(tmp, exist_ok=True)
    jobs = []
    for code, snippet in REPROS:
        if snippet is None:
            f = os.path.join(tmp, f"repro_{REPROS.index((code, snippet)):02d}.cpp")
            with open(f, "w", encoding="utf-8", newline="\n") as fh:
                fh.write('const char* s = "broken\nliteral";\n')
            found, _ = check([f])
            print(f"{'ok  ' if found and 'unterminated' in found[0] else 'MISS'} {code}: string-literal pass")
            continue
        f = os.path.join(tmp, f"repro_{REPROS.index((code, snippet)):02d}.cpp")
        with open(f, "w", encoding="utf-8", newline="\n") as fh:
            fh.write("#include <cstddef>\n" + snippet)
        for c in ALL_COMPILERS:
            e = dict(cmds[c])
            e["command"] = e["command"].replace(f'"{src}"', f'"{f}"')
            jobs.append((code, c, e))
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(lambda j: (j, run(j[1], j[2], env[j[1]])), jobs))
    misses, times = 0, []
    for (code, c, _), (rc, diags, secs) in results:
        times.append(secs)
        hit = any(code in d for d in diags) if c == "msvc" else any(re.search(r": (?:fatal )?(?:error|warning):", d) for d in diags)
        misses += not hit
        print(f"{'ok  ' if hit else 'MISS'} {code} [{c}] {secs:.1f} s: {next((d[d.find(": ") + 2:] for d in diags), "(nothing)")[:110]}")
    times.sort()
    print(f"compile_check repro: {len(results) - misses} of {len(results)} caught; median {times[len(times) // 2]:.1f} s "
          "per compile")
    return 1 if misses else 0


def selftest() -> int:
    bad = []

    def ok(c, what):
        if not c:
            bad.append(what)

    m = syntax_only("msvc", r'cl.exe /nologo /W4 /Foa\b.obj /Fd"a b\c.pdb" /FS -c "x y\z.cpp"')
    ok(m == r'cl.exe /nologo /W4 -c "x y\z.cpp" /Zs', f"msvc command: {m}")
    c = syntax_only("clang", r'clang++.exe -Wall -o engine\a.cpp.obj -c "x y\z.cpp"')
    ok(c == r'clang++.exe -Wall "x y\z.cpp" -fsyntax-only', f"clang command: {c}")
    d = diagnostics("z.cpp\nE:\\a\\z.cpp(3): error C2065: 'q': undeclared identifier\nE:\\a\\z.cpp(9): warning C4244: "
                    "conversion\n")
    ok(len(d) == 2 and "C2065" in d[0] and "C4244" in d[1], f"msvc diagnostics: {d}")
    d = diagnostics("E:/a/z.cpp:3:5: warning: implicit conversion loses precision [-Wshorten-64-to-32]\n   int x = y;\n"
                    "       ~ ^\n1 warning generated.\n")
    ok(len(d) == 3 and "-Wshorten" in d[0], f"clang diagnostics: {d}")
    ok(diagnostics("all good\n") == [], "no diagnostics on clean output")
    for b in bad:
        print("compile_check selftest FAIL: " + b)
    if bad:
        return 1
    print("compile_check selftest: ok")
    return 0


def main(argv: list[str]) -> int:
    if "--selftest" in argv:
        return selftest()
    if "--hook" in argv:
        return hook()
    if "--repro" in argv:
        return repro()
    paths = [os.path.abspath(a) for a in argv if not a.startswith("--")]
    if not paths:
        print(__doc__)
        return 2
    findings, secs = check(paths)
    print("\n".join(findings) if findings else "compile check: clean")
    print(f"compile check: {secs:.1f} s")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
