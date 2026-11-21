#!/usr/bin/env python3
"""Affected tests from the build graph (wr3 t01; replaces changed_tests.py's module-folder mapping).

A test is affected by a changed file when
  - an executable its ctest command runs (the command itself, or any `.exe` of the tree named in its arguments, e.g.
    `-DEXE=` of a CMake script test) depends on the file through the build graph: the exe's transitive inputs
    (`ninja -t inputs`) give its objects, each object's source (from CMake's `<target>.dir/<path>.obj` layout) and headers
    (`ninja -t deps`) give the files; so a change in a library selects the tests of every module that links it, the apps
    scenario tests included;
  - or its command line names the file (data streams, goldens, scripts, recipes).
A change to a build file or shared test support (engine/CMakeLists.txt, engine/test_support/, the top-level
CMakeLists.txt, cmake/) selects every test. Tests labelled slow are left out unless --slow.

  python -I tools/lint/affected_tests.py <tree> <out file> [--base origin/dev] [--slow]   one test name per line
  python -I tools/lint/affected_tests.py <tree> --evaluate N [--ref origin/dev]   the last N dev commits that changed
      engine code and a tracked golden together: is the golden's test selected by the commit's other files?
  python -I tools/lint/affected_tests.py --selftest

The graph is cached in <tree>/affected_graph.json, rebuilt when build.ninja or .ninja_deps change. Exit 0; 2 on usage.
"""

from __future__ import annotations

import json
import os
import re
import shutil
import statistics
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
EVERYTHING = ("engine/CMakeLists.txt", "engine/test_support/", "CMakeLists.txt", "cmake/")
OBJ_RE = re.compile(r"^(?:(?P<dir>.*?)/)?CMakeFiles/[^/]+\.dir/(?P<rest>.+)\.obj$")


def norm_rel(path: str, base: str) -> str | None:
    """A path (absolute, or relative to base) as repo-relative with forward slashes; None when outside the repo."""
    p = os.path.normpath(path if os.path.isabs(path) else os.path.join(base, path))
    root = os.path.normpath(ROOT)
    if os.path.normcase(p).startswith(os.path.normcase(root) + os.sep):
        return p[len(root) + 1:].replace("\\", "/")
    return None


def obj_source(obj: str) -> str | None:
    """engine/core/CMakeFiles/alamo_core.dir/src/dmath.cpp.obj -> engine/core/src/dmath.cpp (tree-relative object)."""
    m = OBJ_RE.match(obj.replace("\\", "/"))
    if not m:
        return None
    return f"{m.group('dir')}/{m.group('rest')}" if m.group("dir") else m.group("rest")


# ---------------------------------------------------------------- the graph

def find_tool(name: str) -> str:
    exe = shutil.which(name)
    if exe:
        return exe
    for sub in ("venv/Scripts", "cmake/bin", "ninja"):
        p = os.path.join(ROOT, "toolchain", *sub.split("/"), name + ".exe")
        if os.path.isfile(p):
            return p
    raise RuntimeError(f"{name} not found")


def parse_deps(text: str, tree: str) -> dict[str, set[str]]:
    """object (tree-relative) -> repo-relative files it was compiled from (source and headers)."""
    out: dict[str, set[str]] = {}
    cur = None
    for line in text.splitlines():
        if not line.strip():
            continue
        if not line.startswith(" "):
            cur = line.split(":", 1)[0].strip()
            out[cur] = set()
            src = obj_source(cur)
            if src:
                r = norm_rel(src, ROOT) if not src.startswith("..") else norm_rel(src, tree)
                if r:
                    out[cur].add(r)
            continue
        if cur is not None:
            r = norm_rel(line.strip(), tree)
            if r:
                out[cur].add(r)
    return out


