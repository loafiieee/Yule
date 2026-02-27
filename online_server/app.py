from __future__ import annotations

import hashlib
import hmac
import os
import secrets
import time
import uuid
from dataclasses import dataclass, field
from typing import Dict, List, Literal, Optional, Set, Tuple

import jwt
from fastapi import Depends, FastAPI, Header, HTTPException
from fastapi.responses import HTMLResponse
from pydantic import BaseModel, Field
from passlib.context import CryptContext


APP_NAME = "Eggnogg Online Prototype"
JWT_ALG = "HS256"
ACCESS_TTL_SECONDS = 60 * 20
REFRESH_TTL_SECONDS = 60 * 60 * 24 * 14
MATCH_CONFIRM_TIMEOUT_SECONDS = 60

pwd_context = CryptContext(schemes=["bcrypt"], deprecated="auto")


def _now() -> int:
    return int(time.time())


def _uuid() -> str:
    return str(uuid.uuid4())


JWT_SECRET = os.getenv("EGGNOGG_JWT_SECRET", "dev-secret-change-me")


@dataclass
class User:
    user_id: str
    email: str
    password_hash: str
    display_name: str
    created_at: int
    friends: Set[str] = field(default_factory=set)
    incoming_requests: Set[str] = field(default_factory=set)
    outgoing_requests: Set[str] = field(default_factory=set)
    rating_ranked: int = 1000
    rating_casual: int = 1000
    games_ranked: int = 0
    games_casual: int = 0


@dataclass
class Session:
    refresh_id: str
    user_id: str
    refresh_hash: str
    expires_at: int


@dataclass
class QueueEntry:
    user_id: str
    mode: Literal["ranked", "casual"]
    rating: int
    enqueued_at: int


@dataclass
class Match:
    match_id: str
    mode: Literal["ranked", "casual"]
    players: Tuple[str, str]
    state: Literal["active", "pending_finalize", "finalized", "disputed"]
    created_at: int
    pending_deadline: Optional[int] = None
    reports: Dict[str, str] = field(default_factory=dict)  # user_id -> winner_id
    winner_id: Optional[str] = None
    reason: Optional[str] = None


class RegisterRequest(BaseModel):
    email: str = Field(min_length=3, max_length=256)
    password: str = Field(min_length=8, max_length=256)
    display_name: str = Field(min_length=2, max_length=32)


class LoginRequest(BaseModel):
    email: str
    password: str


class RefreshRequest(BaseModel):
    refresh_token: str


class AuthResponse(BaseModel):
    access_token: str
    refresh_token: str
    token_type: str = "bearer"


class MeResponse(BaseModel):
    user_id: str
    email: str
    display_name: str
    rating_ranked: int
    rating_casual: int


class FriendRequestPayload(BaseModel):
    target_user_id: str


class QueueJoinRequest(BaseModel):
    mode: Literal["ranked", "casual"]


class QueueJoinResponse(BaseModel):
    status: Literal["queued", "matched"]
    match_id: Optional[str] = None


class MatchReportRequest(BaseModel):
    winner_user_id: str


class MatchResponse(BaseModel):
    match_id: str
    mode: str
    players: List[str]
    state: str
    winner_id: Optional[str]
    reason: Optional[str]


app = FastAPI(title=APP_NAME)
users_by_id: Dict[str, User] = {}
users_by_email: Dict[str, str] = {}
sessions_by_refresh_id: Dict[str, Session] = {}
queues: List[QueueEntry] = []
matches: Dict[str, Match] = {}


# ----------------------
# Auth helpers
# ----------------------
def hash_refresh_token(raw: str) -> str:
    return hashlib.sha256(raw.encode("utf-8")).hexdigest()


def mint_access_token(user: User) -> str:
    now = _now()
    payload = {
        "sub": user.user_id,
        "iat": now,
        "exp": now + ACCESS_TTL_SECONDS,
        "email": user.email,
    }
    return jwt.encode(payload, JWT_SECRET, algorithm=JWT_ALG)


def mint_refresh_token(user: User) -> str:
    now = _now()
    rid = _uuid()
    raw = secrets.token_urlsafe(48)
    token = f"{rid}.{raw}"
    sessions_by_refresh_id[rid] = Session(
        refresh_id=rid,
        user_id=user.user_id,
        refresh_hash=hash_refresh_token(raw),
        expires_at=now + REFRESH_TTL_SECONDS,
    )
    return token


