#!/usr/bin/env python3
r"""The verifier (wr2 t02, t03): the one process that advances dev (docs/DECISIONS.md "Workflow redesign").

It takes landing-queue entries (tools/gate/land_queue.py) in order, places them on origin/dev's tip in its own locked
worktree `.claude/worktrees/verifier` (warm trees build-verify-msvc / build-verify-clang; no writer uses them), and runs
the landing checks there:

  1. build_agent.cmd msvc verify all (every module, the landing tier with one rotating worker count, ALAMO_WC_SLOT),
     with the build record on (ALAMO_RECORD_BUILD=1); the test tier budget is reported (test_tiers.py check);
  2. build_agent.cmd clang verify all when the placed commits touch sim code (engine/ except render, audio, video, ui);
  3. tools/lint/golden_merge.py slow origin/dev HEAD (the slow checks of goldens the entries changed);
  4. tools/goldens/behaviour_check.py range origin/dev HEAD (every sim commit declares `Behaviour:` and no undeclared
     column moved); its one-line landing record goes to <main checkout>/logs/behaviour_landings.txt (wr1 t04 count).

All pass: dev is fast-forwarded to the placed commits (`git push origin HEAD:refs/heads/dev`; a rejected push means dev
moved and the batch is placed again). A failure rejects the entry with the summary file
`<main checkout>/.cache/land-queue/<id>.summary.txt`; dev does not move. Several waiting entries are checked as one batch;
when the batch fails, the first bad entry is found by bisecting prefixes, the good prefix lands and the bad entry is
rejected (t03). The verifier never edits code.

  python -I tools/gate/verifier.py run [--once] [--no-batch] [--poll SECONDS]
  python -I tools/gate/verifier.py --selftest

`tools\gate\install_verifier_task.cmd` copies this file and land_queue.py to `<main checkout>\logs\verifier_task\` and
registers the per-user task AlamoVerifier that runs `run --once` every 5 minutes. Standard library only.
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import time
from typing import Callable

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import land_queue as lq  # noqa: E402

NOT_SIM = ("engine/render/", "engine/audio/", "engine/video/", "engine/ui/")
WT_NAME = ".claude/worktrees/verifier"
TREE = "verify"


def git(repo: str, *args: str, check: bool = True) -> str:
    return lq.git(repo, *args, check=check)


# ---------------------------------------------------------------- placing

def place(wt: str, entries: list[dict], base: str = "origin/dev") -> tuple[str, list[tuple[dict, str]]]:
    """Check out base in wt and rebase each entry's series onto the growing tip, in order. Returns (tip, conflicts):
    an entry that does not apply is left out and listed with git's message."""
    git(wt, "checkout", "-q", "-f", "--detach", base)
    conflicts = []
    for e in entries:
        head = git(wt, "rev-parse", "HEAD").strip()
        r = subprocess.run(["git", "-C", wt, "rebase", "-q", "--onto", head, e["base"], e["tip"]],
                           capture_output=True, text=True, errors="replace")
        if r.returncode != 0:
            subprocess.run(["git", "-C", wt, "rebase", "--abort"], capture_output=True)
            git(wt, "checkout", "-q", "-f", "--detach", head)
            conflicts.append((e, (r.stdout + r.stderr).strip()[-600:]))
    return git(wt, "rev-parse", "HEAD").strip(), conflicts


def touches_sim(wt: str, base: str, tip: str) -> bool:
    paths = [p for p in git(wt, "diff", "--name-only", base, tip).splitlines() if p]
    return any(p.startswith("engine/") and not p.startswith(NOT_SIM) for p in paths)


# ---------------------------------------------------------------- checking

