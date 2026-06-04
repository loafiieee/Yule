#!/usr/bin/env python3
"""
Eggnogg+ online control server.

This server owns account login, sessions, public Elo, private MMR, casual queue,
ranked queue, and match assignment. Gameplay packets remain peer-to-peer; the
server only returns which client should host and which client should join.
"""

from __future__ import annotations

import base64
import hashlib
import hmac
import json
import os
import secrets
import signal
import sys
import threading
import time
from dataclasses import asdict, dataclass, field
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any


DEFAULT_HOST = "0.0.0.0"
DEFAULT_PORT = 47778
DEFAULT_DATA_PATH = "online_server_data.json"
DEFAULT_RANKED_MMR_CAP = 250
SESSION_SECONDS = 60 * 60 * 24 * 30
PBKDF2_ROUNDS = 180_000
INITIAL_ELO = 1000
INITIAL_MMR = 1000
K_ELO = 32
K_MMR = 40


def now() -> float:
    return time.time()


def clean_username(value: str) -> str:
    out = "".join(ch for ch in value.strip() if ch.isalnum() or ch in "_-")
    return out[:24]


def clean_queue(value: str) -> str:
    value = value.strip().lower()
    return value if value in ("casual", "ranked") else ""


def clean_host(value: str) -> str:
    value = value.strip()
    if len(value) > 128:
        value = value[:128]
    return value


def clamp_port(value: Any) -> int:
    try:
        port = int(value)
    except Exception:
        return 0
    if port < 0 or port > 65535:
        return 0
    return port


def b64(data: bytes) -> str:
    return base64.b64encode(data).decode("ascii")


def hash_password(password: str, salt: bytes | None = None) -> dict[str, Any]:
    if salt is None:
        salt = secrets.token_bytes(16)
    digest = hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"), salt, PBKDF2_ROUNDS)
    return {
        "algo": "pbkdf2_sha256",
        "rounds": PBKDF2_ROUNDS,
        "salt": b64(salt),
        "hash": b64(digest),
    }


def verify_password(password: str, stored: dict[str, Any]) -> bool:
    try:
        rounds = int(stored.get("rounds", PBKDF2_ROUNDS))
        salt = base64.b64decode(stored.get("salt", ""))
        expected = base64.b64decode(stored.get("hash", ""))
    except Exception:
        return False
    got = hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"), salt, rounds)
    return hmac.compare_digest(got, expected)


def rating_delta(my_rating: int, their_rating: int, score: float, k: int) -> int:
    expected = 1.0 / (1.0 + 10.0 ** ((their_rating - my_rating) / 400.0))
    return int(round(k * (score - expected)))


@dataclass
class Account:
    username: str
    password: dict[str, Any]
    elo: int = INITIAL_ELO
    mmr: int = INITIAL_MMR
    created_at: float = field(default_factory=now)
    last_login_at: float = 0.0


@dataclass
class Session:
    token: str
    username: str
    expires_at: float


@dataclass
class QueueEntry:
    username: str
    queue: str
    listen_host: str
    listen_port: int
    joined_at: float


@dataclass
class MatchAssignment:
    match_id: str
    queue: str
    username: str
    opponent: str
    role: str
    player: int
    peer_host: str
    peer_port: int
    created_at: float
    reported: bool = False


