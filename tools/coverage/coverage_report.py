r"""Merge the raw profiles of a coverage tree and report line / function / region coverage (docs/BUILD.md, "Code coverage").

  python -I tools/coverage/coverage_report.py [--tree build-cov] [--no-html] [--files] [--min-lines N]
  python -I tools/coverage/coverage_report.py --selftest

build_coverage.cmd runs this after ctest. Steps: llvm-profdata merge <tree>/coverage/raw/*.profraw -> merged.profdata;
llvm-cov export -summary-only over every instrumented exe in <tree>/bin; keep the files under engine/ and tools/ (not tests,
toolchain, third_party, build trees); sum them per module (engine/<module>, engine/<module>/<sub> for the subfolder libraries
such as tactical/combat, tools/<tool>). Writes <tree>/coverage/summary.txt and summary.json, and llvm-cov show HTML in
<tree>/coverage/html/index.html (skip with --no-html). --files also prints every file, lowest line coverage first.
Exit 0 = report written, 2 = nothing to report (no raw profiles or no instrumented exe). Standard library only.
"""
import argparse
import glob
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'lint'))
import build_trees  # noqa: E402
LLVM_BIN = os.path.join(ROOT, "toolchain", "llvm", "bin")
# llvm-cov regexes for files that are not engine code (LLVM regex: no look-ahead, so several patterns).
CLI_FILES = ("megtool.cpp", "fx2dump.cpp")
IGNORE = [r"[\\/]tools[\\/][^\\/]+[\\/]tools[\\/]",r"megtool\.cpp", r"fx2dump\.cpp",
          r"[\\/]tests?[\\/]", r"[\\/]toolchain[\\/]", r"[\\/]third_party[\\/]", r"[\\/]build-[^\\/]*[\\/]",
          r"[\\/]test_support[\\/]", r"Microsoft Visual Studio", r"Windows Kits"]


def rel_path(path, root):
    try:
        rel = os.path.relpath(os.path.normcase(os.path.normpath(path)), os.path.normcase(os.path.normpath(root)))
    except ValueError:  # another drive
        return None
    rel = rel.replace("\\", "/")
    return None if rel.startswith("..") else rel


def keep(rel):
    if rel is None or not (rel.startswith("engine/") or rel.startswith("tools/")):
        return False
    parts = rel.split("/")
    if any(p in ("tests", "test", "third_party", "test_support", "bench") for p in parts) or rel.startswith("tools/fuzz/"):
        return False
    # Command-line programs (no test runs them; not engine code): tools/<tool>/tools/*.cpp and the two top-level CLIs.
    return not (rel.startswith("tools/") and ("tools" in parts[2:-1] or parts[-1] in CLI_FILES))


def module_of(rel):
    """engine/core/src/x.cpp -> engine/core; engine/tactical/combat/src/x.cpp -> engine/tactical/combat;
    engine/ai/include/alamo/ai/x.hpp -> engine/ai; tools/meg/src/meg.cpp -> tools/meg."""
    parts = rel.split("/")
    if parts[0] == "tools":
        return "/".join(parts[:2])
    for i in range(2, len(parts)):
        if parts[i] in ("src", "include"):
            return "/".join(parts[:i])
    return "/".join(parts[:2])


def add(acc, summary):
    for k in ("lines", "functions", "regions", "branches"):
        s = summary.get(k)
        if s:
            a = acc.setdefault(k, [0, 0])
            a[0] += s.get("covered", 0)
            a[1] += s.get("count", 0)


def pct(pair):
    return 100.0 * pair[0] / pair[1] if pair and pair[1] else 0.0


def summarize(export, root):
    files, modules, total = [], {}, {}
    for data in export.get("data", []):
        for f in data.get("files", []):
            rel = rel_path(f["filename"], root)
            if not keep(rel):
                continue
            s = f["summary"]
            files.append((rel, s))
            add(modules.setdefault(module_of(rel), {}), s)
            add(total, s)
    return files, modules, total


def table(modules, total):
    rows = [f"{'module':34} {'lines':>16} {'functions':>16} {'regions':>16}"]
    for name in sorted(modules, key=lambda m: pct(modules[m].get("lines"))):
        m = modules[name]
        rows.append(f"{name:34} " + " ".join(f"{pct(m.get(k)):6.1f}% {m.get(k, [0, 0])[1]:>8}" for k in ("lines", "functions", "regions")))
    rows.append(f"{'TOTAL':34} " + " ".join(f"{pct(total.get(k)):6.1f}% {total.get(k, [0, 0])[1]:>8}" for k in ("lines", "functions", "regions")))
    return rows


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", **kw)


