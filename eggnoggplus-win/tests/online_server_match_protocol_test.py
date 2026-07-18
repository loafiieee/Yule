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
    assert message.get("match_protocol") == 3
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
                "MATCH_REPORT_TIMEOUT_MS": "250",
                "MATCH_SETUP_STALE_MS": "3000",
                "MATCH_SWEEP_INTERVAL_MS": "50",
                "CLIENT_IDLE_TIMEOUT_MS": "1000",
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

            def make_match(
                pair: list[Client] | None = None,
                queue: str = "casual",
            ) -> list[dict[str, object]]:
                pair = pair or clients[:2]
                pair[0].send({"type": "join_queue", "queue": queue})
                pair[1].send({"type": "join_queue", "queue": queue})
                found = [client.receive_type("match_found") for client in pair]
                assert found[0].get("match_id") == found[1].get("match_id")
                assert all(message.get("match_protocol") == 3 for message in found)
                assert all(message.get("p2p_protocol") == 16 for message in found)
                assert {message.get("p2p_role") for message in found} == {"host", "join"}
                auth_tokens = [message.get("p2p_auth_token") for message in found]
                assert all(isinstance(token, str) and TOKEN_RE.fullmatch(token) for token in auth_tokens)
                assert auth_tokens[0] == auth_tokens[1], "peers received different packet-auth keys"
                probe_tokens = [message.get("p2p_token") for message in found]
                assert all(isinstance(token, str) and PROBE_TOKEN_RE.fullmatch(token) for token in probe_tokens)
                assert probe_tokens[0] != probe_tokens[1], "per-user rendezvous tokens must stay distinct"
                return found

            def new_registered_pair(label: str) -> list[Client]:
                pair = [Client(port), Client(port)]
                clients.extend(pair)
                for index, client in enumerate(pair):
                    assert_protocol(client.receive_type("server_info"))
                    client.send(
                        {
                            "type": "register",
                            "username": f"protocol_{label}_{index}",
                            "password": "test-password",
                        }
                    )
                    assert_protocol(client.receive_type("auth_ok"))
                return pair

            # A setup abort cancels both clients without manufacturing a winner.
            matches = make_match(queue="competitive")
            pending_id = int(matches[0]["match_id"])
            clients[0].send({"type": "match_abort", "match_id": pending_id, "reason": "test setup abort"})
            aborts = [client.receive_type("match_abort") for client in clients]
            assert all(message.get("match_id") == pending_id for message in aborts)

            # A loss report before the two-client start commit is also a cancel,
            # never a delayed YOU WON/LOST result.
            matches = make_match(queue="competitive")
            premature_id = int(matches[0]["match_id"])
            clients[0].send({"type": "match_end", "match_id": premature_id, "result": "loss"})
            premature = [client.receive_type("match_abort") for client in clients]
            assert all(message.get("reason") == "premature match result" for message in premature)

            # A committed competitive match cannot be settled by one client.
            # The confirmation timeout is a no-contest and must not change Elo.
            matches = make_match(queue="competitive")
            unilateral_id = int(matches[0]["match_id"])
            for client in clients:
                client.send({"type": "match_started", "match_id": unilateral_id})
            for client in clients:
                client.receive_type("match_started")
            clients[0].send({"type": "match_end", "match_id": unilateral_id, "result": "win"})
            clients[0].receive_type("match_report_ack")
            unilateral_aborts = [client.receive_type("match_abort") for client in clients]
            assert all("not confirmed" in str(message.get("reason", "")) for message in unilateral_aborts)

            # Two reports that name different winners are also a no-contest;
            # insertion order must never decide a competitive result.
            matches = make_match(queue="competitive")
            assert all(message.get("opponent_elo") == 1000 for message in matches), (
                "unilateral report changed competitive Elo"
            )
            conflict_id = int(matches[0]["match_id"])
            for client in clients:
                client.send({"type": "match_started", "match_id": conflict_id})
            for client in clients:
                client.receive_type("match_started")
            clients[0].send({"type": "match_end", "match_id": conflict_id, "result": "win"})
            clients[1].send({"type": "match_end", "match_id": conflict_id, "result": "win"})
            conflict_aborts = [client.receive_type("match_abort") for client in clients]
            assert all("conflicting" in str(message.get("reason", "")) for message in conflict_aborts)

            # A delayed message from the previous match cannot affect the current
            # one. Both READY messages then commit gameplay, after which ordinary
            # agreed results are accepted.
            matches = make_match(queue="competitive")
            normal_id = int(matches[0]["match_id"])
            assert all(message.get("opponent_elo") == 1000 for message in matches), (
                "pre-commit or disputed result changed competitive Elo"
            )
            clients[0].send({"type": "match_abort", "match_id": premature_id, "reason": "stale"})
            assert "stale" in str(clients[0].receive_type("error").get("message", ""))
            for client in clients:
                client.send({"type": "match_started", "match_id": normal_id})
            commits = [client.receive_type("match_started") for client in clients]
            assert all(message.get("committed") == 1 for message in commits)
            clients[0].send({"type": "match_end", "match_id": normal_id, "result": "win"})
            clients[0].receive_type("match_report_ack")
            clients[1].send({"type": "match_end", "match_id": normal_id, "result": "loss"})
            results = [client.receive_type("match_result") for client in clients]
            assert results[0].get("result") == "win"
            assert results[1].get("result") == "loss"

            # Abort after commit is a forfeit, not a setup cancel.
            matches = make_match()
            active_id = int(matches[0]["match_id"])
            for client in clients:
                client.send({"type": "match_started", "match_id": active_id})
            for client in clients:
                client.receive_type("match_started")
            clients[0].send({"type": "match_abort", "match_id": active_id, "reason": "active abort"})
            forfeits = [client.receive_type("match_result") for client in clients]
            assert forfeits[0].get("result") == "loss"
            assert forfeits[1].get("result") == "win"

            # A committed game is not a setup timeout merely because it runs
            # longer than MATCH_SETUP_STALE_MS. Ordinary ping/pong traffic also
            # keeps it alive past the test's deliberately tiny socket idle TTL.
            matches = make_match()
            long_id = int(matches[0]["match_id"])
            for client in clients[:2]:
                client.send({"type": "match_started", "match_id": long_id})
            for client in clients[:2]:
                client.receive_type("match_started")
            heartbeat_until = time.monotonic() + 3.3
            heartbeat_seq = 0
            while time.monotonic() < heartbeat_until:
                heartbeat_seq += 1
                for client in clients[:2]:
                    client.send({"type": "ping", "seq": heartbeat_seq})
                for client in clients[:2]:
                    pong = client.receive_type("pong")
                    assert pong.get("seq") == heartbeat_seq
                time.sleep(0.15)
            clients[0].send({"type": "match_end", "match_id": long_id, "result": "win"})
            clients[0].receive_type("match_report_ack")
            clients[1].send({"type": "match_end", "match_id": long_id, "result": "loss"})
            long_results = [client.receive_type("match_result") for client in clients[:2]]
            assert long_results[0].get("result") == "win"
            assert long_results[1].get("result") == "loss"

            # Disconnect after only one READY remains a no-result setup cancel.
            half_pair = new_registered_pair("half_ready")
            matches = make_match(half_pair)
            half_started_id = int(matches[0]["match_id"])
            half_pair[0].send({"type": "match_started", "match_id": half_started_id})
            half_pair[0].close()
            disconnected = half_pair[1].receive_type("match_abort")
            assert disconnected.get("match_id") == half_started_id

            # The same disconnect after commit is a forfeit for the live peer.
            disconnect_pair = new_registered_pair("active")
            matches = make_match(disconnect_pair)
            disconnect_id = int(matches[0]["match_id"])
            for client in disconnect_pair:
                client.send({"type": "match_started", "match_id": disconnect_id})
            for client in disconnect_pair:
                client.receive_type("match_started")
            disconnect_pair[0].close()
            disconnect_result = disconnect_pair[1].receive_type("match_result")
            assert disconnect_result.get("match_id") == disconnect_id
            assert disconnect_result.get("result") == "win"
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
