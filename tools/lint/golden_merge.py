"""Golden-only merge conflicts in tools\\land_on_dev.cmd (2026-11-13).

When `git merge origin/dev` stops on conflicts and every conflicted file is a golden that tools\\regen_goldens.cmd can
write, the documented manual fix is "take dev's, then regenerate". land_on_dev.cmd does it with this script:
  resolve   after the failed merge: takes origin/dev's version of each conflicted golden, commits the merge and saves the
            list in the git dir (alamo_golden_merge.txt). Exit 1, merge left as it is, when any conflicted file is not
            such a golden (or the checkout fails): resolve those by hand as before.
  regen     after land_on_dev built the merged HEAD (build_agent.cmd msvc wt all none): runs regen_goldens for the listed
            goldens only, puts back any other golden it rewrote (the land check's fast tier then shows that drift as a
            failure, for a person to look at) and commits the listed ones. Exit 1 when regen_goldens failed.
  slow [BASE [HEAD]] [--dry-run]
            ref6 t06, after land_on_dev's fast-tier check: the slow-labelled ctest entries of the warm MSVC tree whose
            command names a golden the branch changed against BASE (default origin/dev; any file under a testdata/,
            goldens/ or benchmark/ folder, matched as the path or its stem) run through build_agent.cmd (fast tier only
            otherwise: a game smoke golden or a slow headless golden the branch moved is checked before it lands). Exit 1
            when one fails. It also prints a line when the nightly dev gate (tools\\gate_scheduled.cmd: every test on the
            tip of origin/dev, slow goldens included - the daily dev-tip record) has not written logs\\gate_history.txt in
            26 hours. --dry-run prints the ctest regex instead of running it.
Goldens it handles: engine/apps/headless/testdata/*.crc, engine/apps/game/testdata/game_smoke_*.crc / .rec and
tests/benchmark/* (scenario files). Goldens a test exe owns (--regen / --write-golden) stay manual.
Selftest: golden_merge.py --selftest
"""
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_trees  # noqa: E402
LIST = 'alamo_golden_merge.txt'
HEADLESS = re.compile(r'^engine/apps/headless/testdata/[^/]+\.crc$')
SMOKE = re.compile(r'^engine/apps/game/testdata/game_smoke_[a-z]+\.(?:crc|rec)$')
BENCH = re.compile(r'^tests/benchmark/[^/]+\.(?:abench|alsv|acmd|crc)$')


def kind(path):
    p = path.replace('\\', '/')
    return 'headless' if HEADLESS.match(p) else 'game' if SMOKE.match(p) else 'bench' if BENCH.match(p) else None


def git(cwd, *args):
    p = subprocess.run(['git', *args], cwd=cwd, capture_output=True, text=True)
    return p.returncode, p.stdout.strip(), p.stderr.strip()


def list_path(cwd) -> Path:
    _, p, _ = git(cwd, 'rev-parse', '--git-path', LIST)
    return Path(cwd) / p


def cmd_resolve(cwd) -> int:
    _, out, _ = git(cwd, 'diff', '--name-only', '--diff-filter=U')
    files = [f for f in out.splitlines() if f]
    if not files:
        print('golden_merge: no conflicted files')
        return 1
    other = [f for f in files if not kind(f)]
    if other:
        print('golden_merge: conflicts outside the goldens regen_goldens writes, resolve by hand:')
        for f in other:
            print('  ' + f)
        return 1
    for f in files:
        rc, _, err = git(cwd, 'checkout', '--theirs', '--', f)
        if rc:
            print(f'golden_merge: cannot take origin/dev\'s {f} ({err}); resolve by hand')
            return 1
        git(cwd, 'add', '--', f)
    rc, _, err = git(cwd, 'commit', '--no-edit')
    if rc:
        print(f'golden_merge: merge commit failed: {err}')
        return 1
    list_path(cwd).write_text(''.join(f + '\n' for f in files), encoding='utf8')
    print(f'golden_merge: took origin/dev\'s version of {len(files)} conflicted golden(s) and committed the merge:')
    for f in files:
        print('  ' + f)
    return 0


def regen_runs(files):
    """regen_goldens.py argument lists that cover the listed goldens."""
    kinds = {kind(f) for f in files}
    runs = []
    if 'headless' in kinds or 'bench' in kinds:
        args = ['--only', 'headless' if 'headless' in kinds else 'bench']
        if 'headless' in kinds:
            names = sorted(Path(f).name for f in files if kind(f) == 'headless')
            args += ['--match', '^(' + '|'.join(re.escape(n) for n in names) + ')$']
            if 'bench' not in kinds:
                args.append('--no-bench')
        runs.append(args)
    if 'game' in kinds:
        runs.append(['--only', 'game'])
    return runs


