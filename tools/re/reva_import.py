"""One-time setup: import a binary into the kept ReVa project and run Ghidra's auto-analysis on it.

Starts tools\\re\\reva_mcp.cmd as a stdio MCP server, calls `import-file` (skipped when the program is
already in the project), then `analyze-program` and polls `analysis-status` until the job ends. The
analysis is saved in the project (toolchain\\re\\reva-work\\foc), so later ReVa sessions open it as is.

    toolchain\\re\\venv\\Scripts\\python.exe -I tools\\re\\reva_import.py [path to exe] [--log <file>]

Default exe: StarWarsG.exe under ALAMO_FOC_DATA, else Gabriel's Steam install (tools\\test_env.cmd).
RE / doc tasks only (clean-room rule in CLAUDE.md).
"""
import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_FOC = r"F:\SteamLibrary\steamapps\common\Star Wars Empire at War\corruption"


class Client:
    def __init__(self, log_path: Path) -> None:
        self.err = open(log_path, "a", encoding="utf-8")
        self.proc = subprocess.Popen(["cmd", "/c", str(HERE / "reva_mcp.cmd")], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=self.err, text=True, encoding="utf-8")
        self.next_id = 0
        self.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                    "clientInfo": {"name": "reva_import", "version": "1"}})
        self.send({"jsonrpc": "2.0", "method": "notifications/initialized"})

    def send(self, message: dict) -> None:
        self.proc.stdin.write(json.dumps(message) + "\n")
        self.proc.stdin.flush()

    def request(self, method: str, params: dict) -> dict:
        self.next_id += 1
        self.send({"jsonrpc": "2.0", "id": self.next_id, "method": method, "params": params})
        while True:
            line = self.proc.stdout.readline()
            if not line:
                raise SystemExit("ReVa closed its output; see the log file")
            try:
                message = json.loads(line)
            except ValueError:
                continue
            if message.get("id") == self.next_id:
                if "error" in message:
                    raise SystemExit(f"{method} failed: {message['error']}")
                return message["result"]

    def call(self, tool: str, arguments: dict) -> str:
        result = self.request("tools/call", {"name": tool, "arguments": arguments})
        return "\n".join(part.get("text", "") for part in result.get("content", []))

    def close(self) -> None:
        self.proc.stdin.close()
        try:
            self.proc.wait(120)
        except subprocess.TimeoutExpired:
            self.proc.kill()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    foc = os.environ.get("ALAMO_FOC_DATA", DEFAULT_FOC)
    parser.add_argument("exe", nargs="?", default=str(Path(foc) / "StarWarsG.exe"))
    parser.add_argument("--log", default=str(HERE.parent.parent / "toolchain" / "re" / "reva-work" / "import.log"))
    args = parser.parse_args()
    exe = Path(args.exe)
    if not exe.is_file():
        print(f"not found: {exe}")
        return 1
    Path(args.log).parent.mkdir(parents=True, exist_ok=True)
    program = "/" + exe.name
    client = Client(Path(args.log))
    try:
        listing = client.call("list-project-files", {"folderPath": "/", "recursive": True})
        if exe.name in listing:
            print(f"{program} already in the project", flush=True)
        else:
            print(client.call("import-file", {"path": str(exe), "enableVersionControl": False,
                                              "analyzeAfterImport": False})[:1500], flush=True)
        start = time.time()
        text = client.call("analyze-program", {"programPath": program, "timeoutSeconds": -1,
                                               "persist": "save", "waitSeconds": 0})
        print(text[:1500], flush=True)
        while True:
            text = client.call("analysis-status", {"programPath": program, "waitSeconds": 120, "maxLogEntries": 5})
            try:
                status = json.loads(text).get("status", "?")
            except ValueError:
                status = "?"
            print(f"[{time.time() - start:6.0f} s] status {status}", flush=True)
            if status != "running":
                print(text[:3000], flush=True)
                break
        print("functions:", client.call("get-function-count", {"programPath": program})[:300], flush=True)
        return 0 if status in ("completed", "complete", "done", "success") else 2
    finally:
        client.close()


if __name__ == "__main__":
    sys.exit(main())
