#!/usr/bin/env python3
"""Landing queue (wr2 t01, docs/DECISIONS.md "Workflow redesign": only the verifier pushes dev).

A writer hands its work to the queue with one command and goes on working; the verifier (tools/gate/verifier.py) takes
entries one at a time (or a batch, t03), places them on dev's tip, builds and checks them, and fast-forwards dev.

An entry is the commits `base..tip` of the submitting checkout's branch, where base is the merge base with origin/dev at
submit time. The queue lives in the main checkout: `.cache/land-queue/<id>.json` per entry (ordered by id, which is the
submit time plus a counter) and the ref `refs/land-queue/<id>` pinning the tip, so the commits survive a branch reset or
a deleted worktree. State: waiting -> building -> landed | rejected (with the summary path) | withdrawn.

  python -I tools/gate/land_queue.py submit [REV] [--note TEXT]     queue REV (default HEAD) of this checkout
  python -I tools/gate/land_queue.py status [ID]                    the queue, newest last (or one entry in full)
  python -I tools/gate/land_queue.py withdraw ID                    your own waiting entry
  python -I tools/gate/land_queue.py audit SINCE                    every commit SINCE..origin/dev belongs to a landed entry
  python -I tools/gate/land_queue.py --selftest

`audit` walks the landed entries in landing order and counts the first-parent commits each landing added to origin/dev:
more commits than its entries hold (fewer is normal: a rebase drops commits already on dev) means something reached dev without the verifier (exit 1, the range is named). It also
prints how long each landing took from the start of its check. The wr2 exit test (seven days of verifier-only dev) is
`audit <dev commit of the first day>`.

`submit` refuses uncommitted tracked edits and a tip already on origin/dev, and prints the entry id and where the result
will appear. Exit 0 on success. Standard library only.
"""

from __future__ import annotations

import json
import os
import socket
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
STATES = ("waiting", "building", "landed", "rejected", "withdrawn")


def git(repo: str, *args: str, check: bool = True) -> str:
    r = subprocess.run(["git", "-C", repo, *args], capture_output=True, text=True, encoding="utf-8", errors="replace")
    if check and r.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)}: {r.stderr.strip()}")
    return r.stdout


def main_checkout(repo: str) -> str:
    return os.path.dirname(git(repo, "rev-parse", "--path-format=absolute", "--git-common-dir").strip())


