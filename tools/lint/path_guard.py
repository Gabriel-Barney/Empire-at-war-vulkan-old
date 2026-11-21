#!/usr/bin/env python3
"""Path guard for the two writers (wr4 t02): a PreToolUse hook on Edit / Write / MultiEdit / NotebookEdit.

Each fixed checkout has a role file `.alamo_writer` in its root (`sim`, `non-sim`; made by tools/gate/writer_setup.py):
  - a session whose checkout is the non-sim writer may not edit sim code: engine/<module>/... for every module except
    render, audio, video and ui (the sim definition of CLAUDE.md); the block names the sim checkout;
  - no session may edit inside the verifier's checkout (.claude/worktrees/verifier), which only the verifier changes.
Everything else passes, reads are never hooked, and anything unexpected passes too (a guard must not break editing).
A blocked edit exits 2 with the reason on stderr, which Claude Code shows to the agent instead of running the tool.

  path_guard.py --hook        hook mode (JSON on stdin: cwd, tool_input.file_path / notebook_path)
  path_guard.py --check <session dir> <file>   print the decision for one case
  path_guard.py --selftest
"""

from __future__ import annotations

import json
import os
import sys

ROLE_FILE = ".alamo_writer"
NON_SIM = ("render", "audio", "video", "ui")


def norm(p: str) -> str:
    return os.path.normcase(os.path.normpath(os.path.abspath(p)))


def checkout_of(path: str) -> str | None:
    """The checkout root that holds path (the nearest folder with a .git entry), or None."""
    d = os.path.abspath(path if os.path.isdir(path) else os.path.dirname(path))
    while True:
        if os.path.exists(os.path.join(d, ".git")):
            return d
        parent = os.path.dirname(d)
        if parent == d:
            return None
        d = parent


def main_checkout(checkout: str) -> str:
    """The main checkout: a worktree's .git is a file pointing into <main>/.git/worktrees/<name>."""
    g = os.path.join(checkout, ".git")
    if os.path.isfile(g):
        try:
            text = open(g, encoding="utf-8").read().strip()
        except OSError:
            return checkout
        if text.startswith("gitdir:"):
            gitdir = os.path.normpath(os.path.join(checkout, text[7:].strip()))
            marker = os.sep + "worktrees" + os.sep
            if marker in gitdir:
                return os.path.dirname(os.path.dirname(gitdir.split(marker)[0] + os.sep + "x"))
    return checkout


def role_of(checkout: str | None) -> str:
    if not checkout:
        return ""
    try:
        return open(os.path.join(checkout, ROLE_FILE), encoding="utf-8").read().strip()
    except OSError:
        return ""


def is_sim_path(rel: str) -> bool:
    parts = rel.replace("\\", "/").split("/")
    return len(parts) >= 3 and parts[0] == "engine" and parts[1] not in NON_SIM


def decide(session_dir: str, target: str) -> str | None:
    """None when the edit may go ahead, else the reason it is blocked."""
    target = os.path.abspath(target)
    session = checkout_of(session_dir)
    tgt_co = checkout_of(target)
    main = main_checkout(session or tgt_co or session_dir)
    verifier = os.path.join(main, ".claude", "worktrees", "verifier")
    if norm(target).startswith(norm(verifier) + os.sep):
        return (f"{target} is in the verifier's checkout ({verifier}); only tools/gate/verifier.py changes it. Edit your own "
                "checkout and land it with tools\\land_on_dev.cmd.")
    if role_of(session) == "non-sim" and tgt_co:
        rel = os.path.relpath(target, tgt_co)
        if is_sim_path(rel):
            sim = os.path.join(main, ".claude", "worktrees", "writer-sim")
            return (f"{rel.replace(os.sep, '/')} is sim code (engine modules other than render, audio, video, ui) and this "
                    f"session is the non-sim writer ({session}). Sim code belongs to the sim writer's checkout ({sim}): ask "
                    "there, or file the change as a task for a sim group.")
    return None


def hook() -> int:
    try:
        data = json.loads(sys.stdin.read() or "{}")
        ti = data.get("tool_input") or {}
        target = ti.get("file_path") or ti.get("notebook_path") or ""
        if not target:
            return 0
        reason = decide(data.get("cwd") or os.getcwd(), target)
    except Exception:  # never get in the way because of the guard itself
        return 0
    if reason:
        sys.stderr.write("path guard (wr4): " + reason + "\n")
        return 2
    return 0


def selftest() -> int:
    import tempfile
    bad = []

    def ok(c, what):
        if not c:
            bad.append(what)

    with tempfile.TemporaryDirectory() as tmp:
        main = os.path.join(tmp, "main")
        os.makedirs(os.path.join(main, ".git", "worktrees"))
        wts = {}
        for name, role in (("writer-sim", "sim"), ("writer-nonsim", "non-sim"), ("verifier", ""), ("other", "")):
            wt = os.path.join(main, ".claude", "worktrees", name)
            os.makedirs(os.path.join(wt, "engine", "tactical", "src"))
            os.makedirs(os.path.join(wt, "engine", "render", "src"))
            os.makedirs(os.path.join(main, ".git", "worktrees", name))
            with open(os.path.join(wt, ".git"), "w", encoding="utf-8") as f:
                f.write(f"gitdir: {os.path.join(main, '.git', 'worktrees', name)}\n")
            if role:
                with open(os.path.join(wt, ROLE_FILE), "w", encoding="utf-8") as f:
                    f.write(role + "\n")
            wts[name] = wt
        sim_file = lambda wt: os.path.join(wt, "engine", "tactical", "src", "a.cpp")  # noqa: E731
        render_file = lambda wt: os.path.join(wt, "engine", "render", "src", "r.cpp")  # noqa: E731
        ns = wts["writer-nonsim"]
        r = decide(ns, sim_file(ns))
        ok(r and "sim code" in r and "writer-sim" in r, f"non-sim session editing sim code is blocked, naming the owner: {r}")
        ok(decide(ns, render_file(ns)) is None, "non-sim session may edit render")
        ok(decide(ns, os.path.join(ns, "tools", "x.py")) is None, "non-sim session may edit tools")
        ok(decide(wts["writer-sim"], sim_file(wts["writer-sim"])) is None, "the sim session may edit sim code")
        ok(decide(wts["writer-sim"], render_file(wts["writer-sim"])) is None, "the sim session may edit render (cross-cutting)")
        for who in ("writer-sim", "writer-nonsim", "other"):
            r = decide(wts[who], sim_file(wts["verifier"]))
            ok(r and "verifier" in r, f"{who} editing the verifier's checkout is blocked: {r}")
        ok(decide(wts["other"], sim_file(wts["other"])) is None, "a checkout without a role is not guarded")
        ok(decide(ns, os.path.join(ns, "engine", "test_support", "t.hpp")) is not None, "shared test support is sim")
    for b in bad:
        print("path_guard selftest FAIL: " + b)
    if bad:
        return 1
    print("path_guard selftest: ok")
    return 0


def main(argv: list[str]) -> int:
    if "--selftest" in argv:
        return selftest()
    if "--hook" in argv:
        return hook()
    if argv[:1] == ["--check"] and len(argv) == 3:
        r = decide(argv[1], argv[2])
        print(r or "allowed")
        return 1 if r else 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
