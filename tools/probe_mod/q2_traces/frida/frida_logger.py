"""skai t15: attach a read-only Frida hook script to retail StarWarsG.exe and write its events as JSONL.

Started by run_skai_t15_hooks.ps1 before the Q2 run: waits for StarWarsG.exe, attaches, loads the script
(default skai_t15_hooks.js beside this file), appends every message to --out (one JSON object per line, plus
"attach" / "detach" lines of its own with wall-clock time), and re-attaches if the game process is replaced (Steam may
restart it at launch). Stops when the stop file exists, or after --minutes.

    toolchain\\re\\venv\\Scripts\\python.exe -I tools\\probe_mod\\q2_traces\\frida\\frida_logger.py --out <file.jsonl>
        [--script <js>] [--stop-file <path>] [--minutes 40]

Needs the frida module of toolchain\\re\\venv (toolchain\\14_re_tools.cmd). Exit 0 when it attached at least once.
"""
import argparse
import json
import os
import sys
import threading
import time

import frida

EXE = "StarWarsG.exe"


def find_pid(device):
    for p in device.enumerate_processes():
        if p.name.lower() == EXE.lower():
            return p.pid
    return None


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--script", default=os.path.join(here, "skai_t15_hooks.js"))
    ap.add_argument("--stop-file", default="")
    ap.add_argument("--minutes", type=float, default=40.0)
    a = ap.parse_args()

    with open(a.script, encoding="utf-8") as f:
        source = f.read()
    device = frida.get_local_device()
    lock = threading.Lock()
    out = open(a.out, "a", encoding="utf-8")
    attached = 0
    deadline = time.time() + a.minutes * 60.0

    def write(obj):
        with lock:
            out.write(json.dumps(obj, separators=(",", ":")) + "\n")
            out.flush()

    def stopping():
        return time.time() > deadline or (a.stop_file and os.path.exists(a.stop_file))

    while not stopping():
        pid = find_pid(device)
        if pid is None:
            time.sleep(0.5)
            continue
        gone = threading.Event()
        try:
            session = device.attach(pid)
        except Exception as e:  # noqa: BLE001 - a process that is still starting or already gone
            write({"k": "attach_failed", "pid": pid, "wall": time.time(), "error": str(e)})
            time.sleep(2.0)
            continue
        session.on("detached", lambda reason, crash=None: (write({"k": "detach", "wall": time.time(), "reason": str(reason)}),
                                                          gone.set()))
        script = session.create_script(source)

        def on_message(msg, data, pid=pid):
            if msg.get("type") == "send":
                p = msg.get("payload")
                if isinstance(p, dict):
                    p["wall"] = round(time.time(), 3)
                write(p)
            else:
                write({"k": "script_error", "pid": pid, "wall": time.time(), "msg": msg})

        script.on("message", on_message)
        script.load()
        attached += 1
        write({"k": "attach", "pid": pid, "wall": time.time()})
        print("frida_logger: attached to %s pid %d" % (EXE, pid), flush=True)
        while not gone.is_set() and not stopping():
            gone.wait(1.0)
        if not gone.is_set():
            try:
                session.detach()
            except Exception:  # noqa: BLE001
                pass
        time.sleep(1.0)
    out.close()
    print("frida_logger: done, attached %d time(s), log %s" % (attached, a.out), flush=True)
    return 0 if attached else 1


if __name__ == "__main__":
    sys.exit(main())
