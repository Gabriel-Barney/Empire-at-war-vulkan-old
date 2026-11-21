#!/usr/bin/env python3
r"""launch_foc.py - start FoC (corruption\StarWarsG.exe) from a Claude shell so that it keeps running. The one place a
probe kit starts the retail game (ref3 t01: `tools/lint/check_probe_launch.py` refuses a launch anywhere else under
tools/probe_mod).

  toolchain\venv\Scripts\python.exe -I tools\probe_mod\common\launch_foc.py [--game-root DIR] [--no-windowed] [--watch]
                                                                           [game args...]

From a kit runner (Python):

  sys.path.insert(0, <probe_mod>/common); import launch_foc as foc
  foc.DEFAULT_GAME                      the FoC install root (the folder holding corruption\StarWarsG.exe)
  foc.corruption_dir(game), foc.game_exe(game)
  foc.cmdline(args, windowed)           'StarWarsG.exe WINDOWED ... <args>' for the log and the "start it yourself" hint
  game = foc.start(game, args, windowed=True, env=None)
                                        starts the game detached; game.pid, game.poll() (exit code or None)

Background (2026-11-09): a StarWarsG.exe started from a Claude shell (Start-Process, cmd start, subprocess) exits at once,
and since 2026-11-09 a one-line .cmd started with `Start-Process explorer.exe <cmd>` dies silently too. What works:
CreateProcess with DEBUG_ONLY_THIS_PROCESS, then DebugActiveProcessStop at the first debug event (CREATE_PROCESS):
the game runs on, detached, with no debugger attached. Used for the storyb t09 and a2m t13 runs.

Default game args: WINDOWED SCREENWIDTH=1920 SCREENHEIGHT=1080 (--no-windowed drops them); any further arguments are
passed on as they are, e.g. MODPATH=Mods\Q2RebelSlice. Prints "pid <n>" and exits 0 once detached (the game keeps
running). --watch stays attached instead (the old dbg_launch.py without --early): prints OutputDebugString text,
non-breakpoint exceptions and the exit code, and detaches after 30 s without a debug event. Exit codes: 0 started,
1 CreateProcess failed, 2 the game folder was not found.
"""
import argparse
import ctypes
import ctypes.wintypes as W
import os
import subprocess
import sys

STEAM_LIBRARY = r'F:\SteamLibrary'   # q2common.ps1 $Q2DefaultGameRoot is the same install
DEFAULT_GAME = os.path.join(STEAM_LIBRARY, 'steamapps', 'common', 'Star Wars Empire at War')
DEFAULT_ROOT = DEFAULT_GAME          # older name, kept for callers
WORKSHOP = os.path.join(STEAM_LIBRARY, 'steamapps', 'workshop', 'content', '32470')   # FoC's Steam workshop mods
EXE_NAME = 'StarWarsG.exe'
WINDOWED = ['WINDOWED', 'SCREENWIDTH=1920', 'SCREENHEIGHT=1080']
DEBUG_ONLY_THIS_PROCESS = 0x2
CREATE_UNICODE_ENVIRONMENT = 0x400
CREATE_PROCESS_DEBUG_EVENT, EXIT_PROCESS_DEBUG_EVENT, OUTPUT_DEBUG_STRING_EVENT, EXCEPTION_DEBUG_EVENT = 3, 5, 8, 1
DBG_CONTINUE, DBG_EXCEPTION_NOT_HANDLED = 0x00010002, 0x80010001
EXCEPTION_BREAKPOINT = 0x80000003
STILL_ACTIVE = 259


def corruption_dir(game):
    return os.path.join(game, 'corruption')


def game_exe(game):
    return os.path.join(corruption_dir(game), EXE_NAME)


def game_root(hint=None):
    """The first of `hint` and DEFAULT_GAME that holds corruption\\StarWarsG.exe, else None."""
    for c in (hint, DEFAULT_GAME):
        if c and os.path.isfile(game_exe(c)):
            return c
    return None


def game_args(args, windowed=True):
    return (list(WINDOWED) if windowed else []) + list(args)


def cmdline(args, windowed=True):
    """The command line as a person would type it in corruption\\ (for logs and the start-it-yourself hint)."""
    return ' '.join([EXE_NAME] + game_args(args, windowed))


