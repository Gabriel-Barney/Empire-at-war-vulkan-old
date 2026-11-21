#!/usr/bin/env python3
"""The two fixed writer checkouts (wr4 t01): the sim writer and the non-sim writer.

  python -I tools/gate/writer_setup.py create   make any missing one: <main>/.claude/worktrees/writer-sim and writer-nonsim,
                                                each a worktree on its own scratch branch claude/writer-sim / claude/writer-nonsim
                                                from origin/dev, with the Data / extracted / toolchain / roadmap-store junctions
                                                (tools/new_worktree.cmd), the role file .alamo_writer the path guard reads
                                                (tools/lint/path_guard.py) and a git worktree lock, so the cleanup tools keep it
  python -I tools/gate/writer_setup.py warm      configure and build the warm MSVC agent tree of both (no tests; clang
                                                builds run nightly only), which the compile hook, clangd and the
                                                affected-test selector read
  python -I tools/gate/writer_setup.py status    each checkout: path, branch, role, lock, trees, commits not yet on dev
  python -I tools/gate/writer_setup.py sync <checkout>   after a landing: rebase the scratch branch onto origin/dev (commits
                                                already on dev drop out); refuses with uncommitted edits
  python -I tools/gate/writer_setup.py --selftest

A session opened in either folder gets the project's skills, scripts and settings from its own tracked .claude folder.
"""

from __future__ import annotations

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
WRITERS = (("writer-sim", "sim"), ("writer-nonsim", "non-sim"))
ROLE_FILE = ".alamo_writer"
sys.path.insert(0, os.path.join(ROOT, "tools", "lint"))
import build_trees  # noqa: E402


def git(repo: str, *args: str, check: bool = True) -> str:
    p = subprocess.run(["git", "-C", repo, *args], capture_output=True, text=True, errors="replace")
    if check and p.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)}: {p.stderr.strip()}")
    return p.stdout


def main_checkout() -> str:
    return os.path.dirname(git(ROOT, "rev-parse", "--path-format=absolute", "--git-common-dir").strip())


def path_of(main: str, name: str) -> str:
    return os.path.join(main, ".claude", "worktrees", name)


def create(main: str) -> int:
    for name, role in WRITERS:
        wt = path_of(main, name)
        if not os.path.isdir(wt):
            r = subprocess.run(["cmd", "/d", "/c", os.path.join(main, "tools", "new_worktree.cmd"), name, "--base", "origin/dev",
                                "--fetch", "--no-sync"], cwd=main)
            if r.returncode != 0 or not os.path.isdir(wt):
                print(f"writer_setup: could not create {wt}")
                return 1
        with open(os.path.join(wt, ROLE_FILE), "w", encoding="utf-8") as f:
            f.write(role + "\n")
        git(main, "worktree", "lock", "--reason", f"fixed {role} writer checkout (wr4 t01, tools/gate/writer_setup.py)", wt,
            check=False)
        print(f"{name}: {wt} ({role})")
    return 0


def warm(main: str) -> int:
    rc = 0
    for name, _ in WRITERS:
        wt = path_of(main, name)
        for comp in ("msvc",):  # clang builds run nightly only (docs/DECISIONS.md, "Clang runs nightly only")
            r = subprocess.run(["cmd", "/d", "/c", os.path.join(wt, "build_agent.cmd"), comp, "all", "none"], cwd=wt,
                               capture_output=True, text=True, errors="replace")
            tail = (r.stdout + r.stderr).strip().splitlines()[-1:] or [""]
            print(f"{name} {comp}: exit {r.returncode} {tail[0]}")
            rc |= r.returncode
    return 1 if rc else 0


def status(main: str) -> int:
    locked = git(main, "worktree", "list", "--porcelain")
    for name, role in WRITERS:
        wt = path_of(main, name)
        if not os.path.isdir(wt):
            print(f"{name}: missing (writer_setup.py create)")
            continue
        branch = git(wt, "branch", "--show-current", check=False).strip()
        try:
            have = open(os.path.join(wt, ROLE_FILE), encoding="utf-8").read().strip()
        except OSError:
            have = "(no role file)"
        is_locked = any(block.splitlines()[0].replace("\\", "/").endswith("/" + name) and "locked" in block
                        for block in locked.split("\n\n") if block.strip())
        trees = [t for t in (build_trees.agent_tree("msvc"), build_trees.agent_tree("clang"))
                 if os.path.isfile(os.path.join(wt, t, "build.ninja"))]
        ahead = git(wt, "rev-list", "--count", "origin/dev..HEAD", check=False).strip() or "?"
        print(f"{name}: {wt}  branch {branch}  role {have}{'' if have == role else ' (expected ' + role + ')'}  "
              f"{'locked' if is_locked else 'NOT LOCKED'}  trees {', '.join(trees) or 'none (writer_setup.py warm)'}  "
              f"{ahead} commit(s) not on origin/dev")
    return 0


