"""One kept REA session (rea-agents 6.8.0 MCP server on stdio, Ghidra 12.1.4 behind it), many calls.

REA's command line re-imports and re-analyses the executable on every command (about 4 minutes for
StarWarsG.exe) and its Ghidra project is deleted on close, so only a session that stays open is usable.
This script keeps one: `call` starts a background `serve` process when none is running, drops the
request into a queue folder and prints the answer. The session is shared by every worktree (it lives
under toolchain\\re\\rea-work, reached through the toolchain junction) and ends by itself after
ALAMO_REA_IDLE_MIN minutes without a call (default 60).

Usage (through tools\\re\\rea.cmd, which checks the install):
    rea.cmd call <tool> [key=text ...] [key:=json ...] [--tag T] [--max N] [--timeout S]
    rea.cmd tools [substring] [--schema NAME]   tool names and argument names (* = required)
    rea.cmd start | status | stop
    rea.cmd --selftest                          offline check against a fake server

`key=text` passes a string, `key:=json` any JSON value (limit:=200, addresses:='["0x1","0x2"]').
`call` prints one status line (tool, seconds, ok / ERROR, and the path of `<tag>.payload.json`, which
holds the whole result without REA's envelope) and then that result, cut at --max characters. The first analysis query of a session waits for Ghidra's
auto-analysis; later ones answer in well under a second.

The server opens a private copy of retail StarWarsG.exe (toolchain\\re\\rea-work\\target), taken from
ALAMO_FOC_DATA or the default Steam path. RE / doc tasks only: never while writing engine code, and
never paste its output into a file (CLAUDE.md, clean-room).
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
RE_ROOT = (HERE.parent.parent / "toolchain" / "re").resolve()
NODE_DIR = RE_ROOT / "node-v24.22.3-win-x64"
REA_MJS = RE_ROOT / "rea" / "node_modules" / "rea-agents" / "scripts" / "rea.mjs"
GHIDRA_DIR = RE_ROOT / "ghidra_12.1.4_PUBLIC"
JDK_DIR = RE_ROOT / "jdk-21.0.13+9"
DEFAULT_FOC = r"F:\SteamLibrary\steamapps\common\Star Wars Empire at War\corruption"

WORK = Path(os.environ.get("ALAMO_REA_WORK") or (RE_ROOT / "rea-work")).resolve()
QUEUE = WORK / "queue"
OUT = WORK / "out"
LOCK = WORK / "session.lock"  # held by the serving process; its mtime is the heartbeat
STOP = WORK / "STOP"
LOG = WORK / "serve.log"
FAKE = os.environ.get("ALAMO_REA_FAKE") == "1"
HEARTBEAT_S = 2.0
FRESH_S = 15.0


def lock_fresh() -> bool:
    try:
        return time.time() - LOCK.stat().st_mtime < FRESH_S
    except OSError:
        return False


def log(text: str) -> None:
    with open(LOG, "a", encoding="utf-8") as f:
        f.write(time.strftime("%Y-%m-%d %H:%M:%S ") + text + "\n")


# ---------------------------------------------------------------- the serving process


class Mcp:
    """Line-delimited JSON-RPC to the MCP server's stdio."""

    def __init__(self, cmd, env, cwd, err_path):
        self.err = open(err_path, "w", encoding="utf-8")
        self.p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.err,
                                  env=env, cwd=str(cwd))
        self.next_id = 0

    def _write(self, msg) -> None:
        self.p.stdin.write((json.dumps(msg) + "\n").encode("utf-8"))
        self.p.stdin.flush()

    def notify(self, method, params=None) -> None:
        msg = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            msg["params"] = params
        self._write(msg)

    def request(self, method, params=None):
        self.next_id += 1
        msg = {"jsonrpc": "2.0", "id": self.next_id, "method": method}
        if params is not None:
            msg["params"] = params
        self._write(msg)
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise RuntimeError("MCP server closed its output (see mcp.err)")
            try:
                reply = json.loads(line)
            except ValueError:
                continue  # not a protocol line
            if "method" in reply and "id" in reply:  # a request from the server: this client offers nothing
                self._write({"jsonrpc": "2.0", "id": reply["id"],
                             "error": {"code": -32601, "message": "not supported by this client"}})
                continue
            if reply.get("id") == self.next_id:
                return reply

    def close(self) -> None:
        try:
            self.p.stdin.close()
            self.p.wait(timeout=60)
        except (OSError, subprocess.TimeoutExpired):
            self.p.kill()
        self.err.close()


