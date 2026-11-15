"""Run clang-tidy (toolchain/llvm, checks in the root .clang-tidy) over engine/ and tools/ sources of a clang build tree.

  python -I tools/lint/run_tidy.py [--tree build-wt-clang] [--only REGEX] [--tests] [-j N] [--strict]
  python -I tools/lint/run_tidy.py --selftest

Reads <tree>/compile_commands.json (build_agent.cmd clang ... writes it), keeps the .cpp files under engine/ and tools/ (vendored
code lives under toolchain/ and engine/*/third_party/ and is skipped; test sources only with --tests), runs clang-tidy on each in
parallel, and prints one line per distinct finding plus a count per check. Full output: logs/tidy_<tree>.txt.
Exit 0 = ran (findings are information), 1 = findings with --strict, 2 = setup error. Standard library only.
"""
import argparse
import collections
import concurrent.futures
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TIDY = os.path.join(ROOT, "toolchain", "llvm", "bin", "clang-tidy.exe")
FINDING = re.compile(r"^(?P<file>[A-Za-z]:[^:]+|[^:]+):(?P<line>\d+):(?P<col>\d+): (?P<kind>warning|error): (?P<msg>.*) \[(?P<check>[\w.,-]+)\]$")


def norm(p):
    return os.path.normcase(os.path.normpath(p))


def select(entries, root, only, tests):
    rx = re.compile(only) if only else None
    out, seen = [], set()
    for e in entries:
        f = e["file"] if os.path.isabs(e["file"]) else os.path.join(e["directory"], e["file"])
        rel = os.path.relpath(norm(f), norm(root)).replace("\\", "/")
        if rel.startswith("..") or not rel.endswith(".cpp"):
            continue
        if not (rel.startswith("engine/") or rel.startswith("tools/")):
            continue
        if "/third_party/" in rel or rel.startswith("tools/fuzz/"):
            continue
        if not tests and "/tests/" in rel:
            continue
        if rx and not rx.search(rel):
            continue
        if rel not in seen:
            seen.add(rel)
            out.append(rel)
    return sorted(out)


def parse(text, root):
    found = []
    for line in text.splitlines():
        m = FINDING.match(line.strip())
        if not m:
            continue
        f = m["file"]
        rel = os.path.relpath(norm(f), norm(root)).replace("\\", "/") if os.path.isabs(f) else f.replace("\\", "/")
        found.append((rel, int(m["line"]), m["kind"], m["check"], m["msg"]))
    return found


def run_one(tree, rel):
    p = subprocess.run([TIDY, "-p", tree, "--quiet", os.path.join(ROOT, rel)], capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    return rel, p.returncode, p.stdout + p.stderr


def selftest():
    root = "E:/proj"
    entries = [{"directory": root, "file": "E:/proj/" + f} for f in (
        "engine/core/src/a.cpp", "engine/core/tests/t.cpp", "engine/render/third_party/smaa/s.cpp",
        "toolchain/lua-5.0.2/src/lvm.c", "tools/meg/src/meg.cpp", "tools/fuzz/targets/fuzz_xml.cpp")]
    assert select(entries, root, None, False) == ["engine/core/src/a.cpp", "tools/meg/src/meg.cpp"]
    assert select(entries, root, None, True) == ["engine/core/src/a.cpp", "engine/core/tests/t.cpp", "tools/meg/src/meg.cpp"]
    assert select(entries, root, "meg", False) == ["tools/meg/src/meg.cpp"]
    got = parse("E:/proj/engine/core/src/a.cpp:12:3: warning: 'x' used after it was moved [bugprone-use-after-move]\n"
                "  12 | f(x);\nnoise\n", root)
    assert got == [("engine/core/src/a.cpp", 12, "warning", "bugprone-use-after-move", "'x' used after it was moved")], got
    print("run_tidy selftest OK")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tree", default="build-wt-clang")
    ap.add_argument("--only", help="regex on the repo-relative source path")
    ap.add_argument("--tests", action="store_true", help="include */tests/* sources")
    ap.add_argument("-j", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    ap.add_argument("--strict", action="store_true", help="exit 1 when there is any finding")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    tree = a.tree if os.path.isabs(a.tree) else os.path.join(ROOT, a.tree)
    cc = os.path.join(tree, "compile_commands.json")
    if not os.path.isfile(TIDY) or not os.path.isfile(cc):
        print(f"run_tidy: need {TIDY} and {cc} (configure a clang tree: build_agent.cmd clang ...)", file=sys.stderr)
        return 2
    with open(cc, encoding="utf-8") as fh:
        files = select(json.load(fh), ROOT, a.only, a.tests)
    print(f"run_tidy: {len(files)} files, -j {a.j}", flush=True)
    findings, logs, failed = set(), [], []
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.j) as ex:
        for rel, rc, text in ex.map(lambda r: run_one(tree, r), files):
            logs.append(f"=== {rel} (exit {rc})\n{text}")
            got = parse(text, ROOT)
            findings.update(got)
            if rc != 0 and not got:
                failed.append(rel)
    os.makedirs(os.path.join(ROOT, "logs"), exist_ok=True)
    log_path = os.path.join(ROOT, "logs", f"tidy_{os.path.basename(tree)}.txt")
    with open(log_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(logs))
    per_check = collections.Counter(f[3] for f in findings)
    for f in sorted(findings):
        print(f"{f[0]}:{f[1]}: {f[2]}: {f[4]} [{f[3]}]")
    print("run_tidy: per check: " + (", ".join(f"{c} {n}" for c, n in per_check.most_common()) or "none"))
    if failed:
        print(f"run_tidy: clang-tidy failed without findings on {len(failed)} file(s): {', '.join(failed[:5])} (see {log_path})")
    print(f"run_tidy: {len(findings)} distinct findings in {len(files)} files; full output {log_path}")
    if failed:
        return 2
    return 1 if (a.strict and findings) else 0


if __name__ == "__main__":
    sys.exit(main())
