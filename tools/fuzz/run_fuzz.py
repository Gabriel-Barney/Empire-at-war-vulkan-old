"""Run the libFuzzer targets built by build_fuzz.cmd (docs/BUILD.md, "Fuzzing").

  python -I tools/fuzz/run_fuzz.py [--tree build-fuzz] [--seconds 60] [--only fuzz_xml,fuzz_meg] [--jobs 1]
  python -I tools/fuzz/run_fuzz.py --corpus-only     build the seed corpus and stop
  python -I tools/fuzz/run_fuzz.py --selftest

Seed corpus: on the first run (or with --reseed) small files are taken from the retail MEGs under ALAMO_FOC_DATA /
ALAMO_EAW_DATA (tools\\test_env.cmd sets them): XML from config.meg, ALO / ALA from models.meg, DDS / TGA from
textures.meg, FXO from shaders.meg, and a few tiny v1 archives written from those XML files for fuzz_meg. Without game data the fuzzers start
from an empty corpus (they still work, just slower). The corpus lives in <tree>/fuzz/corpus/<target>/ and grows across
runs; crashes, leaks, timeouts and out-of-memory inputs land in <tree>/fuzz/artifacts/<target>/ (crash-*, leak-*,
timeout-*, oom-*). Reproduce one with: <tree>\\bin\\<target>.exe <artifact file>.

Exit 0 when no target wrote a new artifact, 1 when one did, 2 on a usage or setup error. Standard library only.
"""
import argparse
import fnmatch
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TARGETS = ["fuzz_xml", "fuzz_meg", "fuzz_alo", "fuzz_ala", "fuzz_texture", "fuzz_fx2",
           # mcx2 t08: the smoke targets (smoke/fuzz_targets.hpp); alamo_fuzz_smoke runs the same readers in every tree
           "fuzz_ted", "fuzz_dat", "fuzz_mtd", "fuzz_rc", "fuzz_clip", "fuzz_particle", "fuzz_lc", "fuzz_wav", "fuzz_bink"]

# target -> (meg file name, globs, max seed bytes, max seed count)
SEEDS = {
    "fuzz_xml": ("config.meg", ["*.XML"], 64 * 1024, 300),
    "fuzz_alo": ("models.meg", ["*.ALO"], 256 * 1024, 200),
    "fuzz_ala": ("models.meg", ["*.ALA"], 128 * 1024, 200),
    "fuzz_texture": ("textures.meg", ["*.DDS", "*.TGA", "*.BMP"], 64 * 1024, 150),
    "fuzz_fx2": ("shaders.meg", ["*.FXO"], 128 * 1024, 150),
    "fuzz_ted": ("maps.meg", ["*.TED"], 256 * 1024, 60),
    "fuzz_clip": ("models.meg", ["*.ALA"], 128 * 1024, 200),
    "fuzz_particle": ("models.meg", ["P_*.ALO"], 128 * 1024, 150),
    "fuzz_mtd": ("textures.meg", ["*.MTD"], 256 * 1024, 10),
    "fuzz_rc": ("config.meg", ["*.RC"], 256 * 1024, 10),
}


def load_megtool():
    sys.path.insert(0, os.path.join(ROOT, "tools", "meg"))
    import megtool  # noqa: E402  (tools/meg/megtool.py, the reference reader)
    return megtool


def data_dirs():
    out = []
    for var in ("ALAMO_FOC_DATA", "ALAMO_EAW_DATA"):
        v = os.environ.get(var)
        if v and os.path.isdir(os.path.join(v, "Data")):
            out.append(os.path.join(v, "Data"))
    return out


def find_meg(name):
    for d in data_dirs():
        for f in os.listdir(d):
            if f.lower() == name.lower():
                return os.path.join(d, f)
    return None