class Queue:
    def __init__(self, main: str, folder: str | None = None):
        self.main = main
        self.folder = folder or os.environ.get("ALAMO_LAND_QUEUE") or os.path.join(main, ".cache", "land-queue")
        os.makedirs(self.folder, exist_ok=True)

    # ------------------------------------------------------------ storage
    def _path(self, eid: str) -> str:
        return os.path.join(self.folder, eid + ".json")

    def _write(self, entry: dict) -> None:
        tmp = self._path(entry["id"]) + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(entry, f, indent=1, sort_keys=True)
        os.replace(tmp, self._path(entry["id"]))

    def get(self, eid: str) -> dict:
        with open(self._path(eid), encoding="utf-8") as f:
            return json.load(f)

    def entries(self, states: tuple[str, ...] | None = None) -> list[dict]:
        out = []
        for n in sorted(os.listdir(self.folder)):
            if n.endswith(".json"):
                e = self.get(n[:-5])
                if states is None or e["state"] in states:
                    out.append(e)
        return out

    def _new_id(self) -> str:
        stamp = time.strftime("%Y%m%d-%H%M%S")
        n = 0
        while True:
            eid = f"{stamp}-{n:02d}"
            try:
                fd = os.open(self._path(eid), os.O_CREAT | os.O_EXCL | os.O_WRONLY)  # claim the id atomically
                os.close(fd)
                return eid
            except FileExistsError:
                n += 1

    # ------------------------------------------------------------ operations
    def submit(self, checkout: str, rev: str = "HEAD", note: str = "", base_ref: str = "origin/dev") -> dict:
        if [l for l in git(checkout, "status", "--porcelain", "--untracked-files=no").splitlines() if l.strip()]:
            raise RuntimeError("uncommitted tracked edits: commit them first")
        tip = git(checkout, "rev-parse", rev).strip()
        base = git(checkout, "merge-base", tip, base_ref).strip()
        if base == tip:
            raise RuntimeError(f"{tip[:8]} is already on {base_ref}: nothing to land")
        commits = [c for c in git(checkout, "rev-list", "--reverse", f"{base}..{tip}").split() if c]
        eid = self._new_id()
        git(self.main, "update-ref", f"refs/land-queue/{eid}", tip)
        branch = git(checkout, "rev-parse", "--abbrev-ref", "HEAD").strip()
        entry = {"id": eid, "state": "waiting", "tip": tip, "base": base, "commits": commits, "branch": branch,
                 "checkout": os.path.abspath(checkout), "submitter": f"{os.environ.get('USERNAME', '?')}@{socket.gethostname()}",
                 "note": note, "submitted": time.strftime("%Y-%m-%dT%H:%M:%S"), "history": []}
        self._write(entry)
        return entry

    def set_state(self, eid: str, state: str, **fields) -> dict:
        assert state in STATES, state
        e = self.get(eid)
        e["history"].append({"at": time.strftime("%Y-%m-%dT%H:%M:%S"), "from": e["state"], "to": state})
        e["state"] = state
        e.update(fields)
        self._write(e)
        if state in ("landed", "rejected", "withdrawn"):
            git(self.main, "update-ref", "-d", f"refs/land-queue/{eid}", check=False)
        return e

    def withdraw(self, eid: str, checkout: str) -> dict:
        e = self.get(eid)
        if e["state"] != "waiting":
            raise RuntimeError(f"{eid} is {e['state']}: only a waiting entry can be withdrawn")
        if os.path.normcase(e["checkout"]) != os.path.normcase(os.path.abspath(checkout)):
            raise RuntimeError(f"{eid} was submitted from {e['checkout']}: withdraw it from there")
        return self.set_state(eid, "withdrawn")

    def next_waiting(self) -> list[dict]:
        """Waiting entries in queue order (the verifier takes the first, or all of them as a batch)."""
        return self.entries(("waiting",))

    def recover(self) -> int:
        """After a verifier restart: entries left in `building` go back to `waiting` (their run did not finish)."""
        n = 0
        for e in self.entries(("building",)):
            self.set_state(e["id"], "waiting", recovered=True)
            n += 1
        return n


def landed_at(e: dict) -> str:
    return next((h["at"] for h in reversed(e.get("history", [])) if h["to"] == "landed"), "")


def audit_ranges(groups: list[tuple[str, int]], count) -> list[tuple[str, str, int, int]]:
    """groups: (landed_as, commits the entries of that landing hold) in landing order, starting after `since`;
    count(a, b) = first-parent commits in a..b. Returns the (from, to, found, expected) of each landing whose range holds
    more commits than its entries (fewer is normal: the rebase drops a commit that is already on dev)."""
    out = []
    prev = None
    for tip, expected in groups:
        found = count(prev, tip)
        if found > expected:
            out.append((prev or "SINCE", tip, found, expected))
        prev = tip
    return out


