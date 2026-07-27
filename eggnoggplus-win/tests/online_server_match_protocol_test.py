"""Runtime test for server capability advertisement and shared match auth."""

from __future__ import annotations

import json
import os
from pathlib import Path
import re
import shutil
import socket
import struct
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
        self.name = "unauthenticated client"
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
            try:
                raw = self.stream.readline(512 * 1024 + 1)
            except TimeoutError as exc:
                raise AssertionError(
                    f"{self.name} timed out waiting for {wanted}"
                ) from exc
            if not raw:
                raise AssertionError(f"connection closed before {wanted}")
            assert len(raw) <= 512 * 1024 and raw.endswith(b"\n"), "oversized server line"
            message = json.loads(raw)
            if message.get("type") == wanted:
                return message
            if message.get("type") == "error":
                raise AssertionError(
                    f"{self.name} received server error while waiting for {wanted}: "
                    f"{message.get('message', 'unknown error')}"
                )
        raise AssertionError(f"server did not send {wanted}")

    def receive_snapshot(self) -> list[dict[str, object]]:
        self.receive_type("friend_snapshot_begin")
        messages: list[dict[str, object]] = []
        for _ in range(256):
            raw = self.stream.readline(512 * 1024 + 1)
            if not raw:
                raise AssertionError("connection closed during friend snapshot")
            assert len(raw) <= 512 * 1024 and raw.endswith(b"\n"), "oversized server line"
            message = json.loads(raw)
            if message.get("type") == "friend_snapshot_end":
                return messages
            messages.append(message)
        raise AssertionError("friend snapshot did not terminate")