def pick(entries, globs, max_bytes, max_count):
    """Smallest-first, but spread over sizes: every k-th entry of the size-sorted list that fits."""
    fit = [e for e in entries if 0 < e.size <= max_bytes and any(fnmatch.fnmatchcase(e.name.upper(), g) for g in globs)]
    fit.sort(key=lambda e: (e.size, e.name))
    if len(fit) <= max_count:
        return fit
    step = len(fit) / max_count
    return [fit[int(i * step)] for i in range(max_count)]


def write_seed(dir_, name, data):
    safe = name.replace("\\", "_").replace("/", "_")
    with open(os.path.join(dir_, safe), "wb") as f:
        f.write(data)


def build_corpus(fuzz_dir, targets, log):
    megtool = None
    archives = {}
    xml_seeds = []
    for t in targets:
        cdir = os.path.join(fuzz_dir, "corpus", t)
        os.makedirs(cdir, exist_ok=True)
        if os.listdir(cdir):
            continue
        if t == "fuzz_meg":
            continue  # after the XML seeds, below
        meg_name, globs, max_bytes, max_count = SEEDS[t]
        path = find_meg(meg_name)
        if path is None:
            log(f"corpus {t}: no {meg_name} under ALAMO_FOC_DATA / ALAMO_EAW_DATA, starting empty")
            continue
        megtool = megtool or load_megtool()
        if path not in archives:
            archives[path] = megtool.MegArchive(path)
        m = archives[path]
        chosen = pick(m.entries, globs, max_bytes, max_count)
        for e in chosen:
            data = m.read(e)
            write_seed(cdir, e.name, data)
            if t == "fuzz_xml":
                xml_seeds.append((e.name, data))
        log(f"corpus {t}: {len(chosen)} seeds from {os.path.basename(path)}")
    if "fuzz_meg" in targets:
        cdir = os.path.join(fuzz_dir, "corpus", "fuzz_meg")
        os.makedirs(cdir, exist_ok=True)
        if not os.listdir(cdir):
            megtool = megtool or load_megtool()
            if not xml_seeds:
                xml_dir = os.path.join(fuzz_dir, "corpus", "fuzz_xml")
                if os.path.isdir(xml_dir):
                    for f in sorted(os.listdir(xml_dir))[:12]:
                        with open(os.path.join(xml_dir, f), "rb") as fh:
                            xml_seeds.append(("DATA\\XML\\" + f, fh.read()))
            small = sorted(xml_seeds, key=lambda s: len(s[1]))[:12]
            groups = [small[i:i + n] for n, i in ((1, 0), (2, 1), (3, 3), (6, 6))] if small else [[]]
            for i, g in enumerate(groups):
                megtool.write_v1(os.path.join(cdir, f"seed_{i}.meg"), [(n, d[:2048]) for n, d in g])
            log(f"corpus fuzz_meg: {len(groups)} v1 archives written")


def runtime_env():
    """The clang 20 ASan runtime is a DLL (clang_rt.asan_dynamic-x86_64.dll) next to the fuzz libs: put it on PATH."""
    env = dict(os.environ)
    rt = os.path.join(ROOT, "toolchain", "llvm", "lib", "clang", "20", "lib", "windows")
    env["PATH"] = rt + os.pathsep + env.get("PATH", "")
    # detect_container_overflow=0: with the MSVC STL, clang ASan reports container-overflow inside plain std::string
    # concatenation (seen 2026-11-15 in error-message paths of the XML, ALO and DDS readers, even on an empty input),
    # a false positive from STL annotations that do not match across instrumented and prebuilt code. Real heap overflows
    # are still reported as heap-buffer-overflow.
    opts = env.get("ASAN_OPTIONS", "")
    if "detect_container_overflow" not in opts:
        env["ASAN_OPTIONS"] = (opts + ":" if opts else "") + "detect_container_overflow=0"
    return env


def artifacts(dir_):
    return set(os.listdir(dir_)) if os.path.isdir(dir_) else set()


