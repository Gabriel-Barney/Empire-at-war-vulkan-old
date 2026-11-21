#!/usr/bin/env python3
"""Scenario results computed from a build, cached outside git (wr1 t02, "Goldens as computed diffs").

A scenario is a golden-backed headless run: a ctest entry of a build tree that runs alamo_headless with `--golden <tracked
file>` (the derived goldens of docs/status/golden-inventory.md). Its result is the full CRC log the run writes with
`--crc-out` (ALAMO_CRC 1, every tick). Results are stored under the main checkout's `.cache/scenario-results/` (gitignored;
`ALAMO_SCENARIO_CACHE` overrides), keyed by

  - the build-relevant source of the commit (git tree ids of SOURCE_PATHS, so a docs or tools commit keeps the key),
  - the scenario definition (its command line with the build and source folders written as <build> / <src>),
  - the game-data identity (ALAMO_FOC_DATA / ALAMO_EAW_DATA: path, and size + mtime of every file under their Data folders),
  - the compiler (msvc or clang) and the worker count.

Asking again for the same key returns the stored log without running. `result_for` gives any commit's result, building it
in the locked worktree `.claude/worktrees/scenario-base` when no entry exists (the parent commit's result for t03's diff).
A result is stored only from a checkout whose SOURCE_PATHS have no uncommitted edits. The cache keeps at most
ALAMO_SCENARIO_CACHE_MB (default 4096) MB and drops the least recently used entries first.

  python -I tools/goldens/scenario_results.py list [--build DIR]
  python -I tools/goldens/scenario_results.py run NAME [--build DIR] [--compiler msvc] [--workers N]   this checkout's HEAD
  python -I tools/goldens/scenario_results.py result NAME --commit C [--compiler msvc] [--workers N]   any commit
  python -I tools/goldens/scenario_results.py prune | stats
  python -I tools/goldens/scenario_results.py --selftest

Each command prints the path of the stored CRC log (or the listing). Standard library only.
"""

from __future__ import annotations

import dataclasses
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from typing import Callable

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools", "lint"))
import build_trees  # noqa: E402  (tree names come from one place)

# What a scenario's result can depend on besides game data: engine sources and their build files, and the benchmark
# scenario definitions some runs read. Tools, docs and the roadmap are not in it, so commits touching only those keep
# every key (their results are the parent's).
SOURCE_PATHS = ("engine", "CMakeLists.txt", "tests/benchmark")
DATA_VARS = ("ALAMO_FOC_DATA", "ALAMO_EAW_DATA")
KEY_VERSION = 1
BASE_WORKTREE = ".claude/worktrees/scenario-base"
# Comparison and output options a scenario's ctest command carries; the definition is the command without them.
_OPT_WITH_VALUE = ("--golden", "--crc-out", "--profile-out", "--crc-checkpoint", "--crc-keep", "--workers")
_OPT_FLAG = ("--check-workers", "--profile")


# ---------------------------------------------------------------- small helpers

def git(repo: str, *args: str, check: bool = True) -> str:
    r = subprocess.run(["git", "-C", repo, *args], capture_output=True, text=True, encoding="utf-8", errors="replace")
    if check and r.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)}: {r.stderr.strip()}")
    return r.stdout


def main_checkout(repo: str) -> str:
    common = git(repo, "rev-parse", "--path-format=absolute", "--git-common-dir").strip()
    return os.path.dirname(common)


def norm(p: str) -> str:
    return os.path.normcase(os.path.abspath(p)).replace("\\", "/")


def find_ctest() -> str:
    exe = shutil.which("ctest")
    if exe:
        return exe
    for base in (os.path.join(ROOT, "toolchain"), os.path.join(main_checkout(ROOT), "toolchain")):
        for dirpath, _dirs, files in os.walk(base):
            if "ctest.exe" in files:
                return os.path.join(dirpath, "ctest.exe")
            if dirpath.count(os.sep) - base.count(os.sep) > 3:
                _dirs[:] = []
    raise RuntimeError("ctest not found (PATH or toolchain\\)")


# ---------------------------------------------------------------- scenarios