def env_block(env):
    """A CreateProcessW unicode environment block from a dict."""
    return ctypes.create_unicode_buffer(''.join('%s=%s\0' % kv for kv in sorted(env.items())) + '\0')


class STARTUPINFOW(ctypes.Structure):
    _fields_ = [('cb', W.DWORD), ('lpReserved', W.LPWSTR), ('lpDesktop', W.LPWSTR), ('lpTitle', W.LPWSTR),
                ('dw', W.DWORD * 7), ('dwFlags', W.DWORD), ('wShowWindow', W.WORD), ('cbReserved2', W.WORD),
                ('lpReserved2', ctypes.c_void_p), ('hStd', W.HANDLE * 3)]


class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [('hProcess', W.HANDLE), ('hThread', W.HANDLE), ('dwProcessId', W.DWORD), ('dwThreadId', W.DWORD)]


class DEBUG_EVENT(ctypes.Structure):
    # x64 layout: three DWORDs, 4 bytes padding, then the 8-aligned union (160 bytes)
    _fields_ = [('code', W.DWORD), ('pid', W.DWORD), ('tid', W.DWORD), ('pad', W.DWORD), ('u', ctypes.c_byte * 160)]


class LaunchError(RuntimeError):
    pass


class Game:
    """A started game: pid, poll() -> exit code or None while it runs (like subprocess.Popen)."""

    def __init__(self, k, pi, cmd):
        self._k = k
        self._h = pi.hProcess
        self.pid = pi.dwProcessId
        self.cmd = cmd
        k.CloseHandle(pi.hThread)

    def poll(self):
        code = W.DWORD()
        if not self._k.GetExitCodeProcess(self._h, ctypes.byref(code)):
            return None
        return None if code.value == STILL_ACTIVE else code.value


def _create(cmd, cwd, env):
    k = ctypes.WinDLL('kernel32', use_last_error=True)
    si = STARTUPINFOW()
    si.cb = ctypes.sizeof(STARTUPINFOW)
    pi = PROCESS_INFORMATION()
    flags = DEBUG_ONLY_THIS_PROCESS | (CREATE_UNICODE_ENVIRONMENT if env is not None else 0)
    block = env_block(env) if env is not None else None
    if not k.CreateProcessW(None, ctypes.create_unicode_buffer(cmd), None, None, False, flags, block, cwd,
                            ctypes.byref(si), ctypes.byref(pi)):
        raise LaunchError('CreateProcess failed, error %d' % ctypes.get_last_error())
    k.DebugSetProcessKillOnExit(False)   # never take the game down with this script
    return k, pi


def _detach_at_create(k, pi):
    """Wait for the CREATE_PROCESS debug event and detach there. Returns a status line."""
    ev = DEBUG_EVENT()
    while True:
        if not k.WaitForDebugEvent(ctypes.byref(ev), 30000):
            k.DebugActiveProcessStop(pi.dwProcessId)
            return 'no debug event for 30 s: detaching (the game runs on)'
        if ev.code == CREATE_PROCESS_DEBUG_EVENT:
            h_file = ctypes.c_void_p.from_buffer(ev.u, 0).value
            if h_file:
                k.CloseHandle(ctypes.c_void_p(h_file))
            k.ContinueDebugEvent(ev.pid, ev.tid, DBG_CONTINUE)
            ok = k.DebugActiveProcessStop(pi.dwProcessId)
            return 'detached' if ok else 'DebugActiveProcessStop failed, error %d' % ctypes.get_last_error()
        k.ContinueDebugEvent(ev.pid, ev.tid, DBG_CONTINUE)


def start(game, args=(), windowed=True, env=None):
    """Start corruption\\StarWarsG.exe of `game` with `args` (WINDOWED SCREENWIDTH=1920 SCREENHEIGHT=1080 first when
    `windowed`), detached. env: a full environment dict for the game (None: this process's). Returns a Game; raises
    LaunchError when CreateProcess fails or the exe is missing."""
    exe = game_exe(game)
    if not os.path.isfile(exe):
        raise LaunchError('%s not found' % exe)
    cmd = '"%s" %s' % (exe, subprocess.list2cmdline(game_args(args, windowed)))
    k, pi = _create(cmd, corruption_dir(game), env)
    _detach_at_create(k, pi)
    return Game(k, pi, cmd)