def run_target(tree, fuzz_dir, t, seconds, jobs, log):
    exe = os.path.join(tree, "bin", t + ".exe")
    if not os.path.isfile(exe):
        log(f"{t}: {exe} not built, skipped")
        return None
    cdir = os.path.join(fuzz_dir, "corpus", t)
    adir = os.path.join(fuzz_dir, "artifacts", t)
    os.makedirs(adir, exist_ok=True)
    before = artifacts(adir)
    args = [exe, cdir, f"-artifact_prefix={adir}{os.sep}", f"-max_total_time={seconds}", "-timeout=10",
            "-rss_limit_mb=2048", "-max_len=262144", "-print_final_stats=1"]
    if jobs > 1:
        args += [f"-jobs={jobs}", f"-workers={jobs}"]
    logf = os.path.join(fuzz_dir, f"{t}.log")
    t0 = time.time()
    with open(logf, "w", encoding="utf-8", errors="replace") as fh:
        rc = subprocess.call(args, stdout=fh, stderr=subprocess.STDOUT, cwd=fuzz_dir, env=runtime_env())
    new = sorted(artifacts(adir) - before)
    if rc != 0 and not new:
        # Did not start or died without an artifact (0xC0000135 = a DLL is missing): a setup failure, not a clean run.
        new = [f"<exit {rc:#x} without an artifact, see the log>"]
    stats = {}
    with open(logf, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if line.startswith("stat::"):
                k, _, v = line[6:].partition(":")
                stats[k.strip()] = v.strip()
    log(f"{t}: exit {rc}, {time.time() - t0:.0f} s, runs {stats.get('number_of_executed_units', '?')}, "
        f"corpus {len(os.listdir(cdir))}, new artifacts {len(new)}" + (f" -> {', '.join(new)}" if new else "")
        + f" (log {logf})")
    return new


def selftest():
    class E:
        def __init__(self, name, size):
            self.name, self.size = name, size
    es = [E(f"A{i}.XML", i) for i in range(1, 101)] + [E("B.ALO", 5), E("BIG.XML", 10**9)]
    got = pick(es, ["*.XML"], 1000, 10)
    assert len(got) == 10 and all(e.name.endswith(".XML") and e.size <= 1000 for e in got), got
    assert got[0].size == 1 and got[-1].size > 80
    assert len(pick(es, ["*.ALO"], 1000, 10)) == 1
    print("run_fuzz selftest OK")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tree", default="build-fuzz")
    ap.add_argument("--seconds", type=int, default=60, help="time budget per target")
    ap.add_argument("--only", default="", help="comma-separated targets (default: all)")
    ap.add_argument("--jobs", type=int, default=1, help="parallel libFuzzer workers per target")
    ap.add_argument("--reseed", action="store_true", help="delete each target's corpus and rebuild the seeds")
    ap.add_argument("--corpus-only", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    targets = [t for t in (a.only.split(",") if a.only else TARGETS) if t]
    bad = [t for t in targets if t not in TARGETS]
    if bad:
        print(f"unknown target(s): {', '.join(bad)}; known: {', '.join(TARGETS)}", file=sys.stderr)
        return 2
    tree = a.tree if os.path.isabs(a.tree) else os.path.join(ROOT, a.tree)
    fuzz_dir = os.path.join(tree, "fuzz")
    os.makedirs(fuzz_dir, exist_ok=True)
    if a.reseed:
        import shutil
        for t in targets:
            shutil.rmtree(os.path.join(fuzz_dir, "corpus", t), ignore_errors=True)

    def log(msg):
        print(f"run_fuzz: {msg}", flush=True)

    build_corpus(fuzz_dir, targets, log)
    if a.corpus_only:
        return 0
    found = False
    for t in targets:
        new = run_target(tree, fuzz_dir, t, a.seconds, a.jobs, log)
        found = found or bool(new)
    log("FOUND new artifacts: reproduce with <tree>\\bin\\<target>.exe <file>" if found else "no new artifacts")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
