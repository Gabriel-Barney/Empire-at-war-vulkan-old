#!/usr/bin/env python3
"""Scenario results computed from a build, cached outside git (wr1 t02, "Goldens as computed diffs").

A scenario is a run whose output replaces a derived golden of docs/status/golden-inventory.md. Two kinds:

  - headless: a ctest entry of a build tree that runs alamo_headless with `--golden <tracked file>`, found by `discover`.
    Its result is the full CRC log the run writes with `--crc-out` (ALAMO_CRC 1, every tick), stored as `result.crc`.
  - write (wr1 t05): an entry of tools/goldens/scenarios.json: a test executable (taken from its ctest entry) or a script,
    run with arguments that write its outputs into a result folder (`<out>` in the arguments) instead of over the tracked
    files. Its result is that folder: CRC logs, own text formats, binary recordings or screenshots, one file per output.

Results are stored under the main checkout's `.cache/scenario-results/` (gitignored;
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

Each command prints the stored result folder (or the listing). Standard library only.
"""

from __future__ import annotations

import dataclasses
import hashlib
import json
import os
import re
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
    kind: str = "headless"   # headless (--crc-out result.crc) or write (argv writes the outputs into <out>)
    goldens: tuple = ()      # every tracked file the scenario covers (write kind: one per output, same order)
    outputs: tuple = ()      # write kind: the files the run writes into <out>, relative to it; the first len(goldens)
                             # replace the goldens in order, any after them (registry `extra`) replace none
    system: str = ""         # the column a difference in a non-CRC output is charged to (Behaviour: names it)
    needs: tuple = ()        # what the run needs besides a build: foc (game data), gpu, mod:<workshop id>
    workers_arg: str = ""    # write kind: the option that sets the worker count ("" = the run picks its own counts)
    env: tuple = ()          # write kind: (name, value) pairs added to the run's environment
    drop: tuple = ()         # write kind: globs of files the run also writes that are not results (wall-clock saves)
    ignore: tuple = ()       # regexes of text lines left out of the compare (a header line with wall-clock time)
    nightly: bool = False    # too slow or needs a GPU: run by the nightly gate, left out of the landing check
    key_paths: tuple = ()    # repo paths outside SOURCE_PATHS the result depends on (a mod spec, its runner script)
    fingerprint: tuple = ()  # args (after argv0) of a command whose output is an input identity (an installed mod's data)
    output_systems: tuple = ()  # (output, system) pairs that override `system` for one output
    agreement: bool = True   # expand phase: the stored golden changes exactly when the result does (False: it is also
                             # regenerated for input updates no code diff shows, e.g. a workshop mod update)

    def definition(self) -> dict:
        d = {"argv": list(self.argv), "cwd": self.cwd}
        if self.kind != "headless":  # headless keys stay as they were before wr1 t05
            d.update(kind=self.kind, outputs=list(self.outputs), env=[list(x) for x in self.env], drop=list(self.drop))
        return d

    def covers(self) -> tuple:
        return self.goldens or (self.golden,)


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


REGISTRY = os.path.join("tools", "goldens", "scenarios.json")