def acquire_lock() -> bool:
    for _ in range(2):
        try:
            fd = os.open(str(LOCK), os.O_CREAT | os.O_EXCL | os.O_WRONLY)
        except FileExistsError:
            if lock_fresh():
                return False
            try:
                LOCK.unlink()  # left by a session that was killed
            except OSError:
                return False
            continue
        os.write(fd, str(os.getpid()).encode("ascii"))
        os.close(fd)
        return True
    return False


def target_copy() -> Path:
    """REA wants a local file nobody else changes: keep a private copy of the retail exe."""
    src = Path(os.environ.get("ALAMO_FOC_DATA", DEFAULT_FOC)) / "StarWarsG.exe"
    dst = WORK / "target" / "StarWarsG.exe"
    if not src.is_file():
        if dst.is_file():
            return dst
        raise RuntimeError("StarWarsG.exe not found at %s (set ALAMO_FOC_DATA)" % src)
    if not dst.is_file() or dst.stat().st_size != src.stat().st_size:
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, dst)
    return dst


def server_command():
    if FAKE:
        return [sys.executable, "-I", str(Path(__file__).resolve()), "_fake"], dict(os.environ)
    env = dict(os.environ)
    env["PATH"] = str(NODE_DIR) + os.pathsep + env.get("PATH", "")
    env["JAVA_HOME"] = str(JDK_DIR)
    env["GHIDRA_INSTALL_DIR"] = str(GHIDRA_DIR)
    return [str(NODE_DIR / "node.exe"), str(REA_MJS), "--mcp"], env


def is_error(reply) -> bool:
    return bool(reply.get("error") or (reply.get("result") or {}).get("isError"))


def serve() -> int:
    for d in (WORK, QUEUE, OUT):
        d.mkdir(parents=True, exist_ok=True)
    if not acquire_lock():
        return 0  # another session is serving
    alive = True

    def heartbeat():
        while alive:
            try:
                os.utime(LOCK, None)
            except OSError:
                pass
            time.sleep(HEARTBEAT_S)

    threading.Thread(target=heartbeat, daemon=True).start()
    mcp = None
    try:
        cmd, env = server_command()
        mcp = Mcp(cmd, env, WORK, WORK / "mcp.err")
        mcp.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                   "clientInfo": {"name": "alamo-rea-session", "version": "1"}})
        mcp.notify("notifications/initialized")
        if not FAKE:
            t0 = time.time()
            reply = mcp.request("tools/call", {"name": "open_binary", "arguments": {"path": str(target_copy())}})
            log("open_binary %.1fs err=%s" % (time.time() - t0, is_error(reply)))
            if is_error(reply):
                log("open_binary failed: " + json.dumps(reply)[:2000])
                return 1
        log("session up, pid %d" % os.getpid())
        idle_s = 60.0 * float(os.environ.get("ALAMO_REA_IDLE_MIN", "60"))
        last = time.time()
        while not STOP.exists():
            todo = sorted((f for f in QUEUE.iterdir() if f.suffix == ".json"), key=lambda f: f.stat().st_mtime)
            if not todo:
                if time.time() - last > idle_s:
                    log("idle for %.0f min, closing" % (idle_s / 60.0))
                    break
                time.sleep(0.2)
                continue
            f = todo[0]
            try:
                req = json.loads(f.read_text(encoding="utf-8"))
            except (OSError, ValueError):
                time.sleep(0.2)  # still being written
                continue
            f.unlink()
            t0 = time.time()
            if req.get("method") == "tools/list":
                reply = mcp.request("tools/list")
                what = "tools/list"
            else:
                reply = mcp.request("tools/call", {"name": req["name"], "arguments": req.get("arguments", {})})
                what = req["name"]
            dt = time.time() - t0
            reply["_seconds"] = round(dt, 2)
            tmp = OUT / (f.name + ".tmp")
            tmp.write_text(json.dumps(reply, indent=1), encoding="utf-8")
            os.replace(tmp, OUT / f.name)
            log("%s %s %.1fs err=%s" % (f.stem, what, dt, is_error(reply)))
            last = time.time()
        return 0
    except Exception as e:  # the session is detached: the log is the only place this can be seen
        log("session failed: %r" % (e,))
        return 1
    finally:
        if mcp is not None:
            mcp.close()
        alive = False
        for f in (STOP, LOCK):
            try:
                f.unlink()
            except OSError:
                pass
        log("session closed")


