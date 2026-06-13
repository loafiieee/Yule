"use strict";

const net = require("net");
const dgram = require("dgram");
const crypto = require("crypto");
const fs = require("fs");
const path = require("path");

const PORT = Number.parseInt(process.env.PORT || "47778", 10);
const HOST = process.env.HOST || "0.0.0.0";
const UDP_PORT = Number.parseInt(process.env.UDP_PORT || `${PORT}`, 10);
const UDP_HOST = process.env.UDP_HOST || HOST;
const DB_FILE = process.env.DB || path.join(__dirname, "users.json");
const RATINGS_FILE = process.env.RATINGS || path.join(__dirname, "ratings.json");
const SECRET_FILE = process.env.SECRET_FILE || path.join(__dirname, "server_secret.key");
const DEFAULT_ELO = Number.parseInt(process.env.DEFAULT_ELO || "1000", 10);
const DEFAULT_MMR = Number.parseInt(process.env.DEFAULT_MMR || "1000", 10);
const DEFAULT_INPUT_DELAY = Number.parseInt(process.env.INPUT_DELAY || "1", 10);
const MAX_LINE_BYTES = 512 * 1024;
const VANILLA_MAPS = 5;
const COMPETITIVE_BLOCKED_MAP_KEYS = new Set(["vanilla:4"]);
const CHALLENGE_TTL_MS = 5 * 60 * 1000;
const MATCH_SINGLE_REPORT_GRACE_MS = Number.parseInt(process.env.MATCH_SINGLE_REPORT_GRACE_MS || "750", 10);
const MATCH_STALE_MS = Number.parseInt(process.env.MATCH_STALE_MS || `${10 * 60 * 1000}`, 10);
const COMPETITIVE_BASE_RANGE = Number.parseInt(process.env.COMPETITIVE_BASE_RANGE || "150", 10);
const COMPETITIVE_RANGE_PER_SEC = Number.parseInt(process.env.COMPETITIVE_RANGE_PER_SEC || "8", 10);
const COMPETITIVE_MAX_RANGE = Number.parseInt(process.env.COMPETITIVE_MAX_RANGE || "650", 10);

let nextMatchId = 1;
let nextChallengeId = 1;

function now() {
  return Date.now();
}

function normalizeUsername(value) {
  return String(value || "").trim().toLowerCase();
}

function validUsername(username) {
  return /^[a-z0-9_]{1,24}$/.test(username);
}

function normalizeRemoteAddress(raw) {
  const value = String(raw || "");
  if (value.startsWith("::ffff:")) return value.slice(7);
  if (value === "::1") return "127.0.0.1";
  return value;
}

function makeP2pToken() {
  return crypto.randomBytes(16).toString("hex");
}

function clientP2pPort(client) {
  return Number.isInteger(client.p2p_port) ? client.p2p_port : 0;
}

function clientRouteVersion(client) {
  return Number.isInteger(client && client.route_version) ? client.route_version : 0;
}

function sanitizeHostHint(value) {
  const host = String(value || "").trim();
  if (!host || host.length > 64) return "";
  if (!/^[a-zA-Z0-9_.:-]+$/.test(host)) return "";
  return host;
}

function loadDB() {
  try {
    if (fs.existsSync(DB_FILE)) {
      const parsed = JSON.parse(fs.readFileSync(DB_FILE, "utf8"));
      if (parsed && typeof parsed === "object") return parsed;
    }
  } catch (err) {
    console.error("[db] load failed:", err.message);
  }
  return { users: {} };
}

function saveDB() {
  try {
    fs.mkdirSync(path.dirname(DB_FILE), { recursive: true });
    fs.writeFileSync(DB_FILE, JSON.stringify(db, null, 2), "utf8");
  } catch (err) {
    console.error("[db] save failed:", err.message);
  }
}

function loadRatings() {
  try {
    if (fs.existsSync(RATINGS_FILE)) {
      const parsed = JSON.parse(fs.readFileSync(RATINGS_FILE, "utf8"));
      if (parsed && typeof parsed === "object") return parsed;
    }
  } catch (err) {
    console.error("[ratings] load failed:", err.message);
  }
  return { users: {} };
}

function saveRatings() {
  try {
    fs.mkdirSync(path.dirname(RATINGS_FILE), { recursive: true });
    fs.writeFileSync(RATINGS_FILE, JSON.stringify(ratings, null, 2), "utf8");
  } catch (err) {
    console.error("[ratings] save failed:", err.message);
  }
}

function loadServerSecret() {
  if (process.env.RATING_SECRET) return Buffer.from(String(process.env.RATING_SECRET), "utf8");
  try {
    if (fs.existsSync(SECRET_FILE)) {
      const text = fs.readFileSync(SECRET_FILE, "utf8").trim();
      if (/^[0-9a-fA-F]{64}$/.test(text)) return Buffer.from(text, "hex");
      if (text.length >= 16) return Buffer.from(text, "utf8");
    }
  } catch (err) {
    console.error("[ratings] secret load failed:", err.message);
  }
  const secret = crypto.randomBytes(32).toString("hex");
  try {
    fs.mkdirSync(path.dirname(SECRET_FILE), { recursive: true });
    fs.writeFileSync(SECRET_FILE, secret, { encoding: "utf8", mode: 0o600 });
  } catch (err) {
    console.error("[ratings] secret save failed:", err.message);
  }
  return Buffer.from(secret, "hex");
}