def cmd_regen(cwd, tree) -> int:
    lp = list_path(cwd)
    if not lp.is_file():
        print('golden_merge: no golden merge pending')
        return 0
    files = [f for f in lp.read_text(encoding='utf8').splitlines() if f]
    _, dirty, _ = git(cwd, 'status', '--porcelain', '-uno')
    if dirty:  # everything regen_goldens changes is put back or committed below, so start from a clean tree
        print('golden_merge: tracked files have uncommitted edits; commit them first:\n' + dirty)
        return 1
    script = ROOT / 'tools' / 'regen_goldens.cmd'
    failed = False
    for args in regen_runs(files):
        # --force: the second run sees the first run's goldens as uncommitted edits
        full = ['--build-dir', tree, '--force', *args]
        line = ' '.join(f'"{a}"' for a in full)
        print(f'golden_merge: regen_goldens {line}', flush=True)
        r = subprocess.run(f'cmd.exe /d /s /c ""{script}" {line}"', cwd=cwd)
        failed |= r.returncode != 0
    _, out, _ = git(cwd, 'diff', '--name-only')
    changed = [f for f in out.splitlines() if f]
    extra = [f for f in changed if f not in files]
    if extra:
        git(cwd, 'checkout', '--', *extra)
        print('golden_merge: put back goldens outside the conflict that regen_goldens also changed (the merged code changes '
              'them: the land check will fail on them; look before regenerating):')
        for f in extra:
            print('  ' + f)
    mine = [f for f in changed if f in files]
    if failed:
        print('golden_merge: regen_goldens failed (above); nothing committed. Fix it, then regenerate and commit by hand.')
        git(cwd, 'checkout', '--', *mine) if mine else None
        return 1
    if mine:
        git(cwd, 'add', '--', *mine)
        msg = ('Regenerate goldens after merging origin/dev (land_on_dev: golden-only merge conflict)\n\n'
               + ''.join(f'- {f}\n' for f in mine))
        rc, _, err = git(cwd, 'commit', '-m', msg)
        if rc:
            print(f'golden_merge: commit failed: {err}')
            return 1
        print(f'golden_merge: committed {len(mine)} regenerated golden(s)')
    else:
        print('golden_merge: origin/dev\'s goldens already match the merged code; nothing to commit')
    lp.unlink()
    return 0


GOLDEN_DIRS = re.compile(r'(^|/)(testdata|goldens|benchmark)/')


def changed_goldens(cwd, base, head):
    """Golden-like files the branch changed against `base`: anything under a testdata/, goldens/ or benchmark/ folder."""
    _, out, _ = git(cwd, 'diff', '--name-only', f'{base}...{head}')
    return [f for f in out.splitlines() if f and GOLDEN_DIRS.search(f.replace('\\', '/'))]


def slow_tests_for(tests, files, root):
    """Names of the slow-labelled ctest entries (ctest --show-only=json-v1 'tests') whose command names one of `files`
    (repo-relative): the file's path, or its path without the extension (the smoke golden checks pass a stem)."""
    def norm(p):
        return os.path.normcase(os.path.abspath(p)).replace('\\', '/')
    keys = set()
    for f in files:
        p = norm(os.path.join(str(root), f))
        keys.add(p)
        keys.add(os.path.splitext(p)[0])
        if '/benchmark/' in p:  # a scenario's .crc / .alsv / .acmd: its checks name the .abench
            keys.add(os.path.splitext(p)[0] + '.abench')
    out = []
    for t in tests:
        labels = []
        for pr in t.get('properties', []):
            if pr.get('name') == 'LABELS':
                labels = pr.get('value') or []
        if 'slow' not in labels:
            continue
        hit = False
        for a in t.get('command') or []:
            for part in re.split(r'[=;]', a):
                if part and ('/' in part or '\\' in part) and norm(part) in keys:
                    hit = True
        if hit:
            out.append(t['name'])
    return sorted(set(out))


