"""Start the headless ReVa MCP server (stdio) on a Ghidra project that is kept between sessions.

Stock `mcp-reva` (reverse-engineering-assistant 7.3.2) creates its project in a temp folder and deletes
it on exit, so StarWarsG.exe would be imported and analysed again in every session (a long run). This
wrapper points the same launcher at a fixed folder and skips the delete. It relies on two details of
reva_cli 7.3.2 (`ReVaLauncher.start` calls `tempfile.mkdtemp(prefix="reva_project_")`, `stop` removes
`temp_project_dir`); re-check both when the pin in toolchain\\14_re_tools.cmd changes.

Usage (through tools\\re\\reva_mcp.cmd, which sets GHIDRA_INSTALL_DIR and JAVA_HOME):
    python reva_persistent.py <project folder> [mcp-reva arguments]

The project is named after the folder. Ghidra locks an open project, so one ReVa session at a time.
"""
import os
import sys
import tempfile
from pathlib import Path


_session_lock = None  # kept open for the life of the process


def main() -> None:
    if len(sys.argv) < 2:
        raise SystemExit("usage: reva_persistent.py <project folder> [mcp-reva arguments]")
    project_dir = Path(sys.argv[1]).resolve()
    project_dir.mkdir(parents=True, exist_ok=True)
    del sys.argv[1]
    # One session at a time: Ghidra locks the project, and when that lock fails ReVa neither exits nor closes its
    # output, so the MCP client hangs. Take our own lock first and exit at once when another session holds it.
    import msvcrt
    global _session_lock
    _session_lock = open(project_dir.parent / (project_dir.name + ".session.lock"), "a+b")
    try:
        _session_lock.seek(0)
        msvcrt.locking(_session_lock.fileno(), msvcrt.LK_NBLCK, 1)
    except OSError:
        print(f"ReVa project {project_dir} is in use by another session (one at a time); try again later.",
              file=sys.stderr)
        raise SystemExit(3)
    os.chdir(project_dir)  # the project name is the current folder's name

    real_mkdtemp = tempfile.mkdtemp

    def mkdtemp(suffix=None, prefix=None, dir=None):  # noqa: A002 (same signature as tempfile.mkdtemp)
        if prefix == "reva_project_":
            return str(project_dir)
        return real_mkdtemp(suffix=suffix, prefix=prefix, dir=dir)

    tempfile.mkdtemp = mkdtemp

    from reva_cli import launcher as reva_launcher
    from reva_cli.__main__ import main as reva_main

    real_stop = reva_launcher.ReVaLauncher.stop

    def stop(self):
        self.temp_project_dir = None  # keep the project on disk
        real_stop(self)

    reva_launcher.ReVaLauncher.stop = stop
    reva_main()


if __name__ == "__main__":
    main()