function numericRating(value, fallback) {
  const n = Number(value);
  if (!Number.isFinite(n)) return fallback;
  return Math.max(0, Math.min(5000, Math.round(n)));
}

function ratingSignature(username, elo, mmr) {
  return crypto
    .createHmac("sha256", ratingSecret)
    .update(`${username}\n${elo}\n${mmr}`)
    .digest("hex");
}

function setRating(username, elo, mmr) {
  const cleanElo = numericRating(elo, DEFAULT_ELO);
  const cleanMmr = numericRating(mmr, DEFAULT_MMR);
  const rec = {
    elo: cleanElo,
    mmr: cleanMmr,
    sig: ratingSignature(username, cleanElo, cleanMmr),
    updated_at: new Date().toISOString(),
  };
  ratings.users[username] = rec;
  return rec;
}

function ensureRating(username) {
  if (!username) return null;
  const stored = ratings.users[username];
  if (stored &&
      Number.isFinite(Number(stored.elo)) &&
      Number.isFinite(Number(stored.mmr)) &&
      stored.sig === ratingSignature(username, numericRating(stored.elo, DEFAULT_ELO), numericRating(stored.mmr, DEFAULT_MMR))) {
    stored.elo = numericRating(stored.elo, DEFAULT_ELO);
    stored.mmr = numericRating(stored.mmr, DEFAULT_MMR);
    return stored;
  }
  if (stored) console.warn(`[ratings] invalid signature for ${username}; resetting rating`);
  const user = db.users[username] || {};
  const rec = setRating(username, numericRating(user.elo, DEFAULT_ELO), numericRating(user.mmr, DEFAULT_MMR));
  saveRatings();
  return rec;
}

function publicElo(username) {
  const rec = ensureRating(username);
  return rec ? rec.elo : DEFAULT_ELO;
}

function hashPassword(password, salt) {
  return crypto.createHash("sha256").update(`${salt}:${password}`).digest("hex");
}

function ensureUserShape(username) {
  const rec = db.users[username];
  if (!rec) return null;
  if (Object.prototype.hasOwnProperty.call(rec, "elo")) delete rec.elo;
  if (Object.prototype.hasOwnProperty.call(rec, "mmr")) delete rec.mmr;
  if (!Array.isArray(rec.friends)) rec.friends = [];
  if (!Array.isArray(rec.friend_requests)) rec.friend_requests = [];
  return rec;
}

function defaultManifest() {
  const maps = [];
  for (let i = 0; i < VANILLA_MAPS; i += 1) {
    maps.push({ key: `vanilla:${i}`, selector: i, label: `Vanilla ${i + 1}`, kind: "vanilla" });
  }
  return maps;
}

function sanitizeManifest(rawMaps) {
  const out = [];
  const seen = new Set();
  const maps = Array.isArray(rawMaps) ? rawMaps : [];
  for (const item of maps) {
    if (!item || typeof item !== "object") continue;
    const key = String(item.key || "").trim().toLowerCase();
    const selector = Number.parseInt(item.selector, 10);
    if (!key || !Number.isFinite(selector) || selector < 0 || selector > 4096) continue;
    if (seen.has(key)) continue;
    seen.add(key);
    out.push({
      key,
      selector,
      label: String(item.label || key).slice(0, 80),
      kind: String(item.kind || (key.startsWith("vanilla:") ? "vanilla" : "custom")).slice(0, 24),
    });
  }
  return out.length ? out : defaultManifest();
}

function mapIndex(maps) {
  const idx = new Map();
  for (const map of maps) idx.set(map.key, map);
  return idx;
}

function chooseSharedMap(a, b, options = {}) {
  const right = b.mapIndex || mapIndex(defaultManifest());
  const shared = [];
  const blocked = options.competitive ? COMPETITIVE_BLOCKED_MAP_KEYS : null;
  for (const left of a.maps || defaultManifest()) {
    const r = right.get(left.key);
    if (!r) continue;
    if (blocked && blocked.has(left.key)) continue;
    shared.push({
      key: left.key,
      label: left.label || r.label || left.key,
      aSelector: left.selector,
      bSelector: r.selector,
    });
  }
  if (!shared.length) return null;
  return shared[crypto.randomInt(0, shared.length)];
}

function send(client, obj) {
  try {
    client.socket.write(`${JSON.stringify(obj)}\n`);
  } catch (_) {
  }
}

function sendError(client, message) {
  send(client, { type: "error", message });
}

function connectedClient(username) {
  return onlineByUser.get(username) || null;
}

