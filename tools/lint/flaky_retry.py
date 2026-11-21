#!/usr/bin/env python3
"""Flaky test quarantine (wr3 t05): run ctest; a failed test that passes on one immediate retry is flaky.

  python -I tools/lint/flaky_retry.py -- ctest --test-dir <tree> [ctest args...]
  python -I tools/lint/flaky_retry.py --report [--days 7]     flaky tests of the last days (all checkouts)
  python -I tools/lint/flaky_retry.py --selftest

After ctest fails, each failed test is run once more on its own (same tree, source unchanged) unless it is a
determinism test: ctest label `determinism` (alamo_add_worker_count_check sets it) or a name that says so (worker-count
entries `_w<N>`, goldens, CRC, replay, selfcheck, the parity harness). Those, and tests that did not run, are never
retried: a second run of a determinism check proves nothing. A test that passes on the retry
  - is recorded in <main checkout>/logs/flaky_tests.jsonl with its first output,
  - does not fail the run: a final ctest-style block is printed ("N% tests passed, F tests failed out of M" and the
    remaining "The following tests FAILED:" list), which build_summary.py reads (it uses the last such block),
  - is named on a "flaky:" line in the summary.
A test flaky three times in seven days is filed once as an S2 task of the standing group ngt1 when the run records
builds (ALAMO_RECORD_BUILD=1: the verifier and the nightly gate); otherwise the line says to file it.
Exit: ctest's code, or 0 when every failure passed on its retry.
"""

from __future__ import annotations

import datetime
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
DETERMINISM = re.compile(r"(_w\d+|_k\d+_w\d+)$|golden|_crc|crc_|replay|selfcheck|determinism|^parity_harness",
                         re.I)
FAILED_LINE = re.compile(r"^\s+\d+ - (\S+) \(([^)]*)\)\s*$")
TOTAL = re.compile(r"\d+% tests passed(?:, (\d+) tests failed)? out of (\d+)")
THRESHOLD, WINDOW_DAYS = 3, 7


def main_checkout() -> str:
    r = subprocess.run(["git", "-C", ROOT, "rev-parse", "--path-format=absolute", "--git-common-dir"],
                       capture_output=True, text=True)
    return os.path.dirname(r.stdout.strip()) if r.returncode == 0 else ROOT


def log_path() -> str:
    return os.path.join(main_checkout(), "logs", "flaky_tests.jsonl")


def failed_tests(text: str) -> list[tuple[str, str]]:
    """(name, status) of the last "The following tests FAILED:" block."""
    block = text.rsplit("The following tests FAILED:", 1)
    if len(block) < 2:
        return []
    out = []
    for line in block[1].splitlines()[1:] if block[1].startswith("\n") else block[1].splitlines():
        m = FAILED_LINE.match(line)
        if m:
            out.append((m.group(1), m.group(2)))
        elif line.strip() and out:
            break
    return out


def labels_of(tree: str, ctest: str) -> dict[str, list[str]]:
    p = subprocess.run([ctest, "--test-dir", tree, "--show-only=json-v1"], capture_output=True, text=True, errors="replace")
    try:
        tests = json.loads(p.stdout).get("tests", [])
    except ValueError:
        return {}
    return {t["name"]: next((x.get("value") or [] for x in t.get("properties", []) if x.get("name") == "LABELS"), [])
            for t in tests}


def retryable(name: str, status: str, labels: list[str]) -> bool:
    if "determinism" in labels or DETERMINISM.search(name):
        return False
    return "Not Run" not in status and "Disabled" not in status


def test_output(text: str, name: str) -> str:
    """The test's own lines from ctest --output-on-failure (between its status line and the next test)."""
    lines = text.splitlines()
    start = next((i for i, l in enumerate(lines) if re.search(rf"Test\s+#\d+: {re.escape(name)} \.", l)), None)
    if start is None:
        return ""
    end = next((i for i in range(start + 1, len(lines)) if re.search(r"Test\s+#\d+: \S+ \.", lines[i])), len(lines))
    return "\n".join(lines[start:end])[-3000:]


def source_id() -> str:
    r = subprocess.run(["git", "-C", ROOT, "rev-parse", "HEAD"], capture_output=True, text=True)
    return r.stdout.strip()[:12]


def record(entries: list[dict]) -> None:
    path = log_path()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "a", encoding="utf-8") as f:
        for e in entries:
            f.write(json.dumps(e) + "\n")


def history(days: int = WINDOW_DAYS) -> list[dict]:
    cutoff = (datetime.datetime.now() - datetime.timedelta(days=days)).isoformat(timespec="seconds")
    out = []
    try:
        with open(log_path(), encoding="utf-8") as f:
            for line in f:
                try:
                    e = json.loads(line)
                except ValueError:
                    continue
                if e.get("at", "") >= cutoff:
                    out.append(e)
    except OSError:
        pass
    return out