def selftest():
    root = "E:/p"
    assert keep(rel_path("E:/p/engine/core/src/a.cpp", root))
    assert not keep(rel_path("E:/p/engine/core/tests/t.cpp", root))
    assert not keep(rel_path("E:/p/toolchain/msvc/include/vector", root))
    assert not keep(rel_path("E:/p/engine/render/third_party/smaa/s.h", root))
    assert not keep(rel_path("E:/p/tools/fuzz/targets/fuzz_xml.cpp", root))
    assert not keep(rel_path("E:/p/tools/formats/tools/alodump.cpp", root))
    assert not keep(rel_path("E:/p/tools/meg/src/megtool.cpp", root))
    assert keep(rel_path("E:/p/tools/formats/src/alo.cpp", root))
    assert module_of("engine/core/src/a.cpp") == "engine/core"
    assert module_of("engine/tactical/combat/src/c.cpp") == "engine/tactical/combat"
    assert module_of("engine/ai/include/alamo/ai/x.hpp") == "engine/ai"
    assert module_of("tools/meg/src/meg.cpp") == "tools/meg"
    ex = {"data": [{"files": [
        {"filename": "E:/p/engine/core/src/a.cpp", "summary": {"lines": {"covered": 3, "count": 4}, "functions": {"covered": 1, "count": 1}}},
        {"filename": "E:/p/engine/core/src/b.cpp", "summary": {"lines": {"covered": 1, "count": 4}, "functions": {"covered": 0, "count": 1}}},
        {"filename": "E:/p/engine/core/tests/t.cpp", "summary": {"lines": {"covered": 9, "count": 9}}},
    ]}]}
    files, modules, total = summarize(ex, root)
    assert len(files) == 2 and modules["engine/core"]["lines"] == [4, 8] and total["functions"] == [1, 2]
    assert table(modules, total)[1].startswith("engine/core") and "50.0%" in table(modules, total)[1]
    print("coverage_report selftest OK")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tree", default=build_trees.main_tree("cov"))
    ap.add_argument("--no-html", action="store_true")
    ap.add_argument("--files", action="store_true", help="print every file, lowest line coverage first")
    ap.add_argument("--min-lines", type=int, default=0, help="with --files: only files with at least N lines")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    tree = a.tree if os.path.isabs(a.tree) else os.path.join(ROOT, a.tree)
    out = os.path.join(tree, "coverage")
    raws = glob.glob(os.path.join(out, "raw", "*.profraw"))
    exes = sorted(glob.glob(os.path.join(tree, "bin", "*.exe")))
    if not raws or not exes:
        print(f"coverage_report: {len(raws)} raw profiles, {len(exes)} exes in {tree}: nothing to report", file=sys.stderr)
        return 2
    prof = os.path.join(out, "merged.profdata")
    p = run([os.path.join(LLVM_BIN, "llvm-profdata.exe"), "merge", "-sparse", "-o", prof] + raws)
    if p.returncode != 0:
        print(p.stdout + p.stderr, file=sys.stderr)
        return 2
    # Only exes that ran (have a profile) carry counters; listing all is fine: llvm-cov ignores objects without data.
    objs = [exes[0]] + [x for e in exes[1:] for x in ("-object", e)]
    ign = [f"-ignore-filename-regex={r}" for r in IGNORE]
    cov = os.path.join(LLVM_BIN, "llvm-cov.exe")
    p = run([cov, "export", "-summary-only", f"-instr-profile={prof}"] + ign + objs)
    if p.returncode != 0 or not p.stdout.strip():
        print(p.stderr, file=sys.stderr)
        return 2
    files, modules, total = summarize(json.loads(p.stdout), ROOT)
    rows = table(modules, total)
    head = f"coverage: {len(raws)} raw profiles from {len(exes)} exes, {len(files)} source files (engine/ and tools/, tests excluded)"
    lines = [head, ""] + rows
    if a.files:
        lines += ["", f"{'file (lowest line coverage first)':70} {'lines':>16}"]
        for rel, s in sorted(files, key=lambda fs: (pct([fs[1]["lines"]["covered"], fs[1]["lines"]["count"]]), fs[0])):
            n = s["lines"]["count"]
            if n >= a.min_lines:
                lines.append(f"{rel:70} {pct([s['lines']['covered'], n]):6.1f}% {n:>8}")
    with open(os.path.join(out, "summary.txt"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines) + "\n")
    with open(os.path.join(out, "summary.json"), "w", encoding="utf-8", newline="\n") as fh:
        json.dump({"modules": modules, "total": total, "files": {rel: s for rel, s in files}}, fh, indent=1, sort_keys=True)
    print("\n".join(lines))
    if not a.no_html:
        html = os.path.join(out, "html")
        p = run([cov, "show", "-format=html", f"-output-dir={html}", "-show-line-counts-or-regions", f"-instr-profile={prof}"] + ign + objs)
        print(f"coverage_report: html {os.path.join(html, 'index.html')}" if p.returncode == 0 else f"coverage_report: html failed: {p.stderr[:300]}")
    print(f"coverage_report: summary {os.path.join(out, 'summary.txt')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