def cmd_slow(cwd, base, head, tree, dry) -> int:
    """ref6 t06: run the slow golden checks of the goldens the branch changed (the landing check's fast tier skips them)."""
    files = changed_goldens(cwd, base, head)
    if not files:
        print('golden_merge slow: the branch changes no golden; no slow golden check to run')
        return 0
    tdir = Path(cwd) / tree
    script = Path(cwd) / 'build_agent.cmd'
    if not dry:
        # configure and build the warm tree first: it may be missing (the land check also accepts a build-msvc stamp) or
        # configured before this branch added a slow golden test
        print('golden_merge slow: building the merged HEAD in the warm tree (no tests) to list its slow golden checks', flush=True)
        b = subprocess.run(f'cmd.exe /d /s /c ""{script}" msvc wt all none"', cwd=cwd)
        if b.returncode != 0:
            print('golden_merge slow: the warm tree build failed (above)')
            return 1
    import shutil
    ctest = shutil.which('ctest') or str(ROOT / 'toolchain' / 'venv' / 'Scripts' / 'ctest.exe')
    r = subprocess.run([ctest, '--test-dir', str(tdir), '--show-only=json-v1'], cwd=cwd, capture_output=True, text=True)
    if r.returncode != 0:
        print(f'golden_merge slow: no test list in {tdir} ({(r.stderr or r.stdout).strip()}); build it: build_agent.cmd msvc wt all')
        return 1
    import json
    names = slow_tests_for(json.loads(r.stdout).get('tests', []), files, cwd)
    if not names:
        print(f'golden_merge slow: {len(files)} golden file(s) changed, none checked by a slow test')
        return 0
    regex = '^(' + '|'.join(names) + ')$'  # anchored: a name must not pull in its _w* / _land* siblings
    print(f'golden_merge slow: the branch changed {len(files)} golden file(s); slow checks: {" ".join(names)}', flush=True)
    if dry:
        print(regex)
        return 0
    p = subprocess.run(f'cmd.exe /d /s /c ""{script}" msvc wt all "{regex}""', cwd=cwd)
    return 0 if p.returncode == 0 else 1


def gate_age_note(main_root):
    """The nightly dev gate (tools\\gate_scheduled.cmd: build_gate.cmd on origin/dev, every test, slow goldens included) is
    the daily dev-tip record; a line when its history has no run in the last 26 hours."""
    import time
    hist = Path(main_root) / 'logs' / 'gate_history.txt'
    if not hist.is_file():
        return 'golden_merge: no nightly dev gate history (logs\\gate_history.txt): slow goldens on dev are unchecked; ' \
               'tools\\install_gate_task.cmd registers it'
    age_h = (time.time() - hist.stat().st_mtime) / 3600.0
    if age_h > 26.0:
        return (f'golden_merge: the nightly dev gate (all tests, slow goldens included) last wrote its history '
                f'{age_h / 24.0:.1f} days ago: slow goldens on dev may be stale (schtasks /Query /TN AlamoGateNightly)')
    return ''