class Store:
    def __init__(self, path: Path, ranked_mmr_cap: int):
        self.path = path
        self.ranked_mmr_cap = ranked_mmr_cap
        self.lock = threading.RLock()
        self.accounts: dict[str, Account] = {}
        self.sessions: dict[str, Session] = {}
        self.queues: dict[str, list[QueueEntry]] = {"casual": [], "ranked": []}
        self.matches: dict[str, dict[str, MatchAssignment]] = {}
        self.pending: dict[str, MatchAssignment] = {}
        self.load()

    def load(self) -> None:
        if not self.path.exists():
            return
        with self.path.open("r", encoding="utf-8") as f:
            data = json.load(f)
        for name, item in data.get("accounts", {}).items():
            self.accounts[name] = Account(**item)

    def save(self) -> None:
        tmp = self.path.with_suffix(self.path.suffix + ".tmp")
        data = {
            "schema": 1,
            "saved_at": now(),
            "accounts": {name: asdict(account) for name, account in self.accounts.items()},
        }
        tmp.parent.mkdir(parents=True, exist_ok=True)
        with tmp.open("w", encoding="utf-8") as f:
            json.dump(data, f, indent=2, sort_keys=True)
        tmp.replace(self.path)

    def account_profile(self, username: str) -> dict[str, Any]:
        account = self.accounts[username]
        return {
            "username": account.username,
            "elo": account.elo,
        }

    def find_account_name(self, username: str) -> str:
        wanted = username.strip().lower()
        for name in self.accounts:
            if name.lower() == wanted:
                return name
        return ""

    def register(self, username: str, password: str) -> dict[str, Any]:
        username = clean_username(username)
        if len(username) < 3:
            raise ValueError("username must be 3-24 letters, numbers, _ or -")
        if len(password) < 6:
            raise ValueError("password must be at least 6 characters")
        with self.lock:
            if username.lower() in (name.lower() for name in self.accounts):
                raise ValueError("username is already taken")
            self.accounts[username] = Account(username=username, password=hash_password(password))
            self.save()
            return self.login(username, password)

    def login(self, username: str, password: str) -> dict[str, Any]:
        username = username.strip()
        with self.lock:
            canonical = self.find_account_name(username)
            account = self.accounts.get(canonical)
            if not account or not verify_password(password, account.password):
                raise ValueError("invalid username or password")
            token = secrets.token_urlsafe(32)
            username = canonical
            self.sessions[token] = Session(token=token, username=username, expires_at=now() + SESSION_SECONDS)
            account.last_login_at = now()
            self.save()
            profile = self.account_profile(username)
            profile["token"] = token
            return profile

    def auth(self, token: str) -> str:
        token = (token or "").strip()
        with self.lock:
            session = self.sessions.get(token)
            if not session or session.expires_at < now():
                if token in self.sessions:
                    del self.sessions[token]
                raise ValueError("not logged in")
            session.expires_at = now() + SESSION_SECONDS
            return session.username

    def cancel_queue_locked(self, username: str) -> None:
        for q in self.queues.values():
            q[:] = [entry for entry in q if entry.username != username]

    def join_queue(self, token: str, queue: str, listen_host: str, listen_port: int) -> dict[str, Any]:
        username = self.auth(token)
        queue = clean_queue(queue)
        listen_host = clean_host(listen_host)
        listen_port = clamp_port(listen_port)
        if not queue:
            raise ValueError("queue must be ranked or casual")
        if not listen_host:
            raise ValueError("listen_host is required for p2p")
        if listen_port <= 0:
            raise ValueError("listen_port is required for p2p")

        with self.lock:
            if username in self.pending:
                return self.status_for(username)
            self.cancel_queue_locked(username)
            entry = QueueEntry(
                username=username,
                queue=queue,
                listen_host=listen_host,
                listen_port=listen_port,
                joined_at=now(),
            )
            opponent = self.find_match_locked(entry)
            if opponent:
                self.queues[queue] = [e for e in self.queues[queue] if e.username != opponent.username]
                self.create_match_locked(opponent, entry)
                return self.status_for(username)
            self.queues[queue].append(entry)
            return {
                "status": "queued",
                "queue": queue,
                "elo": self.accounts[username].elo,
                "queued_count": len(self.queues[queue]),
            }

    def find_match_locked(self, entry: QueueEntry) -> QueueEntry | None:
        candidates = [e for e in self.queues[entry.queue] if e.username != entry.username]
        if not candidates:
            return None
        if entry.queue == "casual":
            return min(candidates, key=lambda e: e.joined_at)
        my_mmr = self.accounts[entry.username].mmr
        allowed = []
        for candidate in candidates:
            their_mmr = self.accounts[candidate.username].mmr
            diff = abs(my_mmr - their_mmr)
            if diff <= self.ranked_mmr_cap:
                allowed.append((diff, candidate.joined_at, candidate))
        if not allowed:
            return None
        allowed.sort(key=lambda item: (item[0], item[1]))
        return allowed[0][2]

    def create_match_locked(self, host: QueueEntry, joiner: QueueEntry) -> None:
        match_id = secrets.token_urlsafe(18)
        created = now()
        host_assignment = MatchAssignment(
            match_id=match_id,
            queue=host.queue,
            username=host.username,
            opponent=joiner.username,
            role="host",
            player=0,
            peer_host=joiner.listen_host,
            peer_port=joiner.listen_port,
            created_at=created,
        )
        join_assignment = MatchAssignment(
            match_id=match_id,
            queue=joiner.queue,
            username=joiner.username,
            opponent=host.username,
            role="join",
            player=1,
            peer_host=host.listen_host,
            peer_port=host.listen_port,
            created_at=created,
        )
        self.matches[match_id] = {
            host.username: host_assignment,
            joiner.username: join_assignment,
        }
        self.pending[host.username] = host_assignment
        self.pending[joiner.username] = join_assignment

    def status(self, token: str) -> dict[str, Any]:
        username = self.auth(token)
        with self.lock:
            return self.status_for(username)

    def status_for(self, username: str) -> dict[str, Any]:
        account = self.accounts[username]
        pending = self.pending.get(username)
        if pending:
            return {
                "status": "matched",
                "username": username,
                "elo": account.elo,
                "match_id": pending.match_id,
                "queue": pending.queue,
                "opponent": pending.opponent,
                "role": pending.role,
                "player": pending.player,
                "peer_host": pending.peer_host,
                "peer_port": pending.peer_port,
            }
        for name, q in self.queues.items():
            for i, entry in enumerate(q):
                if entry.username == username:
                    return {
                        "status": "queued",
                        "username": username,
                        "elo": account.elo,
                        "queue": name,
                        "position": i + 1,
                        "queued_count": len(q),
                    }
        return {
            "status": "idle",
            "username": username,
            "elo": account.elo,
        }

    def cancel_queue(self, token: str) -> dict[str, Any]:
        username = self.auth(token)
        with self.lock:
            self.cancel_queue_locked(username)
            return self.status_for(username)

    def profile(self, token: str) -> dict[str, Any]:
        username = self.auth(token)
        with self.lock:
            return self.account_profile(username)

    def report_result(self, token: str, match_id: str, winner: str) -> dict[str, Any]:
        username = self.auth(token)
        with self.lock:
            assignments = self.matches.get(match_id)
            if not assignments or username not in assignments:
                raise ValueError("unknown match")
            if any(a.reported for a in assignments.values()):
                return {"status": "already_reported", **self.account_profile(username)}

            players = list(assignments.keys())
            if len(players) != 2:
                raise ValueError("invalid match")
            a_name, b_name = players[0], players[1]
            if winner not in (a_name, b_name):
                raise ValueError("winner must be one of the match players")

            a = self.accounts[a_name]
            b = self.accounts[b_name]
            a_score = 1.0 if winner == a_name else 0.0
            b_score = 1.0 - a_score
            a_elo_delta = rating_delta(a.elo, b.elo, a_score, K_ELO)
            b_elo_delta = rating_delta(b.elo, a.elo, b_score, K_ELO)
            a_mmr_delta = rating_delta(a.mmr, b.mmr, a_score, K_MMR)
            b_mmr_delta = rating_delta(b.mmr, a.mmr, b_score, K_MMR)
            a.elo += a_elo_delta
            b.elo += b_elo_delta
            a.mmr += a_mmr_delta
            b.mmr += b_mmr_delta
            for assignment in assignments.values():
                assignment.reported = True
                self.pending.pop(assignment.username, None)
            self.save()
            return {
                "status": "reported",
                "winner": winner,
                "players": {
                    a_name: {"elo": a.elo, "elo_delta": a_elo_delta},
                    b_name: {"elo": b.elo, "elo_delta": b_elo_delta},
                },
            }


