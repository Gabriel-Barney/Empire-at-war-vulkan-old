"""payload.py - mod-payload and Lua helpers the probe kits share (ref3 t01, 2026-11-19; each kit had its own copy).

  crlf(text)                    text as the game's files want it: CRLF line ends, latin-1 bytes
  lua_quote(s)                  a Lua string literal: "..." with \\ and " escaped
  lua_path(p)                   a path for a Lua dofile / io.open: backslashes turned into forward slashes
  run_lua_chunks(lua, workdir, name, chunks)
                                write chunks to <workdir>/<name>.lua, run it with `lua` (lua50.find_lua()) in workdir;
                                returns (exit code, stdout + stderr stripped)
  run_harness(lua, path, parts, log=None, timeout=None)
                                write parts to path, run it, copy stdout + stderr to log; returns (exit code, stdout, text)
  lines_with(text, prefix)      the lines of text that start with prefix
  restore_profile(run_dir, dry_run=False, done_name='profile_restore_done.txt', again='run this again')
                                put back the player profile a run snapshotted (q2_traces/q2_profile.py); (ok, message)
"""
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
Q2 = os.path.join(os.path.dirname(HERE), 'q2_traces')
PROFILE_DONE = 'profile_restore_done.txt'


def crlf(text):
    return text.replace('\r\n', '\n').replace('\n', '\r\n').encode('latin-1')


def lua_quote(s):
    return '"%s"' % str(s).replace('\\', '\\\\').replace('"', '\\"')


def lua_path(p):
    return p.replace('\\', '/')


def run_lua_chunks(lua, workdir, name, chunks):
    path = os.path.join(workdir, name + '.lua')
    with open(path, 'w', newline='\n') as f:
        f.write('\n'.join(chunks))
    r = subprocess.run([lua, path], capture_output=True, text=True, cwd=workdir)
    return r.returncode, (r.stdout + r.stderr).strip()


def run_harness(lua, path, parts, log=None, timeout=None, encoding=None, newline=None):
    """Write the harness parts to path, run lua on it, write stdout + stderr to log (when given)."""
    with open(path, 'w', encoding=encoding, newline=newline) as f:
        for p in parts:
            f.write(p)
    r = subprocess.run([lua, path], capture_output=True, text=True, timeout=timeout)
    text = r.stdout + r.stderr
    if log:
        with open(log, 'w') as f:
            f.write(text)
    return r.returncode, r.stdout, text


def lines_with(text, prefix):
    return [ln for ln in text.splitlines() if ln.startswith(prefix)]


def first_line_with(text, prefix):
    return next((ln for ln in text.splitlines() if ln.startswith(prefix)), '')


def game_running():
    r = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq StarWarsG.exe', '/NH'], capture_output=True, text=True)
    return 'starwarsg.exe' in r.stdout.lower()


def restore_profile(run_dir, dry_run=False, done_name=PROFILE_DONE, again='run this again'):
    """Restore the player profile snapshotted into <run_dir>/profile_backup by q2_profile.snapshot. Returns (ok, message).
    A <run_dir>/<done_name> file marks a verified restore, so a second cleanup does nothing."""
    if not run_dir or not os.path.isdir(run_dir):
        return True, 'no run folder: no profile to restore'
    if not os.path.exists(os.path.join(run_dir, 'profile_backup', 'profile.json')):
        return True, 'run %s has no profile snapshot' % run_dir
    if os.path.exists(os.path.join(run_dir, done_name)):
        return True, 'profile of %s was already restored (%s)' % (run_dir, done_name)
    if game_running():
        return False, 'StarWarsG.exe is running: close it and %s' % again
    if Q2 not in sys.path:
        sys.path.insert(0, Q2)
    import q2_profile
    if dry_run:
        snap = q2_profile._load(run_dir)
        return True, 'would restore; differences now: %s' % (q2_profile.diff(snap) or 'none')
    rc = q2_profile.restore(run_dir)
    if rc == 0:
        with open(os.path.join(run_dir, done_name), 'w') as f:      # q2_profile writes no file when nothing changed
            f.write('profile restored and verified ' + time.strftime('%Y-%m-%d %H:%M:%S') + '\n')
    return rc == 0, 'profile restore %s' % ('verified' if rc == 0 else 'NOT VERIFIED, see profile_restore.json')


def selftest():
    bad = []
    if crlf('a\nb\r\nc') != b'a\r\nb\r\nc':
        bad.append('crlf')
    if lua_quote('a"b\\c') != '"a\\"b\\\\c"':
        bad.append('lua_quote')
    if lua_path('C:\\x\\y.lua') != 'C:/x/y.lua':
        bad.append('lua_path')
    if lines_with('A|1\nB|2\nA|3', 'A|') != ['A|1', 'A|3'] or first_line_with('x\nST 1\nST 2', 'ST') != 'ST 1':
        bad.append('lines_with')
    import tempfile
    with tempfile.TemporaryDirectory() as td:
        if restore_profile(os.path.join(td, 'none')) != (True, 'no run folder: no profile to restore'):
            bad.append('restore_profile without a run folder')
        if not restore_profile(td)[1].endswith('has no profile snapshot'):
            bad.append('restore_profile without a snapshot')
    for b in bad:
        print('FAIL: ' + b)
    print('payload selftest: ' + ('FAILED' if bad else 'ok'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(selftest())