function sendFriendSnapshot(client) {
  if (!client.username) return;
  const rec = ensureUserShape(client.username);
  if (!rec) return;

  send(client, { type: "friend_snapshot_begin" });
  const incoming = [...rec.friend_requests].sort();
  for (const from of incoming) {
    send(client, {
      type: "friend_request",
      from,
      elo: ensureUserShape(from) ? publicElo(from) : DEFAULT_ELO,
    });
  }

  const activeChallenges = [...challenges.values()]
    .filter((c) => c.to === client.username && c.expires_at > now())
    .sort((a, b) => a.created_at - b.created_at);
  for (const challenge of activeChallenges) {
    send(client, {
      type: "challenge",
      id: challenge.id,
      from: challenge.from,
      elo: ensureUserShape(challenge.from) ? publicElo(challenge.from) : DEFAULT_ELO,
      expires_in: Math.max(0, Math.ceil((challenge.expires_at - now()) / 1000)),
    });
  }

  const friends = [...rec.friends].sort();
  for (const friend of friends) {
    send(client, {
      type: "friend",
      username: friend,
      elo: ensureUserShape(friend) ? publicElo(friend) : DEFAULT_ELO,
      online: connectedClient(friend) ? 1 : 0,
    });
  }
  send(client, { type: "friend_snapshot_end" });
}

function refreshFriendsFor(username) {
  const rec = ensureUserShape(username);
  const targets = new Set([username]);
  if (rec) for (const f of rec.friends) targets.add(f);
  for (const target of targets) {
    const client = connectedClient(target);
    if (client) sendFriendSnapshot(client);
  }
}

function broadcastQueueCounts() {
  const msg = {
    type: "queue_update",
    casual: casualQueue.length,
    competitive: competitiveQueue.length,
  };
  for (const client of clients) {
    if (client.username) send(client, msg);
  }
}

function removeFromQueues(client) {
  for (const queue of [casualQueue, competitiveQueue]) {
    const idx = queue.indexOf(client);
    if (idx >= 0) queue.splice(idx, 1);
  }
  client.queue = "";
  client.queue_joined_at = 0;
}

function competitiveRange(client) {
  const waitedSec = Math.max(0, (now() - client.queue_joined_at) / 1000);
  return Math.min(COMPETITIVE_MAX_RANGE, COMPETITIVE_BASE_RANGE + waitedSec * COMPETITIVE_RANGE_PER_SEC);
}

function canCompetitiveMatch(a, b) {
  const ar = ensureRating(a.username);
  const br = ensureRating(b.username);
  if (!ar || !br) return false;
  const diff = Math.abs(ar.mmr - br.mmr);
  return diff <= Math.min(competitiveRange(a), competitiveRange(b));
}

function makeMatch(a, b, source, queueName, challengeId = 0) {
  const shared = chooseSharedMap(a, b, { competitive: queueName === "competitive" });
  if (!shared) {
    sendError(a, "No shared map with opponent.");
    sendError(b, "No shared map with opponent.");
    return false;
  }

  removeFromQueues(a);
  removeFromQueues(b);

  const match = {
    id: nextMatchId++,
    a: a.username,
    b: b.username,
    source,
    queue: queueName || "",
    challenge_id: challengeId,
    map: shared,
    competitive: queueName === "competitive",
    started_at: now(),
    results: new Map(),
    p2p_tokens: {},
    p2p_endpoints: new Map(),
    p2p_notified: {},
  };
  activeMatches.set(match.id, match);
  a.match_id = match.id;
  b.match_id = match.id;

  const seed = crypto.randomBytes(4).readUInt32LE(0);
  const aHosts = Math.random() < 0.5;
  const hostClient = aHosts ? a : b;
  const joinClient = aHosts ? b : a;
  const hostMapSel = aHosts ? shared.aSelector : shared.bSelector;
  const joinMapSel = aHosts ? shared.bSelector : shared.aSelector;
  const hostOpponent = joinClient.username;
  const joinOpponent = hostClient.username;
  match.p2p_tokens[hostClient.username] = makeP2pToken();
  match.p2p_tokens[joinClient.username] = makeP2pToken();

  send(hostClient, {
    type: "match_found",
    match_id: match.id,
    source,
    queue: queueName,
    opponent: hostOpponent,
    opponent_elo: publicElo(hostOpponent),
    role: 0,
    p2p_role: "host",
    peer_host: "",
    peer_port: 0,
    local_port: clientP2pPort(hostClient),
    p2p_token: match.p2p_tokens[hostClient.username],
    map_key: shared.key,
    map_label: shared.label,
    map_sel: hostMapSel,
    seed,
    start_tick: 0,
    input_delay: DEFAULT_INPUT_DELAY,
  });
  send(joinClient, {
    type: "match_found",
    match_id: match.id,
    source,
    queue: queueName,
    opponent: joinOpponent,
    opponent_elo: publicElo(joinOpponent),
    role: 1,
    p2p_role: "join",
    peer_host: "",
    peer_port: 0,
    local_port: clientP2pPort(joinClient),
    p2p_token: match.p2p_tokens[joinClient.username],
    map_key: shared.key,
    map_label: shared.label,
    map_sel: joinMapSel,
    seed,
    start_tick: 0,
    input_delay: DEFAULT_INPUT_DELAY,
  });

  console.log(`[match#${match.id}] ${source}/${queueName || "challenge"} ${a.username} vs ${b.username} map=${shared.key} host=${hostClient.username} join=${joinClient.username} udp_punch=required`);
  broadcastQueueCounts();
  return true;
}