@dataclasses.dataclass(frozen=True)
class Scenario:
    name: str            # the golden file's stem, e.g. skirmish_ai_foc_golden
    golden: str          # repo-relative tracked golden it replaces
    test: str            # the ctest entry it came from
    argv: tuple          # definition: command without comparison options, <build> / <src> placeholders
    cwd: str             # working directory with placeholders
    workers: int | None  # the worker count the ctest entry used (None: the program default)

    def definition(self) -> dict:
        return {"argv": list(self.argv), "cwd": self.cwd}


def _placeholders(value: str, build_dir: str, src_dir: str) -> str:
    v = value.replace("\\", "/")
    folded = os.path.normcase(v).replace("\\", "/")
    for real, ph in ((norm(build_dir), "<build>"), (norm(src_dir), "<src>")):
        if folded.startswith(real):
            return ph + v[len(real):]
    return v


def _resolve(value: str, build_dir: str, src_dir: str) -> str:
    return value.replace("<build>", build_dir.replace("\\", "/")).replace("<src>", src_dir.replace("\\", "/"))


def split_command(cmd: list[str]) -> tuple[list[str], int | None]:
    out, workers, i = [], None, 0
    while i < len(cmd):
        a = cmd[i]
        if a in _OPT_WITH_VALUE:
            if a == "--workers" and i + 1 < len(cmd):
                workers = int(cmd[i + 1])
            i += 2
            continue
        if a in _OPT_FLAG:
            i += 1
            continue
        out.append(a)
        i += 1
    return out, workers


def scenarios_from_ctest(tests: list[dict], build_dir: str, src_dir: str, tracked: set[str]) -> list[Scenario]:
    """One scenario per tracked golden that an alamo_headless ctest entry passes to --golden (first entry by name)."""
    seen: dict[str, Scenario] = {}
    for t in sorted(tests, key=lambda t: t["name"]):
        cmd = t.get("command") or []
        if "--golden" not in cmd or not os.path.basename(cmd[0]).lower().startswith("alamo_headless"):
            continue
        g = norm(cmd[cmd.index("--golden") + 1])
        src = norm(src_dir)
        if not g.startswith(src + "/"):
            continue
        rel = g[len(src) + 1:]
        if rel not in tracked or rel in seen:
            continue
        cwd = next((p["value"] for p in t.get("properties", []) if p.get("name") == "WORKING_DIRECTORY"), build_dir)
        argv, workers = split_command(cmd)
        name = os.path.splitext(os.path.basename(rel))[0]
        seen[rel] = Scenario(name, rel, t["name"], tuple(_placeholders(a, build_dir, src_dir) for a in argv),
                             _placeholders(cwd, build_dir, src_dir), workers)
    return sorted(seen.values(), key=lambda s: s.name)


def discover(build_dir: str, src_dir: str = ROOT) -> list[Scenario]:
    r = subprocess.run([find_ctest(), "--test-dir", build_dir, "--show-only=json-v1"], capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"ctest --show-only failed in {build_dir}: {r.stderr.strip()}")
    tracked = set(git(src_dir, "ls-files").splitlines())
    return scenarios_from_ctest(json.loads(r.stdout).get("tests", []), build_dir, src_dir, tracked)


# ---------------------------------------------------------------- keys

def source_key(repo: str, commit: str) -> str:
    listing = git(repo, "ls-tree", commit, "--", *SOURCE_PATHS)
    return hashlib.sha256(listing.encode()).hexdigest()


def data_identity(env: dict) -> dict:
    out = {}
    for var in DATA_VARS:
        root = env.get(var)
        if not root or not os.path.isdir(root):
            out[var] = None
            continue
        h = hashlib.sha256()
        data = os.path.join(root, "Data")
        for dirpath, dirs, files in os.walk(data if os.path.isdir(data) else root):
            dirs.sort()
            for f in sorted(files):
                p = os.path.join(dirpath, f)
                try:
                    st = os.stat(p)
                except OSError:
                    continue
                h.update(f"{os.path.relpath(p, root)}|{st.st_size}|{st.st_mtime_ns}\n".encode("utf-8", "replace"))
        out[var] = {"path": norm(root), "files": h.hexdigest()}
    return out


def result_key(source: str, scenario: Scenario, data: dict, compiler: str, workers: int | None) -> str:
    doc = {"v": KEY_VERSION, "source": source, "scenario": scenario.definition(), "data": data, "compiler": compiler,
           "workers": scenario.workers if workers is None else workers}
    return hashlib.sha256(json.dumps(doc, sort_keys=True).encode()).hexdigest()


