#!/usr/bin/env python3
"""Mod compatibility corpus runner (docs/MOD_COMPAT_DESIGN.md §13.2; roadmap mcx7 t01, t03, t05).

Runs every installed mod (and its resolved stack) through the port's checks and grades it G0-G3 (grade.py):

1. Discovery: `alamo_mod_check --list-mods` (the mcx3 registry: Steam workshop items of every library, then
   <game>\\Mods folders; one setup per descriptor, so a folder with variant modinfo files gives one setup per variant).
2. Per setup, reusing the cached result when the build, the options and every stacked mod's content hash are unchanged:
   i.   check   `alamo_mod_check --resolve --json` (load report v4; the stack resolved as the launcher does).
   ii.  runs    `alamo_headless --skirmish-ai skirmish` on the mod's first space and land map, 3000 ticks, workers 0
                and 4, CRC logs compared with `--compare`; one galactic campaign 9000 ticks with
                `--economy --galactic-ai --story --scoring --object-scripts` (the campaign: corpus.json's pick, else the
                first one with planets in database order).
   iii. smokes  `alamo_game --smoke skirmish|campaign --hidden --audio null` (needs a GPU).
   iv.  --asan  the runs of ii again (workers 4, no compare) with the ASan build's alamo_headless.
3. Writes logs/modcorpus/<date>/<setup>.json (result + grade), matrix.json and matrix.md; --write-md replaces the
   generated table of docs/MODS.md §10.4.
4. Compares the grades with the accepted baseline of the mode (logs/modcorpus/baseline.json, baseline-asan.json for
   --asan; or --baseline FILE): a lower grade is a regression, printed with the mod and its first new blocker, exit 1. A
   sweep without a drop updates the baseline (only when it ran at least the baseline's stages); after a drop it stays,
   so the next sweep fails again until the drop is fixed or accepted with --accept-drops.

Each headless / game run holds one of the machine's test slots (tools/lint/test_slot.py) while it runs; the timeout
counts from when it has the slot.

  tools\\modcorpus\\run_corpus.cmd [options]           (sets the toolchain PATH and the game data; see README.md)
  python -I tools/modcorpus/run_corpus.py --list      what would run

Options: --game DIR (default ALAMO_FOC_DATA), --build-dir DIR (default build-wt-msvc, else build-msvc), --out-root DIR
(default logs/modcorpus), --date LABEL (default today), --only KEY[,KEY] (setup keys or substrings of names),
--stages check,runs,smokes, --space-ticks N / --land-ticks N (3000), --galactic-ticks N (9000), --timeout S (1800 per
run), --no-cache, --baseline FILE, --accept-drops, --write-md, --asan [--asan-build-dir build-asan], --corpus FILE, --no-slot,
--steam-root DIR (sets ALAMO_STEAM_ROOT for the children), --keep-crc.
Exit: 0 no grade dropped, 1 a grade dropped, 2 usage / a tool is missing.
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools", "lint"))
import grade as gr  # noqa: E402
import build_trees  # noqa: E402

try:
    import test_slot  # noqa: E402
except ImportError:  # pragma: no cover
    test_slot = None

RUNNER_VERSION = 1  # bump when a stage's commands or parsing change (invalidates the cache)
MD_BEGIN, MD_END = "<!-- modcorpus:begin -->", "<!-- modcorpus:end -->"

SKIRMISH_RE = re.compile(r"skirmish-ai \S+ workers (\d+): (\d+) ticks \(([\d.]+) ms\), AI orders (\d+) rejected (\d+), (\d+) decisions, "
                         r"(\d+) Lua errors, final CRC ([0-9a-f]+)")
RETAIL_ALL_RE = re.compile(r"^Lua errors: (\d+), all retail", re.M)
RETAIL_SOME_RE = re.compile(r"^Lua errors: \d+ \((\d+) retail", re.M)
GALACTIC_RE = re.compile(r"galactic-ai (.+?) workers (\d+): (\d+) ticks \(([\d.]+) ms\), galactic frame -?\d+, decisions (\d+), orders (\d+) "
                         r"\(rejected (\d+)\), conflicts (\d+), battles (\d+), planet owner changes (\d+), running plans (\d+), Lua errors (\d+)")
STORY_RE = re.compile(r"galactic-ai: story: (\d+) events fired, (\d+) Lua notes")
SMOKE_RE = re.compile(r"^smoke (\S+): (\d+) / (\d+) steps", re.M)
STEP_TIMEOUT_RE = re.compile(r"^script: step '([^']*)' timed out", re.M)
SANITIZER_RE = re.compile(r"ERROR: AddressSanitizer: (\S+)|runtime error: (.{0,120})")


# ------------------------------------------------------------------------------------------------------------- helpers

def log(msg: str) -> None:
    print(msg, flush=True)


def rel(path: str) -> str:
    """A path relative to the repository when it is inside it, with forward slashes."""
    try:
        r = os.path.relpath(path, ROOT)
    except ValueError:  # another drive
        r = path
    return (path if r.startswith("..") else r).replace(os.sep, "/")


def setup_file(key: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]", "_", key)


def file_id(path: str) -> str:
    st = os.stat(path)
    return f"{os.path.basename(path)}:{st.st_size}:{st.st_mtime_ns}"


def run(cmd: list[str], out_path: str, timeout: float, env: dict, use_slot: bool) -> dict:
    """Run cmd with stdout + stderr into out_path. {exit, seconds, timed_out, text}. Holds a test slot while it runs."""
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    slot = test_slot.acquire(cmd) if (use_slot and test_slot is not None) else None
    try:
        t0 = time.monotonic()
        with open(out_path, "w", encoding="utf-8", errors="replace") as f:
            f.write("$ " + subprocess.list2cmdline(cmd) + "\n")
            f.flush()
            try:
                p = subprocess.Popen(cmd, stdout=f, stderr=subprocess.STDOUT, env=env, cwd=ROOT)
            except OSError as e:
                return {"exit": None, "seconds": 0.0, "timed_out": False, "text": "", "error": str(e)}
            timed_out = False
            try:
                p.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
                p.kill()
                p.wait()
        secs = round(time.monotonic() - t0, 1)
    finally:
        if slot is not None:
            slot.release()
    with open(out_path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    return {"exit": None if timed_out else p.returncode, "seconds": secs, "timed_out": timed_out, "text": text}


def last_line(text: str, pattern: str = r"FAIL|error|Error|failed") -> str:
    lines = [l.strip() for l in text.splitlines() if l.strip() and not l.startswith("$ ")]
    hits = [l for l in lines if re.search(pattern, l) and not l.startswith("[warn]")]
    return ((hits or lines or [""])[-1])[:200]


def merged_settings(corpus: dict, setup_key: str, stack_keys: list[str]) -> dict:
    """corpus.json entries of the stack's mods, the base mod first, the setup's own entry last (it wins); accepts add up."""
    out: dict = {}
    accepts: list = []
    mods = corpus.get("mods", {})
    order = [k for k in reversed(stack_keys) if k != setup_key] + [setup_key]  # launch order is submod first
    for k in order:
        e = mods.get(k) or {}
        for name, v in e.items():
            if name == "accept":
                accepts += v
            elif name == "name" and k != setup_key:
                continue
            else:
                out[name] = v
    out["accept"] = accepts + list(corpus.get("accept_all", []))
    return out


# ----------------------------------------------------------------------------------------------------------- the stages

class Runner:
    def __init__(self, a, corpus: dict):
        self.a = a
        self.corpus = corpus
        self.bin = a.bin or os.path.join(a.build_dir, "bin")
        self.mod_check = os.path.join(self.bin, "alamo_mod_check.exe")
        self.headless = os.path.join(self.bin, "alamo_headless.exe")
        self.game_exe = os.path.join(self.bin, "alamo_game.exe")
        self.asan_headless = os.path.join(a.asan_build_dir, "bin", "alamo_headless.exe")
        self.env = dict(os.environ)
        if a.steam_root:
            self.env["ALAMO_STEAM_ROOT"] = a.steam_root
        self.day = os.path.join(a.out_root, a.date)
        self.cache_dir = os.path.join(a.out_root, "cache")

    def build_id(self) -> str:
        parts = [file_id(self.mod_check)]
        if "runs" in self.a.stages:
            parts.append(file_id(self.headless))
        if "smokes" in self.a.stages:
            parts.append(file_id(self.game_exe))
        if self.a.asan:
            parts.append(file_id(self.asan_headless))
        return hashlib.sha1("|".join(parts).encode()).hexdigest()[:16]

    def missing_tools(self) -> list[str]:
        need = [self.mod_check]
        if "runs" in self.a.stages:
            need.append(self.headless)
        if "smokes" in self.a.stages:
            need.append(self.game_exe)
        if self.a.asan:
            need.append(self.asan_headless)
        return [p for p in need if not os.path.isfile(p)]

    def discover(self) -> list[dict]:
        out = os.path.join(self.day, "mod_list.json")
        r = run([self.mod_check, "--game", self.a.game, "--list-mods", "--json", out], os.path.join(self.day, "mod_list.txt"),
                self.a.timeout, self.env, False)
        if r["exit"] != 0 or not os.path.isfile(out):
            raise SystemExit(f"run_corpus: alamo_mod_check --list-mods failed (exit {r['exit']}): {last_line(r['text'])}")
        with open(out, encoding="utf-8") as f:
            listed = json.load(f)["mods"]
        mods = [m for m in listed if m.get("has_data")]
        # Workshop items without a Data folder (an uploader's test item): the registry leaves them out, the corpus grades
        # them G0 "nothing to mount" so every installed item has a grade (design 13.1).
        content_dirs = sorted({os.path.dirname(m["root"]) for m in listed if m["kind"] == "workshop"})
        known = {m["id"] for m in listed}
        for cd in content_dirs:
            for n in sorted(os.listdir(cd)):
                if n.isdigit() and n not in known and os.path.isdir(os.path.join(cd, n)) and not os.path.isdir(os.path.join(cd, n, "Data")):
                    mods.append({"key": n, "arg": "STEAMMOD=" + n, "kind": "workshop", "id": n, "variant": "", "root": os.path.join(cd, n),
                                 "name": n, "has_data": False, "content_hash": "", "dependencies": []})
        return mods

    @staticmethod
    def vfs_arg(m: dict) -> str | None:
        if m["kind"] == "workshop":
            return "STEAMMOD=" + m["key"]  # STEAMMOD=<id>:<variant> names one variant (mod_registry.hpp find_arg)
        return m["arg"]  # a local folder (a variant mounts the same folder: only its descriptor differs)

    # -- i. check
    def check(self, m: dict, d: str) -> tuple[dict, dict | None]:
        arg = self.vfs_arg(m)
        if arg is None:
            return {"tool_error": "a local mod's modinfo variant cannot be named on a command line"}, None
        jpath = os.path.join(d, "report.json")
        if os.path.exists(jpath):
            os.remove(jpath)
        cmd = [self.mod_check, "--game", self.a.game, "--resolve", "--json", jpath,
               "--corpus-cache", os.path.join(self.a.out_root, "asset_cache"), arg]
        r = run(cmd, os.path.join(d, "check.txt"), self.a.timeout, self.env, False)
        if r["timed_out"]:
            return {"tool_error": f"timeout after {r['seconds']} s"}, None
        if r["exit"] not in (0, 1) or not os.path.isfile(jpath):
            return {"tool_error": f"exit {r['exit']}: {last_line(r['text'])}"}, None
        with open(jpath, encoding="utf-8", errors="replace") as f:
            rep = json.load(f)
        camp = rep.get("campaigns", {})
        g = rep.get("grade", {}) or {}
        stack = rep.get("stack", {}) or {}
        summary = {
            "result": rep.get("result"), "errors": rep.get("errors", 0), "warnings": rep.get("warnings", 0),
            "loaded": rep.get("loaded"), "load_error": rep.get("load_error", ""), "base_eaw": rep.get("base_eaw", False),
            "stack_errors": stack.get("errors", []), "stack_warnings": stack.get("warnings", []),
            "check_errors": list(rep.get("check_errors", [])) + list(g.get("check_errors", [])),
            "no_startable_campaign": camp.get("checked", 0) > 0 and len(camp.get("without_planets", [])) == camp.get("checked", 0),
            "grade": {"grade": g.get("grade", ""), "g3_static": g.get("g3_static", False), "blocking": g.get("blocking", [])},
            "content": {k: v for k, v in (rep.get("content") or {}).items() if k != "skirmish_maps"},
            "seconds": r["seconds"],
        }
        return summary, rep

    # -- ii. runs
    def skirmish(self, exe: str, kind: str, mp: str | None, args: list[str], s: dict, d: str, workers: tuple, prefix: str,
                 timeout: float) -> dict:
        if not mp:
            return {"status": "n/a", "why": f"no {kind} skirmish map"}
        ticks = self.a.space_ticks if kind == "space" else self.a.land_ticks
        res: dict = {"map": mp, "ticks": ticks, "workers": {}}
        crcs = {}
        for w in workers:
            crc = os.path.join(d, f"{prefix}{kind}_w{w}.crc")
            cmd = [exe, "--game", self.a.game, "--skirmish-ai", "skirmish", "--map", mp, "--ticks", str(ticks), "--workers", str(w),
                   "--crc-out", crc] + list(s.get("skirmish_args", [])) + args
            r = run(cmd, os.path.join(d, f"{prefix}{kind}_w{w}.txt"), timeout, self.env, not self.a.no_slot)
            one = {"exit": r["exit"], "seconds": r["seconds"]}
            if r["timed_out"]:
                res.update(status="timeout", seconds=r["seconds"])
                res["workers"][str(w)] = one
                return res
            mt = SKIRMISH_RE.search(r["text"])
            if mt:
                one.update(ticks=int(mt.group(2)), orders=int(mt.group(4)), rejected=int(mt.group(5)), decisions=int(mt.group(6)),
                           lua_errors=int(mt.group(7)), final_crc=mt.group(8))
                ra, rs = RETAIL_ALL_RE.search(r["text"]), RETAIL_SOME_RE.search(r["text"])
                one["retail_lua_errors"] = int(ra.group(1)) if ra else int(rs.group(1)) if rs else 0
            san = SANITIZER_RE.search(r["text"])
            if san:
                one["sanitizer"] = (san.group(1) or san.group(2) or "").strip()
            if r["exit"] != 0:
                one["error"] = last_line(r["text"])
            res["workers"][str(w)] = one
            crcs[w] = crc
        last = res["workers"][str(workers[-1])]
        res["status"] = "ok" if all(v.get("exit") == 0 for v in res["workers"].values()) else "failed"
        res["exit"] = next((v["exit"] for v in res["workers"].values() if v.get("exit") != 0), 0)
        res["error"] = next((v.get("error", "") for v in res["workers"].values() if v.get("exit") != 0), "")
        res["parsed"] = all("orders" in v for v in res["workers"].values())
        for k in ("orders", "decisions", "lua_errors", "retail_lua_errors", "final_crc"):
            if k in last:
                res[k] = last[k]
        res["seconds"] = round(sum(v.get("seconds", 0) for v in res["workers"].values()), 1)
        sans = [v["sanitizer"] for v in res["workers"].values() if v.get("sanitizer")]
        if sans:
            res["sanitizer"] = sans[0]
        if len(workers) > 1 and res["status"] == "ok" and all(os.path.isfile(crcs[w]) for w in workers):
            c = run([self.headless, "--compare", crcs[workers[0]], crcs[workers[1]]], os.path.join(d, f"{prefix}{kind}_compare.txt"),
                    300, self.env, False)
            res["deterministic"] = c["exit"] == 0
            if c["exit"] != 0:
                res["compare"] = last_line(c["text"], r"tick|differ|diverg")
            if res["deterministic"] and not self.a.keep_crc:
                for w in workers:
                    os.remove(crcs[w])
        return res

    def galactic(self, exe: str, camp: str | None, args: list[str], s: dict, d: str, prefix: str, timeout: float) -> dict:
        if not camp:
            return {"status": "n/a", "why": "no campaign with planets"}
        cmd = [exe, "--game", self.a.game, "--economy", "--galactic-ai", "--story", "--scoring", "--object-scripts", "--campaign", camp,
               "--ticks", str(self.a.galactic_ticks), "--workers", "4"] + list(s.get("galactic_args", [])) + args
        r = run(cmd, os.path.join(d, f"{prefix}galactic.txt"), timeout, self.env, not self.a.no_slot)
        res: dict = {"campaign": camp, "ticks": self.a.galactic_ticks, "exit": r["exit"], "seconds": r["seconds"]}
        if r["timed_out"]:
            res["status"] = "timeout"
            return res
        mt = GALACTIC_RE.search(r["text"])
        res["parsed"] = bool(mt)
        if mt:
            res.update(decisions=int(mt.group(5)), orders=int(mt.group(6)), battles=int(mt.group(9)), owner_changes=int(mt.group(10)),
                       lua_errors=int(mt.group(12)))
        st = STORY_RE.search(r["text"])
        if st:
            res.update(story_events=int(st.group(1)), story_lua_notes=int(st.group(2)))
        san = SANITIZER_RE.search(r["text"])
        if san:
            res["sanitizer"] = (san.group(1) or san.group(2) or "").strip()
        res["status"] = "ok" if r["exit"] == 0 else "failed"
        if r["exit"] != 0:
            res["error"] = last_line(r["text"])
        return res

    @staticmethod
    def pick_map(rep: dict, kind: str, want: str | None) -> str | None:
        maps = [m for m in (rep.get("content") or {}).get("skirmish_maps", []) if m.get("kind") == kind]
        if want and any(m["map"].upper() == want.upper() for m in maps):
            return want
        # the Official tab first (the mod's own maps, then any), then the Custom tab (same order)
        for pool in ([m for m in maps if not m.get("custom")], [m for m in maps if m.get("custom")]):
            own = [m for m in pool if m.get("in_mod")]
            if own or pool:
                return (own or pool)[0].get("map")
        return None

    @staticmethod
    def pick_campaign(rep: dict, want: str | None) -> str | None:
        startable = (rep.get("campaigns") or {}).get("startable", [])
        if want and want in startable:
            return want
        return startable[0] if startable else None

    def runs(self, rep: dict, args: list[str], s: dict, d: str) -> tuple[dict, dict]:
        space, land = self.pick_map(rep, "space", s.get("space_map")), self.pick_map(rep, "land", s.get("land_map"))
        camp = self.pick_campaign(rep, s.get("campaign"))
        out = {"space": self.skirmish(self.headless, "space", space, args, s, d, (0, 4), "", self.a.timeout),
               "land": self.skirmish(self.headless, "land", land, args, s, d, (0, 4), "", self.a.timeout),
               "galactic": self.galactic(self.headless, camp, args, s, d, "", self.a.timeout)}
        asan = {}
        if self.a.asan:  # t05: the same runs under the ASan build; determinism is the normal runs' job
            env_save = self.env
            self.env = dict(self.env)
            rt = os.path.join(ROOT, "toolchain", "llvm", "lib", "clang", "20", "lib", "windows")
            self.env["PATH"] = rt + os.pathsep + os.path.join(ROOT, "toolchain", "llvm", "bin") + os.pathsep + self.env.get("PATH", "")
            self.env.setdefault("ASAN_OPTIONS", "detect_container_overflow=0")  # MSVC STL false positives (docs/BUILD.md)
            t = self.a.timeout * self.a.asan_slowdown
            asan = {"space": self.skirmish(self.asan_headless, "space", space, args, s, d, (4,), "asan_", t),
                    "land": self.skirmish(self.asan_headless, "land", land, args, s, d, (4,), "asan_", t),
                    "galactic": self.galactic(self.asan_headless, camp, args, s, d, "asan_", t)}
            self.env = env_save
        return out, asan

    # -- iii. smokes
    def smokes(self, args: list[str], s: dict, d: str) -> dict:
        out = {}
        env = dict(self.env)
        env.update({k: str(v) for k, v in (s.get("smoke_env") or {}).items()})
        for kind in ("skirmish", "campaign"):
            png = os.path.join(d, f"smoke_{kind}_png")
            os.makedirs(png, exist_ok=True)  # alamo_game does not create it (every shot fails otherwise)
            cmd = [self.game_exe, "--game", self.a.game, "--smoke", kind, "--hidden", "--audio", "null", "--png-dir", png] + args
            save_env, self.env = self.env, env
            r = run(cmd, os.path.join(d, f"smoke_{kind}.txt"), self.a.timeout, self.env, not self.a.no_slot)
            first_timeout = None
            if r["exit"] != 0 and not r["timed_out"] and STEP_TIMEOUT_RE.search(r["text"]):
                # A step that waits in frames can run out under machine load (another lane's build): retry once and
                # record it, so a timing flake is not taken for a mod gap; a second failure counts.
                first_timeout = STEP_TIMEOUT_RE.search(r["text"]).group(1)
                r = run(cmd, os.path.join(d, f"smoke_{kind}_retry.txt"), self.a.timeout, self.env, not self.a.no_slot)
            self.env = save_env
            res = {"exit": r["exit"], "seconds": r["seconds"]}
            if first_timeout:
                res["retried"] = f"first run: step '{first_timeout}' timed out"
            mt = SMOKE_RE.search(r["text"])
            if mt:
                res.update(steps=int(mt.group(2)), of=int(mt.group(3)))
            if r["timed_out"]:
                res["status"] = "timeout"
            elif r["exit"] == 0 and mt and mt.group(2) == mt.group(3):
                res["status"] = "ok"
            elif r["exit"] == 0 and not mt:  # the game skipped the smoke (no game data, no GPU): nothing was checked
                res["status"] = "skipped"
                res["error"] = last_line(r["text"], r"skip")
            else:
                res["status"] = "failed"
                notes = [l[6:].strip() for l in r["text"].splitlines() if l.startswith("note: ")]  # the smoke's reasons
                why = "; ".join(notes)[:300] if notes else last_line(r["text"])
                res["error"] = (f"step {mt.group(2)} of {mt.group(3)}; " if mt else "") + why
            out["smoke_" + kind] = res
        return out

    # -- one setup
    def setup(self, m: dict, registry: dict, build: str) -> dict:
        key = m["key"]
        d = os.path.join(self.day, setup_file(key))
        os.makedirs(d, exist_ok=True)
        opts = {"v": RUNNER_VERSION, "stages": self.a.stages, "ticks": [self.a.space_ticks, self.a.land_ticks, self.a.galactic_ticks],
                "asan": self.a.asan, "settings": self.corpus.get("mods", {}), "game": self.a.game}
        okey = hashlib.sha1(json.dumps(opts, sort_keys=True).encode()).hexdigest()[:16]
        cache_path = os.path.join(self.cache_dir, f"{setup_file(key)}__{okey}.json")  # accepts are re-applied on a hit
        if not self.a.no_cache and os.path.isfile(cache_path):
            with open(cache_path, encoding="utf-8") as f:
                c = json.load(f)
            if c.get("build") == build and c.get("options") == okey and c.get("hashes") and all(
                    registry.get(k, {}).get("content_hash") == h for k, h in c["hashes"].items()):
                res = c["result"]
                res["cached"] = True
                res["grade"] = gr.grade(res, merged_settings(self.corpus, key, res.get("stack") or [key])["accept"])  # current rules
                log(f"  {key}: cached ({c.get('date', '?')})")
                return res
        t0 = time.monotonic()
        res: dict = {"key": key, "name": m.get("name", key), "stages": list(self.a.stages), "asan_mode": self.a.asan, "cached": False}
        if not m.get("has_data", True):
            res.update(report={"no_data": True}, stack=[key], args=[], fingerprint="", display_name=m.get("name", key))
            res["grade"] = gr.grade(res, [])
            res["seconds"] = 0.0
            log(f"  {key}: no Data folder")
            return res
        summary, rep = self.check(m, d)
        res["report"] = summary
        stack = (rep or {}).get("stack", {}) or {}
        res["stack"] = [sm.get("key") for sm in stack.get("mods", [])] or [key]
        res["args"] = stack.get("args") or ([self.vfs_arg(m)] if self.vfs_arg(m) else [])
        res["fingerprint"] = stack.get("fingerprint", "")
        s = merged_settings(self.corpus, key, res["stack"])
        res["display_name"] = s.get("name") or m.get("name") or key
        log(f"  {key}: check {summary.get('result') or summary.get('tool_error')} ({summary.get('errors', '?')} errors, "
            f"{summary.get('warnings', '?')} warnings, {summary.get('seconds', '?')} s)")
        refused = rep is None or gr.grade({"stages": ["check"], "report": summary}, s["accept"])["grade"] == "G0"
        if "runs" in self.a.stages and not refused:
            res["runs"], asan = self.runs(rep, res["args"], s, d)
            if self.a.asan:
                res["asan"] = asan
            for k, v in res["runs"].items():
                log(f"  {key}: {k} {v.get('status')} " + ", ".join(f"{n} {v[n]}" for n in ("map", "campaign", "orders", "decisions",
                                                                                         "lua_errors", "deterministic", "seconds") if n in v))
        if "smokes" in self.a.stages and not refused:
            res["smokes"] = self.smokes(res["args"], s, d)
            for k, v in res["smokes"].items():
                log(f"  {key}: {k} {v.get('status')} " + (f"{v.get('steps')} / {v.get('of')} steps" if "steps" in v else ""))
        res["grade"] = gr.grade(res, s["accept"])
        res["seconds"] = round(time.monotonic() - t0, 1)
        hashes = {k: registry[k]["content_hash"] for k in res["stack"] if k in registry}
        transient = [v for v in list((res.get("runs") or {}).values()) + list((res.get("smokes") or {}).values())
                     if v.get("status") in ("timeout", "skipped")]
        if hashes and "tool_error" not in summary and not transient:  # a timeout / skip / tool failure is run again next time
            os.makedirs(self.cache_dir, exist_ok=True)
            with open(cache_path, "w", encoding="utf-8") as f:
                json.dump({"build": build, "options": okey, "hashes": hashes, "date": self.a.date, "result": res}, f, indent=1)
        return res


# ------------------------------------------------------------------------------------------------------------ the matrix

def cell_run(v: dict | None) -> str:
    if not v:
        return "-"
    st = v.get("status")
    if st == "n/a":
        return "n/a"
    where = v.get("map") or v.get("campaign") or ""
    if st == "timeout":
        return f"`{where}` timeout"
    if st != "ok":
        return f"`{where}` **{st}** (exit {v.get('exit')})"
    bits = [f"{v.get('orders', '?')} orders", f"{v.get('decisions', '?')} decisions", f"{v.get('lua_errors', 0)} Lua errors"]
    if v.get("retail_lua_errors"):
        bits[-1] += f" ({v['retail_lua_errors']} retail)"
    if "deterministic" in v:
        bits.append("0 = 4 workers" if v["deterministic"] else "**0 != 4 workers**")
    if "story_events" in v:
        bits.append(f"{v['story_events']} story events")
    return f"`{where}`: " + ", ".join(bits)


def cell_smokes(sm: dict | None) -> str:
    if not sm:
        return "-"
    out = []
    for k in ("smoke_skirmish", "smoke_campaign"):
        v = sm.get(k) or {}
        steps = f" {v.get('steps')}/{v.get('of')}" if "steps" in v else ""
        out.append(f"{k[6:]} {v.get('status', '-')}{steps}" + (" (retried)" if v.get("retried") else ""))
    return ", ".join(out)


def md_escape(s: str) -> str:
    return s.replace("|", "\\|").replace("\n", " ")


def matrix_md(matrix: dict) -> str:
    lines = [f"Sweep `{matrix['date']}` (`tools/modcorpus/run_corpus.py`; build `{matrix['build']}` from `{matrix['build_dir']}`, "
             f"stages {', '.join(matrix['stages'])}{', ASan' if matrix.get('asan') else ''}; per-setup results in "
             f"`{matrix.get('out', 'logs/modcorpus/' + matrix['date'])}/`). Grades: `docs/MOD_COMPAT_DESIGN.md` §1.3, rules in `tools/modcorpus/grade.py`.",
             "",
             "| Setup | Stack | Grade | First blocker | Space skirmish | Land skirmish | Galactic | Smokes |",
             "|---|---|---|---|---|---|---|---|"]
    for key in sorted(matrix["mods"], key=lambda k: (matrix["mods"][k].get("display_name", k).lower(), k)):
        e = matrix["mods"][key]
        g = e["grade"]
        first = (g["blocking"] or ["-"])[0]
        if g.get("accepted"):
            first += f" ({len(g['accepted'])} accepted)"
        lines.append("| " + " | ".join(md_escape(x) for x in [
            f"{e.get('display_name', key)} (`{key}`)", " > ".join(e.get("stack", [key])), g["grade"], first,
            cell_run((e.get("runs") or {}).get("space")), cell_run((e.get("runs") or {}).get("land")),
            cell_run((e.get("runs") or {}).get("galactic")), cell_smokes(e.get("smokes"))]) + " |")
    counts = {x: sum(1 for e in matrix["mods"].values() if e["grade"]["grade"] == x) for x in gr.GRADES}
    lines += ["", "Grades: " + ", ".join(f"{k} {v}" for k, v in counts.items()) + f" ({len(matrix['mods'])} setups)."]
    acc = [(k, a) for k, e in sorted(matrix["mods"].items()) for a in e["grade"].get("accepted", [])]
    if acc:
        lines += ["", "Accepted blockers (corpus.json, not counted):"]
        lines += [f"- `{k}` {a['blocker']}: {a['why']} ({a['ref']})" for k, a in acc]
    ni = matrix.get("not_installed") or {}
    if ni:
        lines += ["", "Not installed, so not graded (corpus.json `not_installed`):"]
        lines += [f"- {md_escape(v.get('name', k))} (`{k}`): {md_escape(v.get('why', 'not installed'))}" for k, v in sorted(ni.items())]
    return "\n".join(lines) + "\n"


def write_md(md_path: str, table: str) -> bool:
    with open(md_path, encoding="utf-8", newline="") as f:
        text = f.read()
    i, j = text.find(MD_BEGIN), text.find(MD_END)
    if i < 0 or j < i:
        log(f"run_corpus: {md_path} has no {MD_BEGIN} ... {MD_END} block")
        return False
    new = text[:i + len(MD_BEGIN)] + "\n" + table + text[j:]
    with open(md_path, "w", encoding="utf-8", newline="") as f:
        f.write(new)
    return True


def baseline_path_of(out_root: str, asan: bool) -> str:
    """The accepted grades of one mode: what a sweep is compared with. A sweep without a drop (or --accept-drops) updates
    it, so a drop keeps failing until it is fixed or accepted, and an ASan sweep (graded to G2 at most) never stands in
    for a normal one."""
    return os.path.join(out_root, "baseline-asan.json" if asan else "baseline.json")


def update_baseline(path: str, baseline: dict | None, matrix: dict) -> bool:
    """Merge this sweep's grades into the baseline. Only a sweep with at least the baseline's stages updates it (a quicker
    sweep compares, but its lower cap would hide later drops)."""
    if baseline and not set(baseline.get("stages", [])) <= set(matrix["stages"]):
        return False
    out = {"schema": "alamo.mod_corpus_baseline", "version": 1, "stages": matrix["stages"], "asan": matrix["asan"],
           "mods": dict((baseline or {}).get("mods", {}))}
    for key, e in matrix["mods"].items():
        out["mods"][key] = {"grade": e["grade"]["grade"], "blocking": e["grade"]["blocking"], "date": matrix["date"],
                            "build": e.get("build", matrix["build"])}
    with open(path, "w", encoding="utf-8") as f:
        json.dump(out, f, indent=1)
    return True


# ------------------------------------------------------------------------------------------------------------------ main

def parse_args(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--game", default=os.environ.get("ALAMO_FOC_DATA", ""))
    ap.add_argument("--build-dir", default=None)
    ap.add_argument("--bin", default=None, help="the folder of the exes (default <build-dir>/bin)")
    ap.add_argument("--asan", action="store_true", help="also run the headless runs under the ASan build (t05)")
    ap.add_argument("--asan-build-dir", default=build_trees.main_tree("asan"))
    ap.add_argument("--asan-slowdown", type=float, default=8.0, help="timeout factor of the ASan runs")
    ap.add_argument("--out-root", default=os.path.join(ROOT, "logs", "modcorpus"))
    ap.add_argument("--date", default=datetime.date.today().isoformat())
    ap.add_argument("--only", default="")
    ap.add_argument("--stages", default="check,runs,smokes")
    ap.add_argument("--space-ticks", type=int, default=3000)
    ap.add_argument("--land-ticks", type=int, default=3000)
    ap.add_argument("--galactic-ticks", type=int, default=9000)
    ap.add_argument("--timeout", type=float, default=1800.0)
    ap.add_argument("--no-cache", action="store_true")
    ap.add_argument("--no-slot", action="store_true")
    ap.add_argument("--keep-crc", action="store_true")
    ap.add_argument("--baseline", default=None, help="compare with this matrix / baseline file (it is not updated)")
    ap.add_argument("--accept-drops", action="store_true", help="record this sweep's grades as the baseline even with drops")
    ap.add_argument("--write-md", action="store_true")
    ap.add_argument("--md", default=os.path.join(ROOT, "docs", "MODS.md"))
    ap.add_argument("--corpus", default=os.path.join(HERE, "corpus.json"))
    ap.add_argument("--steam-root", default=None)
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args(argv)
    a.stages = [s for s in gr.ALL_STAGES if s in {x.strip() for x in a.stages.split(",")}]
    if "check" not in a.stages:
        ap.error("--stages must include check")
    if a.build_dir is None:
        a.build_dir = os.path.basename(build_trees.find_tree(ROOT) or build_trees.WARM_TREE)
    if a.game:
        a.game = os.path.abspath(a.game)
    if a.steam_root:
        a.steam_root = os.path.abspath(a.steam_root)
    if a.bin:
        a.bin = os.path.abspath(a.bin)
    for k in ("build_dir", "asan_build_dir", "out_root"):
        v = getattr(a, k)
        if not os.path.isabs(v):
            setattr(a, k, os.path.join(ROOT, v))
    return a


def main(argv=None) -> int:
    a = parse_args(argv)
    if not a.game or not os.path.isdir(a.game):
        log("run_corpus: no game folder (--game or ALAMO_FOC_DATA)")
        return 2
    corpus = {}
    if os.path.isfile(a.corpus):
        with open(a.corpus, encoding="utf-8") as f:
            corpus = json.load(f)
    rn = Runner(a, corpus)
    miss = rn.missing_tools()
    if miss:
        log("run_corpus: missing " + ", ".join(miss) + " (build first: build_agent.cmd msvc all" + (", build_asan.cmd" if a.asan else "") + ")")
        return 2
    os.makedirs(rn.day, exist_ok=True)
    log(f"run_corpus: game {a.game}, build {a.build_dir}, stages {','.join(a.stages)}{', asan' if a.asan else ''}, out {rn.day}")
    mods = rn.discover()
    registry = {m["key"]: m for m in mods}
    only = [x.strip().lower() for x in a.only.split(",") if x.strip()]
    if only:
        mods = [m for m in mods if any(o == m["key"].lower() or o in m.get("name", "").lower() for o in only)]
    log(f"setups ({len(mods)}): " + ", ".join(f"{m['key']} ({m.get('name')})" for m in mods))
    if a.list:
        return 0
    build = rn.build_id()
    baseline_path = a.baseline or baseline_path_of(a.out_root, a.asan)
    baseline = None
    if os.path.isfile(baseline_path):
        with open(baseline_path, encoding="utf-8") as f:
            baseline = json.load(f)
    day_matrix = os.path.join(rn.day, "matrix.json")
    matrix = {"schema": "alamo.mod_corpus", "version": 1, "date": a.date, "build": build, "build_dir": rel(a.bin or a.build_dir),
              "stages": a.stages, "asan": a.asan, "game": a.game,
              "out": rel(rn.day), "mods": {}}
    if only and os.path.isfile(day_matrix):  # a partial rerun keeps the day's other setups (each with its build)
        with open(day_matrix, encoding="utf-8") as f:
            matrix["mods"] = dict(json.load(f).get("mods", {}))
    # mcx8 (DECISIONS "Corpus graded on installed mods only"): corpus.json names corpus mods that are not installed now;
    # they are listed under the matrix, not graded. One that is installed again is graded as usual.
    listed_ni = corpus.get("not_installed", {})
    installed = {k.split(":")[0] for k in registry}
    matrix["not_installed"] = {k: v for k, v in listed_ni.items() if not k.startswith("_") and k not in installed}
    for k in sorted(k for k in listed_ni if k in installed):
        log(f"run_corpus: {k} is listed as not installed in corpus.json but is installed: graded (remove the entry)")
    t0 = time.monotonic()
    for m in mods:
        log(f"{m['key']} ({m.get('name')}):")
        res = rn.setup(m, registry, build)
        with open(os.path.join(rn.day, setup_file(m["key"]) + ".json"), "w", encoding="utf-8") as f:
            json.dump(res, f, indent=1)
        matrix["mods"][m["key"]] = {k: res.get(k) for k in ("display_name", "name", "stack", "args", "fingerprint", "grade", "cached",
                                                             "runs", "smokes", "asan", "seconds")}
        matrix["mods"][m["key"]]["build"] = build  # per entry: a partial rerun keeps the day's older entries with their build
        g = res["grade"]
        log(f"  => {g['grade']}" + (f", first blocker: {g['blocking'][0]}" if g["blocking"] else ""))
    with open(os.path.join(rn.day, "matrix.json"), "w", encoding="utf-8") as f:
        json.dump(matrix, f, indent=1)
    table = matrix_md(matrix)
    with open(os.path.join(rn.day, "matrix.md"), "w", encoding="utf-8", newline="\n") as f:
        f.write(table)
    log("")
    log(table)
    if a.write_md and write_md(a.md, table):
        log(f"wrote the matrix into {a.md}")
    rc = 0
    if baseline:
        cmp_ = gr.compare(baseline, matrix)
        log(f"compared with {baseline_path}: {len(cmp_['drops'])} drop(s), {len(cmp_['raised'])} raised, "
            f"new {cmp_['new'] or '-'}, gone {cmp_['gone'] or '-'}")
        for r in cmp_["raised"]:
            log(f"  raised: {r['key']} {r['before']} -> {r['after']}")
        for dr in cmp_["drops"]:
            name = matrix["mods"][dr["key"]].get("display_name", dr["key"])
            log(f"GRADE DROP {dr['key']} ({name}): {dr['before']} -> {dr['after']}, first new blocker: {dr['first_new_blocker']}")
        rc = 1 if cmp_["drops"] else 0
    else:
        log("no earlier sweep to compare with (first recorded run)")
    if a.baseline is None and (rc == 0 or a.accept_drops):
        if update_baseline(baseline_path, baseline, matrix):
            log(f"baseline {baseline_path} updated" + (" (drops accepted)" if rc else ""))
    elif rc:
        log(f"baseline {baseline_path} kept: fix the drop, or rerun with --accept-drops if the lower grade is right")
    log(f"run_corpus: {len(mods)} setup(s) in {time.monotonic() - t0:.0f} s, matrix {os.path.join(rn.day, 'matrix.json')}, exit {rc}")
    return rc


if __name__ == "__main__":
    sys.exit(main())