def response_ok(handler: BaseHTTPRequestHandler, data: dict[str, Any], code: int = 200) -> None:
    raw = json.dumps({"ok": True, **data}, separators=(",", ":")).encode("utf-8")
    handler.send_response(code)
    handler.send_header("Content-Type", "application/json")
    handler.send_header("Content-Length", str(len(raw)))
    handler.end_headers()
    handler.wfile.write(raw)


def response_error(handler: BaseHTTPRequestHandler, message: str, code: int = 400) -> None:
    raw = json.dumps({"ok": False, "error": message}, separators=(",", ":")).encode("utf-8")
    handler.send_response(code)
    handler.send_header("Content-Type", "application/json")
    handler.send_header("Content-Length", str(len(raw)))
    handler.end_headers()
    handler.wfile.write(raw)


class Handler(BaseHTTPRequestHandler):
    store: Store

    def log_message(self, fmt: str, *args: Any) -> None:
        sys.stderr.write("[%s] %s\n" % (time.strftime("%H:%M:%S"), fmt % args))

    def do_GET(self) -> None:
        if self.path.rstrip("/") in ("", "/"):
            response_ok(self, {"service": "eggnogg-online", "status": "ok"})
        elif api_path(self.path) == "/api/health":
            response_ok(self, {"status": "ok"})
        else:
            response_error(self, "not found", 404)

    def do_POST(self) -> None:
        try:
            body = self.read_json()
            path = api_path(self.path)
            if path == "/api/register":
                out = self.store.register(str(body.get("username", "")), str(body.get("password", "")))
            elif path == "/api/login":
                out = self.store.login(str(body.get("username", "")), str(body.get("password", "")))
            elif path == "/api/profile":
                out = self.store.profile(str(body.get("token", "")))
            elif path == "/api/queue/join":
                out = self.store.join_queue(
                    str(body.get("token", "")),
                    str(body.get("queue", "")),
                    str(body.get("listen_host", "")),
                    clamp_port(body.get("listen_port", 0)),
                )
            elif path == "/api/queue/cancel":
                out = self.store.cancel_queue(str(body.get("token", "")))
            elif path == "/api/queue/status":
                out = self.store.status(str(body.get("token", "")))
            elif path == "/api/match/result":
                out = self.store.report_result(
                    str(body.get("token", "")),
                    str(body.get("match_id", "")),
                    str(body.get("winner", "")),
                )
            else:
                response_error(self, "not found", 404)
                return
            response_ok(self, out)
        except ValueError as exc:
            response_error(self, str(exc), 400)
        except Exception as exc:
            response_error(self, "server error: %s" % exc, 500)

    def read_json(self) -> dict[str, Any]:
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except Exception:
            length = 0
        if length <= 0 or length > 64 * 1024:
            raise ValueError("invalid request body")
        raw = self.rfile.read(length)
        try:
            data = json.loads(raw.decode("utf-8"))
        except Exception:
            raise ValueError("invalid json")
        if not isinstance(data, dict):
            raise ValueError("json body must be an object")
        return data