def fake_server() -> int:
    """Stands in for REA in --selftest: answers initialize, tools/list and echoes tools/call."""
    for line in sys.stdin:
        msg = json.loads(line)
        if "id" not in msg:
            continue
        if msg["method"] == "tools/list":
            result = {"tools": [{"name": "echo", "inputSchema": {"properties": {"text": {}, "n": {}},
                                                                 "required": ["text"]}}]}
        elif msg["method"] == "tools/call":
            args = msg["params"]["arguments"]
            if msg["params"]["name"] == "fail":
                result = {"isError": True, "content": [{"type": "text", "text": "no such tool"}]}
            else:
                result = {"structuredContent": {"evidence_id": "x", "normalized_result": {"echo": args}}}
        else:
            result = {}
        sys.stdout.write(json.dumps({"jsonrpc": "2.0", "id": msg["id"], "result": result}) + "\n")
        sys.stdout.flush()
    return 0


# ---------------------------------------------------------------- the calling side


def check_install() -> None:
    if FAKE:
        return
    for p in (NODE_DIR / "node.exe", REA_MJS, GHIDRA_DIR, JDK_DIR):
        if not p.exists():
            raise SystemExit("missing %s: run toolchain\\15_rea.cmd (and 14_re_tools.cmd for Ghidra and the JDK)" % p)


def ensure_session(wait_s: float = 120.0) -> None:
    if lock_fresh():
        return
    check_install()
    for d in (WORK, QUEUE, OUT):
        d.mkdir(parents=True, exist_ok=True)
    flags = 0
    if os.name == "nt":
        flags = subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.CREATE_NO_WINDOW
    subprocess.Popen([sys.executable, "-I", str(Path(__file__).resolve()), "serve"], stdin=subprocess.DEVNULL,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=flags, close_fds=True,
                     cwd=str(WORK))
    end = time.time() + wait_s
    while time.time() < end:
        if lock_fresh():
            return
        time.sleep(0.3)
    raise SystemExit("REA session did not start; see %s and %s" % (LOG, WORK / "mcp.err"))


def roundtrip(req, tag: str, timeout_s: float):
    ensure_session()
    name = tag + ".json"
    answer = OUT / name
    if answer.exists():
        answer.unlink()
    tmp = QUEUE / (name + ".part")
    tmp.write_text(json.dumps(req), encoding="utf-8")
    os.replace(tmp, QUEUE / name)
    end = time.time() + timeout_s
    while not answer.exists():
        if not lock_fresh():
            time.sleep(1.0)
            if not answer.exists() and not lock_fresh():
                raise SystemExit("REA session ended before answering; see %s and %s" % (LOG, WORK / "mcp.err"))
        if time.time() > end:
            raise SystemExit("no answer after %.0f s (the request stays queued; `status` shows the session)" % timeout_s)
        time.sleep(0.1)
    return json.loads(answer.read_text(encoding="utf-8")), answer