# ---------------------------------------------------------------- the store

class Cache:
    def __init__(self, folder: str, cap_bytes: int):
        self.folder = folder
        self.cap = cap_bytes

    @staticmethod
    def default(repo: str = ROOT) -> "Cache":
        folder = os.environ.get("ALAMO_SCENARIO_CACHE") or os.path.join(main_checkout(repo), ".cache", "scenario-results")
        return Cache(folder, int(os.environ.get("ALAMO_SCENARIO_CACHE_MB", "4096")) * 1024 * 1024)

    def _dir(self, key: str) -> str:
        return os.path.join(self.folder, key[:2], key)

    def get(self, key: str) -> str | None:
        """Path of the stored CRC log, or None. A hit marks the entry as used (eviction order)."""
        d = self._dir(key)
        path = os.path.join(d, "result.crc")
        if not os.path.isfile(path):
            return None
        try:
            os.utime(os.path.join(d, "meta.json"))
        except OSError:
            pass
        return path

    def meta(self, key: str) -> dict | None:
        try:
            with open(os.path.join(self._dir(key), "meta.json"), encoding="utf-8") as f:
                return json.load(f)
        except OSError:
            return None

    def put(self, key: str, crc_path: str, meta: dict) -> str:
        d = self._dir(key)
        os.makedirs(os.path.dirname(d), exist_ok=True)
        tmp = tempfile.mkdtemp(prefix=key[:8] + ".", dir=os.path.dirname(d))
        shutil.copyfile(crc_path, os.path.join(tmp, "result.crc"))
        with open(os.path.join(tmp, "meta.json"), "w", encoding="utf-8") as f:
            json.dump(dict(meta, key=key, stored=time.strftime("%Y-%m-%dT%H:%M:%S")), f, indent=1)
        if os.path.isdir(d):
            shutil.rmtree(d, ignore_errors=True)
        os.replace(tmp, d)
        self.evict()
        return os.path.join(d, "result.crc")

    def entries(self) -> list[tuple[float, int, str]]:
        """(last used, bytes, folder) per entry."""
        out = []
        if not os.path.isdir(self.folder):
            return out
        for shard in os.listdir(self.folder):
            sd = os.path.join(self.folder, shard)
            if not os.path.isdir(sd):
                continue
            for k in os.listdir(sd):
                d = os.path.join(sd, k)
                m = os.path.join(d, "meta.json")
                if not os.path.isfile(m):
                    continue
                size = sum(os.path.getsize(os.path.join(d, f)) for f in os.listdir(d))
                out.append((os.path.getmtime(m), size, d))
        return out

    def evict(self) -> int:
        """Drop least recently used entries until the cache fits its cap; returns how many were dropped."""
        ents = sorted(self.entries())
        total = sum(s for _, s, _ in ents)
        dropped = 0
        while ents and total > self.cap:
            _, size, d = ents.pop(0)
            shutil.rmtree(d, ignore_errors=True)
            total -= size
            dropped += 1
        return dropped


# ---------------------------------------------------------------- running

def source_dirty(checkout: str) -> list[str]:
    """Uncommitted edits (tracked or untracked) under SOURCE_PATHS of a checkout."""
    out = git(checkout, "status", "--porcelain", "--", *SOURCE_PATHS)
    return [line for line in out.splitlines() if line.strip()]


def test_env() -> dict:
    """The environment the build scripts give tests: game data defaults from tools/test_env.cmd when unset."""
    env = dict(os.environ)
    defaults = {
        "ALAMO_FOC_DATA": r"F:\SteamLibrary\steamapps\common\Star Wars Empire at War\corruption",
        "ALAMO_EAW_DATA": r"F:\SteamLibrary\steamapps\common\Star Wars Empire at War\GameData",
    }
    for k, v in defaults.items():
        if not env.get(k) and os.path.isdir(v):
            env[k] = v
    return env