def scenarios_from_registry(entries: list[dict], tests: list[dict], build_dir: str, src_dir: str) -> list[Scenario]:
    """Write-kind scenarios of tools/goldens/scenarios.json. An entry names its program by ctest entry (`test`: the program
    and working directory of that entry; left out when the build tree has no such entry, e.g. a module not built) or by
    `argv0` (a path with <src> / <build> placeholders, or `python` for this interpreter); `args` follow it, `<out>` is the
    result folder."""
    by_name = {t["name"]: t for t in tests}
    out = []
    for e in entries:
        if "test" in e:
            t = by_name.get(e["test"])
            if t is None or not t.get("command"):
                continue
            argv0 = _placeholders(t["command"][0], build_dir, src_dir)
            cwd = next((p["value"] for p in t.get("properties", []) if p.get("name") == "WORKING_DIRECTORY"), build_dir)
            cwd = _placeholders(cwd, build_dir, src_dir)
        else:
            argv0, cwd = e["argv0"], e.get("cwd", "<build>")
        goldens = tuple(e["goldens"])
        outputs = tuple(e.get("outputs") or [os.path.basename(g) for g in goldens])
        if len(outputs) != len(goldens):
            raise RuntimeError(f"{REGISTRY}: {e['name']}: {len(goldens)} goldens but {len(outputs)} outputs")
        outputs += tuple(e.get("extra", []))  # results that replace no golden (a run's CRC log beside its AI log)
        out.append(Scenario(e["name"], goldens[0], e.get("test", ""), (argv0, *e.get("args", [])), cwd, None, "write",
                            goldens, outputs, e.get("system", ""), tuple(e.get("needs", [])), e.get("workers_arg", ""),
                            tuple(sorted(e.get("env", {}).items())), tuple(e.get("drop", [])), tuple(e.get("ignore", [])),
                            bool(e.get("nightly", False)), tuple(e.get("key_paths", [])), tuple(e.get("fingerprint", [])),
                            tuple(sorted(e.get("systems", {}).items())), bool(e.get("agreement", True))))
    return out


def registry_entry(repo: str, commit: str, name: str) -> dict | None:
    """A scenario's registry entry as committed in `commit` (None: no registry or no such entry there)."""
    r = subprocess.run(["git", "-C", repo, "show", f"{commit}:{REGISTRY.replace(os.sep, '/')}"], capture_output=True,
                       text=True, encoding="utf-8", errors="replace")
    if r.returncode != 0:
        return None
    try:
        entries = json.loads(r.stdout).get("scenarios", [])
    except ValueError:
        return None
    return next((e for e in entries if e.get("name") == name), None)


def parent_outputs(repo: str, base: str, head: str, scenario: Scenario) -> set | None:
    """The outputs of a write-kind scenario that the base commit can give too: None when the base has no such entry or
    runs it differently (program, arguments, environment: its programs may not even take the options, so the commit that
    changes the run is its first), else the outputs both definitions list (a golden added to a scenario is new; the
    others are still compared). Headless scenarios: every output."""
    if scenario.kind == "headless":
        return set(scenario.outputs or ("result.crc",))
    new = registry_entry(repo, head, scenario.name)
    old = registry_entry(repo, base, scenario.name)
    if old is None or new is None or any(old.get(k) != new.get(k) for k in ("test", "argv0", "args", "env")):
        return None

    def outs(e: dict) -> set:
        return set(e.get("outputs") or [os.path.basename(g) for g in e.get("goldens", [])]) | set(e.get("extra", []))
    return outs(old) & outs(new)


def load_registry(src_dir: str = ROOT) -> list[dict]:
    path = os.path.join(src_dir, REGISTRY)
    if not os.path.isfile(path):
        return []
    with open(path, encoding="utf-8") as f:
        return json.load(f).get("scenarios", [])


def discover(build_dir: str, src_dir: str = ROOT) -> list[Scenario]:
    r = subprocess.run([find_ctest(), "--test-dir", build_dir, "--show-only=json-v1"], capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"ctest --show-only failed in {build_dir}: {r.stderr.strip()}")
    tracked = set(git(src_dir, "ls-files").splitlines())
    tests = json.loads(r.stdout).get("tests", [])
    found = scenarios_from_ctest(tests, build_dir, src_dir, tracked) + \
        scenarios_from_registry(load_registry(src_dir), tests, build_dir, src_dir)
    return sorted(found, key=lambda s: s.name)


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


def result_key(source: str, scenario: Scenario, data: dict, compiler: str, workers: int | None,
               inputs: dict | None = None) -> str:
    doc = {"v": KEY_VERSION, "source": source, "scenario": scenario.definition(), "data": data, "compiler": compiler,
           "workers": scenario.workers if workers is None else workers}
    if inputs:  # left out when empty, so the keys of scenarios without own inputs stay as they were
        doc["inputs"] = inputs
    return hashlib.sha256(json.dumps(doc, sort_keys=True).encode()).hexdigest()