def assert_protocol(message: dict[str, object]) -> None:
    assert message.get("control_protocol") == 3
    assert message.get("match_protocol") == 3
    assert message.get("p2p_protocol") == 17
    assert message.get("cap_p2p_auth") == 1
    assert message.get("cap_social_controls") == 1
    assert message.get("cap_private_rematch") == 1
    assert message.get("cap_p2p_relay") == 1


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
                "REMATCH_TTL_MS": "750",
            }
        )
        server_log_path = temp_dir / "server.log"
        server_log = server_log_path.open("w", encoding="utf-8")
        process = subprocess.Popen(
            [node, str(SERVER)],
            cwd=SERVER_DIR,
            env=env,
            stdout=server_log,
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
                client.name = f"protocol_test_{index}"
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
                assert found[0].get("opponent") == pair[1].name
                assert found[1].get("opponent") == pair[0].name
                assert all(message.get("match_protocol") == 3 for message in found)
                assert all(message.get("p2p_protocol") == 17 for message in found)
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
                    client.name = f"protocol_{label}_{index}"
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

            # Friend challenges use the server-computed map intersection. The
            # challenger sees every compatible key, chooses one, and the server
            # validates that choice again before issuing role-local selectors.
            clients[0].send(
                {
                    "type": "map_manifest",
                    "maps": [
                        {
                            "key": "custom:shared:abc",
                            "selector": 11,
                            "label": "Shared Spring Hall",
                            "kind": "custom",
                        },
                        {
                            "key": "custom:host-only:def",
                            "selector": 12,
                            "label": "Host Only",
                            "kind": "custom",
                        },
                    ],
                }
            )
            clients[1].send(
                {
                    "type": "map_manifest",
                    "maps": [
                        {
                            "key": "custom:shared:abc",
                            "selector": 27,
                            "label": "Receiver Copy",
                            "kind": "custom",
                        },
                        {
                            "key": "custom:join-only:ghi",
                            "selector": 28,
                            "label": "Join Only",
                            "kind": "custom",
                        },
                    ],
                }
            )
            clients[0].send(
                {"type": "friend_request", "username": "protocol_test_1"}
            )
            clients[0].receive_type("friend_request_sent")
            request = clients[1].receive_type("friend_request")
            assert request.get("from") == "protocol_test_0"
            clients[1].send(
                {"type": "friend_accept", "username": "protocol_test_0"}
            )
            accepted_friend = clients[0].receive_type("friend")
            assert accepted_friend.get("username") == "protocol_test_1"
            assert accepted_friend.get("presence") == "online"

            # Presence is derived from server-owned queue/match state rather
            # than a client-supplied status string.
            clients[1].send({"type": "join_queue", "queue": "casual"})
            clients[1].receive_type("queue_joined")
            queued_presence = clients[0].receive_snapshot()
            assert any(
                message.get("type") == "friend"
                and message.get("username") == "protocol_test_1"
                and message.get("presence") == "queue_casual"
                for message in queued_presence
            )
            clients[1].send({"type": "leave_queue"})
            clients[1].receive_type("queue_left")
            online_presence = clients[0].receive_snapshot()
            assert any(
                message.get("type") == "friend"
                and message.get("username") == "protocol_test_1"
                and message.get("presence") == "online"
                for message in online_presence
            )

            # Mute is persistent and per-recipient: it suppresses only the
            # receiver's toast. The challenge remains visible and actionable.
            clients[1].send(
                {
                    "type": "friend_mute",
                    "username": "protocol_test_0",
                    "muted": 1,
                }
            )
            muted_update = clients[1].receive_type("social_update")
            assert muted_update.get("action") == "muted"
            muted_snapshot = clients[1].receive_snapshot()
            muted_friend = next(
                message
                for message in muted_snapshot
                if message.get("type") == "friend"
                and message.get("username") == "protocol_test_0"
            )
            assert muted_friend.get("muted") == 1
            persisted = json.loads((temp_dir / "users.json").read_text(encoding="utf-8"))
            assert "protocol_test_0" in persisted["users"]["protocol_test_1"]["muted_users"]

            clients[0].send(
                {
                    "type": "challenge_maps",
                    "username": "protocol_test_1",
                    "request_id": 73,
                }
            )
            picker_begin = clients[0].receive_type("challenge_maps_begin")
            assert picker_begin.get("request_id") == 73
            assert picker_begin.get("count") == 1
            picker_choice = clients[0].receive_type("challenge_map_choice")
            assert picker_choice.get("key") == "custom:shared:abc"
            assert picker_choice.get("label") == "Shared Spring Hall"
            picker_end = clients[0].receive_type("challenge_maps_end")
            assert picker_end.get("request_id") == 73
            assert picker_end.get("count") == 1

            clients[0].send(
                {
                    "type": "challenge",
                    "username": "protocol_test_1",
                    "map_key": "custom:host-only:def",
                }
            )
            invalid_choice = clients[0].receive_type("error")
            assert "no longer compatible" in str(invalid_choice.get("message", ""))

            clients[0].send(
                {
                    "type": "challenge",
                    "username": "protocol_test_1",
                    "map_key": "custom:shared:abc",
                }
            )
            sent_challenge = clients[0].receive_type("challenge_sent")
            assert sent_challenge.get("map_key") == "custom:shared:abc"
            incoming_challenge = clients[1].receive_type("challenge")
            assert incoming_challenge.get("map_key") == "custom:shared:abc"
            assert incoming_challenge.get("map_label") == "Shared Spring Hall"
            assert incoming_challenge.get("muted") == 1
            clients[1].send(
                {
                    "type": "friend_mute",
                    "username": "protocol_test_0",
                    "muted": 0,
                }
            )
            unmuted_update = clients[1].receive_type("social_update")
            assert unmuted_update.get("action") == "unmuted"
            unmuted_snapshot = clients[1].receive_snapshot()
            assert any(
                message.get("type") == "challenge"
                and message.get("id") == sent_challenge["id"]
                and message.get("muted") == 0
                for message in unmuted_snapshot
            )
            clients[1].send(
                {
                    "type": "challenge_accept",
                    "id": sent_challenge["id"],
                    "username": "protocol_test_0",
                }
            )
            challenge_found = [
                client.receive_type("match_found") for client in clients
            ]
            assert all(
                message.get("source") == "challenge"
                and message.get("map_key") == "custom:shared:abc"
                and message.get("map_label") == "Shared Spring Hall"
                for message in challenge_found
            )
            assert challenge_found[0].get("map_sel") == 11
            assert challenge_found[1].get("map_sel") == 27
            for client in clients:
                setup_presence = client.receive_snapshot()
                assert any(
                    message.get("type") == "friend"
                    and message.get("presence") == "match_setup"
                    for message in setup_presence
                )
            challenge_match_id = int(challenge_found[0]["match_id"])
            clients[0].send(
                {
                    "type": "match_abort",
                    "match_id": challenge_match_id,
                    "reason": "picker test complete",
                }
            )
            picker_aborts = [
                client.receive_type("match_abort") for client in clients
            ]
            assert all(
                message.get("match_id") == challenge_match_id
                for message in picker_aborts
            )

            # A setup abort cancels both clients without manufacturing a winner.
            matches = make_match(queue="competitive")
            pending_id = int(matches[0]["match_id"])
            udp_peers = [socket.socket(socket.AF_INET, socket.SOCK_DGRAM) for _ in clients]
            try:
                for udp_peer in udp_peers:
                    udp_peer.bind(("127.0.0.1", 0))
                    udp_peer.settimeout(2.0)
                for index, udp_peer in enumerate(udp_peers):
                    udp_peer.sendto(
                        json.dumps(
                            {
                                "type": "p2p_probe",
                                "match_id": pending_id,
                                "username": clients[index].name,
                                "token": matches[index]["p2p_token"],
                                "local_port": udp_peer.getsockname()[1],
                            },
                            separators=(",", ":"),
                        ).encode(),
                        ("127.0.0.1", port),
                    )
                direct_routes = [
                    client.receive_type("p2p_peer") for client in clients
                ]
                assert all(message.get("peer_route") != "relay" for message in direct_routes)

                # A fresh local socket is the client's bounded direct-path
                # retry. The server switches both peers, not only the retrying
                # side, to one symmetric relay route.
                udp_peers[0].close()
                udp_peers[0] = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                udp_peers[0].bind(("127.0.0.1", 0))
                udp_peers[0].settimeout(2.0)
                udp_peers[0].sendto(
                    json.dumps(
                        {
                            "type": "p2p_probe",
                            "match_id": pending_id,
                            "username": clients[0].name,
                            "token": matches[0]["p2p_token"],
                            "local_port": udp_peers[0].getsockname()[1],
                        },
                        separators=(",", ":"),
                    ).encode(),
                    ("127.0.0.1", port),
                )
                relay_routes = [
                    client.receive_type("p2p_peer") for client in clients
                ]
                assert all(
                    message.get("peer_route") == "relay"
                    and message.get("peer_host") == "relay"
                    and message.get("peer_port") == port
                    for message in relay_routes
                )

                sender_player = int(matches[0]["role"])
                relayed_packet = struct.pack(
                    "<IHHQIQ16s",
                    0x50474E45,
                    17,
                    1,
                    0x1122334455667788,
                    sender_player,
                    1,
                    b"\0" * 16,
                )
                udp_peers[0].sendto(relayed_packet, ("127.0.0.1", port))
                received, _ = udp_peers[1].recvfrom(2048)
                assert received == relayed_packet
            finally:
                for udp_peer in udp_peers:
                    udp_peer.close()
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
            for client in clients:
                in_match_presence = client.receive_snapshot()
                assert any(
                    message.get("type") == "friend"
                    and message.get("presence") == "in_match"
                    for message in in_match_presence
                )
            synchronized_winner = int(matches[0]["role"])
            clients[0].send({
                "type": "match_end",
                "match_id": normal_id,
                # The synchronized slot is authoritative even if a client's
                # local win/loss interpretation is inverted.
                "result": "loss",
                "winner_player": synchronized_winner,
            })
            clients[0].receive_type("match_report_ack")
            clients[1].send({
                "type": "match_end",
                "match_id": normal_id,
                "result": "loss",
                "winner_player": synchronized_winner,
            })
            results = [client.receive_type("match_result") for client in clients]
            assert results[0].get("result") == "win"
            assert results[1].get("result") == "loss"
            assert all(
                message.get("rematch_available") == 1
                and message.get("rematch_unranked") == 1
                and message.get("rematch_expires_in", 0) >= 1
                for message in results
            )
            for client in clients:
                restored_presence = client.receive_snapshot()
                assert any(
                    message.get("type") == "friend"
                    and message.get("presence") == "online"
                    for message in restored_presence
                )

            # A private rematch is a bilateral, server-owned vote. It preserves
            # the exact map, starts a fresh authenticated match, and is always
            # unranked even when the completed source match was competitive.
            clients[0].send({"type": "rematch_request", "match_id": normal_id})
            waiting = clients[0].receive_type("rematch_waiting")
            offer = clients[1].receive_type("rematch_offer")
            assert waiting.get("match_id") == normal_id
            assert offer.get("match_id") == normal_id
            assert offer.get("from") == "protocol_test_0"
            assert offer.get("unranked") == 1
            clients[1].send({"type": "rematch_request", "match_id": normal_id})
            for client in clients:
                assert client.receive_type("rematch_starting").get("match_id") == normal_id
            rematch_found = [client.receive_type("match_found") for client in clients]
            assert all(message.get("source") == "rematch" for message in rematch_found)
            assert all(message.get("queue") == "" for message in rematch_found)
            assert rematch_found[0].get("map_key") == matches[0].get("map_key")
            assert rematch_found[1].get("map_key") == matches[1].get("map_key")
            rematch_match_id = int(rematch_found[0]["match_id"])
            assert rematch_match_id != normal_id
            clients[0].send(
                {
                    "type": "match_abort",
                    "match_id": rematch_match_id,
                    "reason": "bilateral rematch test complete",
                }
            )
            for client in clients:
                client.receive_type("match_abort")

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
            # The winner can observe the P2P teardown after the committed
            # forfeit has already resolved server-side and queue its own stale
            # loss report. Replaying the exact terminal win is idempotent and
            # must not surface "invalid or stale match result".
            clients[1].send({"type": "match_end", "match_id": active_id, "result": "loss"})
            replayed_forfeit = clients[1].receive_type("match_result")
            assert replayed_forfeit.get("match_id") == active_id
            assert replayed_forfeit.get("result") == "win"

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
            assert disconnect_result.get("rematch_available") == 0

            def finish_for_rematch(pair: list[Client]) -> tuple[int, list[dict[str, object]]]:
                found = make_match(pair)
                match_id = int(found[0]["match_id"])
                for participant in pair:
                    participant.send({"type": "match_started", "match_id": match_id})
                for participant in pair:
                    participant.receive_type("match_started")
                pair[0].send({"type": "match_end", "match_id": match_id, "result": "win"})
                pair[0].receive_type("match_report_ack")
                pair[1].send({"type": "match_end", "match_id": match_id, "result": "loss"})
                terminal = [participant.receive_type("match_result") for participant in pair]
                assert all(message.get("rematch_available") == 1 for message in terminal)
                return match_id, terminal

            decline_pair = new_registered_pair("rdecl")
            decline_id, _ = finish_for_rematch(decline_pair)
            decline_pair[0].send({"type": "rematch_request", "match_id": decline_id})
            decline_pair[0].receive_type("rematch_waiting")
            decline_pair[1].receive_type("rematch_offer")
            decline_pair[1].send({"type": "rematch_decline", "match_id": decline_id})
            assert decline_pair[1].receive_type("rematch_closed").get("match_id") == decline_id
            assert decline_pair[0].receive_type("rematch_declined").get("match_id") == decline_id
            decline_pair[0].send(
                {"type": "match_end", "match_id": decline_id, "result": "win"}
            )
            replay_after_decline = decline_pair[0].receive_type("match_result")
            assert replay_after_decline.get("rematch_available") == 0

            queue_pair = new_registered_pair("rqueue")
            queue_id, _ = finish_for_rematch(queue_pair)
            queue_pair[0].send({"type": "rematch_request", "match_id": queue_id})
            queue_pair[0].receive_type("rematch_waiting")
            queue_pair[1].receive_type("rematch_offer")
            queue_pair[1].send({"type": "join_queue", "queue": "casual"})
            assert queue_pair[1].receive_type("rematch_closed").get("match_id") == queue_id
            assert (
                queue_pair[0].receive_type("rematch_unavailable").get("match_id")
                == queue_id
            )
            queue_pair[1].receive_type("queue_joined")
            queue_pair[1].send({"type": "leave_queue"})
            queue_pair[1].receive_type("queue_left")

            offer_disconnect_pair = queue_pair
            offer_disconnect_id, _ = finish_for_rematch(offer_disconnect_pair)
            offer_disconnect_pair[0].send(
                {"type": "rematch_request", "match_id": offer_disconnect_id}
            )
            offer_disconnect_pair[0].receive_type("rematch_waiting")
            offer_disconnect_pair[1].receive_type("rematch_offer")
            offer_disconnect_pair[1].close()
            assert (
                offer_disconnect_pair[0]
                .receive_type("rematch_unavailable")
                .get("match_id")
                == offer_disconnect_id
            )

            block_pair = new_registered_pair("rblock")
            block_id, _ = finish_for_rematch(block_pair)
            block_pair[0].send({"type": "rematch_request", "match_id": block_id})
            block_pair[0].receive_type("rematch_waiting")
            block_pair[1].receive_type("rematch_offer")
            block_pair[1].send(
                {"type": "friend_block", "username": "protocol_rblock_0"}
            )
            assert block_pair[1].receive_type("rematch_closed").get("match_id") == block_id
            assert (
                block_pair[0].receive_type("rematch_unavailable").get("match_id")
                == block_id
            )

            expiry_pair = new_registered_pair("rexp")
            expiry_id, _ = finish_for_rematch(expiry_pair)
            expired_messages = [
                participant.receive_type("rematch_expired") for participant in expiry_pair
            ]
            assert all(message.get("match_id") == expiry_id for message in expired_messages)

            # Blocking is bilateral for interaction checks, private in its
            # snapshot data, persistent, and excludes queue pairings. It does
            # not silently restore friendship when later removed.
            social_pair = new_registered_pair("social")
            social_pair[0].send(
                {"type": "friend_request", "username": "protocol_social_1"}
            )
            social_pair[0].receive_type("friend_request_sent")
            assert (
                social_pair[1].receive_type("friend_request").get("from")
                == "protocol_social_0"
            )
            social_pair[1].send(
                {"type": "friend_accept", "username": "protocol_social_0"}
            )
            social_pair[0].receive_type("friend")

            social_pair[0].send(
                {
                    "type": "challenge",
                    "username": "protocol_social_1",
                    "map_key": "vanilla:0",
                }
            )
            pending_social_challenge = social_pair[0].receive_type("challenge_sent")
            social_pair[1].receive_type("challenge")
            social_pair[1].send(
                {"type": "friend_block", "username": "protocol_social_0"}
            )
            block_update = social_pair[1].receive_type("social_update")
            assert block_update.get("action") == "blocked"
            blocker_snapshot = social_pair[1].receive_snapshot()
            blocked_entry = next(
                message
                for message in blocker_snapshot
                if message.get("type") == "blocked_user"
                and message.get("username") == "protocol_social_0"
            )
            assert "online" not in blocked_entry and "elo" not in blocked_entry
            expired = social_pair[0].receive_type("challenge_expired")
            assert expired.get("id") == pending_social_challenge.get("id")
            blocked_target_snapshot = social_pair[0].receive_snapshot()
            assert not any(
                message.get("type") == "friend"
                and message.get("username") == "protocol_social_1"
                for message in blocked_target_snapshot
            )
            persisted = json.loads((temp_dir / "users.json").read_text(encoding="utf-8"))
            assert "protocol_social_0" in persisted["users"]["protocol_social_1"]["blocked_users"]

            social_pair[0].send(
                {"type": "friend_request", "username": "protocol_social_1"}
            )
            assert "unavailable" in str(
                social_pair[0].receive_type("error").get("message", "")
            )
            social_pair[0].send(
                {
                    "type": "challenge",
                    "username": "protocol_social_1",
                    "map_key": "vanilla:0",
                }
            )
            assert "unavailable" in str(
                social_pair[0].receive_type("error").get("message", "")
            )

            social_extra_pair = new_registered_pair("social_extra")
            social_pair[0].send({"type": "join_queue", "queue": "casual"})
            social_pair[1].send({"type": "join_queue", "queue": "casual"})
            social_extra_pair[0].send({"type": "join_queue", "queue": "casual"})
            allowed_match = [
                social_pair[0].receive_type("match_found"),
                social_extra_pair[0].receive_type("match_found"),
            ]
            assert allowed_match[0].get("opponent") == "protocol_social_extra_0"
            assert allowed_match[1].get("opponent") == "protocol_social_0"
            allowed_match_id = int(allowed_match[0]["match_id"])
            social_pair[0].send(
                {
                    "type": "match_abort",
                    "match_id": allowed_match_id,
                    "reason": "social exclusion test complete",
                }
            )
            social_pair[0].receive_type("match_abort")
            social_extra_pair[0].receive_type("match_abort")
            social_pair[1].send({"type": "leave_queue"})
            social_pair[1].receive_type("queue_left")

            social_pair[1].send(
                {"type": "friend_unblock", "username": "protocol_social_0"}
            )
            unblock_update = social_pair[1].receive_type("social_update")
            assert unblock_update.get("action") == "unblocked"
            unblocked_snapshot = social_pair[1].receive_snapshot()
            assert not any(
                message.get("type") in {"friend", "blocked_user"}
                and message.get("username") == "protocol_social_0"
                for message in unblocked_snapshot
            )
        finally:
            test_failed = sys.exc_info()[0] is not None
            for client in clients:
                try:
                    client.close()
                except OSError:
                    pass
            process.terminate()
            try:
                process.wait(timeout=3.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3.0)
            server_log.close()
            if test_failed or process.returncode not in (0, -15, 1):
                print(server_log_path.read_text(encoding="utf-8"), file=sys.stderr)

    print("online server match protocol runtime checks: OK")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError, AssertionError) as exc:
        print(f"online_server_match_protocol_test: FAILED: {exc}", file=sys.stderr)
        raise SystemExit(1)