function matchHasUser(match, username) {
  return !!match && (match.a === username || match.b === username);
}

function p2pPeerUsername(match, username) {
  if (!match) return "";
  if (match.a === username) return match.b;
  if (match.b === username) return match.a;
  return "";
}

function legacyP2pAddress(receiverEndpoint, peerClient, peerEndpoint) {
  if (!peerEndpoint) return null;
  let host = peerEndpoint.host;
  let route = "public";
  if (receiverEndpoint && receiverEndpoint.host === peerEndpoint.host) {
    const lanHost = sanitizeHostHint(peerClient && peerClient.lan_host);
    if (lanHost) {
      host = lanHost;
      route = "legacy_lan_observed_port";
    }
  }
  return {
    host,
    port: peerEndpoint.port,
    route,
    public_host: peerEndpoint.host,
    public_port: peerEndpoint.port,
    lan_host: sanitizeHostHint(peerClient && peerClient.lan_host),
    lan_port: sanitizePort(peerEndpoint.local_port, 0),
  };
}

function p2pAddressFor(receiverClient, receiverEndpoint, peerClient, peerEndpoint) {
  if (!peerEndpoint) return null;

  const routeV2 = clientRouteVersion(receiverClient) >= 2 && clientRouteVersion(peerClient) >= 2;
  if (routeV2 && receiverEndpoint && receiverEndpoint.host === peerEndpoint.host) {
    const receiverLanHost = sanitizeHostHint(receiverClient && receiverClient.lan_host);
    const peerLanHost = sanitizeHostHint(peerClient && peerClient.lan_host);
    const peerLocalPort = sanitizePort(peerEndpoint.local_port, 0);
    if (peerLanHost && peerLocalPort) {
      if (receiverLanHost && receiverLanHost === peerLanHost) {
        return {
          host: "127.0.0.1",
          port: peerLocalPort,
          route: "loopback",
          public_host: peerEndpoint.host,
          public_port: peerEndpoint.port,
          lan_host: peerLanHost,
          lan_port: peerLocalPort,
        };
      }
      return {
        host: peerLanHost,
        port: peerLocalPort,
        route: "lan",
        public_host: peerEndpoint.host,
        public_port: peerEndpoint.port,
        lan_host: peerLanHost,
        lan_port: peerLocalPort,
      };
    }
  }

  return legacyP2pAddress(receiverEndpoint, peerClient, peerEndpoint);
}

function sendP2pPeerIfReady(match, username) {
  const peer = p2pPeerUsername(match, username);
  if (!peer) return;

  const client = connectedClient(username);
  const peerClient = connectedClient(peer);
  const endpoint = match.p2p_endpoints && match.p2p_endpoints.get(username);
  const peerEndpoint = match.p2p_endpoints && match.p2p_endpoints.get(peer);
  if (!client || !endpoint || !peerEndpoint) return;

  const peerAddress = p2pAddressFor(client, endpoint, peerClient, peerEndpoint);
  if (!peerAddress || !peerAddress.host || !peerAddress.port) return;

  const notifyKey = `${peerAddress.route}:${peerAddress.host}:${peerAddress.port}:${peerAddress.public_host}:${peerAddress.public_port}`;
  if (match.p2p_notified[username] === notifyKey) return;
  match.p2p_notified[username] = notifyKey;
  send(client, {
    type: "p2p_peer",
    match_id: match.id,
    peer_host: peerAddress.host,
    peer_port: peerAddress.port,
    peer_route: peerAddress.route,
    peer_public_host: peerAddress.public_host,
    peer_public_port: peerAddress.public_port,
    peer_lan_host: peerAddress.lan_host,
    peer_lan_port: peerAddress.lan_port,
  });
}

function maybeSendP2pPeers(match) {
  if (!match || match.finished) return;
  sendP2pPeerIfReady(match, match.a);
  sendP2pPeerIfReady(match, match.b);
}

function registerP2pEndpoint(matchId, username, token, host, port, localPort, source) {
  if (!Number.isInteger(matchId) || !validUsername(username) || !token) return;

  const match = activeMatches.get(matchId);
  if (!match || match.finished || !matchHasUser(match, username)) return;
  if (match.p2p_tokens[username] !== token) return;
  if (!host || !port) return;

  const prev = match.p2p_endpoints.get(username);
  match.p2p_endpoints.set(username, {
    host,
    port,
    local_port: localPort,
    seen_at: now(),
  });

  if (!prev || prev.host !== host || prev.port !== port || prev.local_port !== localPort) {
    console.log(`[p2p#${match.id}] ${username} ${source}=${host}:${port} local=${localPort}`);
  }

  maybeSendP2pPeers(match);
}

function handleUdpProbeMessage(msg, rinfo) {
  if (!msg || msg.type !== "p2p_probe") return;

  const matchId = Number.parseInt(msg.match_id, 10);
  const username = normalizeUsername(msg.username);
  const token = String(msg.token || "");
  const host = normalizeRemoteAddress(rinfo.address);
  const port = sanitizePort(rinfo.port, 0);
  const localPort = sanitizePort(msg.local_port, 0);
  registerP2pEndpoint(matchId, username, token, host, port, localPort, "udp");
}

