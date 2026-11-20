#!/usr/bin/env python
"""Probe kits start the retail game only through tools/probe_mod/common (ref3 t01, 2026-11-19).

A StarWarsG.exe started plainly from a Claude shell exits at once; common/launch_foc.py starts it the way that works
(CreateProcess under the debug API, detached at the first event). Kits used to carry their own launch line, so a fix
reached only the kits that copied it. This check refuses a launch of StarWarsG.exe in any file under tools/probe_mod
outside common/:

  .py    a process start (subprocess.Popen / run / call / check_call / check_output, os.startfile, os.system,
         os.spawn*, CreateProcess*) whose program is StarWarsG.exe: a string ending in StarWarsG.exe, os.path.join(...,
         'StarWarsG.exe'), or a name the file binds to such a path (`exe = os.path.join(corr, 'StarWarsG.exe')`)
  .cmd   a non-rem line that runs StarWarsG.exe (`start "" StarWarsG.exe ...`, `"...\\StarWarsG.exe" ...`)
  .ps1   Start-Process or `&` on StarWarsG.exe or on a variable the file binds to a StarWarsG.exe path

Attaching to a running game (OpenProcess, DebugActiveProcess, tasklist lookups) is not a launch.

  python -I tools/lint/check_probe_launch.py [--root DIR]   exit 1 with one line per launch found
  python -I tools/lint/check_probe_launch.py --selftest
"""
import ast
import os
import re
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXE_RE = re.compile(r'(?i)starwarsg\.exe')
PY_STARTERS = {'Popen', 'run', 'call', 'check_call', 'check_output', 'startfile', 'system', 'CreateProcess',
               'CreateProcessW', 'CreateProcessA', 'spawnl', 'spawnv', 'spawnlp', 'spawnvp'}


def _src(text, node):
    return ast.get_source_segment(text, node) or ''


def _is_exe_expr(text, node, names):
    """True when node evaluates to a StarWarsG.exe path (as far as a static look can tell)."""
    if isinstance(node, ast.Constant) and isinstance(node.value, str):
        return bool(re.match(r'(?i)^"?[^"]*starwarsg\.exe"?(\s|$)', node.value))
    if isinstance(node, ast.Name):
        return node.id in names
    if isinstance(node, ast.Call):
        f = node.func
        fname = f.attr if isinstance(f, ast.Attribute) else f.id if isinstance(f, ast.Name) else ''
        if fname == 'join' and node.args and _is_exe_expr(text, node.args[-1], names):
            return True
        if fname == 'game_exe':   # launch_foc.game_exe(game)
            return True
    if isinstance(node, ast.JoinedStr):
        return bool(EXE_RE.search(_src(text, node)))
    return False


def _program(node):
    """The program part of a process-start argument: the first list element, the left end of `[exe] + args`."""
    while isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
        node = node.left
    if isinstance(node, (ast.List, ast.Tuple)):
        return node.elts[0] if node.elts else None
    return node


def py_findings(text):
    try:
        tree = ast.parse(text)
    except SyntaxError:
        return []
    names = set()
    for _ in range(2):   # names bound to the exe path (twice, so `b = a` after `a = <exe>` is seen)
        for n in ast.walk(tree):
            if isinstance(n, ast.Assign) and _is_exe_expr(text, n.value, names):
                names.update(t.id for t in n.targets if isinstance(t, ast.Name))
    out = []
    for n in ast.walk(tree):
        if not isinstance(n, ast.Call):
            continue
        f = n.func
        fname = f.attr if isinstance(f, ast.Attribute) else f.id if isinstance(f, ast.Name) else ''
        if fname not in PY_STARTERS:
            continue
        args = list(n.args)
        if fname.startswith('CreateProcess'):
            hit = any(EXE_RE.search(_src(text, a)) or any(isinstance(x, ast.Name) and x.id in names
                                                          for x in ast.walk(a)) for a in args[:2])
        else:
            prog = _program(args[0]) if args else None
            hit = prog is not None and _is_exe_expr(text, prog, names)
        if hit:
            out.append(n.lineno)
    return out


def cmd_findings(text):
    out = []
    for i, line in enumerate(text.splitlines(), 1):
        s = line.strip().lstrip('@').strip()
        low = s.lower()
        if not s or low.startswith('rem ') or low == 'rem' or s.startswith('::') or low.startswith('echo'):
            continue
        if re.match(r'(?i)^(start\s+("[^"]*"\s+)?(/\w+\s+)*)?"?[^"\s]*starwarsg\.exe"?(\s|$)', s):
            out.append(i)
    return out


def ps1_findings(text):
    names = set(m.group(1).lower() for m in re.finditer(r'(?im)^\s*\$(\w+)\s*=.*starwarsg\.exe', text))
    out = []
    in_block = False
    for i, line in enumerate(text.splitlines(), 1):
        s = line.strip()
        if s.startswith('<#'):
            in_block = True
        if in_block:
            if '#>' in s:
                in_block = False
            continue
        if s.startswith('#'):
            continue
        m = re.search(r'(?i)(Start-Process\s+(-FilePath\s+)?|^&\s*)(\S+)', s)
        if not m:
            continue
        target = m.group(3).strip('"\'()')
        if EXE_RE.search(target) or (target.startswith('$') and target[1:].lower() in names):
            out.append(i)
    return out