def run_checks(wt: str, base_sha: str, log: list[str]) -> bool:
    """The landing checks on wt's HEAD (placed on base_sha). Appends what ran to log; True when every check passed."""
    env = dict(os.environ, ALAMO_RECORD_BUILD="1", ALAMO_RERUN="1", ALAMO_WC_SLOT=str(next_wc_slot(wt)))
    py = os.path.join(wt, "toolchain", "venv", "Scripts", "python.exe")
    py = py if os.path.isfile(py) else "python"
    head = git(wt, "rev-parse", "HEAD").strip()
    steps = [("msvc fast tier", ["cmd", "/d", "/c", os.path.join(wt, "build_agent.cmd"), "msvc", TREE, "all"])]
    if touches_sim(wt, base_sha, head):
        steps.append(("clang fast tier", ["cmd", "/d", "/c", os.path.join(wt, "build_agent.cmd"), "clang", TREE, "all"]))
    steps.append(("test tier budget (report only)", [py, "-I", os.path.join(wt, "tools", "lint", "test_tiers.py"), "check",
                                                     "--landing", os.path.join(wt, f"build-{TREE}-msvc", "ctest_junit.xml")]))
    steps.append(("slow golden checks", [py, "-I", os.path.join(wt, "tools", "lint", "golden_merge.py"), "slow", base_sha, "HEAD"]))
    steps.append(("behaviour check", [py, "-I", os.path.join(wt, "tools", "goldens", "behaviour_check.py"), "range",
                                      base_sha, "HEAD", "--build", os.path.join(wt, f"build-{TREE}-msvc"),
                                      "--log", "auto"]))
    for name, cmd in steps:
        t0 = time.time()
        r = subprocess.run(cmd, cwd=wt, env=env, capture_output=True, text=True, errors="replace")
        tail = "\n".join((r.stdout + r.stderr).strip().splitlines()[-25:])
        log.append(f"== {name}: exit {r.returncode} ({time.time() - t0:.0f} s)\n{tail}")
        if r.returncode != 0 and "report only" not in name:
            return False
    return True


def next_wc_slot(wt: str) -> int:
    """The worker-count slot of this check (wr3 t04): 0, 1, 2, 3, 0, ... per check run, kept in the verifier worktree,
    so each count of a four-count group runs at least once in four landings (the nightly run checks them all)."""
    path = os.path.join(wt, ".verifier_wc_slot")
    try:
        with open(path, encoding="utf-8") as f:
            slot = int(f.read().strip() or 0)
    except (OSError, ValueError):
        slot = 0
    try:
        with open(path, "w", encoding="utf-8") as f:
            f.write(str((slot + 1) % 4))
    except OSError:
        pass
    return slot


def first_bad(n: int, passes: Callable[[int], bool]) -> int:
    """Index of the first bad entry of a failing batch of n, given passes(k) = the first k entries pass together
    (passes(0) is True, passes(n) is False). Prefix bisection: about log2(n) checks."""
    lo, hi = 0, n  # passes(lo) True, passes(hi) False
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if passes(mid):
            lo = mid
        else:
            hi = mid
    return hi - 1


# ---------------------------------------------------------------- the loop