def build_graph(tree: str) -> dict:
    ninja, ctest = find_tool("ninja"), find_tool("ctest")
    deps = parse_deps(subprocess.run([ninja, "-C", tree, "-t", "deps"], capture_output=True, text=True, errors="replace").stdout, tree)
    tests = json.loads(subprocess.run([ctest, "--test-dir", tree, "--show-only=json-v1"], capture_output=True, text=True).stdout).get("tests", [])
    exe_files: dict[str, list[str]] = {}

    by_source: dict[str, set[str]] = {}
    for files in deps.values():
        for f in files:
            if f.endswith((".cpp", ".c", ".cc")):
                by_source.setdefault(f, set()).update(files)

    def files_of(target: str) -> list[str]:
        # `ninja -t inputs` lists the transitive inputs one per line (sources quoted when they hold spaces, not objects).
        if target not in exe_files:
            lines = subprocess.run([ninja, "-C", tree, "-t", "inputs", target], capture_output=True, text=True,
                                   errors="replace").stdout.splitlines()
            fs: set[str] = set()
            for line in lines:
                r = norm_rel(line.strip().strip('"'), tree)
                if r:
                    fs.add(r)
                    fs |= by_source.get(r, set())
            exe_files[target] = sorted(fs)
        return exe_files[target]

    out_tests = {}
    tree_n = os.path.normcase(os.path.normpath(tree))
    for t in tests:
        cmd = t.get("command") or []
        labels = next((p.get("value") or [] for p in t.get("properties", []) if p.get("name") == "LABELS"), [])
        files: set[str] = set()
        for arg in cmd:
            for piece in re.split(r"[=;]", arg):
                if not piece:
                    continue
                pn = os.path.normcase(os.path.normpath(piece))
                if pn.endswith(".exe") and pn.startswith(tree_n + os.sep):
                    files |= set(files_of(os.path.relpath(piece, tree).replace("\\", "/")))
                    continue
                r = norm_rel(piece, ROOT) if os.path.isabs(piece) else None
                if r:
                    files.add(r)
        out_tests[t["name"]] = {"slow": "slow" in labels, "files": sorted(files)}
    return {"tests": out_tests}


def graph(tree: str) -> dict:
    cache = os.path.join(tree, "affected_graph.json")
    stamp = [os.path.getmtime(os.path.join(tree, f)) if os.path.exists(os.path.join(tree, f)) else 0 for f in ("build.ninja", ".ninja_deps")]
    try:
        with open(cache, encoding="utf-8") as f:
            g = json.load(f)
        if g.get("stamp") == stamp:
            return g
    except (OSError, ValueError):
        pass
    g = build_graph(tree)
    g["stamp"] = stamp
    with open(cache, "w", encoding="utf-8") as f:
        json.dump(g, f)
    return g


# ---------------------------------------------------------------- selection

def select(g: dict, changed: list[str], slow: bool = False) -> tuple[list[str], str]:
    """(test names, reason) for the changed repo-relative files."""
    tests = {n: t for n, t in g["tests"].items() if slow or not t["slow"]}
    if any(c == e or c.startswith(e) for c in changed for e in EVERYTHING):
        return sorted(tests), "a build file or shared test support changed: every test"
    ch = set(changed)
    dirs = [c[: -len("CMakeLists.txt")] for c in changed if c.endswith("/CMakeLists.txt")]
    picked = sorted(n for n, t in tests.items()
                    if ch & set(t["files"]) or any(f.startswith(d) for d in dirs for f in t["files"]))
    return picked, f"{len(picked)} of {len(tests)} tests depend on the {len(ch)} changed file(s)"


def git_lines(*args: str) -> list[str]:
    p = subprocess.run(["git", "-C", ROOT, *args], capture_output=True, text=True)
    return [l for l in p.stdout.splitlines() if l.strip()] if p.returncode == 0 else []


def changed_files(base: str) -> list[str]:
    mb = git_lines("merge-base", base, "HEAD")
    names = set(git_lines("diff", "--name-only", mb[0] if mb else "HEAD"))
    names |= set(git_lines("ls-files", "--others", "--exclude-standard"))
    return sorted(n.replace("\\", "/") for n in names)


GOLDEN_RE = re.compile(r"^(?!tools/|launcher/)(.*/)?(testdata|golden|goldens|benchmark)/")


def evaluate(g: dict, n: int, ref: str) -> int:
    """The last n commits on ref that changed a tracked golden. The golden's tests (those whose command names it) must
    be selected by engine files changed since the golden's previous change (its own commit included): a regeneration
    commit usually carries no code, the change that moved the golden landed before it. A test that misses names it."""
    readers: dict[str, list[str]] = {}
    for name, t in g["tests"].items():
        for f in t["files"]:
            if GOLDEN_RE.search(f):
                readers.setdefault(f, []).append(name)
    history = git_lines("rev-list", "--no-merges", ref, "-n", "6000")
    files_at = {}

    def files(sha: str) -> list[str]:
        if sha not in files_at:
            files_at[sha] = git_lines("diff-tree", "--no-commit-id", "--name-only", "-r", sha)
        return files_at[sha]

    cases = hits = 0
    sizes = []
    for i, sha in enumerate(history):
        goldens = [f for f in files(sha) if f in readers]
        if not goldens:
            continue
        missing = set()
        for gld in goldens:
            code: set[str] = set()
            for older in history[i:i + 400]:
                fs = files(older)
                code |= {f for f in fs if f.startswith("engine/") and not GOLDEN_RE.search(f)}
                if older != sha and gld in fs:
                    break
            if not code:
                continue
            picked, _ = select(g, sorted(code), slow=True)
            sizes.append(len(picked))
            missing |= set(readers[gld]) - set(picked)
        if not sizes:
            continue
        cases += 1
        hits += not missing
        print(("HIT  " if not missing else "MISS ") + sha[:8] + " " + ", ".join(goldens)[:120]
              + ("" if not missing else "; missing " + ", ".join(sorted(missing))))
        if cases >= n:
            break
    print(f"evaluate: {hits} of {cases} golden changes had their tests selected by the engine changes behind them; "
          f"median selection {statistics.median(sizes) if sizes else 0} of {len(g['tests'])} tests")
    return 0 if hits == cases else 1