def run_scenario(scenario: Scenario, build_dir: str, src_dir: str, out_path: str, workers: int | None = None,
                 env: dict | None = None) -> int:
    argv = [_resolve(a, build_dir, src_dir) for a in scenario.argv]
    w = scenario.workers if workers is None else workers
    if w is not None:
        argv += ["--workers", str(w)]
    argv += ["--crc-out", out_path]
    r = subprocess.run(argv, cwd=_resolve(scenario.cwd, build_dir, src_dir), env=env or test_env(),
                       capture_output=True, text=True, errors="replace")
    if not os.path.isfile(out_path):
        raise RuntimeError(f"{scenario.name}: no CRC log written (exit {r.returncode}): {(r.stderr or r.stdout)[-400:]}")
    return r.returncode


Runner = Callable[[Scenario, str, str, str, "int | None"], int]


def result_here(cache: Cache, scenario: Scenario, checkout: str, build_dir: str, compiler: str,
                workers: int | None = None, runner: Runner | None = None, env: dict | None = None) -> str:
    """This checkout's HEAD result (its build must be of HEAD): from the cache, else run and store (only when clean)."""
    env = env or test_env()
    head = git(checkout, "rev-parse", "HEAD").strip()
    key = result_key(source_key(checkout, head), scenario, data_identity(env), compiler, workers)
    # A checkout with uncommitted source edits built something no commit has: always run, never read or write the cache.
    dirty = source_dirty(checkout)
    hit = None if dirty else cache.get(key)
    if hit:
        return hit
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "result.crc")
        rc = runner(scenario, build_dir, checkout, out, workers) if runner else \
            run_scenario(scenario, build_dir, checkout, out, workers, env)
        if dirty:
            # Never cached: the build may contain edits no commit has. The caller still gets this run's log.
            keep = os.path.join(tempfile.gettempdir(), f"scenario_{scenario.name}_{os.getpid()}_uncached.crc")
            shutil.copyfile(out, keep)
            return keep
        return cache.put(key, out, {"commit": head, "scenario": scenario.name, "compiler": compiler,
                                    "workers": scenario.workers if workers is None else workers, "exit": rc})


def ensure_base_worktree(repo: str) -> str:
    main = main_checkout(repo)
    wt = os.path.join(main, *BASE_WORKTREE.split("/"))
    if not os.path.isdir(os.path.join(wt, ".git")) and not os.path.isfile(os.path.join(wt, ".git")):
        git(main, "worktree", "add", "--detach", wt, "HEAD")
        git(main, "worktree", "lock", "--reason", "scenario results of other commits (tools/goldens/scenario_results.py)", wt)
    for d in ("Data", "extracted", "toolchain", "roadmap-store"):
        src, dst = os.path.join(main, d), os.path.join(wt, d)
        if os.path.exists(src) and not os.path.exists(dst):
            subprocess.run(["cmd", "/c", "mklink", "/J", dst, src], capture_output=True)
    return wt


def result_for(cache: Cache, scenario: Scenario, commit: str, compiler: str = "msvc", workers: int | None = None,
               repo: str = ROOT, env: dict | None = None) -> str:
    """Any commit's result: from the cache, else built and run in the scenario-base worktree."""
    env = env or test_env()
    commit = git(repo, "rev-parse", commit).strip()
    key = result_key(source_key(repo, commit), scenario, data_identity(env), compiler, workers)
    hit = cache.get(key)
    if hit:
        return hit
    wt = ensure_base_worktree(repo)
    git(wt, "checkout", "-f", "--detach", commit)
    script = os.path.join(wt, "build_agent.cmd")
    r = subprocess.run(["cmd", "/d", "/c", script, compiler, "base", "all", "none"], cwd=wt, env=env)
    if r.returncode != 0:
        raise RuntimeError(f"building {commit[:8]} in {wt} failed (exit {r.returncode}); see {wt}\\logs\\agent_base_{compiler}.txt")
    build = os.path.join(wt, build_trees.agent_tree(compiler, "base"))
    return result_here(cache, scenario, wt, build, compiler, workers, env=env)


# ---------------------------------------------------------------- selftest