def verify_refresh_token(refresh_token: str) -> User:
    try:
        rid, raw = refresh_token.split(".", 1)
    except ValueError as exc:
        raise HTTPException(status_code=401, detail="Malformed refresh token") from exc

    sess = sessions_by_refresh_id.get(rid)
    if not sess:
        raise HTTPException(status_code=401, detail="Unknown refresh token")
    if sess.expires_at < _now():
        sessions_by_refresh_id.pop(rid, None)
        raise HTTPException(status_code=401, detail="Refresh token expired")

    expected = sess.refresh_hash
    actual = hash_refresh_token(raw)
    if not hmac.compare_digest(expected, actual):
        raise HTTPException(status_code=401, detail="Invalid refresh token")

    user = users_by_id.get(sess.user_id)
    if not user:
        raise HTTPException(status_code=401, detail="Unknown session user")
    return user


def parse_bearer_token(authorization: Optional[str]) -> str:
    if not authorization:
        raise HTTPException(status_code=401, detail="Missing Authorization header")
    parts = authorization.split(" ", 1)
    if len(parts) != 2 or parts[0].lower() != "bearer":
        raise HTTPException(status_code=401, detail="Invalid auth header")
    return parts[1]


def get_current_user(authorization: Optional[str] = Header(default=None)) -> User:
    token = parse_bearer_token(authorization)
    try:
        payload = jwt.decode(token, JWT_SECRET, algorithms=[JWT_ALG])
    except jwt.PyJWTError as exc:
        raise HTTPException(status_code=401, detail="Invalid access token") from exc
    uid = payload.get("sub")
    user = users_by_id.get(uid)
    if not user:
        raise HTTPException(status_code=401, detail="User not found")
    return user


# ----------------------
# ELO helpers
# ----------------------
def elo_expected(r_a: int, r_b: int) -> float:
    return 1.0 / (1.0 + 10 ** ((r_b - r_a) / 400.0))


def elo_k(games_played: int) -> int:
    if games_played < 10:
        return 40
    if games_played < 30:
        return 24
    return 16


def apply_elo(winner: User, loser: User, mode: Literal["ranked", "casual"]) -> None:
    if mode != "ranked":
        return

    rw = winner.rating_ranked
    rl = loser.rating_ranked
    ew = elo_expected(rw, rl)
    el = elo_expected(rl, rw)
    kw = elo_k(winner.games_ranked)
    kl = elo_k(loser.games_ranked)
    winner.rating_ranked = round(rw + kw * (1.0 - ew))
    loser.rating_ranked = round(rl + kl * (0.0 - el))
    winner.games_ranked += 1
    loser.games_ranked += 1


# ----------------------
# Matchmaking
# ----------------------
def try_matchmake(mode: Literal["ranked", "casual"]) -> Optional[Match]:
    relevant = [q for q in queues if q.mode == mode]
    if len(relevant) < 2:
        return None

    relevant.sort(key=lambda q: q.enqueued_at)
    for idx, first in enumerate(relevant):
        best = None
        best_diff = 999999
        for second in relevant[idx + 1 :]:
            diff = abs(first.rating - second.rating)
            if mode == "ranked" and diff > 200:
                continue
            if diff < best_diff:
                best_diff = diff
                best = second
        if best is None:
            continue

        # Remove both from global queue
        kept: List[QueueEntry] = []
        for q in queues:
            if q.user_id in (first.user_id, best.user_id) and q.mode == mode:
                continue
            kept.append(q)
        queues.clear()
        queues.extend(kept)

        match = Match(
            match_id=_uuid(),
            mode=mode,
            players=(first.user_id, best.user_id),
            state="active",
            created_at=_now(),
        )
        matches[match.match_id] = match
        return match
    return None


# ----------------------
# Routes
# ----------------------
@app.get("/health")
def health() -> dict:
    return {"ok": True, "app": APP_NAME, "time": _now()}


@app.get("/", response_class=HTMLResponse)
def index() -> str:
    return """
    <html>
      <head><title>Eggnogg Online Prototype</title></head>
      <body style="font-family: sans-serif; margin: 2rem;">
        <h1>Eggnogg Online Prototype</h1>
        <p>Server is running.</p>
        <ul>
          <li><a href="/health">/health</a></li>
          <li><a href="/docs">/docs</a></li>
        </ul>
        <p>Use <code>/docs</code> to register/login and test queue/match endpoints.</p>
      </body>
    </html>
    """