# ---------------------------------------------------------------- selftest

def selftest() -> int:
    bad = []

    def check(c, what):
        if not c:
            bad.append(what)

    check(obj_source("engine/core/CMakeFiles/alamo_core.dir/src/dmath.cpp.obj") == "engine/core/src/dmath.cpp", "obj -> source")
    check(obj_source("CMakeFiles/x.dir/a.cpp.obj") == "a.cpp" and obj_source("foo.obj") is None, "obj edge cases")
    g = {"tests": {
        "core_tests": {"slow": False, "files": ["engine/core/src/a.cpp", "engine/core/include/a.hpp"]},
        "tactical_tests": {"slow": False, "files": ["engine/tactical/src/t.cpp", "engine/core/include/a.hpp"]},
        "headless_golden": {"slow": True, "files": ["engine/apps/headless/main.cpp", "engine/core/include/a.hpp",
                                                    "engine/tactical/src/t.cpp", "engine/apps/headless/testdata/g.crc"]},
        "render_tests": {"slow": False, "files": ["engine/render/src/r.cpp"]}}}
    p, _ = select(g, ["engine/core/include/a.hpp"])
    check(p == ["core_tests", "tactical_tests"], f"a core header selects every linking module's fast tests: {p}")
    p, _ = select(g, ["engine/core/include/a.hpp"], slow=True)
    check("headless_golden" in p, "with --slow the apps scenario test that links core is selected")
    p, _ = select(g, ["engine/apps/headless/testdata/g.crc"], slow=True)
    check(p == ["headless_golden"], f"a data file named by the command selects its test: {p}")
    p, why = select(g, ["engine/CMakeLists.txt"])
    check(len(p) == 3 and "every test" in why, "a build file selects every (fast) test")
    p, _ = select(g, ["engine/tactical/CMakeLists.txt"])
    check(p == ["tactical_tests"], f"a module CMakeLists selects the tests built from that folder: {p}")
    p, _ = select(g, ["docs/x.md"])
    check(p == [], "a docs change selects nothing")
    deps = parse_deps("engine/core/CMakeFiles/alamo_core.dir/src/a.cpp.obj: #deps 2, deps mtime 1 (VALID)\n"
                      "    ../engine/core/include/a.hpp\n    C:/outside/stdio.h\n", os.path.join(ROOT, "build-x"))
    got = deps.get("engine/core/CMakeFiles/alamo_core.dir/src/a.cpp.obj")
    check(got == {"engine/core/src/a.cpp", "engine/core/include/a.hpp"}, f"deps parse keeps repo files: {got}")
    for b in bad:
        print("affected_tests selftest FAIL: " + b)
    if bad:
        return 1
    print("affected_tests selftest: ok")
    return 0


def main(argv: list[str]) -> int:
    import argparse
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("tree", nargs="?")
    ap.add_argument("out", nargs="?")
    ap.add_argument("--base", default="origin/dev")
    ap.add_argument("--slow", action="store_true")
    ap.add_argument("--evaluate", type=int)
    ap.add_argument("--ref", default="origin/dev")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest()
    if not a.tree:
        ap.print_help()
        return 2
    tree = a.tree if os.path.isabs(a.tree) else os.path.join(ROOT, a.tree)
    g = graph(tree)
    sys.path.insert(0, HERE)  # python -I leaves the script's folder off sys.path
    import test_tiers  # the measured tiers decide what "slow" means (wr3 t03)
    for name, tier in test_tiers.load_tiers().items():
        if name in g["tests"]:
            g["tests"][name]["slow"] = tier == "nightly"
    if a.evaluate:
        return evaluate(g, a.evaluate, a.ref)
    if not a.out:
        ap.error("an out file is needed")
    picked, why = select(g, changed_files(a.base), a.slow)
    if not picked:
        picked, why = select(g, ["engine/CMakeLists.txt"], a.slow)
        why = "no test depends on the changed files: the whole fast tier" if not a.slow else why
    with open(a.out, "w", encoding="utf-8") as f:
        f.write("\n".join(picked) + "\n")
    print(f"affected_tests: {why}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
