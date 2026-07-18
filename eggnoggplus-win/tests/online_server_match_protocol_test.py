"""Runtime test for server capability advertisement and shared match auth."""

from __future__ import annotations

import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]
SERVER_DIR = ROOT / "online_server"
SERVER = SERVER_DIR / "server.js"
PREFLIGHT = SERVER_DIR / "check_deployment.py"
TOKEN_RE = re.compile(r"[0-9a-f]{64}")
PROBE_TOKEN_RE = re.compile(r"[0-9a-f]{32}")


def reserve_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


class Client:
    def __init__(self, port: int) -> None:
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=3.0)
        self.sock.settimeout(3.0)
        self.stream = self.sock.makefile("rwb", buffering=0)

    def close(self) -> None:
        self.stream.close()
        self.sock.close()

    def send(self, message: dict[str, object]) -> None:
        self.stream.write(json.dumps(message, separators=(",", ":")).encode() + b"\n")

    def receive_type(self, wanted: str) -> dict[str, object]:
        for _ in range(64):
            raw = self.stream.readline(512 * 1024 + 1)
            if not raw:
                raise AssertionError(f"connection closed before {wanted}")
            assert len(raw) <= 512 * 1024 and raw.endswith(b"\n"), "oversized server line"
            message = json.loads(raw)
            if message.get("type") == wanted:
                return message
        raise AssertionError(f"server did not send {wanted}")


def assert_protocol(message: dict[str, object]) -> None:
    assert message.get("control_protocol") == 2
    assert message.get("match_protocol") == 2
    assert message.get("p2p_protocol") == 16
    assert message.get("cap_p2p_auth") == 1


def main() -> int:
    node = shutil.which("node")
    if not node:
        raise RuntimeError("node is required for the online server runtime test")

    port = reserve_port()
    with tempfile.TemporaryDirectory(prefix="eggnogg-server-test-") as temp:
        temp_dir = Path(temp)
        env = os.environ.copy()
        env.update(
            {
                "HOST": "127.0.0.1",
                "PORT": str(port),
                "UDP_HOST": "127.0.0.1",
                "UDP_PORT": str(port),
                "DB": str(temp_dir / "users.json"),
                "RATINGS": str(temp_dir / "ratings.json"),
                "SECRET_FILE": str(temp_dir / "server_secret.key"),
            }
        )
        process = subprocess.Popen(
            [node, str(SERVER)],
            cwd=SERVER_DIR,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        clients: list[Client] = []
        try:
            probe = None
            deadline = time.monotonic() + 5.0
            while time.monotonic() < deadline:
                probe = subprocess.run(
                    [sys.executable, str(PREFLIGHT), "127.0.0.1", str(port), "--timeout", "0.5"],
                    capture_output=True,
                    text=True,
                )
                if probe.returncode == 0:
                    break
                if process.poll() is not None:
                    break
                time.sleep(0.05)
            assert probe is not None and probe.returncode == 0, (
                probe.stderr.strip() if probe else "deployment probe did not run"
            )

            clients = [Client(port), Client(port)]
            for client in clients:
                assert_protocol(client.receive_type("server_info"))

            for index, client in enumerate(clients):
                client.send(
                    {
                        "type": "register",
                        "username": f"protocol_test_{index}",
                        "password": "test-password",
                    }
                )
                assert_protocol(client.receive_type("auth_ok"))

            clients[0].send({"type": "join_queue", "queue": "casual"})
            clients[1].send({"type": "join_queue", "queue": "casual"})
            matches = [client.receive_type("match_found") for client in clients]

            assert matches[0].get("match_id") == matches[1].get("match_id")
            assert matches[0].get("match_protocol") == 2
            assert matches[1].get("match_protocol") == 2
            assert matches[0].get("p2p_protocol") == 16
            assert matches[1].get("p2p_protocol") == 16
            assert {matches[0].get("p2p_role"), matches[1].get("p2p_role")} == {"host", "join"}

            auth_tokens = [message.get("p2p_auth_token") for message in matches]
            assert all(isinstance(token, str) and TOKEN_RE.fullmatch(token) for token in auth_tokens)
            assert auth_tokens[0] == auth_tokens[1], "peers received different packet-auth keys"

            probe_tokens = [message.get("p2p_token") for message in matches]
            assert all(isinstance(token, str) and PROBE_TOKEN_RE.fullmatch(token) for token in probe_tokens)
            assert probe_tokens[0] != probe_tokens[1], "per-user rendezvous tokens must stay distinct"
        finally:
            for client in clients:
                try:
                    client.close()
                except OSError:
                    pass
            process.terminate()
            try:
                output, _ = process.communicate(timeout=3.0)
            except subprocess.TimeoutExpired:
                process.kill()
                output, _ = process.communicate(timeout=3.0)
            if process.returncode not in (0, -15, 1):
                print(output, file=sys.stderr)

    print("online server match protocol runtime checks: OK")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError, AssertionError) as exc:
        print(f"online_server_match_protocol_test: FAILED: {exc}", file=sys.stderr)
        raise SystemExit(1)