def scan(root):
    pm = os.path.join(root, 'tools', 'probe_mod')
    common = os.path.normcase(os.path.join(pm, 'common'))
    bad = []
    for d, dirs, files in os.walk(pm):
        dirs[:] = sorted(x for x in dirs if x not in ('out', 'work', 'Mods', '__pycache__'))
        if os.path.normcase(d) == common or os.path.normcase(d).startswith(common + os.sep):
            continue
        for f in sorted(files):
            ext = os.path.splitext(f)[1].lower()
            fn = {'.py': py_findings, '.cmd': cmd_findings, '.bat': cmd_findings, '.ps1': ps1_findings}.get(ext)
            if not fn:
                continue
            p = os.path.join(d, f)
            text = open(p, encoding='utf-8-sig', errors='replace').read()
            bad += ['%s:%d' % (os.path.relpath(p, root).replace('\\', '/'), n) for n in fn(text)]
    return bad


def check(root=None):
    bad = scan(root or REPO)
    for b in bad:
        print('%s: starts StarWarsG.exe outside tools/probe_mod/common; use launch_foc.start() (Python) or '
              'common\\launch_foc.py (scripts)' % b)
    print('probe launch check: %d finding(s)' % len(bad) if bad else 'probe launch check: ok')
    return 1 if bad else 0


def selftest():
    fails = []

    def expect(c, msg):
        if not c:
            fails.append(msg)

    good_py = ("import os, subprocess\nexe = os.path.join(corr, 'StarWarsG.exe')\nif not os.path.isfile(exe): pass\n"
               "subprocess.run(['tasklist', '/FI', 'IMAGENAME eq StarWarsG.exe'])\nfoc.start(a.game, MODPATH)\n"
               "subprocess.Popen([sys.executable, '-I', 'reader.py'])\n")
    expect(py_findings(good_py) == [], 'clean Python passes: %s' % py_findings(good_py))
    bad_py = ("import os, subprocess\nexe = os.path.join(corr, 'StarWarsG.exe')\n"
              "subprocess.Popen([exe] + WINDOWED + MODPATH, cwd=corr)\n"
              "subprocess.Popen(r'C:\\g\\StarWarsG.exe WINDOWED')\nos.startfile(exe)\n"
              "k.CreateProcessW(None, '\"%s\" x' % exe, None)\n")
    expect(py_findings(bad_py) == [3, 4, 5, 6], 'Python launches: %s' % py_findings(bad_py))
    good_cmd = ('@echo off\nrem start "" StarWarsG.exe\necho Starting StarWarsG.exe\n'
                'tasklist /FI "IMAGENAME eq StarWarsG.exe" | find /I "StarWarsG.exe"\n'
                '"%PY%" -I "%~dp0..\\common\\launch_foc.py" MODPATH=Mods\\X\n')
    expect(cmd_findings(good_cmd) == [], 'clean cmd passes: %s' % cmd_findings(good_cmd))
    bad_cmd = '@echo off\nstart "" StarWarsG.exe WINDOWED\n"F:\\g\\corruption\\StarWarsG.exe" MODPATH=x\n'
    expect(cmd_findings(bad_cmd) == [2, 3], 'cmd launches: %s' % cmd_findings(bad_cmd))
    good_ps = ("$exe = Join-Path $root 'corruption\\StarWarsG.exe'\n$sha = (Get-FileHash $exe).Hash\n"
               "$r = Start-Process -FilePath $reader -PassThru\n& $Q2Python -I $launchFoc x\n"
               "# Start-Process $exe\n")
    expect(ps1_findings(good_ps) == [], 'clean ps1 passes: %s' % ps1_findings(good_ps))
    bad_ps = ("$exe = Join-Path $root 'corruption\\StarWarsG.exe'\n$g = Start-Process -FilePath $exe -PassThru\n"
              "& $exe WINDOWED\nStart-Process 'C:\\g\\StarWarsG.exe'\n")
    expect(ps1_findings(bad_ps) == [2, 3, 4], 'ps1 launches: %s' % ps1_findings(bad_ps))
    with tempfile.TemporaryDirectory() as td:
        for rel, body in (('tools/probe_mod/common/launch_foc.py', bad_py), ('tools/probe_mod/k/run.py', good_py)):
            os.makedirs(os.path.dirname(os.path.join(td, rel)), exist_ok=True)
            open(os.path.join(td, rel), 'w').write(body)
        expect(scan(td) == [], 'common/ is allowed to launch')
        open(os.path.join(td, 'tools/probe_mod/k/go.cmd'), 'w').write(bad_cmd)
        expect(scan(td) == ['tools/probe_mod/k/go.cmd:2', 'tools/probe_mod/k/go.cmd:3'], 'a kit launch is found')
    for f in fails:
        print('FAIL: ' + f)
    print('check_probe_launch selftest: ' + ('FAILED' if fails else 'ok'))
    return 1 if fails else 0


def main(argv):
    if argv == ['--selftest']:
        return selftest()
    root = None
    if argv[:1] == ['--root'] and len(argv) == 2:
        root = argv[1]
    elif argv:
        print(__doc__)
        return 2
    return check(root)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