def audit(q: "Queue", repo: str, since: str, dev: str = "origin/dev") -> int:
    def is_anc(a: str, b: str) -> bool:
        return subprocess.run(["git", "-C", repo, "merge-base", "--is-ancestor", a, b], capture_output=True).returncode == 0

    def count(a: str | None, b: str) -> int:
        return int(git(repo, "rev-list", "--first-parent", "--count", f"{a or since}..{b}").strip())

    since_sha, dev_sha = git(repo, "rev-parse", since).strip(), git(repo, "rev-parse", dev).strip()
    by_tip: dict[str, list[dict]] = {}
    for e in sorted(q.entries(("landed",)), key=landed_at):
        tip = e.get("landed_as", "")
        if len(tip) == 40 and is_anc(since_sha, tip) and tip != since_sha and is_anc(tip, dev_sha):
            by_tip.setdefault(tip, []).append(e)
    groups = [(tip, sum(len(e["commits"]) for e in es)) for tip, es in by_tip.items()]
    bad = audit_ranges(groups, count)
    last = groups[-1][0] if groups else since_sha
    extra = count(last, dev_sha) if last != dev_sha else 0
    for tip, es in by_tip.items():
        start = next((h["at"] for h in es[0]["history"] if h["to"] == "building"), "")
        took = ""
        if start and landed_at(es[0]):
            took = f"  check {int(time.mktime(time.strptime(landed_at(es[0]), '%Y-%m-%dT%H:%M:%S')) - time.mktime(time.strptime(start, '%Y-%m-%dT%H:%M:%S')))} s"
        print(f"{landed_at(es[0])}  {tip[:8]}  {len(es)} entr{'y' if len(es) == 1 else 'ies'}, {sum(len(e['commits']) for e in es)} commit(s){took}  {', '.join(e['id'] for e in es)}")
    for a, b, found, expected in bad:
        print(f"land_queue audit: {a[:8]}..{b[:8]} holds {found} commit(s), its landing {expected}")
    if extra:
        print(f"land_queue audit: {extra} commit(s) on {dev} after the last landing {last[:8]}")
    total = count(None, dev_sha)
    ok = not bad and not extra
    print(f"land_queue audit {since_sha[:8]}..{dev_sha[:8]}: {total} commit(s), {len(groups)} landing(s): "
          + ("every commit was landed by the verifier" if ok else "NOT every commit came through the queue"))
    return 0 if ok else 1


def fmt(e: dict) -> str:
    extra = e.get("summary") or e.get("landed_as") or ""
    return (f"{e['id']}  {e['state']:<9} {e['tip'][:8]} ({len(e['commits'])} commit{'s' if len(e['commits']) != 1 else ''})"
            f"  {e['branch']}  {e.get('note', '')}" + (f"  -> {extra}" if extra else ""))


# ---------------------------------------------------------------- selftest

