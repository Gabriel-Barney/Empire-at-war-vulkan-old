#!/usr/bin/env python3
"""Benchmark scenarios as build products (wr1 t06, "Goldens as computed diffs").

A benchmark scenario (`tests/benchmark/<name>.abench`) is defined by its recipe: the manifest's `ALAMO_BENCH`, `name`,
`description`, `make` line and run settings (`ticks`, `workers`, `fingerprint_every`, and for a rendered `host game` scene
`mode`, `preset`, `frames_per_tick`, `camera...`). Its save (`.alsv`), orders (`.acmd`), fingerprint (`.crc`) and the
manifest's `version`, `save`, `orders`, `fingerprint`, `start_tick` and `final_crc` lines are produced from that recipe by
the build (`alamo_headless --bench-regen`, or `alamo_game --hidden --bench-regen` for `host game`), so they are derived.

`materialize` makes a scenario from its recipe for this checkout's build and keeps it under the main checkout's
`.cache/scenario-results/bench/<key>/` (the scenario-result cache: same key parts as tools/goldens/scenario_results.py,
with the recipe as the scenario definition), then copies it to `--out` when given (the ctest fixtures copy into the build
tree). A sim behaviour change therefore never leaves a scenario stale; timing results of two commits with different keys
are not comparable, which tools/benchmark/compare_results.py reports from the result JSONs' build commits.

  python -I tools/goldens/bench_scenarios.py list
  python -I tools/goldens/bench_scenarios.py materialize NAME [--build DIR] [--compiler msvc] [--out DIR]
  python -I tools/goldens/bench_scenarios.py recipe NAME          prints the recipe (the lines that stay in git after t07)
  python -I tools/goldens/bench_scenarios.py --selftest

`materialize` prints the materialized manifest's path; exit 0 also when the game data is missing (the manifest is then
not made and the path printed is empty, so a data-driven test skips). Standard library only.
"""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import scenario_results as sr  # noqa: E402

ROOT = sr.ROOT
BENCH_DIR = "tests/benchmark"
DERIVED_KEYS = ("version", "save", "orders", "fingerprint", "start_tick", "final_crc")
EXTS = (".abench", ".alsv", ".acmd", ".crc")


def recipe_text(manifest_text: str) -> str:
    """The manifest without its derived lines (the scenario definition)."""
    keep = [l for l in manifest_text.splitlines() if l.strip() and l.split(None, 1)[0] not in DERIVED_KEYS]
    return "\n".join(keep) + "\n"


def host_of(text: str) -> str:
    for line in text.splitlines():
        if line.startswith("host "):
            return line.split()[1]
    return "headless"


def scenario_names(src: str = ROOT) -> list[str]:
    d = os.path.join(src, *BENCH_DIR.split("/"))
    return sorted(n[:-7] for n in os.listdir(d) if n.endswith(".abench")) if os.path.isdir(d) else []


def read_recipe(name: str, src: str = ROOT) -> str:
    with open(os.path.join(src, *BENCH_DIR.split("/"), name + ".abench"), encoding="utf-8") as f:
        return recipe_text(f.read())


def bench_key(source: str, name: str, recipe: str, data: dict, compiler: str) -> str:
    doc = {"v": 1, "kind": "bench", "source": source, "name": name, "recipe": recipe, "data": data, "compiler": compiler}
    return hashlib.sha256(json.dumps(doc, sort_keys=True).encode()).hexdigest()


def complete(folder: str, name: str) -> bool:
    return all(os.path.isfile(os.path.join(folder, name + e)) for e in EXTS)


def make(name: str, recipe: str, build_dir: str, folder: str, env: dict) -> tuple[int, str]:
    """Write the recipe into folder and remake the scenario there. Returns (exit code, output)."""
    os.makedirs(folder, exist_ok=True)
    manifest = os.path.join(folder, name + ".abench")
    with open(manifest, "w", encoding="utf-8", newline="\n") as f:
        f.write(recipe)
    if host_of(recipe) == "game":
        cmd = [os.path.join(build_dir, "bin", "alamo_game.exe"), "--hidden", "--bench-regen", manifest]
    else:
        cmd = [os.path.join(build_dir, "bin", "alamo_headless.exe"), "--bench-regen", manifest, "--quiet"]
    r = subprocess.run(cmd, cwd=folder, env=env, capture_output=True, text=True, errors="replace")
    return r.returncode, (r.stdout + r.stderr)