def scenario_inputs(scenario: Scenario, repo: str, commit: str, build_dir: str, src_dir: str, env: dict) -> dict:
    """A scenario's own inputs for its key: its key_paths at the commit, and its fingerprint command's output."""
    out: dict = {}
    if scenario.key_paths:
        out["paths"] = hashlib.sha256(git(repo, "ls-tree", "-r", commit, "--", *scenario.key_paths).encode()).hexdigest()
    if scenario.fingerprint:
        argv = [_resolve(a, build_dir, src_dir) for a in (scenario.argv[0], *scenario.fingerprint)]
        if argv[0].lower() == "python" or os.path.basename(argv[0]).lower().startswith("python"):
            argv[0] = sys.executable
        try:
            r = subprocess.run(argv, cwd=src_dir, env=env, capture_output=True, text=True, errors="replace", timeout=600)
            out["fingerprint"] = r.stdout.strip()
        except (OSError, subprocess.TimeoutExpired) as e:
            out["fingerprint"] = f"unavailable: {e}"
    return out


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
        """The stored result folder (its files; meta.json beside them), or None. A hit marks the entry as used."""
        d = self._dir(key)
        meta = os.path.join(d, "meta.json")
        if not os.path.isfile(meta) or not result_files(d):
            return None
        try:
            os.utime(meta)
        except OSError:
            pass
        return d

    def meta(self, key: str) -> dict | None:
        try:
            with open(os.path.join(self._dir(key), "meta.json"), encoding="utf-8") as f:
                return json.load(f)
        except OSError:
            return None

    def put(self, key: str, result_dir: str, meta: dict) -> str:
        """Store a result folder (every file in it, subfolders too) under key; returns the stored folder."""
        d = self._dir(key)
        os.makedirs(os.path.dirname(d), exist_ok=True)
        tmp = tempfile.mkdtemp(prefix=key[:8] + ".", dir=os.path.dirname(d))
        shutil.copytree(result_dir, tmp, dirs_exist_ok=True)
        with open(os.path.join(tmp, "meta.json"), "w", encoding="utf-8") as f:
            json.dump(dict(meta, key=key, stored=time.strftime("%Y-%m-%dT%H:%M:%S")), f, indent=1)
        if os.path.isdir(d):
            shutil.rmtree(d, ignore_errors=True)
        os.replace(tmp, d)
        self.evict()
        return d

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
                size = sum(os.path.getsize(os.path.join(r, f)) for r, _, fs in os.walk(d) for f in fs)
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

def source_dirty(checkout: str, extra: tuple = ()) -> list[str]:
    """Uncommitted edits (tracked or untracked) under SOURCE_PATHS (and a scenario's key_paths) of a checkout."""
    out = git(checkout, "status", "--porcelain", "--", *SOURCE_PATHS, *extra)
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


def result_files(folder: str) -> dict[str, str]:
    """The files of a result folder by path relative to it ('/' separators), without meta.json."""
    out = {}
    for r, _, fs in os.walk(folder):
        for f in fs:
            rel = os.path.relpath(os.path.join(r, f), folder).replace("\\", "/")
            if rel != "meta.json":
                out[rel] = os.path.join(r, f)
    return dict(sorted(out.items()))


def missing_needs(scenario: Scenario, env: dict) -> list[str]:
    """What a scenario needs that this machine lacks (it is then skipped, like its ctest entry)."""
    miss = []
    for n in scenario.needs:
        if n == "foc" and not (env.get("ALAMO_FOC_DATA") and os.path.isdir(env["ALAMO_FOC_DATA"])):
            miss.append("ALAMO_FOC_DATA")
        elif n == "gpu" and env.get("ALAMO_NO_GPU"):
            miss.append("a GPU (ALAMO_NO_GPU is set)")
    return miss