def sync(checkout: str) -> int:
    if git(checkout, "status", "--porcelain", "--untracked-files=no").strip():
        print("writer_setup sync: uncommitted edits; commit or finish them first")
        return 1
    git(checkout, "fetch", "-q", "origin")
    p = subprocess.run(["git", "-C", checkout, "rebase", "origin/dev"], capture_output=True, text=True, errors="replace")
    if p.returncode != 0:
        subprocess.run(["git", "-C", checkout, "rebase", "--abort"], capture_output=True)
        print("writer_setup sync: the rebase onto origin/dev conflicts; left as it was:\n" + p.stdout + p.stderr)
        return 1
    left = git(checkout, "rev-list", "--count", "origin/dev..HEAD").strip()
    print(f"writer_setup sync: on origin/dev's tip, {left} commit(s) of this checkout not landed yet")
    return 0


def selftest() -> int:
    import tempfile
    bad = []
    with tempfile.TemporaryDirectory() as tmp:
        origin = os.path.join(tmp, "origin.git")
        wc = os.path.join(tmp, "wc")
        env = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@t", GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@t")

        def g(repo, *a):
            return subprocess.run(["git", "-C", repo, *a], capture_output=True, text=True, env=env, check=True).stdout

        subprocess.run(["git", "init", "-q", "--bare", "-b", "dev", origin], check=True)
        subprocess.run(["git", "clone", "-q", origin, wc], check=True, capture_output=True)
        g(wc, "checkout", "-q", "-b", "dev")
        open(os.path.join(wc, "a.txt"), "w").write("a\n")
        g(wc, "add", "a.txt"); g(wc, "commit", "-q", "-m", "a"); g(wc, "push", "-q", "origin", "dev")
        g(wc, "checkout", "-q", "-b", "claude/writer-sim")
        open(os.path.join(wc, "b.txt"), "w").write("b\n")
        g(wc, "add", "b.txt"); g(wc, "commit", "-q", "-m", "b")
        # the verifier lands b on dev as a different commit (rebased), then dev moves on
        other = os.path.join(tmp, "other")
        subprocess.run(["git", "clone", "-q", "-b", "dev", origin, other], check=True, capture_output=True)
        open(os.path.join(other, "b.txt"), "w").write("b\n")
        g(other, "add", "b.txt"); g(other, "commit", "-q", "-m", "b (landed)")
        open(os.path.join(other, "c.txt"), "w").write("c\n")
        g(other, "add", "c.txt"); g(other, "commit", "-q", "-m", "c"); g(other, "push", "-q", "origin", "dev")
        saved = os.environ.copy()
        os.environ.update(env)
        try:
            rc = sync(wc)
        finally:
            os.environ.clear()
            os.environ.update(saved)
        left = g(wc, "rev-list", "--count", "origin/dev..HEAD").strip()
        if rc != 0 or left != "0" or not os.path.isfile(os.path.join(wc, "c.txt")):
            bad.append(f"sync after a landing: rc {rc}, {left} commits left")
        open(os.path.join(wc, "a.txt"), "w").write("dirty\n")
        if sync(wc) == 0:
            bad.append("sync refuses uncommitted edits")
    for b in bad:
        print("writer_setup selftest FAIL: " + b)
    if bad:
        return 1
    print("writer_setup selftest: ok")
    return 0


def main(argv: list[str]) -> int:
    if "--selftest" in argv:
        return selftest()
    if argv[:1] == ["sync"] and len(argv) == 2:
        return sync(os.path.abspath(argv[1]))
    main_co = main_checkout()
    cmds = {"create": create, "warm": warm, "status": status}
    if argv[:1] and argv[0] in cmds:
        return cmds[argv[0]](main_co)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