def api_path(raw_path: str) -> str:
    path = raw_path.split("?", 1)[0].rstrip("/")
    marker = "/api/"
    idx = path.find(marker)
    if idx >= 0:
        return path[idx:]
    return path


def main(argv: list[str]) -> int:
    host = os.environ.get("EGGNOGG_ONLINE_HOST", DEFAULT_HOST)
    port = int(os.environ.get("EGGNOGG_ONLINE_PORT", str(DEFAULT_PORT)))
    data_path = Path(os.environ.get("EGGNOGG_ONLINE_DATA", DEFAULT_DATA_PATH))
    ranked_cap = int(os.environ.get("EGGNOGG_RANKED_MMR_CAP", str(DEFAULT_RANKED_MMR_CAP)))

    if "--help" in argv or "-h" in argv:
        print("Usage: python online_server/server.py [--host HOST] [--port PORT] [--data FILE] [--ranked-cap MMR]")
        return 0
    i = 0
    while i < len(argv):
        arg = argv[i]
        if arg == "--host" and i + 1 < len(argv):
            host = argv[i + 1]
            i += 2
        elif arg == "--port" and i + 1 < len(argv):
            port = int(argv[i + 1])
            i += 2
        elif arg == "--data" and i + 1 < len(argv):
            data_path = Path(argv[i + 1])
            i += 2
        elif arg == "--ranked-cap" and i + 1 < len(argv):
            ranked_cap = int(argv[i + 1])
            i += 2
        else:
            raise SystemExit("unknown argument: %s" % arg)

    Handler.store = Store(data_path, ranked_cap)
    server = ThreadingHTTPServer((host, port), Handler)

    def stop(_signum: int, _frame: Any) -> None:
        server.shutdown()

    signal.signal(signal.SIGINT, stop)
    if hasattr(signal, "SIGTERM"):
        signal.signal(signal.SIGTERM, stop)

    print("Eggnogg+ online server listening on http://%s:%d" % (host, port))
    print("Data file: %s" % data_path)
    print("Ranked MMR cap: %d" % ranked_cap)
    server.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