def run_scenario(scenario: Scenario, build_dir: str, src_dir: str, out_dir: str, workers: int | None = None,
                 env: dict | None = None) -> int:
    """Run one scenario; its outputs land in out_dir (result.crc for headless, the listed outputs for write)."""
    argv = [_resolve(a, build_dir, src_dir).replace("<out>", out_dir.replace("\\", "/")) for a in scenario.argv]
    w = scenario.workers if workers is None else workers
    if scenario.kind == "headless":
        if w is not None:
            argv += ["--workers", str(w)]
        argv += ["--crc-out", os.path.join(out_dir, "result.crc")]
        expected = ["result.crc"]
    else:
        if workers is not None:
            if not scenario.workers_arg:
                raise RuntimeError(f"{scenario.name}: the run picks its own worker counts (no workers_arg)")
            argv += [scenario.workers_arg, str(workers)]
        expected = list(scenario.outputs)
        for rel in expected:
            os.makedirs(os.path.dirname(os.path.join(out_dir, rel)), exist_ok=True)
    if argv[0].lower() == "python":
        argv[0] = sys.executable
    run_env = dict(env or test_env())
    run_env.update({k: _resolve(v, build_dir, src_dir) for k, v in scenario.env})
    try:
        r = subprocess.run(argv, cwd=_resolve(scenario.cwd, build_dir, src_dir), env=run_env,
                           capture_output=True, text=True, errors="replace")
    except OSError as e:  # a program or folder the build does not have (a renamed exe, a module not built)
        raise RuntimeError(f"{scenario.name}: cannot run {argv[0]}: {e}") from e
    if scenario.drop:
        import fnmatch
        for rel, path in result_files(out_dir).items():
            if any(fnmatch.fnmatch(rel, g) for g in scenario.drop):
                os.remove(path)
    absent = [rel for rel in expected if not os.path.isfile(os.path.join(out_dir, rel))]
    said = [ln for ln in (r.stdout + r.stderr).splitlines() if re.search(r"\bskipped\b", ln, re.IGNORECASE)]
    if absent and r.returncode == 0 and said:
        # the program skipped itself (a missing workshop item, data or device), as its ctest entry would
        last = said[-1].strip()
        raise RuntimeError(f"skipped by the run: {last[:200]}")
    if absent:
        raise RuntimeError(f"{scenario.name}: {', '.join(absent)} not written (exit {r.returncode}): "
                           f"{(r.stderr or r.stdout)[-400:]}")
    return r.returncode


Runner = Callable[[Scenario, str, str, str, "int | None"], int]


def result_here(cache: Cache, scenario: Scenario, checkout: str, build_dir: str, compiler: str,
                workers: int | None = None, runner: Runner | None = None, env: dict | None = None) -> str:
    """This checkout's HEAD result folder (its build must be of HEAD): from the cache, else run and store (only when
    clean). A scenario whose needs this machine lacks raises RuntimeError starting with 'skipped'."""
    env = env or test_env()
    miss = missing_needs(scenario, env)
    if miss:
        raise RuntimeError(f"skipped, needs {', '.join(miss)}")
    head = git(checkout, "rev-parse", "HEAD").strip()
    key = result_key(source_key(checkout, head), scenario, data_identity(env), compiler, workers,
                     scenario_inputs(scenario, checkout, head, build_dir, checkout, env))
    # A checkout with uncommitted source edits built something no commit has: always run, never read or write the cache.
    dirty = source_dirty(checkout, scenario.key_paths)
    hit = None if dirty else cache.get(key)
    if hit:
        return hit
    with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as tmp:  # a child may still hold a file
        out = os.path.join(tmp, "result")
        os.makedirs(out)
        rc = runner(scenario, build_dir, checkout, out, workers) if runner else \
            run_scenario(scenario, build_dir, checkout, out, workers, env)
        if dirty:
            # Never cached: the build may contain edits no commit has. The caller still gets this run's result.
            keep = tempfile.mkdtemp(prefix=f"scenario_{scenario.name}_", suffix="_uncached")
            shutil.copytree(out, keep, dirs_exist_ok=True)
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
    key = result_key(source_key(repo, commit), scenario, data_identity(env), compiler, workers,
                     scenario_inputs(scenario, repo, commit, os.path.join(repo, build_trees.agent_tree(compiler, "base")),
                                     repo, env))
    hit = cache.get(key)
    if hit:
        return hit
    wt = ensure_base_worktree(repo)
    # One user of the shared base worktree at a time (the verifier and the nightly gate): checkout, build and run
    # under one lock, so no result is stored under another commit's key.
    with base_lock(main_checkout(repo)):
        git(wt, "checkout", "-f", "--detach", commit)
        script = os.path.join(wt, "build_agent.cmd")
        r = subprocess.run(["cmd", "/d", "/c", script, compiler, "base", "all", "none"], cwd=wt, env=env)
        if r.returncode != 0:
            raise RuntimeError(f"building {commit[:8]} in {wt} failed (exit {r.returncode}); "
                               f"see {wt}\\logs\\agent_base_{compiler}.txt")
        build = os.path.join(wt, build_trees.agent_tree(compiler, "base"))
        return result_here(cache, scenario, wt, build, compiler, workers, env=env)


