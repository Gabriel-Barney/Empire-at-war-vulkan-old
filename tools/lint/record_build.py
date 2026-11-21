#!/usr/bin/env python3
r"""Where a finished build is recorded (wr2 t06, docs/DECISIONS.md "Workflow redesign": only verifier and nightly results
are roadmap build records).

The build scripts (build_agent / build_msvc / build_clang / build_asan / build_module .cmd) call this instead of
`roadmap_store.py record-build`. With ALAMO_RECORD_BUILD=1 (set by the verifier, tools/gate/verifier.py, and the nightly
gate, tools/gate_scheduled.cmd) it hands the logs to `roadmap_store.py record-build` as before. Otherwise it is an
iteration build: the same record (roadmap_store's own log parser) is written to
`<main checkout>\logs\build_records\<id>.json` and nothing is committed or pushed. tools/analysis/workflow_stats.py reads
both folders; the repeat refusal reads each tree's build_state.json and is unchanged.

  python -I tools/lint/record_build.py <compiler> <tree> <log>...
  python -I tools/lint/record_build.py --selftest
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))


def main_checkout() -> str:
    r = subprocess.run(["git", "-C", ROOT, "rev-parse", "--path-format=absolute", "--git-common-dir"],
                       capture_output=True, text=True)
    return os.path.dirname(r.stdout.strip()) if r.returncode == 0 else ROOT


def roadmap_module(main: str):
    sys.path.insert(0, os.path.join(main, "roadmap-store"))
    import roadmap_store  # noqa: E402
    return roadmap_store


def local_record(compiler: str, tree: str, logs: list[str], parse_build, build_extra, folder: str) -> str:
    text = ""
    for path in logs:
        try:
            with open(path, encoding="utf8", errors="replace") as f:
                text += f.read() + "\n"
        except OSError:
            pass
    at = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    at = at[:-2] + ":" + at[-2:] if re.search(r"[+-]\d{4}$", at) else at
    machine = os.environ.get("COMPUTERNAME", "?")
    doc = {"at": at, "by": os.environ.get("ALAMO_SESSION") or os.environ.get("USERNAME", "?"), "machine": machine,
           "compiler": compiler, "tree": tree, "local": True, **parse_build(text)}
    doc.update(build_extra(os.environ.get("ALAMO_BUILD_EXTRA")))
    ex = doc.get("exits", {})
    if doc.get("test_filter") == "none" and "ctest" not in ex and ex.get("configure") == 0 and ex.get("build") == 0:
        doc["status"] = "pass"
    stamp = re.sub(r"\D", "", at[:19])
    doc_id = re.sub(r"[^A-Za-z0-9_\-.]", "-", f"{stamp}-{machine}-{tree}-{compiler}")
    os.makedirs(folder, exist_ok=True)
    path = os.path.join(folder, doc_id + ".json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=2, sort_keys=True)
    return path


def selftest() -> int:
    bad = []
    with tempfile.TemporaryDirectory() as tmp:
        log = os.path.join(tmp, "b.txt")
        with open(log, "w") as f:
            f.write("x\n")
        p = local_record("msvc", "tree", [log], lambda t: {"status": "fail", "exits": {"build": 2}},
                         lambda path: {}, os.path.join(tmp, "rec"))
        d = json.load(open(p))
        if not (d["local"] and d["status"] == "fail" and d["compiler"] == "msvc" and os.path.basename(p).endswith("-msvc.json")):
            bad.append(f"local record fields: {d}")
        p2 = local_record("msvc", "t", [log], lambda t: {"test_filter": "none", "exits": {"configure": 0, "build": 0},
                                                        "status": "fail"}, lambda path: {}, os.path.join(tmp, "rec"))
        if json.load(open(p2))["status"] != "pass":
            bad.append("a build without tests counts as pass")
    for b in bad:
        print("record_build selftest FAIL: " + b)
    if bad:
        return 1
    print("record_build selftest: ok")
    return 0


def main(argv: list[str]) -> int:
    if argv[:1] == ["--selftest"]:
        return selftest()
    if len(argv) < 3:
        print(__doc__)
        return 2
    compiler, tree, logs = argv[0], argv[1], argv[2:]
    main_co = main_checkout()
    if os.environ.get("ALAMO_RECORD_BUILD") == "1":
        return subprocess.run([sys.executable, os.path.join(main_co, "roadmap-store", "roadmap_store.py"), "record-build",
                               compiler, tree, *logs]).returncode
    try:
        rs = roadmap_module(main_co)
        path = local_record(compiler, tree, logs, rs.parse_build, rs.build_extra, os.path.join(main_co, "logs", "build_records"))
        print(f"recorded build locally: {os.path.relpath(path, main_co)}")
    except Exception as e:  # a record must never fail a build
        print(f"record_build: local record not written ({e})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