def selftest() -> int:
    bad = []

    def check(c, what):
        if not c:
            bad.append(what)

    with tempfile.TemporaryDirectory() as tmp:
        envg = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@t", GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@t")
        origin, main, wt2 = (os.path.join(tmp, n) for n in ("origin", "main", "wt2"))

        def g(repo, *a):
            return subprocess.run(["git", "-C", repo, *a], check=True, capture_output=True, text=True, env=envg).stdout

        def commit(repo, name, text):
            with open(os.path.join(repo, name), "w") as f:
                f.write(text)
            g(repo, "add", "-A")
            g(repo, "commit", "-q", "-m", name)

        os.makedirs(origin)
        g(origin, "init", "-q", "--bare", "-b", "dev")
        g(tmp, "clone", "-q", origin, main)
        g(main, "checkout", "-q", "-b", "dev")
        commit(main, "a.txt", "1")
        g(main, "push", "-q", "origin", "dev")
        g(main, "fetch", "-q")
        g(main, "worktree", "add", "-q", "-b", "w2", wt2, "origin/dev")
        g(main, "checkout", "-q", "-b", "w1", "origin/dev")
        commit(main, "b.txt", "1")
        commit(main, "c.txt", "1")
        commit(wt2, "d.txt", "1")

        q = Queue(main, os.path.join(tmp, "queue"))
        e1 = q.submit(main, note="first")
        e2 = q.submit(wt2, note="second")
        check([e["id"] for e in q.next_waiting()] == [e1["id"], e2["id"]], "entries keep submit order")
        check(len(e1["commits"]) == 2 and len(e2["commits"]) == 1, "an entry holds its series")
        check(g(main, "rev-parse", f"refs/land-queue/{e1['id']}").strip() == e1["tip"], "the tip is pinned by a ref")

        # a branch reset does not lose a queued series
        g(main, "reset", "-q", "--hard", "origin/dev")
        check(g(main, "cat-file", "-t", e1["tip"]).strip() == "commit", "pinned commits survive a reset")

        # restart: a fresh Queue object over the same folder sees the same state; building goes back to waiting
        q.set_state(e1["id"], "building")
        q2 = Queue(main, os.path.join(tmp, "queue"))
        check(q2.recover() == 1 and q2.get(e1["id"])["state"] == "waiting", "restart puts a building entry back")

        # withdraw: only your own, only while waiting
        try:
            q2.withdraw(e2["id"], main)
            check(False, "withdrawing another checkout's entry is refused")
        except RuntimeError:
            pass
        check(q2.withdraw(e2["id"], wt2)["state"] == "withdrawn", "withdraw your own waiting entry")
        try:
            g(main, "rev-parse", "--verify", "-q", f"refs/land-queue/{e2['id']}")
            check(False, "a withdrawn entry drops its pin")
        except subprocess.CalledProcessError:
            pass
        q2.set_state(e1["id"], "landed", landed_as="abc")
        check(q2.next_waiting() == [], "landed and withdrawn entries leave the waiting list")

        # refusals
        try:
            q2.submit(wt2, "origin/dev")
            check(False, "a tip already on origin/dev is refused")
        except RuntimeError:
            pass
        with open(os.path.join(wt2, "d.txt"), "w") as f:
            f.write("dirty")
        try:
            q2.submit(wt2)
            check(False, "uncommitted tracked edits are refused")
        except RuntimeError:
            pass

    # audit: each landing's range holds exactly its entries' commits; a direct push shows as extra commits
    dist = {(None, "a"): 2, ("a", "b"): 1, ("b", "c"): 3}
    check(audit_ranges([("a", 2), ("b", 1), ("c", 3)], lambda x, y: dist[(x, y)]) == [], "audit: clean history")
    check(audit_ranges([("a", 3), ("b", 1), ("c", 3)], lambda x, y: dist[(x, y)]) == [], "audit: a commit dropped by the rebase is fine")
    check(audit_ranges([("a", 2), ("b", 1), ("c", 2)], lambda x, y: dist[(x, y)]) == [("b", "c", 3, 2)],
          "audit: a commit pushed past the verifier is found in its range")

    for b in bad:
        print("land_queue selftest FAIL: " + b)
    if bad:
        return 1
    print("land_queue selftest: ok")
    return 0


def main(argv: list[str]) -> int:
    import argparse
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("command", nargs="?", choices=("submit", "status", "withdraw", "audit"))
    ap.add_argument("arg", nargs="?")
    ap.add_argument("--note", default="")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest()
    q = Queue(main_checkout(ROOT))
    try:
        if a.command == "submit":
            e = q.submit(ROOT, a.arg or "HEAD", a.note)
            print(f"queued {e['id']}: {len(e['commits'])} commit(s) of {e['branch']} at {e['tip'][:8]}")
            print(f"result: python -I tools/gate/land_queue.py status {e['id']}  (summary file named there when done)")
            return 0
        if a.command == "audit":
            if not a.arg:
                raise RuntimeError("audit needs the dev commit to start from")
            git(q.main, "fetch", "-q", "origin", check=False)
            return audit(q, q.main, a.arg)
        if a.command == "withdraw":
            print(fmt(q.withdraw(a.arg, ROOT)))
            return 0
        if a.arg:
            print(json.dumps(q.get(a.arg), indent=1))
            return 0
        for e in q.entries():
            print(fmt(e))
        return 0
    except RuntimeError as err:
        print(f"land_queue: {err}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