class base_lock:
    """An exclusive lock on <main checkout>/.cache/scenario-base.lock, held by an open file (released when the process
    ends, so a killed holder never leaves it stale). Waits for another holder."""

    def __init__(self, main: str):
        self.path = os.path.join(main, ".cache", "scenario-base.lock")
        self.f = None

    def __enter__(self):
        os.makedirs(os.path.dirname(self.path), exist_ok=True)
        self.f = open(self.path, "a+b")
        if os.name == "nt":
            import msvcrt
            while True:
                try:
                    self.f.seek(0)
                    msvcrt.locking(self.f.fileno(), msvcrt.LK_NBLCK, 1)
                    return self
                except OSError:
                    time.sleep(2)
        import fcntl
        fcntl.flock(self.f.fileno(), fcntl.LOCK_EX)
        return self

    def __exit__(self, *exc):
        try:
            if os.name == "nt":
                import msvcrt
                self.f.seek(0)
                msvcrt.locking(self.f.fileno(), msvcrt.LK_UNLCK, 1)
        finally:
            self.f.close()
        return False


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
            with open(os.path.join(out, "result.crc"), "w", encoding="utf-8") as f:
                f.write(f"ALAMO_CRC 1\nsystems 1 a\nparity 0\n0 1 0 {len(runs)}\n")
            return 0

        env = {"PATH": os.environ.get("PATH", "")}
        p1 = result_here(cache, s, repo, build, "msvc", runner=runner, env=env)
        p2 = result_here(cache, s, repo, build, "msvc", runner=runner, env=env)
        check(len(runs) == 1 and p1 == p2, f"second request is a hit, no second run ({len(runs)} runs)")
        check(cache.meta(os.path.basename(p1))["commit"] == c3, "meta records the commit")
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
        shutil.rmtree(p3, ignore_errors=True)
        g("checkout", "-q", "--", "engine/a.cpp")
        write("engine/new.cpp", "untracked")
        result_here(cache, s, repo, build, "msvc", workers=1, runner=runner, env=env)
        check(len(cache.entries()) == before, "untracked engine files count as dirty too")
        os.remove(os.path.join(repo, "engine", "new.cpp"))

        # eviction: least recently used first
        small = Cache(os.path.join(tmp, "small"), 1)
        crc = os.path.join(tmp, "cdir")
        os.makedirs(crc)
        with open(os.path.join(crc, "result.crc"), "w") as f:
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

        # write kind: registry entries, <out> substitution, result folders with several files
        reg = [{"name": "ai_x", "test": "alamo_ai_x_tests", "args": ["--write-golden", "<out>/a.txt"],
                "goldens": ["engine/ai/g/a.txt"], "system": "battle.ai", "needs": ["foc"], "extra": ["run.crc"]},
               {"name": "two", "argv0": "python", "args": ["<src>/tools/w.py", "<out>"], "goldens": ["g/1.crc", "g/2.txt"],
                "outputs": ["sub/1.crc", "2.txt"]},
               {"name": "absent", "test": "not_built", "goldens": ["g/z.txt"]}]
        tests2 = [{"name": "alamo_ai_x_tests", "command": [f"{build}/bin/alamo_ai_x_tests.exe"],
                   "properties": [{"name": "WORKING_DIRECTORY", "value": f"{build}/engine/ai"}]}]
        ws = {x.name: x for x in scenarios_from_registry(reg, tests2, build, repo)}
        check(sorted(ws) == ["ai_x", "two"], f"registry: a scenario whose test is not built is left out: {sorted(ws)}")
        check(ws["ai_x"].argv == ("<build>/bin/alamo_ai_x_tests.exe", "--write-golden", "<out>/a.txt")
              and ws["ai_x"].cwd == "<build>/engine/ai" and ws["ai_x"].outputs == ("a.txt", "run.crc")
              and ws["ai_x"].covers() == ("engine/ai/g/a.txt",), f"registry argv, extra outputs: {ws['ai_x']}")
        check(ws["two"].outputs == ("sub/1.crc", "2.txt") and ws["two"].covers() == ("g/1.crc", "g/2.txt"), "outputs map")
        check(missing_needs(ws["ai_x"], {}) == ["ALAMO_FOC_DATA"] and not missing_needs(ws["two"], {}), "needs are checked")
        kw = result_key(source_key(repo, c3), ws["two"], d0, "msvc", None)
        check(kw != result_key(source_key(repo, c3), dataclasses.replace(ws["two"], outputs=("x",)), d0, "msvc", None),
              "a write scenario's outputs are part of its key")
        check(result_key(source_key(repo, c3), s, d0, "msvc", None) == k, "headless keys unchanged by the write kind")
        wdir = os.path.join(tmp, "wout")
        os.makedirs(os.path.join(wdir, "sub"))
        for rel, text in (("sub/1.crc", "ALAMO_CRC 1\n"), ("2.txt", "t\n")):
            with open(os.path.join(wdir, rel), "w") as f:
                f.write(text)
        stored = cache.put("dd" + "0" * 62, wdir, {})
        check(sorted(result_files(stored)) == ["2.txt", "sub/1.crc"] and cache.get("dd" + "0" * 62) == stored,
              "a result folder with a subfolder is stored and found again")
        with tempfile.TemporaryDirectory() as o2:
            try:
                run_scenario(dataclasses.replace(ws["two"], argv=("python", "-c", "pass"), cwd="<src>"), build, repo, o2)
                named = False
            except RuntimeError as e:
                named = "sub/1.crc, 2.txt not written" in str(e)
        check(named, "a write run that leaves an output out is an error naming it")
        with tempfile.TemporaryDirectory() as o3:
            code = ("import os,sys; o=sys.argv[1]; os.makedirs(o+'/saves'); open(o+'/a.txt','w').write(os.environ['X']);"
                    "open(o+'/saves/s.alsv','w').write('t')")
            sc3 = dataclasses.replace(ws["two"], argv=("python", "-c", code, "<out>"), cwd="<src>", outputs=("a.txt",),
                                      env=(("X", "<src>"),), drop=("saves/*",))
            run_scenario(sc3, build, repo, o3)
            with open(os.path.join(o3, "a.txt")) as f:
                check(f.read() == repo.replace("\\", "/"), "env values get placeholders resolved")
            check(sorted(result_files(o3)) == ["a.txt"], f"dropped files leave the result: {sorted(result_files(o3))}")
        with tempfile.TemporaryDirectory() as o4:
            try:
                run_scenario(dataclasses.replace(sc3, argv=("python", "-c", "print('mod not installed, skipped')")), build,
                             repo, o4)
                skipped = False
            except RuntimeError as e:
                skipped = str(e).startswith("skipped")
            check(skipped, "a run that skips itself (exit 0, says skipped, no outputs) is a skip")
            try:
                run_scenario(dataclasses.replace(sc3, argv=("python", "-c", "print('skip 1 ai_moved 1')")), build, repo, o4)
                word = False
            except RuntimeError as e:
                word = not str(e).startswith("skipped")
            check(word, "a line that only contains 'skip' is not a self-skip")

        # a scenario the base commit's registry lacks (or defines differently) is new: no parent result
        reg_path = os.path.join(repo, "tools", "goldens", "scenarios.json")
        os.makedirs(os.path.dirname(reg_path), exist_ok=True)
        with open(reg_path, "w") as f:
            json.dump({"scenarios": [{"name": "two", "args": ["a"]}]}, f)
        g("add", "-A"); g("commit", "-q", "-m", "registry")
        c4 = g("rev-parse", "HEAD").strip()
        with open(reg_path, "w") as f:
            json.dump({"scenarios": [{"name": "two", "args": ["a"], "row": "x"}, {"name": "ai_x", "args": ["b"]}]}, f)
        g("add", "-A"); g("commit", "-q", "-m", "registry 2")
        c5 = g("rev-parse", "HEAD").strip()
        check(parent_outputs(repo, c4, c5, ws["two"]) == set(), "an unchanged entry has a parent result (other fields may change)")
        check(parent_outputs(repo, c4, c5, ws["ai_x"]) is None and parent_outputs(repo, c3, c5, ws["two"]) is None,
              "an entry new since the base has none")
        check(parent_outputs(repo, c4, c5, s) == {"result.crc"}, "headless scenarios always have one")
        with open(reg_path, "w") as f:
            json.dump({"scenarios": [{"name": "two", "args": ["a"], "goldens": ["g/1.crc", "g/2.txt"]}]}, f)
        g("add", "-A"); g("commit", "-q", "-m", "registry 3")
        c6 = g("rev-parse", "HEAD").strip()
        with open(reg_path, "w") as f:
            json.dump({"scenarios": [{"name": "two", "args": ["a"], "goldens": ["g/1.crc", "g/2.txt", "g/3.txt"]}]}, f)
        g("add", "-A"); g("commit", "-q", "-m", "registry 4")
        c7 = g("rev-parse", "HEAD").strip()
        check(parent_outputs(repo, c6, c7, ws["two"]) == {"1.crc", "2.txt"}, "an added golden is new, the others compare")
        # scenario inputs: key_paths and a fingerprint command move the key; the lock can be taken twice in a row
        sk = dataclasses.replace(ws["two"], key_paths=("tools/goldens/scenarios.json",))
        i6, i7 = (scenario_inputs(sk, repo, c, build, repo, os.environ.copy()) for c in (c6, c7))
        check(i6 != i7 and result_key("s", sk, d0, "msvc", None, i6) != result_key("s", sk, d0, "msvc", None, i7),
              "a change to a key path moves the key")
        sf = dataclasses.replace(ws["two"], argv=("python",), fingerprint=("-c", "print('fp1')"))
        check(scenario_inputs(sf, repo, c7, build, repo, os.environ.copy()) == {"fingerprint": "fp1"}, "fingerprint output")
        with base_lock(tmp):
            pass
        with base_lock(tmp):
            pass
        try:
            run_scenario(dataclasses.replace(sc3, argv=("<build>/no_such.exe",)), build, repo, tmp)
            missing_prog = False
        except RuntimeError as e:
            missing_prog = "cannot run" in str(e)
        check(missing_prog, "a missing program is a RuntimeError, not a traceback")

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
            more = f" (+{len(s.covers()) - 1} more)" if len(s.covers()) > 1 else ""
            print(f"{s.name:<45} {s.golden}{more}  ({s.kind}; ctest {s.test or '-'}, workers {s.workers})"
                  + (f" needs {','.join(s.needs)}" if s.needs else "") + (" nightly" if s.nightly else ""))
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