def _watch(k, pi):
    ev = DEBUG_EVENT()
    while True:
        if not k.WaitForDebugEvent(ctypes.byref(ev), 30000):
            print('no debug event for 30 s: detaching (the game runs on)')
            break
        cont = DBG_CONTINUE
        if ev.code == CREATE_PROCESS_DEBUG_EVENT:
            h_file = ctypes.c_void_p.from_buffer(ev.u, 0).value
            if h_file:
                k.CloseHandle(ctypes.c_void_p(h_file))
        elif ev.code == OUTPUT_DEBUG_STRING_EVENT:
            addr = ctypes.c_void_p.from_buffer(ev.u, 0).value
            uni = ctypes.c_ushort.from_buffer(ev.u, 8).value
            ln = ctypes.c_ushort.from_buffer(ev.u, 10).value
            buf = ctypes.create_string_buffer(ln * (2 if uni else 1))
            n = ctypes.c_size_t()
            k.ReadProcessMemory(pi.hProcess, ctypes.c_void_p(addr), buf, len(buf), ctypes.byref(n))
            text = buf.raw.decode('utf-16-le', 'replace') if uni else buf.raw.decode('mbcs', 'replace')
            print('ODS: ' + text.rstrip('\x00\r\n'))
        elif ev.code == EXCEPTION_DEBUG_EVENT:
            code = ctypes.c_uint32.from_buffer(ev.u, 0).value
            first = ctypes.c_uint32.from_buffer(ev.u, 152).value
            if code != EXCEPTION_BREAKPOINT:
                print('exception %#x first_chance=%d' % (code, first))
                cont = DBG_EXCEPTION_NOT_HANDLED
        elif ev.code == EXIT_PROCESS_DEBUG_EVENT:
            print('exit code %d' % ctypes.c_uint32.from_buffer(ev.u, 0).value)
            k.ContinueDebugEvent(ev.pid, ev.tid, cont)
            return 0
        sys.stdout.flush()
        k.ContinueDebugEvent(ev.pid, ev.tid, cont)
    k.DebugActiveProcessStop(pi.dwProcessId)
    return 0


def selftest():
    bad = []
    if cmdline(['MODPATH=Mods\\X']) != 'StarWarsG.exe WINDOWED SCREENWIDTH=1920 SCREENHEIGHT=1080 MODPATH=Mods\\X':
        bad.append('cmdline windowed')
    if cmdline(['A'], windowed=False) != 'StarWarsG.exe A':
        bad.append('cmdline plain')
    if game_exe('G') != os.path.join('G', 'corruption', 'StarWarsG.exe'):
        bad.append('game_exe')
    if env_block({'B': '2', 'A': '1'}).value != 'A=1':   # the block is NUL-separated; .value stops at the first NUL
        bad.append('env block')
    try:
        start(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'no_such_game'), ['X'])
        bad.append('a missing exe started')
    except LaunchError:
        pass
    for b in bad:
        print('FAIL: ' + b)
    print('launch_foc selftest: ' + ('FAILED' if bad else 'ok'))
    return 1 if bad else 0


def main():
    if sys.argv[1:] == ['--selftest']:
        return selftest()
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game-root', help='folder containing corruption\\StarWarsG.exe (default: %s)' % DEFAULT_GAME)
    ap.add_argument('--no-windowed', action='store_true', help='do not add WINDOWED SCREENWIDTH=1920 SCREENHEIGHT=1080')
    ap.add_argument('--watch', action='store_true', help='stay attached and print debug output instead of detaching')
    ap.add_argument('--early', action='store_true', help='detach at the first debug event (the default; kept for old commands)')
    a, extra = ap.parse_known_args()
    root = game_root(a.game_root)
    if not root:
        print('game not found (pass --game-root <folder containing corruption\\StarWarsG.exe>)')
        return 2
    cmd = '"%s" %s' % (game_exe(root), subprocess.list2cmdline(game_args(extra, not a.no_windowed)))
    try:
        k, pi = _create(cmd, corruption_dir(root), None)
    except LaunchError as e:
        print(e)
        return 1
    print('pid %d' % pi.dwProcessId)
    print('cmd %s' % cmd)
    sys.stdout.flush()
    if a.watch:
        return _watch(k, pi)
    print(_detach_at_create(k, pi))
    return 0


if __name__ == '__main__':
    sys.exit(main())