class Verifier:
    def __init__(self, queue: lq.Queue, wt: str, checks: Callable[[str, str, list[str]], bool] = run_checks,
                 push: Callable[[str], bool] | None = None, base: str = "origin/dev"):
        self.q, self.wt, self.checks, self.base = queue, wt, checks, base
        self.push = push or (lambda w: subprocess.run(["git", "-C", w, "push", "-q", "origin", "HEAD:refs/heads/dev"]).returncode == 0)

    def _summary(self, e: dict, text: str) -> str:
        p = os.path.join(self.q.folder, e["id"] + ".summary.txt")
        with open(p, "w", encoding="utf-8") as f:
            f.write(text + "\n")
        return p

    def _try(self, entries: list[dict], log: list[str]) -> tuple[bool, str]:
        tip, conflicts = place(self.wt, entries, self.base)
        if conflicts:
            return False, ""
        base_sha = git(self.wt, "rev-parse", self.base).strip()
        return self.checks(self.wt, base_sha, log), tip

    def step(self, batch: bool = True) -> int:
        """Handle the waiting entries once. Returns how many entries were decided."""
        subprocess.run(["git", "-C", self.wt, "fetch", "-q", "origin"], capture_output=True)
        waiting = self.q.next_waiting()
        if not waiting:
            return 0
        todo = waiting if batch else waiting[:1]
        for e in todo:
            self.q.set_state(e["id"], "building")
        # entries that do not apply on dev's tip are rejected first
        _tip, conflicts = place(self.wt, todo, self.base)
        for e, msg in conflicts:
            self.q.set_state(e["id"], "rejected", summary=self._summary(e, f"conflict placing {e['tip'][:8]} on {self.base}:\n{msg}"))
        todo = [e for e in todo if e not in [c[0] for c in conflicts]]
        decided = len(conflicts)
        while todo:
            log: list[str] = []
            ok, tip = self._try(todo, log)
            if ok:
                if not self.push(self.wt):  # dev moved since the fetch: place again next step
                    for e in todo:
                        self.q.set_state(e["id"], "waiting", note_push="dev moved; placed again")
                    return decided
                for e in todo:
                    self.q.set_state(e["id"], "landed", landed_as=tip, summary=self._summary(e, "\n".join(log) or "landed"))
                return decided + len(todo)
            if len(todo) == 1:
                bad = 0
                bad_log = log
            else:
                logs: dict[int, list[str]] = {}

                def passes(k: int) -> bool:
                    if k == 0:
                        return True
                    lg: list[str] = []
                    good, _ = self._try(todo[:k], lg)
                    logs[k] = lg
                    return good
                bad = first_bad(len(todo), passes)
                bad_log = logs.get(bad + 1, log)
                if bad > 0:  # the good prefix lands now
                    good, tip = self._try(todo[:bad], [])
                    if good and self.push(self.wt):
                        for e in todo[:bad]:
                            self.q.set_state(e["id"], "landed", landed_as=tip, summary=self._summary(e, "landed in a batch"))
                        decided += bad
            e = todo[bad]
            self.q.set_state(e["id"], "rejected", summary=self._summary(e, "\n".join(bad_log)))
            decided += 1
            todo = todo[bad + 1:]
            if todo:
                subprocess.run(["git", "-C", self.wt, "fetch", "-q", "origin"], capture_output=True)
        return decided

    def run(self, once: bool, batch: bool, poll: int) -> None:
        n = self.q.recover()
        if n:
            print(f"verifier: {n} entr{'y' if n == 1 else 'ies'} left building by a stopped run are waiting again", flush=True)
        while True:
            d = self.step(batch)
            if d:
                print(f"verifier: decided {d} entr{'y' if d == 1 else 'ies'}", flush=True)
            elif once:
                return
            if once and not self.q.next_waiting():
                return
            if not d:
                time.sleep(poll)


def ensure_worktree(main: str) -> str:
    wt = os.path.join(main, *WT_NAME.split("/"))
    if not os.path.exists(os.path.join(wt, ".git")):
        git(main, "worktree", "add", "--detach", wt, "origin/dev")
        git(main, "worktree", "lock", "--reason", "the verifier (tools/gate/verifier.py): no writer uses it", wt)
    for d in ("Data", "extracted", "toolchain", "roadmap-store"):
        src, dst = os.path.join(main, d), os.path.join(wt, d)
        if os.path.exists(src) and not os.path.exists(dst):
            subprocess.run(["cmd", "/c", "mklink", "/J", dst, src], capture_output=True)
    return wt


# ---------------------------------------------------------------- selftest