def file_if_due(names: list[str]) -> list[str]:
    """Lines about tests that reached THRESHOLD flaky runs in WINDOW_DAYS; files each once (state in the log dir)."""
    counts: dict[str, int] = {}
    for e in history():
        counts[e["test"]] = counts.get(e["test"], 0) + 1
    state_path = os.path.join(os.path.dirname(log_path()), "flaky_filed.json")
    try:
        with open(state_path, encoding="utf-8") as f:
            filed = json.load(f)
    except (OSError, ValueError):
        filed = {}
    lines = []
    for n in names:
        if counts.get(n, 0) < THRESHOLD or n in filed:
            continue
        text = (f"[flaky] {n} failed and passed on an immediate retry {counts[n]} times in {WINDOW_DAYS} days "
                f"(<main checkout>/logs/flaky_tests.jsonl has each first output). Find the timing or shared-state cause, "
                f"fix it, or move the check out of the landing tier.")
        if os.environ.get("ALAMO_RECORD_BUILD") == "1":
            sys.path.insert(0, os.path.join(ROOT, "tools", "gate"))
            try:
                import nightly_triage as nt
                main = main_checkout()
                nt.ensure_group(main)
                ref = nt.file_task(main, text)
            except Exception as exc:  # filing must never fail a build
                ref = f"not filed ({exc})"
            filed[n] = ref
            lines.append(f"flaky: {n} reached {counts[n]} in {WINDOW_DAYS} days: filed as {ref}")
        else:
            lines.append(f"flaky: {n} reached {counts[n]} in {WINDOW_DAYS} days: file it (roadmap_store.py backlog add "
                         f"... --sev S2 --to <owning group>), or let the verifier / nightly run file it")
    if filed:
        try:
            with open(state_path, "w", encoding="utf-8") as f:
                json.dump(filed, f, indent=1)
        except OSError:
            pass
    return lines


def run(cmd: list[str]) -> int:
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    chunks = []
    assert p.stdout
    for line in p.stdout:
        sys.stdout.write(line)
        chunks.append(line)
    rc = p.wait()
    text = "".join(chunks)
    if rc == 0:
        return 0
    fails = failed_tests(text)
    m = None
    for m in TOTAL.finditer(text):
        pass
    if not fails or not m:
        return rc
    total = int(m.group(2))
    tree = cmd[cmd.index("--test-dir") + 1] if "--test-dir" in cmd else "."
    labels = labels_of(tree, cmd[0])
    retry = [n for n, s in fails if retryable(n, s, labels.get(n, []))]
    if not retry:
        return rc
    print(f"\nflaky retry: running {len(retry)} failed test(s) once more: {', '.join(retry)}", flush=True)
    rr = subprocess.run([cmd[0], "--test-dir", tree, "--output-on-failure", "-j", "4", "-R",
                         "^(" + "|".join(re.escape(n) for n in retry) + ")$"], capture_output=True, text=True,
                        errors="replace")
    still = {n for n, _ in failed_tests(rr.stdout)}
    if rr.returncode != 0 and not still:  # the retry itself broke (no list): trust nothing
        still = set(retry)
    flaky = [n for n in retry if n not in still]
    now = datetime.datetime.now().isoformat(timespec="seconds")
    record([{"at": now, "test": n, "tree": os.path.abspath(tree), "source": source_id(),
             "output": test_output(text, n)} for n in flaky])
    remaining = [(n, s) for n, s in fails if n not in flaky]
    print("\n".join(f"flaky: {n} failed, then passed on one retry with unchanged source (recorded)" for n in flaky))
    for line in file_if_due(flaky):
        print(line)
    print(f"\nafter the flaky retry: {round(100 * (total - len(remaining)) / total) if total else 100}% tests passed"
          + (f", {len(remaining)} tests failed" if remaining else "") + f" out of {total}")
    if remaining:
        print("\nThe following tests FAILED:")
        for n, s in remaining:
            print(f"\t  0 - {n} ({s})")
        return rc
    return 0


def report(days: int) -> int:
    counts: dict[str, int] = {}
    for e in history(days):
        counts[e["test"]] = counts.get(e["test"], 0) + 1
    for n, c in sorted(counts.items(), key=lambda kv: -kv[1]):
        print(f"{c:>3}  {n}")
    print(f"flaky tests in the last {days} days: {len(counts)} ({sum(counts.values())} retries passed)")
    return 0


def selftest() -> int:
    bad = []

    def ok(c, what):
        if not c:
            bad.append(what)

    text = ("Test #3: alamo_jobs_tests ....***Failed  1.2 sec\nFAIL test_jobs.cpp:1031: warm >= 1\n"
            "Test #4: alamo_core_tests ....   Passed\n97% tests passed, 2 tests failed out of 70\n\n"
            "The following tests FAILED:\n\t  3 - alamo_jobs_tests (Failed)\n\t 40 - alamo_sim_save_continue_w4 (Failed)\n"
            "Errors while running CTest\n")
    f = failed_tests(text)
    ok(f == [("alamo_jobs_tests", "Failed"), ("alamo_sim_save_continue_w4", "Failed")], f"failed list: {f}")
    ok(retryable("alamo_jobs_tests", "Failed", []), "a plain test is retried")
    for n in ("alamo_sim_save_continue_w4", "alamo_headless_skirmish_ai_foc_land_ai_lane_k1_w4", "alamo_game_smoke_campaign_golden",
              "alamo_jobs_crc_tests", "alamo_headless_replay_workers", "parity_harness_hotm_galactic"):
        ok(not retryable(n, "Failed", []), f"determinism test {n} is never retried")
    ok(not retryable("alamo_headless_skirmish_ai_foc_land_ai_lane", "Failed", ["slow", "determinism"]),
       "the determinism label (w0 producer, no suffix) is never retried")
    ok(not retryable("alamo_x_tests", "Not Run", []), "a test that did not run is not retried")
    ok("warm >= 1" in test_output(text, "alamo_jobs_tests") and "Passed" not in test_output(text, "alamo_jobs_tests"),
       "the test's own output is cut out")
    for b in bad:
        print("flaky_retry selftest FAIL: " + b)
    if bad:
        return 1
    print("flaky_retry selftest: ok")
    return 0


def main(argv: list[str]) -> int:
    if "--selftest" in argv:
        return selftest()
    if "--report" in argv:
        days = int(argv[argv.index("--days") + 1]) if "--days" in argv else WINDOW_DAYS
        return report(days)
    if "--" not in argv:
        print(__doc__)
        return 2
    return run(argv[argv.index("--") + 1:])


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