def payload(reply):
    """The part of an answer worth reading: REA wraps every result in about 2.5 KB of fixed envelope."""
    if "error" in reply:
        return reply["error"]
    result = reply.get("result") or {}
    body = result.get("structuredContent")
    if body is None:
        texts = [c.get("text", "") for c in result.get("content", []) if c.get("type") == "text"]
        joined = "\n".join(texts)
        try:
            body = json.loads(joined)
        except ValueError:
            return joined
    if isinstance(body, dict):
        for key in ("normalized_result", "result"):
            if body.get(key) is not None:
                return body[key]
    return body


def parse_pairs(pairs):
    args = {}
    for pair in pairs:
        if ":=" in pair and ("=" not in pair.split(":=", 1)[0]):
            key, value = pair.split(":=", 1)
            try:
                args[key] = json.loads(value)
            except ValueError as e:
                raise SystemExit("%s: value is not JSON (%s)" % (key, e))
        elif "=" in pair:
            key, value = pair.split("=", 1)
            args[key] = value
        else:
            raise SystemExit("argument %r is neither key=text nor key:=json" % pair)
    return args


def cmd_call(a) -> int:
    args = parse_pairs(a.pairs)
    if a.args_file:
        args.update(json.loads(Path(a.args_file).read_text(encoding="utf-8")))
    tag = a.tag or "%s_%d_%s" % (time.strftime("%H%M%S"), os.getpid(), a.tool)
    reply, answer = roundtrip({"name": a.tool, "arguments": args}, tag, a.timeout)
    bad = is_error(reply)
    body = payload(reply)
    text = body if isinstance(body, str) else json.dumps(body, ensure_ascii=False)
    # The answer file is the raw JSON-RPC reply; save the payload alone beside it so it can be read or parsed directly.
    full = answer.with_name(answer.stem + ".payload.json")
    full.write_text(body if isinstance(body, str) else json.dumps(body, indent=1, ensure_ascii=False), encoding="utf-8")
    cut = len(text) > a.max
    print("%s %.1fs %s  %s: %s" % (a.tool, reply.get("_seconds", 0.0), "ERROR" if bad else "ok",
                                   "answer cut, full payload" if cut else "full payload", full), flush=True)
    if cut:
        text = text[:a.max] + "\n... cut %d characters; read the full payload file" % (len(text) - a.max)
    sys.stdout.buffer.write((text + "\n").encode("utf-8", "replace"))
    return 1 if bad else 0


def cmd_tools(a) -> int:
    reply, _ = roundtrip({"method": "tools/list"}, "tools_%d" % os.getpid(), a.timeout)
    tools = (reply.get("result") or {}).get("tools", [])
    if a.schema:
        for t in tools:
            if t["name"] == a.schema:
                print(json.dumps(t, indent=1))
                return 0
        raise SystemExit("no tool named %s" % a.schema)
    shown = 0
    for t in tools:
        if a.filter and a.filter.lower() not in t["name"].lower():
            continue
        schema = t.get("inputSchema") or {}
        required = set(schema.get("required") or [])
        names = [k + ("*" if k in required else "") for k in (schema.get("properties") or {})]
        print("%s(%s)" % (t["name"], ", ".join(names)))
        shown += 1
    print("%d of %d tools" % (shown, len(tools)))
    return 0


def cmd_status(_a) -> int:
    print("session: %s (%s)" % ("running" if lock_fresh() else "not running", WORK))
    if LOG.exists():
        for line in LOG.read_text(encoding="utf-8").splitlines()[-8:]:
            print("  " + line)
    return 0