function tryCasualMatchmaking() {
  let changed = true;
  while (changed) {
    changed = false;
    for (let i = 0; i < casualQueue.length && !changed; i += 1) {
      for (let j = i + 1; j < casualQueue.length; j += 1) {
        if (!chooseSharedMap(casualQueue[i], casualQueue[j])) continue;
        makeMatch(casualQueue[i], casualQueue[j], "queue", "casual");
        changed = true;
        break;
      }
    }
  }
}

function tryCompetitiveMatchmaking() {
  let changed = true;
  while (changed) {
    changed = false;
    for (let i = 0; i < competitiveQueue.length && !changed; i += 1) {
      for (let j = i + 1; j < competitiveQueue.length; j += 1) {
        if (!canCompetitiveMatch(competitiveQueue[i], competitiveQueue[j])) continue;
        if (!chooseSharedMap(competitiveQueue[i], competitiveQueue[j], { competitive: true })) continue;
        makeMatch(competitiveQueue[i], competitiveQueue[j], "queue", "competitive");
        changed = true;
        break;
      }
    }
  }
}

function tryMatchmaking() {
  tryCasualMatchmaking();
  tryCompetitiveMatchmaking();
  broadcastQueueCounts();
}

function authOk(client, username) {
  client.username = username;
  client.maps = defaultManifest();
  client.mapIndex = mapIndex(client.maps);
  onlineByUser.set(username, client);
  ensureUserShape(username);
  send(client, { type: "auth_ok", username, elo: publicElo(username) });
  sendFriendSnapshot(client);
  refreshFriendsFor(username);
  broadcastQueueCounts();
}

function handleRegister(client, msg) {
  const username = normalizeUsername(msg.username);
  const password = String(msg.password || "");
  if (!validUsername(username)) return send(client, { type: "auth_fail", reason: "username: a-z 0-9 _ only, max 24" });
  if (password.length < 4 || password.length > 256) return send(client, { type: "auth_fail", reason: "password must be 4..256 chars" });
  if (db.users[username]) return send(client, { type: "auth_fail", reason: "username taken" });
  const salt = crypto.randomBytes(16).toString("hex");
  db.users[username] = {
    salt,
    hash: hashPassword(password, salt),
    friends: [],
    friend_requests: [],
    created_at: new Date().toISOString(),
  };
  setRating(username, DEFAULT_ELO, DEFAULT_MMR);
  saveDB();
  saveRatings();
  authOk(client, username);
  console.log(`[auth] registered ${username}`);
}

function handleLogin(client, msg) {
  const username = normalizeUsername(msg.username);
  const password = String(msg.password || "");
  const rec = ensureUserShape(username);
  if (!rec) return send(client, { type: "auth_fail", reason: "unknown user" });
  if (hashPassword(password, rec.salt) !== rec.hash) return send(client, { type: "auth_fail", reason: "wrong password" });
  const old = connectedClient(username);
  if (old && old !== client) {
    sendError(old, "Logged in from another location.");
    destroyClient(old);
  }
  authOk(client, username);
  console.log(`[auth] login ${username}`);
}

function handleMapManifest(client, msg) {
  client.maps = sanitizeManifest(msg.maps);
  client.mapIndex = mapIndex(client.maps);
  client.map_serial = String(msg.serial || "").slice(0, 4096);
  client.p2p_port = sanitizePort(msg.p2p_port, 0);
  client.lan_host = sanitizeHostHint(msg.lan_host);
  client.route_version = sanitizePort(msg.route_version, 0);
  console.log(`[maps] ${client.username || "anon"} maps=${client.maps.length} p2p=${client.p2p_port}${client.lan_host ? ` lan=${client.lan_host}` : ""}${client.route_version ? ` route_v${client.route_version}` : ""}`);
}

function sanitizePort(value, fallback) {
  const n = Number.parseInt(value, 10);
  if (!Number.isFinite(n) || n < 0 || n > 65535) return fallback;
  return n;
}

function handleJoinQueue(client, msg) {
  if (!client.username) return sendError(client, "not logged in");
  if (client.match_id && !activeMatches.has(client.match_id)) client.match_id = 0;
  if (client.match_id) return sendError(client, "already in a match");
  const queue = String(msg.queue || "casual").toLowerCase() === "competitive" ? "competitive" : "casual";
  removeFromQueues(client);
  client.queue = queue;
  client.queue_joined_at = now();
  if (queue === "competitive") competitiveQueue.push(client);
  else casualQueue.push(client);
  send(client, { type: "queue_joined", queue });
  tryMatchmaking();
}

function handleLeaveQueue(client) {
  removeFromQueues(client);
  send(client, { type: "queue_left" });
  broadcastQueueCounts();
}