def selftest() -> int:
    bad = []

    def check(c, msg):
        if not c:
            bad.append(msg)

    check(kind('engine/apps/headless/testdata/skirmish_ai_foc_land_golden.crc') == 'headless', 'headless kind')
    check(kind('engine/apps/headless/testdata/galactic_ai_hotm_golden.txt') is None, 'AI .txt golden is manual')
    check(kind('engine/apps/game/testdata/game_smoke_land.rec') == 'game', 'smoke kind')
    check(kind('tests/benchmark/land_battle.alsv') == 'bench', 'bench kind')
    check(kind('engine/ai/tests/golden/ai_golden.txt') is None, 'test-owned golden is manual')
    runs = regen_runs(['engine/apps/headless/testdata/a.b.crc', 'engine/apps/game/testdata/game_smoke_land.crc'])
    check(runs == [['--only', 'headless', '--match', r'^(a\.b\.crc)$', '--no-bench'], ['--only', 'game']], f'runs {runs}')
    check(regen_runs(['tests/benchmark/x.crc']) == [['--only', 'bench']], 'bench-only run')
    # ref6 t06: slow checks of changed goldens (a --golden path, a -DGOLDEN= stem, fast tests and other goldens left out)
    root = ROOT
    gd = (root / 'engine/apps/game/testdata').as_posix()
    hd = (root / 'engine/apps/headless/testdata').as_posix()
    tests = [
        {'name': 'smoke_golden', 'command': ['cmake', f'-DGOLDEN={gd}/game_smoke_land', '-P', 'x.cmake'],
         'properties': [{'name': 'LABELS', 'value': ['slow']}]},
        {'name': 'smoke_other', 'command': ['cmake', f'-DGOLDEN={gd}/game_smoke_campaign'],
         'properties': [{'name': 'LABELS', 'value': ['slow']}]},
        {'name': 'headless_slow', 'command': ['alamo_headless.exe', '--golden', f'{hd}/land_golden.crc'],
         'properties': [{'name': 'LABELS', 'value': ['slow']}]},
        {'name': 'headless_fast', 'command': ['alamo_headless.exe', '--golden', f'{hd}/land_golden.crc'], 'properties': []},
    ]
    got = slow_tests_for(tests, ['engine/apps/game/testdata/game_smoke_land.crc',
                                 'engine/apps/headless/testdata/land_golden.crc'], root)
    check(got == ['headless_slow', 'smoke_golden'], f'slow tests {got}')
    check(slow_tests_for(tests, ['engine/apps/sim/src/x.cpp'], root) == [], 'no golden, no slow test')
    bd = (root / 'tests/benchmark').as_posix()
    bench = [{'name': 'bench_land', 'command': ['alamo_headless.exe', '--bench', f'{bd}/land_battle.abench'],
              'properties': [{'name': 'LABELS', 'value': ['slow']}]}]
    check(slow_tests_for(bench, ['tests/benchmark/land_battle.crc'], root) == ['bench_land'], 'bench stem')
    check(bool(GOLDEN_DIRS.search('engine/apps/game/testdata/game_smoke_land.rec')) and
          bool(GOLDEN_DIRS.search('tests/goldens/mods/tr_space.crc')) and not GOLDEN_DIRS.search('engine/apps/sim/src/a.cpp'),
          'golden dirs')
    with tempfile.TemporaryDirectory() as td:
        repo = Path(td)

        def g(*a):
            subprocess.run(['git', *a], cwd=repo, check=True, capture_output=True)

        g('init', '-q', '-b', 'dev')
        g('config', 'user.email', 'selftest@example.invalid')
        g('config', 'user.name', 'selftest')
        gold = repo / 'engine/apps/headless/testdata'
        gold.mkdir(parents=True)
        (gold / 'g.crc').write_text('base\n')
        (repo / 'a.cpp').write_text('int a;\n')
        g('add', '.')
        g('commit', '-q', '-m', 'base')
        g('checkout', '-q', '-b', 'work')
        (gold / 'g.crc').write_text('mine\n')
        g('commit', '-q', '-am', 'mine')
        g('checkout', '-q', 'dev')
        (gold / 'g.crc').write_text('dev\n')
        g('commit', '-q', '-am', 'dev')
        g('checkout', '-q', 'work')
        check(subprocess.run(['git', 'merge', '--no-edit', 'dev'], cwd=repo, capture_output=True).returncode != 0, 'conflict')
        check(cmd_resolve(repo) == 0, 'golden-only conflict resolves')
        check((gold / 'g.crc').read_text() == 'dev\n', 'took dev\'s golden')
        check(git(repo, 'status', '--porcelain', '-uno')[1] == '', 'merge committed')
        check(list_path(repo).read_text() == 'engine/apps/headless/testdata/g.crc\n', 'list saved')
        # a code conflict is left for a person
        (repo / 'a.cpp').write_text('int w;\n')
        g('commit', '-q', '-am', 'w')
        g('checkout', '-q', 'dev')
        (repo / 'a.cpp').write_text('int d;\n')
        g('commit', '-q', '-am', 'd')
        g('checkout', '-q', 'work')
        subprocess.run(['git', 'merge', '--no-edit', 'dev'], cwd=repo, capture_output=True)
        check(cmd_resolve(repo) == 1, 'a code conflict is not resolved')
        check('a.cpp' in git(repo, 'diff', '--name-only', '--diff-filter=U')[1], 'merge left in progress')
    for b in bad:
        print('FAIL: ' + b)
    print('golden_merge selftest: ' + ('FAILED' if bad else 'ok'))
    return 1 if bad else 0


def main(argv) -> int:
    cwd = os.getcwd()
    if argv == ['--selftest']:
        return selftest()
    if argv[:1] == ['resolve']:
        return cmd_resolve(cwd)
    if argv[:1] == ['regen']:
        return cmd_regen(cwd, argv[1] if len(argv) > 1 else build_trees.WARM_TREE)
    if argv[:1] == ['slow']:
        rest = argv[1:]
        dry = '--dry-run' in rest
        rest = [a for a in rest if a != '--dry-run']
        base = rest[0] if len(rest) > 0 else 'origin/dev'
        head = rest[1] if len(rest) > 1 else 'HEAD'
        rc = cmd_slow(cwd, base, head, build_trees.WARM_TREE, dry)
        _, gcd, _ = git(cwd, 'rev-parse', '--path-format=absolute', '--git-common-dir')
        note = gate_age_note(Path(gcd).parent) if gcd else ''
        if note:
            print(note)
        return rc
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