def cmd_stop(_a) -> int:
    if not lock_fresh():
        print("no session running")
        return 0
    STOP.write_text("stop", encoding="ascii")
    end = time.time() + 90.0
    while LOCK.exists() and time.time() < end:
        time.sleep(0.3)
    print("stopped" if not LOCK.exists() else "still closing; see %s" % LOG)
    return 0


def selftest() -> int:
    work = tempfile.mkdtemp(prefix="rea_selftest_")
    env = dict(os.environ, ALAMO_REA_WORK=work, ALAMO_REA_FAKE="1", ALAMO_REA_IDLE_MIN="1")
    me = [sys.executable, "-I", str(Path(__file__).resolve())]

    def run(*argv):
        r = subprocess.run(me + list(argv), env=env, capture_output=True, text=True, timeout=180)
        return r.returncode, r.stdout

    failed = []

    def expect(ok, what):
        if not ok:
            failed.append(what)

    try:
        rc, out = run("call", "echo", "text=4889", "n:=200", 'list:=["0x1","0x2"]')
        expect(rc == 0 and " ok " in out.splitlines()[0], "call succeeds: %r" % out)
        expect('{"echo": {"text": "4889", "n": 200, "list": ["0x1", "0x2"]}}' in out, "payload without envelope: %r" % out)
        rc, out = run("call", "fail")
        expect(rc == 1 and "ERROR" in out, "tool error gives exit 1: %r" % out)
        rc, out = run("call", "echo", "text=" + "x" * 500, "--max", "100")
        expect("cut " in out and "answer cut, full payload" in out, "long answers are cut: %r" % out[-80:])
        pay = out.splitlines()[0].split("full payload: ", 1)[-1].strip()
        expect(json.loads(Path(pay).read_text(encoding="utf-8")) == {"echo": {"text": "x" * 500}},
               "payload file holds the result alone: %s" % pay)
        rc, out = run("tools")
        expect("echo(text*, n)" in out and "1 of 1 tools" in out, "tools lists argument names: %r" % out)
        log_text = (Path(work) / "serve.log").read_text(encoding="utf-8")
        expect(log_text.count("session up") == 1, "one session served every call")
        rc, out = run("stop")
        expect("stopped" in out and not (Path(work) / "session.lock").exists(), "stop ends the session: %r" % out)
        rc, out = run("status")
        expect("not running" in out, "status after stop: %r" % out)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    for what in failed:
        print("FAIL " + what)
    print("rea_session selftest: %s" % ("FAILED" if failed else "ok"))
    return 1 if failed else 0


def main() -> int:
    if "--selftest" in sys.argv[1:]:
        return selftest()
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("call", help="run one REA tool and print its answer")
    c.add_argument("tool")
    c.add_argument("pairs", nargs="*", help="key=text or key:=json")
    c.add_argument("--args-file", help="JSON object with more arguments")
    c.add_argument("--tag", help="name of the answer file (default: time, pid and tool)")
    c.add_argument("--max", type=int, default=8000, help="characters of the answer to print (default 8000)")
    c.add_argument("--timeout", type=float, default=900.0, help="seconds to wait (default 900)")
    c.set_defaults(fn=cmd_call)
    t = sub.add_parser("tools", help="list the server's tools with argument names")
    t.add_argument("filter", nargs="?")
    t.add_argument("--schema", help="print one tool's full definition")
    t.add_argument("--timeout", type=float, default=300.0)
    t.set_defaults(fn=cmd_tools)
    sub.add_parser("start", help="start the session if none is running").set_defaults(
        fn=lambda _a: (ensure_session(), print("session running"), 0)[2])
    sub.add_parser("status", help="is a session running, and its last log lines").set_defaults(fn=cmd_status)
    sub.add_parser("stop", help="end the session").set_defaults(fn=cmd_stop)
    sub.add_parser("serve", help="(internal) the background session").set_defaults(fn=lambda _a: serve())
    sub.add_parser("_fake", help="(internal) selftest server").set_defaults(fn=lambda _a: fake_server())
    a = parser.parse_args()
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