function handleFriendRequest(client, msg) {
  if (!client.username) return sendError(client, "not logged in");
  const target = normalizeUsername(msg.username);
  if (!validUsername(target)) return sendError(client, "invalid username");
  if (target === client.username) return sendError(client, "cannot add yourself");
  const targetRec = ensureUserShape(target);
  const mine = ensureUserShape(client.username);
  if (!targetRec || !mine) return sendError(client, "user not found");
  if (mine.friends.includes(target)) return sendError(client, "already friends");
  if (!targetRec.friend_requests.includes(client.username)) targetRec.friend_requests.push(client.username);
  saveDB();
  send(client, { type: "friend_request_sent", username: target });
  const targetClient = connectedClient(target);
  if (targetClient) sendFriendSnapshot(targetClient);
}

function handleFriendAccept(client, msg) {
  if (!client.username) return sendError(client, "not logged in");
  const from = normalizeUsername(msg.username || msg.from);
  const mine = ensureUserShape(client.username);
  const other = ensureUserShape(from);
  if (!mine || !other) return sendError(client, "user not found");
  mine.friend_requests = mine.friend_requests.filter((u) => u !== from);
  if (!mine.friends.includes(from)) mine.friends.push(from);
  if (!other.friends.includes(client.username)) other.friends.push(client.username);
  saveDB();
  refreshFriendsFor(client.username);
  refreshFriendsFor(from);
}

function handleFriendDecline(client, msg) {
  if (!client.username) return sendError(client, "not logged in");
  const from = normalizeUsername(msg.username || msg.from);
  const mine = ensureUserShape(client.username);
  if (!mine) return;
  mine.friend_requests = mine.friend_requests.filter((u) => u !== from);
  saveDB();
  sendFriendSnapshot(client);
}

function handleFriendRemove(client, msg) {
  if (!client.username) return sendError(client, "not logged in");
  const target = normalizeUsername(msg.username);
  const mine = ensureUserShape(client.username);
  const other = ensureUserShape(target);
  if (!mine || !other) return;
  mine.friends = mine.friends.filter((u) => u !== target);
  other.friends = other.friends.filter((u) => u !== client.username);
  saveDB();
  refreshFriendsFor(client.username);
  refreshFriendsFor(target);
}

function handleChallenge(client, msg) {
  if (!client.username) return sendError(client, "not logged in");
  const target = normalizeUsername(msg.username);
  const mine = ensureUserShape(client.username);
  const other = ensureUserShape(target);
  if (!mine || !other) return sendError(client, "user not found");
  if (!mine.friends.includes(target)) return sendError(client, "you can only challenge friends");
  if (!connectedClient(target)) return sendError(client, "friend is offline");
  const existing = [...challenges.values()].find((c) =>
    c.from === client.username && c.to === target && c.expires_at > now());
  if (existing) {
    send(client, {
      type: "challenge_sent",
      username: target,
      id: existing.id,
      expires_in: Math.max(0, Math.ceil((existing.expires_at - now()) / 1000)),
    });
    return;
  }
  const id = nextChallengeId++;
  const challenge = {
    id,
    from: client.username,
    to: target,
    created_at: now(),
    expires_at: now() + CHALLENGE_TTL_MS,
  };
  challenges.set(id, challenge);
  send(client, { type: "challenge_sent", username: target, id, expires_in: 300 });
  const targetClient = connectedClient(target);
  if (targetClient) sendFriendSnapshot(targetClient);
}

function findChallenge(client, msg) {
  const id = Number.parseInt(msg.id, 10);
  if (Number.isFinite(id) && challenges.has(id)) return challenges.get(id);
  const from = normalizeUsername(msg.username || msg.from);
  for (const challenge of challenges.values()) {
    if (challenge.to === client.username && challenge.from === from) return challenge;
  }
  return null;
}

function handleChallengeAccept(client, msg) {
  if (!client.username) return sendError(client, "not logged in");
  const challenge = findChallenge(client, msg);
  if (!challenge || challenge.to !== client.username || challenge.expires_at <= now()) {
    return sendError(client, "challenge expired");
  }
  const fromClient = connectedClient(challenge.from);
  if (!fromClient) {
    challenges.delete(challenge.id);
    sendFriendSnapshot(client);
    return sendError(client, "challenger is offline");
  }
  challenges.delete(challenge.id);
  send(fromClient, { type: "challenge_accepted", username: client.username, id: challenge.id });
  sendFriendSnapshot(client);
  sendFriendSnapshot(fromClient);
  makeMatch(fromClient, client, "challenge", "", challenge.id);
}

function handleChallengeDecline(client, msg) {
  if (!client.username) return sendError(client, "not logged in");
  const challenge = findChallenge(client, msg);
  if (challenge) {
    challenges.delete(challenge.id);
    const fromClient = connectedClient(challenge.from);
    if (fromClient) {
      send(fromClient, { type: "challenge_declined", username: client.username, id: challenge.id });
      sendFriendSnapshot(fromClient);
    }
  }
  sendFriendSnapshot(client);
}

function expectedScore(a, b) {
  return 1 / (1 + Math.pow(10, (b - a) / 400));
}