@app.post("/auth/register", response_model=AuthResponse)
def register(req: RegisterRequest) -> AuthResponse:
    email = req.email.strip().lower()
    if email in users_by_email:
        raise HTTPException(status_code=409, detail="Email already in use")

    uid = _uuid()
    user = User(
        user_id=uid,
        email=email,
        password_hash=pwd_context.hash(req.password),
        display_name=req.display_name.strip(),
        created_at=_now(),
    )
    users_by_id[uid] = user
    users_by_email[email] = uid

    return AuthResponse(
        access_token=mint_access_token(user),
        refresh_token=mint_refresh_token(user),
    )


@app.post("/auth/login", response_model=AuthResponse)
def login(req: LoginRequest) -> AuthResponse:
    email = req.email.strip().lower()
    uid = users_by_email.get(email)
    if not uid:
        raise HTTPException(status_code=401, detail="Bad credentials")
    user = users_by_id[uid]
    if not pwd_context.verify(req.password, user.password_hash):
        raise HTTPException(status_code=401, detail="Bad credentials")

    return AuthResponse(
        access_token=mint_access_token(user),
        refresh_token=mint_refresh_token(user),
    )


@app.post("/auth/refresh", response_model=AuthResponse)
def refresh(req: RefreshRequest) -> AuthResponse:
    user = verify_refresh_token(req.refresh_token)
    # rotate refresh token
    rid = req.refresh_token.split(".", 1)[0]
    sessions_by_refresh_id.pop(rid, None)
    return AuthResponse(
        access_token=mint_access_token(user),
        refresh_token=mint_refresh_token(user),
    )


@app.get("/me", response_model=MeResponse)
def me(user: User = Depends(get_current_user)) -> MeResponse:
    return MeResponse(
        user_id=user.user_id,
        email=user.email,
        display_name=user.display_name,
        rating_ranked=user.rating_ranked,
        rating_casual=user.rating_casual,
    )


@app.post("/friends/request")
def request_friend(req: FriendRequestPayload, user: User = Depends(get_current_user)) -> dict:
    target = users_by_id.get(req.target_user_id)
    if not target:
        raise HTTPException(status_code=404, detail="Target user not found")
    if target.user_id == user.user_id:
        raise HTTPException(status_code=400, detail="Cannot friend yourself")
    if target.user_id in user.friends:
        return {"ok": True, "message": "Already friends"}

    user.outgoing_requests.add(target.user_id)
    target.incoming_requests.add(user.user_id)
    return {"ok": True}


@app.post("/friends/accept")
def accept_friend(req: FriendRequestPayload, user: User = Depends(get_current_user)) -> dict:
    inviter = users_by_id.get(req.target_user_id)
    if not inviter:
        raise HTTPException(status_code=404, detail="User not found")
    if inviter.user_id not in user.incoming_requests:
        raise HTTPException(status_code=400, detail="No pending friend request")

    user.incoming_requests.discard(inviter.user_id)
    inviter.outgoing_requests.discard(user.user_id)
    user.friends.add(inviter.user_id)
    inviter.friends.add(user.user_id)
    return {"ok": True}


@app.get("/friends")
def list_friends(user: User = Depends(get_current_user)) -> dict:
    def summarize(uid: str) -> dict:
        friend = users_by_id[uid]
        in_match = any(friend.user_id in m.players and m.state in ("active", "pending_finalize") for m in matches.values())
        in_queue = any(q.user_id == friend.user_id for q in queues)
        status = "in_match" if in_match else "in_queue" if in_queue else "online"
        return {
            "user_id": friend.user_id,
            "display_name": friend.display_name,
            "status": status,
            "rating_ranked": friend.rating_ranked,
        }

    return {
        "friends": [summarize(fid) for fid in sorted(user.friends)],
        "incoming_requests": [summarize(uid) for uid in sorted(user.incoming_requests) if uid in users_by_id],
    }


@app.post("/queue/join", response_model=QueueJoinResponse)
def queue_join(req: QueueJoinRequest, user: User = Depends(get_current_user)) -> QueueJoinResponse:
    for m in matches.values():
        if user.user_id in m.players and m.state in ("active", "pending_finalize"):
            return QueueJoinResponse(status="matched", match_id=m.match_id)

    already_queued = any(q.user_id == user.user_id and q.mode == req.mode for q in queues)
    if already_queued:
        maybe = try_matchmake(req.mode)
        if maybe and user.user_id in maybe.players:
            return QueueJoinResponse(status="matched", match_id=maybe.match_id)
        return QueueJoinResponse(status="queued")

    if any(user.user_id in m.players and m.state in ("active", "pending_finalize") for m in matches.values()):
        raise HTTPException(status_code=400, detail="User already in active match")

    rating = user.rating_ranked if req.mode == "ranked" else user.rating_casual
    queues.append(QueueEntry(user_id=user.user_id, mode=req.mode, rating=rating, enqueued_at=_now()))

    maybe = try_matchmake(req.mode)
    if maybe:
        return QueueJoinResponse(status="matched", match_id=maybe.match_id)
    return QueueJoinResponse(status="queued")