def materialize(name: str, build_dir: str, compiler: str = "msvc", out: str | None = None, checkout: str = ROOT,
                cache: sr.Cache | None = None, env: dict | None = None, maker=make) -> str:
    """The scenario's materialized manifest path ('' when the game data is missing). Cached only from a clean checkout."""
    env = sr.test_env() if env is None else env
    if not env.get("ALAMO_FOC_DATA"):
        return ""
    cache = cache or sr.Cache.default(checkout)
    recipe = read_recipe(name, checkout)
    head = sr.git(checkout, "rev-parse", "HEAD").strip()
    dirty = sr.source_dirty(checkout)
    key = bench_key(sr.source_key(checkout, head), name, recipe, sr.data_identity(env), compiler)
    folder = os.path.join(cache.folder, "bench", key[:2], key)
    if dirty or not complete(folder, name):
        target = tempfile.mkdtemp(prefix=f"bench_{name}_") if dirty else folder + ".tmp"
        if os.path.isdir(target) and not dirty:
            shutil.rmtree(target, ignore_errors=True)
        rc, text = maker(name, recipe, build_dir, target, env)
        if rc != 0 or not complete(target, name):
            raise RuntimeError(f"{name}: --bench-regen exit {rc}:\n{text[-800:]}")
        if dirty:
            folder = target  # never cached: the build has edits no commit has
        else:
            if os.path.isdir(folder):
                shutil.rmtree(folder, ignore_errors=True)
            os.replace(target, folder)
            with open(os.path.join(folder, "meta.json"), "w", encoding="utf-8") as f:
                json.dump({"commit": head, "bench": name, "compiler": compiler, "key": key}, f, indent=1)
    else:
        try:
            os.utime(os.path.join(folder, "meta.json"))
        except OSError:
            pass
    if out:
        os.makedirs(out, exist_ok=True)
        for e in EXTS:
            shutil.copyfile(os.path.join(folder, name + e), os.path.join(out, name + e))
        return os.path.join(out, name + ".abench")
    return os.path.join(folder, name + ".abench")


# ---------------------------------------------------------------- selftest

def selftest() -> int:
    bad = []

    def check(c, what):
        if not c:
            bad.append(what)

    full = ("ALAMO_BENCH 1\nname s\nversion 76\ndescription d\nmake --save-session skirmish --ticks 10\n"
            "save s.alsv 1 aa\norders s.acmd 2 bb\nfingerprint s.crc 3 cc\nfingerprint_every 16\nstart_tick 300\n"
            "ticks 10\nworkers auto\nfinal_crc 00\n")
    r = recipe_text(full)
    check(r == "ALAMO_BENCH 1\nname s\ndescription d\nmake --save-session skirmish --ticks 10\nfingerprint_every 16\n"
              "ticks 10\nworkers auto\n", f"recipe keeps only the definition: {r!r}")
    check(recipe_text(full.replace("version 76", "version 77").replace("final_crc 00", "final_crc 11")) == r,
          "a remade scenario (new version, new pins) has the same recipe")
    check(host_of(r) == "headless" and host_of(r + "host game\n") == "game", "host line")
    k = bench_key("src", "s", r, {"a": 1}, "msvc")
    check(k != bench_key("src2", "s", r, {"a": 1}, "msvc") and k != bench_key("src", "s", r + "x\n", {"a": 1}, "msvc")
          and k != bench_key("src", "s", r, {"a": 2}, "msvc"), "the key follows source, recipe and data")

    with tempfile.TemporaryDirectory() as tmp:
        repo = os.path.join(tmp, "repo")
        os.makedirs(os.path.join(repo, *BENCH_DIR.split("/")))
        envg = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@t", GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@t")

        def g(*a):
            subprocess.run(["git", "-C", repo, *a], check=True, capture_output=True, env=envg)

        with open(os.path.join(repo, BENCH_DIR, "s.abench"), "w", newline="\n") as f:
            f.write(full)
        g("init", "-q", "-b", "dev"); g("add", "-A"); g("commit", "-q", "-m", "c")
        made = []

        def fake(name, recipe, build_dir, folder, env):
            made.append(folder)
            os.makedirs(folder, exist_ok=True)
            for e in EXTS:
                with open(os.path.join(folder, name + e), "w") as f:
                    f.write(recipe if e == ".abench" else e)
            return 0, ""

        cache = sr.Cache(os.path.join(tmp, "cache"), 1 << 30)
        data = os.path.join(tmp, "foc")
        os.makedirs(os.path.join(data, "Data"))
        env = {"ALAMO_FOC_DATA": data}
        out = os.path.join(tmp, "out")
        p1 = materialize("s", "b", out=out, checkout=repo, cache=cache, env=env, maker=fake)
        p2 = materialize("s", "b", out=out, checkout=repo, cache=cache, env=env, maker=fake)
        check(len(made) == 1 and p1 == p2 == os.path.join(out, "s.abench") and complete(out, "s"),
              f"second materialize is a cache hit and the copy is complete ({len(made)} makes)")
        check(materialize("s", "b", checkout=repo, cache=cache, env={}, maker=fake) == "", "no game data: empty path")
        with open(os.path.join(repo, BENCH_DIR, "s.abench"), "a") as f:
            f.write("camera_anchor start\n")
        p3 = materialize("s", "b", checkout=repo, cache=cache, env=env, maker=fake)
        check(len(made) == 2 and "bench_s_" in p3, "a dirty recipe is made again and not cached")

    for b in bad:
        print("bench_scenarios selftest FAIL: " + b)
    if bad:
        return 1
    print("bench_scenarios selftest: ok")
    return 0


def main(argv: list[str]) -> int:
    import argparse
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("command", nargs="?", choices=("list", "materialize", "recipe"))
    ap.add_argument("name", nargs="?")
    ap.add_argument("--build")
    ap.add_argument("--compiler", default="msvc")
    ap.add_argument("--out")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest()
    if a.command == "list" or not a.command:
        for n in scenario_names():
            print(f"{n:<20} host {host_of(read_recipe(n))}")
        return 0
    if not a.name:
        ap.error("a scenario name is needed")
    if a.command == "recipe":
        sys.stdout.write(read_recipe(a.name))
        return 0
    try:
        print(materialize(a.name, a.build or sr.default_build(a.compiler), a.compiler, a.out))
    except RuntimeError as e:
        print(f"bench_scenarios: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