function applyCompetitiveResult(match, winner) {
  if (!match.competitive || !winner) return {};
  const loser = winner === match.a ? match.b : match.a;
  const wr = ensureRating(winner);
  const lr = ensureRating(loser);
  if (!wr || !lr) return {};
  const before = {
    [winner]: { elo: wr.elo, mmr: wr.mmr },
    [loser]: { elo: lr.elo, mmr: lr.mmr },
  };

  const kElo = 24;
  const kMmr = 32;
  const wExpectedElo = expectedScore(wr.elo, lr.elo);
  const wExpectedMmr = expectedScore(wr.mmr, lr.mmr);
  const nextWinnerElo = Math.round(wr.elo + kElo * (1 - wExpectedElo));
  const nextLoserElo = Math.round(lr.elo + kElo * (0 - (1 - wExpectedElo)));
  const nextWinnerMmr = Math.round(wr.mmr + kMmr * (1 - wExpectedMmr));
  const nextLoserMmr = Math.round(lr.mmr + kMmr * (0 - (1 - wExpectedMmr)));
  setRating(winner, nextWinnerElo, nextWinnerMmr);
  setRating(loser, nextLoserElo, nextLoserMmr);
  saveRatings();
  const changes = {
    [winner]: { elo_before: before[winner].elo, elo_after: publicElo(winner) },
    [loser]: { elo_before: before[loser].elo, elo_after: publicElo(loser) },
  };
  for (const username of [winner, loser]) {
    const c = connectedClient(username);
    if (c) send(c, { type: "rating_update", elo: publicElo(username), elo_before: changes[username].elo_before });
  }
  return changes;
}

function finishMatch(match, winner, reason = "") {
  if (!match || match.finished) return;
  match.finished = true;
  if (match.result_timer) {
    clearTimeout(match.result_timer);
    match.result_timer = null;
  }
  const loser = winner === match.a ? match.b : match.a;
  const ratings = applyCompetitiveResult(match, winner);
  for (const username of [match.a, match.b]) {
    const c = connectedClient(username);
    const elo = publicElo(username);
    const rating = ratings[username] || {};
    if (c) {
      c.match_id = 0;
      send(c, {
        type: "match_result",
        match_id: match.id,
        result: username === winner ? "win" : "loss",
        winner,
        loser,
        reason,
        competitive: match.competitive ? 1 : 0,
        queue: match.queue || "",
        elo,
        elo_before: Number.isFinite(rating.elo_before) ? rating.elo_before : elo,
      });
    }
  }
  activeMatches.delete(match.id);
}

function cancelMatch(match, reason = "cancelled") {
  if (!match || match.finished) return;
  match.finished = true;
  if (match.result_timer) {
    clearTimeout(match.result_timer);
    match.result_timer = null;
  }
  for (const username of [match.a, match.b]) {
    const c = connectedClient(username);
    if (c) {
      c.match_id = 0;
      send(c, { type: "match_end", match_id: match.id, reason });
    }
  }
  activeMatches.delete(match.id);
}

function handleMatchEnd(client, msg) {
  if (!client.match_id) return;
  const match = activeMatches.get(client.match_id);
  if (!match) {
    client.match_id = 0;
    return;
  }
  const result = String(msg.result || "").toLowerCase();
  if (result === "win" || result === "loss") {
    const winner = result === "win" ? client.username : (client.username === match.a ? match.b : match.a);
    if (match.results.has(client.username)) return;
    match.results.set(client.username, winner);
    if (match.results.size >= 2) {
      const winners = [...match.results.values()];
      const agreed = winners.every((w) => w === winners[0]);
      finishMatch(match, winners[0], agreed ? "reported" : "conflicting reports");
      return;
    }
    send(client, { type: "match_report_ack", match_id: match.id, pending: 1 });
    if (!match.result_timer) {
      match.result_timer = setTimeout(() => {
        if (!activeMatches.has(match.id) || match.finished || match.results.size < 1) return;
        finishMatch(match, [...match.results.values()][0], "single report");
      }, MATCH_SINGLE_REPORT_GRACE_MS);
      if (typeof match.result_timer.unref === "function") match.result_timer.unref();
    }
    return;
  }
  for (const username of [match.a, match.b]) {
    const c = connectedClient(username);
    if (c) c.match_id = 0;
  }
  sendError(client, "match result must be win or loss");
  activeMatches.delete(match.id);
}

function dispatch(client, msg) {
  switch (msg.type) {
    case "register": handleRegister(client, msg); break;
    case "login": handleLogin(client, msg); break;
    case "map_manifest": handleMapManifest(client, msg); break;
    case "join_queue": handleJoinQueue(client, msg); break;
    case "leave_queue": handleLeaveQueue(client); break;
    case "friend_request": handleFriendRequest(client, msg); break;
    case "friend_accept": handleFriendAccept(client, msg); break;
    case "friend_decline": handleFriendDecline(client, msg); break;
    case "friend_remove": handleFriendRemove(client, msg); break;
    case "challenge": handleChallenge(client, msg); break;
    case "challenge_accept": handleChallengeAccept(client, msg); break;
    case "challenge_decline": handleChallengeDecline(client, msg); break;
    case "match_end": handleMatchEnd(client, msg); break;
    case "ping": send(client, { type: "pong", seq: msg.seq || 0 }); break;
    default: sendError(client, "unknown message type");
  }
}