@app.post("/queue/leave")
def queue_leave(user: User = Depends(get_current_user)) -> dict:
    before = len(queues)
    queues[:] = [q for q in queues if q.user_id != user.user_id]
    return {"ok": True, "removed": before - len(queues)}


@app.get("/queue/status")
def queue_status(user: User = Depends(get_current_user)) -> dict:
    for m in matches.values():
        if user.user_id in m.players and m.state in ("active", "pending_finalize"):
            return {"status": "matched", "match_id": m.match_id, "mode": m.mode}

    for q in queues:
        if q.user_id == user.user_id:
            return {
                "status": "queued",
                "mode": q.mode,
                "wait_seconds": max(0, _now() - q.enqueued_at),
            }
    return {"status": "idle"}


@app.get("/match/{match_id}", response_model=MatchResponse)
def match_get(match_id: str, user: User = Depends(get_current_user)) -> MatchResponse:
    m = matches.get(match_id)
    if not m:
        raise HTTPException(status_code=404, detail="Match not found")
    if user.user_id not in m.players:
        raise HTTPException(status_code=403, detail="Not your match")

    _auto_finalize_timeout(m)
    return MatchResponse(
        match_id=m.match_id,
        mode=m.mode,
        players=[m.players[0], m.players[1]],
        state=m.state,
        winner_id=m.winner_id,
        reason=m.reason,
    )


def _finalize_match(m: Match, winner_id: str, reason: str) -> None:
    loser_id = m.players[0] if m.players[1] == winner_id else m.players[1]
    winner = users_by_id[winner_id]
    loser = users_by_id[loser_id]
    apply_elo(winner, loser, m.mode)

    m.winner_id = winner_id
    m.reason = reason
    m.state = "finalized"


def _auto_finalize_timeout(m: Match) -> None:
    if m.state != "pending_finalize" or not m.pending_deadline:
        return
    if _now() < m.pending_deadline:
        return

    # One-sided report timeout: reporter wins by forfeit.
    if len(m.reports) == 1:
        winner = next(iter(m.reports.values()))
        _finalize_match(m, winner, "opponent_report_timeout_forfeit")
    elif len(m.reports) >= 2:
        values = set(m.reports.values())
        if len(values) == 1:
            _finalize_match(m, next(iter(values)), "agreed")
        else:
            m.state = "disputed"
            m.reason = "conflicting_reports"


@app.post("/match/{match_id}/report", response_model=MatchResponse)
def match_report(match_id: str, req: MatchReportRequest, user: User = Depends(get_current_user)) -> MatchResponse:
    m = matches.get(match_id)
    if not m:
        raise HTTPException(status_code=404, detail="Match not found")
    if user.user_id not in m.players:
        raise HTTPException(status_code=403, detail="Not your match")
    if req.winner_user_id not in m.players:
        raise HTTPException(status_code=400, detail="winner_user_id must be one of the two match players")

    if m.state in ("finalized", "disputed"):
        return MatchResponse(
            match_id=m.match_id,
            mode=m.mode,
            players=[m.players[0], m.players[1]],
            state=m.state,
            winner_id=m.winner_id,
            reason=m.reason,
        )

    m.state = "pending_finalize"
    m.pending_deadline = _now() + MATCH_CONFIRM_TIMEOUT_SECONDS
    m.reports[user.user_id] = req.winner_user_id

    _auto_finalize_timeout(m)

    if m.state == "pending_finalize" and len(m.reports) == 2:
        values = set(m.reports.values())
        if len(values) == 1:
            _finalize_match(m, next(iter(values)), "agreed")
        else:
            m.state = "disputed"
            m.reason = "conflicting_reports"

    return MatchResponse(
        match_id=m.match_id,
        mode=m.mode,
        players=[m.players[0], m.players[1]],
        state=m.state,
        winner_id=m.winner_id,
        reason=m.reason,
    )