def selftest() -> int:
    bad = []

    def check(c, what):
        if not c:
            bad.append(what)

    # prefix bisection: bad entry first, middle, last in a batch of five
    for b_idx in (0, 2, 4):
        calls = []

        def passes(k, b_idx=b_idx):
            calls.append(k)
            return k <= b_idx
        got = first_bad(5, passes)
        check(got == b_idx, f"first_bad finds entry {b_idx} (got {got})")
        check(len(calls) <= 3, f"first_bad uses at most 3 checks for 5 entries ({calls})")

    with tempfile.TemporaryDirectory() as tmp:
        envg = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@t", GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@t")
        os.environ.update({k: envg[k] for k in ("GIT_AUTHOR_NAME", "GIT_AUTHOR_EMAIL", "GIT_COMMITTER_NAME", "GIT_COMMITTER_EMAIL")})
        origin, main, vwt = (os.path.join(tmp, n) for n in ("origin", "main", "vwt"))

        def g(repo, *a):
            return subprocess.run(["git", "-C", repo, *a], check=True, capture_output=True, text=True, env=envg).stdout

        def commit(repo, name, text, msg=None):
            with open(os.path.join(repo, name), "w") as f:
                f.write(text)
            g(repo, "add", "-A")
            g(repo, "commit", "-q", "-m", msg or name)
            return g(repo, "rev-parse", "HEAD").strip()

        os.makedirs(origin)
        g(origin, "init", "-q", "--bare", "-b", "dev")
        g(tmp, "clone", "-q", origin, main)
        g(main, "checkout", "-q", "-b", "dev")
        commit(main, "base.txt", "1")
        g(main, "push", "-q", "origin", "dev")
        g(main, "fetch", "-q")
        g(main, "worktree", "add", "-q", "--detach", vwt, "origin/dev")
        q = lq.Queue(main, os.path.join(tmp, "queue"))

        def submit(branch, files):
            g(main, "checkout", "-q", "-B", branch, "origin/dev")
            for name, text in files:
                commit(main, name, text)
            return q.submit(main, note=branch)

        # checks: a file named BAD* breaks the build; pushes go to the bare origin
        def checks(wt, base_sha, log):
            names = g(wt, "diff", "--name-only", base_sha, "HEAD").split()
            log.append("files: " + " ".join(names))
            return not any(n.startswith("BAD") for n in names)

        v = Verifier(q, vwt, checks)
        e_ok = submit("ok1", [("x1.txt", "1")])
        check(v.step() == 1 and q.get(e_ok["id"])["state"] == "landed", "a good entry lands")
        check(g(origin, "rev-parse", "dev").strip() == q.get(e_ok["id"])["landed_as"], "dev moved to the landed tip")

        g(main, "fetch", "-q")
        e_bad = submit("bad", [("BAD.txt", "1")])
        dev_before = g(origin, "rev-parse", "dev").strip()
        check(v.step() == 1 and q.get(e_bad["id"])["state"] == "rejected", "a breaking entry is rejected")
        check(g(origin, "rev-parse", "dev").strip() == dev_before, "dev does not move on a rejection")
        check("BAD.txt" in open(q.get(e_bad["id"])["summary"], encoding="utf-8").read(), "the summary names what failed")

        # conflict: two entries edit the same line differently; the second does not apply
        g(main, "fetch", "-q")
        e_c1 = submit("c1", [("same.txt", "one")])
        e_c2 = submit("c2", [("same.txt", "two")])
        v.step()
        check(q.get(e_c1["id"])["state"] == "landed" and q.get(e_c2["id"])["state"] == "rejected", "a conflicting entry is rejected")
        check("conflict" in open(q.get(e_c2["id"])["summary"], encoding="utf-8").read(), "the conflict is named")

        # batch of five with the bad one in the middle: four land, one rejected
        g(main, "fetch", "-q")
        ids = [submit(f"b{i}", [(("BADb.txt" if i == 2 else f"b{i}.txt"), str(i))])["id"] for i in range(5)]
        decided = v.step()
        states = [q.get(i)["state"] for i in ids]
        check(decided == 5 and states == ["landed", "landed", "rejected", "landed", "landed"], f"batch bisection: {states}")
        names = g(origin, "ls-tree", "--name-only", "dev").split()
        check("BADb.txt" not in names and all(f"b{i}.txt" in names for i in (0, 1, 3, 4)), "dev holds the good entries only")

        # dev moved under the verifier: push rejected, entry waits again
        g(main, "fetch", "-q")
        e_m = submit("moved", [("m.txt", "1")])
        v_no_push = Verifier(q, vwt, checks, push=lambda w: False)
        v_no_push.step()
        check(q.get(e_m["id"])["state"] == "waiting", "a rejected push leaves the entry waiting")

    for b in bad:
        print("verifier selftest FAIL: " + b)
    if bad:
        return 1
    print("verifier selftest: ok")
    return 0


def main(argv: list[str]) -> int:
    import argparse
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("command", nargs="?", choices=("run",))
    ap.add_argument("--once", action="store_true", help="handle what is waiting, then stop")
    ap.add_argument("--no-batch", action="store_true", help="one entry per check")
    ap.add_argument("--poll", type=int, default=30)
    ap.add_argument("--main", help="the main checkout (default: this file's repository)")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest()
    if a.command != "run":
        ap.print_help()
        return 2
    main_co = a.main or lq.main_checkout(lq.ROOT)
    v = Verifier(lq.Queue(main_co), ensure_worktree(main_co))
    v.run(a.once, not a.no_batch, a.poll)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