function destroyClient(client) {
  if (!clients.has(client)) return;
  clients.delete(client);
  removeFromQueues(client);
  if (client.username) {
    let challengeChanged = false;
    for (const [id, challenge] of challenges) {
      if (challenge.from !== client.username && challenge.to !== client.username) continue;
      challenges.delete(id);
      challengeChanged = true;
      const other = challenge.from === client.username ? challenge.to : challenge.from;
      const otherClient = connectedClient(other);
      if (otherClient) {
        send(otherClient, {
          type: "challenge_expired",
          id,
          from: challenge.from,
          username: challenge.from === client.username ? challenge.from : challenge.to,
        });
      }
    }
    if (challengeChanged) {
      for (const otherClient of clients) {
        if (otherClient.username) sendFriendSnapshot(otherClient);
      }
    }
  }
  if (client.username && onlineByUser.get(client.username) === client) {
    onlineByUser.delete(client.username);
    refreshFriendsFor(client.username);
  }
  if (client.match_id) {
    const match = activeMatches.get(client.match_id);
    if (match) {
      const other = client.username === match.a ? match.b : match.a;
      const otherClient = connectedClient(other);
      if (otherClient) {
        finishMatch(match, other, "opponent disconnected");
      }
      else activeMatches.delete(match.id);
    }
  }
  try { client.socket.destroy(); } catch (_) {}
  broadcastQueueCounts();
}

const ratingSecret = loadServerSecret();
const db = loadDB();
if (!db.users || typeof db.users !== "object") db.users = {};
const ratings = loadRatings();
if (!ratings.users || typeof ratings.users !== "object") ratings.users = {};
for (const username of Object.keys(db.users)) {
  ensureRating(username);
  ensureUserShape(username);
}
saveDB();
saveRatings();

const clients = new Set();
const onlineByUser = new Map();
const casualQueue = [];
const competitiveQueue = [];
const challenges = new Map();
const activeMatches = new Map();

setInterval(() => {
  const cutoff = now();
  let expired = false;
  for (const [id, challenge] of challenges) {
    if (challenge.expires_at <= cutoff) {
      challenges.delete(id);
      expired = true;
      const target = connectedClient(challenge.to);
      const challenger = connectedClient(challenge.from);
      if (target) send(target, { type: "challenge_expired", id, from: challenge.from, username: challenge.from });
      if (challenger) send(challenger, { type: "challenge_expired", id, username: challenge.to });
    }
  }
  if (expired) {
    for (const client of clients) if (client.username) sendFriendSnapshot(client);
  }
  for (const match of [...activeMatches.values()]) {
    if (match.finished || cutoff - match.started_at <= MATCH_STALE_MS) continue;
    if (match.results && match.results.size > 0) {
      finishMatch(match, [...match.results.values()][0], "stale single report");
    } else {
      cancelMatch(match, "stale match");
    }
  }
  tryMatchmaking();
}, 5000);

const server = net.createServer((socket) => {
  const client = {
    socket,
    buf: "",
    username: "",
    maps: defaultManifest(),
    mapIndex: mapIndex(defaultManifest()),
    p2p_port: 0,
    lan_host: "",
    route_version: 0,
    queue: "",
    queue_joined_at: 0,
    match_id: 0,
  };
  clients.add(client);
  socket.setEncoding("utf8");
  socket.setNoDelay(true);
  socket.setTimeout(120000);

  socket.on("data", (chunk) => {
    client.buf += chunk;
    let nl;
    while ((nl = client.buf.indexOf("\n")) >= 0) {
      const line = client.buf.slice(0, nl).trim();
      client.buf = client.buf.slice(nl + 1);
      if (!line) continue;
      if (line.length > MAX_LINE_BYTES) {
        sendError(client, "message too long");
        destroyClient(client);
        return;
      }
      let msg = null;
      try {
        msg = JSON.parse(line);
      } catch (_) {
        sendError(client, "invalid JSON");
        continue;
      }
      if (!msg || typeof msg !== "object" || !msg.type) continue;
      dispatch(client, msg);
    }
  });
  socket.on("close", () => destroyClient(client));
  socket.on("error", () => destroyClient(client));
  socket.on("timeout", () => destroyClient(client));
});

server.listen(PORT, HOST, () => {
  console.log(`Eggnogg+ online server listening on ${HOST}:${PORT}`);
  console.log(`DB: ${DB_FILE}`);
  console.log(`Ratings: ${RATINGS_FILE}`);
});

server.on("error", (err) => {
  console.error("Server error:", err.message);
  process.exit(1);
});

const udpServer = dgram.createSocket("udp4");

udpServer.on("message", (buf, rinfo) => {
  if (!buf || buf.length > 2048) return;
  let msg;
  try {
    msg = JSON.parse(buf.toString("utf8").trim());
  } catch (_) {
    return;
  }
  handleUdpProbeMessage(msg, rinfo);
});

udpServer.on("listening", () => {
  const addr = udpServer.address();
  console.log(`Eggnogg+ P2P discovery UDP listening on ${addr.address}:${addr.port}`);
});

udpServer.on("error", (err) => {
  console.error("P2P UDP server error:", err.message);
  process.exit(1);
});

udpServer.bind(UDP_PORT, UDP_HOST);