def selftest() -> int:
    bad: list[str] = []

    def check(cond: bool, what: str) -> None:
        if not cond:
            bad.append(what)

    with tempfile.TemporaryDirectory() as tmp:
        repo = os.path.join(tmp, "repo")
        os.makedirs(os.path.join(repo, "engine"))
        envg = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@t", GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@t")

        def g(*a: str) -> str:
            return subprocess.run(["git", "-C", repo, *a], check=True, capture_output=True, text=True, env=envg).stdout

        def write(rel: str, text: str) -> None:
            p = os.path.join(repo, rel)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "w", encoding="utf-8") as f:
                f.write(text)

        g("init", "-q", "-b", "dev")
        write("engine/a.cpp", "1")
        write("docs/x.md", "1")
        g("add", "-A"); g("commit", "-q", "-m", "c1")
        c1 = g("rev-parse", "HEAD").strip()
        write("docs/x.md", "2")
        g("add", "-A"); g("commit", "-q", "-m", "docs only")
        c2 = g("rev-parse", "HEAD").strip()
        write("engine/a.cpp", "2")
        g("add", "-A"); g("commit", "-q", "-m", "engine")
        c3 = g("rev-parse", "HEAD").strip()

        # keys
        check(source_key(repo, c1) == source_key(repo, c2), "a docs-only commit keeps the source key")
        check(source_key(repo, c2) != source_key(repo, c3), "an engine change moves the source key")
        build = os.path.join(tmp, "build")
        tests = [{"name": "alamo_headless_x", "command": [f"{build}/bin/alamo_headless.exe", "--skirmish-ai", "foc", "--workers",
                                                          "4", "--quiet", "--golden", f"{repo}/engine/t/x_golden.crc"],
                  "properties": [{"name": "WORKING_DIRECTORY", "value": f"{build}/engine/apps"}]},
                 {"name": "alamo_headless_x_w4", "command": [f"{build}/bin/alamo_headless.exe", "--golden", f"{build}/x_w0.crc"]},
                 {"name": "other", "command": [f"{build}/bin/other.exe", "--golden", f"{repo}/engine/t/x_golden.crc"]}]
        sc = scenarios_from_ctest(tests, build, repo, {"engine/t/x_golden.crc"})
        check(len(sc) == 1 and sc[0].name == "x_golden", f"discovery keeps only tracked alamo_headless goldens: {sc}")
        s = sc[0]
        check(s.argv == ("<build>/bin/alamo_headless.exe", "--skirmish-ai", "foc", "--quiet") and s.workers == 4
              and s.cwd == "<build>/engine/apps", f"definition strips options and uses placeholders: {s}")
        s2 = dataclasses.replace(s, argv=s.argv + ("--ticks", "100"))
        d0 = {"ALAMO_FOC_DATA": None, "ALAMO_EAW_DATA": None}
        d1 = {"ALAMO_FOC_DATA": {"path": "x", "files": "1"}, "ALAMO_EAW_DATA": None}
        k = result_key(source_key(repo, c3), s, d0, "msvc", None)
        check(k == result_key(source_key(repo, c3), s, d0, "msvc", None), "same inputs, same key")
        check(k != result_key(source_key(repo, c1), s, d0, "msvc", None), "changed source, new key")
        check(k != result_key(source_key(repo, c3), s2, d0, "msvc", None), "changed scenario, new key")
        check(k != result_key(source_key(repo, c3), s, d1, "msvc", None), "changed data, new key")
        check(k != result_key(source_key(repo, c3), s, d0, "clang", None), "changed compiler, new key")
        check(k != result_key(source_key(repo, c3), s, d0, "msvc", 0), "changed worker count, new key")
        check(k == result_key(source_key(repo, c3), s, d0, "msvc", 4), "default workers equal the entry's own count")

        # data identity follows the files
        data = os.path.join(tmp, "foc")
        os.makedirs(os.path.join(data, "Data", "XML"))
        with open(os.path.join(data, "Data", "XML", "a.xml"), "w") as f:
            f.write("1")
        i1 = data_identity({"ALAMO_FOC_DATA": data})
        with open(os.path.join(data, "Data", "XML", "a.xml"), "w") as f:
            f.write("22")
        i2 = data_identity({"ALAMO_FOC_DATA": data})
        check(i1 != i2 and i1["ALAMO_EAW_DATA"] is None, "data identity changes with a data file")

        # hit / miss through result_here with a fake runner
        cache = Cache(os.path.join(tmp, "cache"), 10 * 1024 * 1024)
        runs: list[str] = []

        def runner(scn, build_dir, src, out, workers):
            runs.append(git(src, "rev-parse", "HEAD").strip())
            with open(out, "w", encoding="utf-8") as f:
                f.write(f"ALAMO_CRC 1\nsystems 1 a\nparity 0\n0 1 0 {len(runs)}\n")
            return 0

        env = {"PATH": os.environ.get("PATH", "")}
        p1 = result_here(cache, s, repo, build, "msvc", runner=runner, env=env)
        p2 = result_here(cache, s, repo, build, "msvc", runner=runner, env=env)
        check(len(runs) == 1 and p1 == p2, f"second request is a hit, no second run ({len(runs)} runs)")
        check(cache.meta(os.path.basename(os.path.dirname(p1)))["commit"] == c3, "meta records the commit")
        g("checkout", "-q", c2)
        result_here(cache, s, repo, build, "msvc", runner=runner, env=env)
        check(len(runs) == 2, "an older engine source is a miss")
        g("checkout", "-q", c1)
        result_here(cache, s, repo, build, "msvc", runner=runner, env=env)
        check(len(runs) == 2, "a docs-only difference shares the entry (c1 and c2)")

        # dirty tree: run, but never stored
        g("checkout", "-q", "dev")
        write("engine/a.cpp", "dirty")
        before = len(cache.entries())
        p3 = result_here(cache, s, repo, build, "msvc", runner=runner, env=env)
        check(len(cache.entries()) == before and "uncached" in p3, "a dirty tree's result is not stored")
        os.remove(p3)
        g("checkout", "-q", "--", "engine/a.cpp")
        write("engine/new.cpp", "untracked")
        result_here(cache, s, repo, build, "msvc", workers=1, runner=runner, env=env)
        check(len(cache.entries()) == before, "untracked engine files count as dirty too")
        os.remove(os.path.join(repo, "engine", "new.cpp"))

        # eviction: least recently used first
        small = Cache(os.path.join(tmp, "small"), 1)
        crc = os.path.join(tmp, "c.crc")
        with open(crc, "w") as f:
            f.write("x" * 100)
        small.cap = 1 << 30
        small.put("aa" + "0" * 62, crc, {})
        small.cap = 2 * small.entries()[0][1] + 10  # room for two entries
        time.sleep(0.05)
        small.put("bb" + "0" * 62, crc, {})
        time.sleep(0.05)
        small.get("aa" + "0" * 62)  # aa is now the most recently used
        time.sleep(0.05)
        small.put("cc" + "0" * 62, crc, {})
        keys = sorted(os.path.basename(d) for _, _, d in small.entries())
        check(keys == ["aa" + "0" * 62, "cc" + "0" * 62], f"eviction drops the least recently used entry: {keys}")

    for b in bad:
        print("scenario_results selftest FAIL: " + b)
    if bad:
        return 1
    print("scenario_results selftest: ok")
    return 0


