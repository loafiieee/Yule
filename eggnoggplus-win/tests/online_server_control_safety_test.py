"""Disposable loopback regressions for control framing and session ownership."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

from online_server_match_protocol_test import Client, SERVER, SERVER_DIR, reserve_port


def main():
    with tempfile.TemporaryDirectory(prefix="eggnogg-control-safety-") as temp:
        root = Path(temp)
        port = reserve_port()
        env = dict(os.environ, HOST="127.0.0.1", PORT=str(port), UDP_HOST="127.0.0.1",
                   UDP_PORT=str(port), DB=str(root / "users.json"),
                   RATINGS=str(root / "ratings.json"), SECRET_FILE=str(root / "secret"),
                   ADMIN_ENABLED="0", DISCORD_LFG_ENABLED="0", CLIENT_IDLE_TIMEOUT_MS="10000")
        with (root / "output.log").open("w") as output:
            process = subprocess.Popen([shutil.which("node"), str(SERVER)], cwd=SERVER_DIR,
                                       env=env, stdout=output, stderr=subprocess.STDOUT)
            clients = []
            try:
                deadline = time.monotonic() + 5
                while True:
                    try:
                        first = Client(port)
                        clients.append(first)
                        break
                    except OSError:
                        if time.monotonic() >= deadline or process.poll() is not None:
                            raise
                        time.sleep(.05)
                first.receive_type("server_info")
                first.send({"type": "login", "username": {"toString": None}, "password": "fixture"})
                assert first.receive_type("error")["message"] == "invalid request"
                first.send({"type": "login", "username": "constructor", "password": "fixture"})
                assert first.receive_type("auth_fail")["reason"] == "unknown user"
                first.send({"type": "register", "username": "__proto__", "password": "fixture"})
                assert first.receive_type("auth_ok")["username"] == "__proto__"
                for action in ("login", "register"):
                    first.send({"type": action, "username": "identity_swap", "password": "fixture"})
                    assert "already authenticated" in first.receive_type("auth_fail")["reason"]
                assert "identity_swap" not in json.loads((root / "users.json").read_text())["users"]
                first.send({"type": "ping", "seq": 17})
                assert first.receive_type("pong")["seq"] == 17

                # No newline, whitespace-only lines, and multibyte UTF-8 must
                # all enforce the wire byte bound before JSON dispatch.
                for payload in (b"x" * (512 * 1024 + 1), b" " * (512 * 1024 + 1) + b"\n",
                                ('"' + 'é' * (270 * 1024) + '"\n').encode()):
                    client = Client(port)
                    clients.append(client)
                    client.receive_type("server_info")
                    client.sock.sendall(payload)
                    assert client.receive_type("error")["message"] == "message too long"
                    assert client.stream.readline() == b""
                assert process.poll() is None
            finally:
                for client in clients:
                    client.close()
                process.terminate()
                process.wait(timeout=5)
    print("online server control safety: OK")


if __name__ == "__main__":
    main()