# ---------------------------------------------------------------- command line

def default_build(compiler: str) -> str:
    """This worktree's agent tree for the compiler (the tree build_agent.cmd builds)."""
    return os.path.join(ROOT, build_trees.agent_tree(compiler))


def main(argv: list[str]) -> int:
    import argparse
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("command", nargs="?", choices=("list", "run", "result", "prune", "stats"))
    ap.add_argument("name", nargs="?")
    ap.add_argument("--build")
    ap.add_argument("--commit")
    ap.add_argument("--compiler", default="msvc")
    ap.add_argument("--workers", type=int)
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest()
    cache = Cache.default()
    if a.command in ("prune", "stats"):
        n = cache.evict() if a.command == "prune" else 0
        ents = cache.entries()
        print(f"{cache.folder}: {len(ents)} entries, {sum(s for _, s, _ in ents) / 1e6:.1f} MB (cap {cache.cap / 1e6:.0f} MB)"
              + (f", dropped {n}" if n else ""))
        return 0
    build = a.build or default_build(a.compiler)
    scen = discover(build)
    if a.command == "list" or not a.command:
        for s in scen:
            print(f"{s.name:<45} {s.golden}  (ctest {s.test}, workers {s.workers})")
        return 0
    match = [s for s in scen if s.name == a.name or s.test == a.name]
    if not match:
        print(f"no scenario {a.name!r}; `list` shows them", file=sys.stderr)
        return 2
    if a.command == "run":
        print(result_here(cache, match[0], ROOT, build, a.compiler, a.workers))
    else:
        if not a.commit:
            ap.error("result needs --commit")
        print(result_for(cache, match[0], a.commit, a.compiler, a.workers))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
