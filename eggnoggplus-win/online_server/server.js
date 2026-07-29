"use strict";


const net = require("net");
const dgram = require("dgram");
const crypto = require("crypto");
const fs = require("fs");
const path = require("path");
const { startAdminServerFromEnv } = require("./admin_server");
const { createDiscordLfgBotFromEnv } = require("./discord_lfg_bot");
const { startRedirectServerFromEnv } = require("./lfg_redirect");

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
const MAX_MANIFEST_MAPS = 4096;
/* Control protocol 3 adds the authoritative friend-challenge map picker.
 * Persistent social controls/presence and bilateral private rematches are
 * additive required capabilities advertised separately from the version.
 * Match protocol 4 retains the explicit two-client gameplay commit and adds a
 * neutral P2P transport-failure report. Before both clients send
 * match_started, aborts, premature results, disconnects, and stale cleanup
 * cancel without a winner or rating change. */
const CONTROL_PROTOCOL_VERSION = 3;
const MATCH_PROTOCOL_VERSION = 4;
const P2P_PROTOCOL_VERSION = 17;
const COMPETITIVE_BLOCKED_MAP_KEYS = new Set(["vanilla:4"]);
// Recently chosen map keys (most-recent last). Used to even out random map
// selection so the pool cycles through every option before any repeats, instead
// of pure-uniform random which clusters and looks biased over a short series.
const RECENT_MAP_KEYS = [];
const RECENT_MAP_MAX = 64;
const CHALLENGE_TTL_MS = 5 * 60 * 1000;
const REMATCH_TTL_CONFIG = Number.parseInt(process.env.REMATCH_TTL_MS || "45000", 10);
const REMATCH_TTL_MS = Number.isFinite(REMATCH_TTL_CONFIG)
  ? Math.max(100, Math.min(5 * 60 * 1000, REMATCH_TTL_CONFIG))
  : 45000;
/* A normal result is authoritative only when both still-connected peers report
 * the same winner.  Keep the old environment name as a deployment-compatible
 * fallback, but a lone report now expires to a no-contest instead of awarding
 * the reporter a win. */
const MATCH_REPORT_TIMEOUT_MS = Number.parseInt(
  process.env.MATCH_REPORT_TIMEOUT_MS || process.env.MATCH_SINGLE_REPORT_GRACE_MS || "10000",
  10,
);
const MATCH_SETUP_STALE_MS = Number.parseInt(
  process.env.MATCH_SETUP_STALE_MS || process.env.MATCH_STALE_MS || `${10 * 60 * 1000}`,
  10,
);
const MATCH_SWEEP_INTERVAL_MS = Number.parseInt(process.env.MATCH_SWEEP_INTERVAL_MS || "5000", 10);
const CLIENT_IDLE_TIMEOUT_MS = Number.parseInt(process.env.CLIENT_IDLE_TIMEOUT_MS || "120000", 10);
const COMPETITIVE_BASE_RANGE = Number.parseInt(process.env.COMPETITIVE_BASE_RANGE || "150", 10);
const COMPETITIVE_RANGE_PER_SEC = Number.parseInt(process.env.COMPETITIVE_RANGE_PER_SEC || "8", 10);
const COMPETITIVE_MAX_RANGE = Number.parseInt(process.env.COMPETITIVE_MAX_RANGE || "650", 10);
const UDP_DIAG = process.env.UDP_DIAG === "1";
const P2P_RELAY_ENABLED = process.env.P2P_RELAY_ENABLED !== "0";
const GGPO_PACKET_MAGIC = 0x50474e45;
const GGPO_PACKET_MIN_BYTES = 44;
const GGPO_PACKET_MAX_BYTES = 2048;
const RELAY_PACKET_RATE_MAX = 600;
const RELAY_BYTE_RATE_MAX = 768 * 1024;
const RELAY_FINISH_GRACE_CONFIG = Number.parseInt(
  process.env.RELAY_FINISH_GRACE_MS || "15000",
  10,
);
const RELAY_FINISH_GRACE_MS = Number.isFinite(RELAY_FINISH_GRACE_CONFIG)
  ? Math.max(1000, Math.min(30000, RELAY_FINISH_GRACE_CONFIG))
  : 15000;
const lfgBot = createDiscordLfgBotFromEnv(process.env, {
  log: (line) => console.log(`[lfg] ${line}`),
});
const lfgRedirectServer = startRedirectServerFromEnv(process.env);
if (lfgRedirectServer) {
  lfgRedirectServer.on("listening", () => {
    const address = lfgRedirectServer.address();
    console.log(`[lfg] strict redirect listening on ${address.address}:${address.port}`);
  });
  lfgRedirectServer.on("error", (err) => {
    console.error(`[lfg] redirect server error: ${err.message}`);
  });
}

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

/* Shared only by the two clients in one match. The per-user p2p_tokens remain
 * separate because they authorize rendezvous probes; this independent
 * high-entropy value authenticates the peer gameplay datagrams themselves. */
function makeP2pAuthToken() {
  return crypto.randomBytes(32).toString("hex");
}

function udpDiag(line) {
  if (UDP_DIAG) console.log(line);
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
  rec.friends = sanitizeUserList(rec.friends, username);
  rec.friend_requests = sanitizeUserList(rec.friend_requests, username);
  rec.blocked_users = sanitizeUserList(rec.blocked_users, username);
  rec.muted_users = sanitizeUserList(rec.muted_users, username);
  if (rec.ban && typeof rec.ban === "object") {
    rec.ban = {
      reason: String(rec.ban.reason || "").slice(0, 160),
      at: String(rec.ban.at || ""),
    };
  } else {
    delete rec.ban;
  }
  return rec;
}

function sanitizeUserList(raw, self) {
  const result = [];
  const seen = new Set();
  for (const value of Array.isArray(raw) ? raw : []) {
    const username = normalizeUsername(value);
    if (!validUsername(username) || username === self || seen.has(username)) continue;
    if (!db.users[username]) continue;
    seen.add(username);
    result.push(username);
  }
  return result;
}

function userBlocks(username, target) {
  const rec = ensureUserShape(username);
  return Boolean(rec && rec.blocked_users.includes(target));
}

function usersBlockEachOther(a, b) {
  const left = typeof a === "string" ? a : a && a.username;
  const right = typeof b === "string" ? b : b && b.username;
  if (!left || !right) return false;
  return userBlocks(left, right) || userBlocks(right, left);
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
    if (out.length >= MAX_MANIFEST_MAPS) break;
    if (!item || typeof item !== "object") continue;
    const key = String(item.key || "").trim().toLowerCase().slice(0, 127);
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

function sharedMapChoices(a, b, options = {}) {
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
  return shared;
}

function chooseSharedMap(a, b, options = {}) {
  const shared = sharedMapChoices(a, b, options);
  if (!shared.length) return null;

  // Even-distribution pick: exclude the maps used in the last (poolSize - 1)
  // selections so we cycle through the whole shared pool before any map repeats.
  // This still uses an unbiased random among the remaining candidates - it just
  // removes the clustering of pure-uniform random that reads as "some maps come
  // up more often than others". Falls back to the full pool if all are recent.
  let pool = shared;
  if (shared.length > 1) {
    const avoid = new Set(RECENT_MAP_KEYS.slice(-(shared.length - 1)));
    const filtered = shared.filter((m) => !avoid.has(m.key));
    if (filtered.length) pool = filtered;
  }
  const chosen = pool[crypto.randomInt(0, pool.length)];
  RECENT_MAP_KEYS.push(chosen.key);
  if (RECENT_MAP_KEYS.length > RECENT_MAP_MAX) RECENT_MAP_KEYS.shift();
  return chosen;
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

function clientProtocolReady(client) {
  return !!client &&
    client.control_protocol === CONTROL_PROTOCOL_VERSION &&
    client.match_protocol === MATCH_PROTOCOL_VERSION &&
    client.p2p_protocol === P2P_PROTOCOL_VERSION;
}

function clientBuildReady(client) {
  return !!client &&
    client.build_id > 0 &&
    client.game_exe_id > 0 &&
    client.framework_dll_id > 0;
}

function clientsProtocolCompatible(a, b) {
  return clientProtocolReady(a) &&
    clientProtocolReady(b) &&
    clientBuildReady(a) &&
    clientBuildReady(b) &&
    a.control_protocol === b.control_protocol &&
    a.match_protocol === b.match_protocol &&
    a.p2p_protocol === b.p2p_protocol &&
    a.build_id === b.build_id &&
    a.game_exe_id === b.game_exe_id &&
    a.framework_dll_id === b.framework_dll_id;
}

function rejectUnsupportedClient(client) {
  if (!clientProtocolReady(client)) {
    sendError(
      client,
      `This build cannot use this server (client control/match/P2P ` +
        `${client.control_protocol || "unknown"}/${client.match_protocol || "unknown"}/` +
        `${client.p2p_protocol || "unknown"}; server requires ` +
        `${CONTROL_PROTOCOL_VERSION}/${MATCH_PROTOCOL_VERSION}/${P2P_PROTOCOL_VERSION}). ` +
        "Update Eggnogg+ and try again.",
    );
    return true;
  }
  if (!clientBuildReady(client)) {
    sendError(
      client,
      "This client did not provide a complete game/framework build identity. " +
        "Restart the updated Eggnogg+ build and try again.",
    );
    return true;
  }
  return false;
}

function notifyIncompatibleQueuePair(a, b) {
  const aKey = `${b.username}:${b.build_id}:${b.game_exe_id}:${b.framework_dll_id}`;
  const bKey = `${a.username}:${a.build_id}:${a.game_exe_id}:${a.framework_dll_id}`;
  if (a.last_incompatible_build !== aKey) {
    a.last_incompatible_build = aKey;
    sendError(a, "A queued player is using a different game/framework build. Both players must update to the same build.");
  }
  if (b.last_incompatible_build !== bKey) {
    b.last_incompatible_build = bKey;
    sendError(b, "A queued player is using a different game/framework build. Both players must update to the same build.");
  }
}

function clientsCanQueueMatch(a, b) {
  if (clientsProtocolCompatible(a, b)) return true;
  if (clientProtocolReady(a) && clientProtocolReady(b) &&
      clientBuildReady(a) && clientBuildReady(b)) {
    notifyIncompatibleQueuePair(a, b);
  }
  return false;
}

function sendServerInfo(client) {
  send(client, {
    type: "server_info",
    control_protocol: CONTROL_PROTOCOL_VERSION,
    match_protocol: MATCH_PROTOCOL_VERSION,
    p2p_protocol: P2P_PROTOCOL_VERSION,
    cap_p2p_auth: 1,
    cap_social_controls: 1,
    cap_private_rematch: 1,
    cap_p2p_relay: P2P_RELAY_ENABLED ? 1 : 0,
    cap_client_build_gate: 1,
  });
}

function connectedClient(username) {
  return onlineByUser.get(username) || null;
}

function friendPresence(username) {
  const client = connectedClient(username);
  if (!client) return "offline";
  if (client.match_id) {
    const match = activeMatches.get(client.match_id);
    if (match && !match.finished) return match.committed ? "in_match" : "match_setup";
  }
  if (client.queue === "competitive") return "queue_competitive";
  if (client.queue === "casual") return "queue_casual";
  return "online";
}

function sendFriendSnapshot(client) {
  if (!client.username) return;
  const rec = ensureUserShape(client.username);
  if (!rec) return;

  send(client, { type: "friend_snapshot_begin" });
  const incoming = rec.friend_requests
    .filter((from) => !usersBlockEachOther(client.username, from))
    .sort();
  for (const from of incoming) {
    send(client, {
      type: "friend_request",
      from,
      elo: ensureUserShape(from) ? publicElo(from) : DEFAULT_ELO,
    });
  }

  const activeChallenges = [...challenges.values()]
    .filter((c) =>
      c.to === client.username &&
      c.expires_at > now() &&
      !usersBlockEachOther(client.username, c.from))
    .sort((a, b) => a.created_at - b.created_at);
  for (const challenge of activeChallenges) {
    send(client, {
      type: "challenge",
      id: challenge.id,
      from: challenge.from,
      elo: ensureUserShape(challenge.from) ? publicElo(challenge.from) : DEFAULT_ELO,
      map_key: challenge.map_key,
      map_label: challenge.map_label,
      expires_in: Math.max(0, Math.ceil((challenge.expires_at - now()) / 1000)),
      muted: rec.muted_users.includes(challenge.from) ? 1 : 0,
    });
  }

  const friends = rec.friends
    .filter((friend) => !usersBlockEachOther(client.username, friend))
    .sort();
  for (const friend of friends) {
    send(client, {
      type: "friend",
      username: friend,
      elo: ensureUserShape(friend) ? publicElo(friend) : DEFAULT_ELO,
      online: connectedClient(friend) ? 1 : 0,
      presence: friendPresence(friend),
      muted: rec.muted_users.includes(friend) ? 1 : 0,
    });
  }
  for (const blocked of [...rec.blocked_users].sort()) {
    /* Deliberately omit rating and presence. A block list is visible only to
     * its owner and must not become a presence side channel. */
    send(client, { type: "blocked_user", username: blocked });
  }
  send(client, { type: "friend_snapshot_end" });
}

function refreshFriendsFor(username) {
  refreshFriendsForUsers([username]);
}

function refreshFriendsForUsers(usernames) {
  const targets = new Set();
  for (const username of usernames) {
    if (!username) continue;
    targets.add(username);
    const rec = ensureUserShape(username);
    if (rec) for (const friend of rec.friends) targets.add(friend);
  }
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

function removeFromQueues(client, reason = "left") {
  const priorQueue = client.queue;
  for (const queue of [casualQueue, competitiveQueue]) {
    const idx = queue.indexOf(client);
    if (idx >= 0) queue.splice(idx, 1);
  }
  client.queue = "";
  client.queue_joined_at = 0;
  if (priorQueue && client.username) lfgBot.queueLeft(client.username, reason);
}

function rematchExpiresIn(rematch) {
  return Math.max(0, Math.ceil((rematch.expires_at - now()) / 1000));
}

function clearTerminalRematch(username, matchId) {
  const client = connectedClient(username);
  if (!client || !client.last_match_terminal ||
      client.last_match_terminal.match_id !== matchId) return;
  client.last_match_terminal.rematch_available = 0;
  client.last_match_terminal.rematch_expires_in = 0;
}

function removeRematch(rematch) {
  if (!rematch || rematches.get(rematch.match_id) !== rematch) return false;
  rematches.delete(rematch.match_id);
  clearTerminalRematch(rematch.a, rematch.match_id);
  clearTerminalRematch(rematch.b, rematch.match_id);
  return true;
}

function notifyRematchClosed(rematch, username, otherType, reason) {
  if (!removeRematch(rematch)) return;
  const other = username === rematch.a ? rematch.b : rematch.a;
  const actorClient = connectedClient(username);
  const otherClient = connectedClient(other);
  if (actorClient) {
    send(actorClient, {
      type: "rematch_closed",
      match_id: rematch.match_id,
      reason: reason || "rematch closed",
    });
  }
  if (otherClient) {
    send(otherClient, {
      type: otherType || "rematch_unavailable",
      match_id: rematch.match_id,
      username,
      reason: reason || "rematch unavailable",
    });
  }
}

function cancelRematchesForUser(username, reason = "rematch unavailable") {
  if (!username) return;
  for (const rematch of [...rematches.values()]) {
    if (rematch.a !== username && rematch.b !== username) continue;
    notifyRematchClosed(rematch, username, "rematch_unavailable", reason);
  }
}

function cancelRematchesBetween(left, right, reason = "rematch unavailable") {
  for (const rematch of [...rematches.values()]) {
    if (!(
      (rematch.a === left && rematch.b === right) ||
      (rematch.a === right && rematch.b === left)
    )) continue;
    notifyRematchClosed(rematch, left, "rematch_unavailable", reason);
  }
}

function createRematchForMatch(match) {
  if (!match || !match.committed) return null;
  const left = connectedClient(match.a);
  const right = connectedClient(match.b);
  if (!left || !right || usersBlockEachOther(match.a, match.b)) return null;
  cancelRematchesForUser(match.a, "superseded by a newer result");
  cancelRematchesForUser(match.b, "superseded by a newer result");
  const rematch = {
    match_id: match.id,
    a: match.a,
    b: match.b,
    map_key: match.map.key,
    map_label: match.map.label,
    created_at: now(),
    expires_at: now() + REMATCH_TTL_MS,
    accepted_by: new Set(),
  };
  rematches.set(rematch.match_id, rematch);
  return rematch;
}

function findClientRematch(client, msg) {
  if (!client || !client.username) return null;
  const matchId = msg && msg.match_id;
  if (!Number.isInteger(matchId) || matchId <= 0) return null;
  const rematch = rematches.get(matchId);
  if (!rematch || (rematch.a !== client.username && rematch.b !== client.username)) return null;
  if (rematch.expires_at <= now()) {
    notifyRematchClosed(rematch, client.username, "rematch_expired", "rematch expired");
    return null;
  }
  return rematch;
}

function competitiveRange(client) {
  const waitedSec = Math.max(0, (now() - client.queue_joined_at) / 1000);
  return Math.min(COMPETITIVE_MAX_RANGE, COMPETITIVE_BASE_RANGE + waitedSec * COMPETITIVE_RANGE_PER_SEC);
}

function canCompetitiveMatch(a, b) {
  if (usersBlockEachOther(a, b)) return false;
  const ar = ensureRating(a.username);
  const br = ensureRating(b.username);
  if (!ar || !br) return false;
  const diff = Math.abs(ar.mmr - br.mmr);
  return diff <= Math.min(competitiveRange(a), competitiveRange(b));
}

function makeMatch(a, b, source, queueName, challengeId = 0, forcedMapKey = "") {
  if (usersBlockEachOther(a, b)) return false;
  if (!clientsProtocolCompatible(a, b)) {
    sendError(a, "Opponent is using a different or incompatible Eggnogg+ build. Both players must update to the same build.");
    sendError(b, "Opponent is using a different or incompatible Eggnogg+ build. Both players must update to the same build.");
    return false;
  }
  const competitive = queueName === "competitive";
  const forcedKey = String(forcedMapKey || "").trim().toLowerCase();
  const shared = forcedKey
    ? sharedMapChoices(a, b, { competitive }).find((map) => map.key === forcedKey) || null
    : chooseSharedMap(a, b, { competitive });
  if (!shared) {
    sendError(a, "No shared map with opponent.");
    sendError(b, "No shared map with opponent.");
    return false;
  }

  cancelRematchesForUser(a.username, "player entered another match");
  cancelRematchesForUser(b.username, "player entered another match");
  removeFromQueues(a, "matched");
  removeFromQueues(b, "matched");
  a.last_match_terminal = null;
  b.last_match_terminal = null;

  const match = {
    id: nextMatchId++,
    a: a.username,
    b: b.username,
    source,
    queue: queueName || "",
    challenge_id: challengeId,
    map: shared,
    competitive,
    created_at: now(),
    results: new Map(),
    transport_failures: new Map(),
    started_by: new Set(),
    committed: false,
    committed_at: 0,
    p2p_tokens: {},
    p2p_auth_token: makeP2pAuthToken(),
    control_protocol: a.control_protocol,
    match_protocol: a.match_protocol,
    p2p_protocol: a.p2p_protocol,
    build_id: a.build_id,
    game_exe_id: a.game_exe_id,
    framework_dll_id: a.framework_dll_id,
    p2p_endpoints: new Map(),
    p2p_notified: {},
    player_by_index: [],
    force_relay: false,
  };
  activeMatches.set(match.id, match);
  a.match_id = match.id;
  b.match_id = match.id;

  const seed = crypto.randomBytes(4).readUInt32LE(0);
  const aHosts = Math.random() < 0.5;
  const hostClient = aHosts ? a : b;
  const joinClient = aHosts ? b : a;
  match.player_by_index = [hostClient.username, joinClient.username];
  const hostMapSel = aHosts ? shared.aSelector : shared.bSelector;
  const joinMapSel = aHosts ? shared.bSelector : shared.aSelector;
  const hostOpponent = joinClient.username;
  const joinOpponent = hostClient.username;
  match.p2p_tokens[hostClient.username] = makeP2pToken();
  match.p2p_tokens[joinClient.username] = makeP2pToken();

  send(hostClient, {
    type: "match_found",
    match_protocol: match.match_protocol,
    p2p_protocol: match.p2p_protocol,
    opponent_framework_version: joinClient.framework_version,
    opponent_build_id: joinClient.build_id,
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
    p2p_auth_token: match.p2p_auth_token,
    map_key: shared.key,
    map_label: shared.label,
    map_sel: hostMapSel,
    seed,
    start_tick: 0,
    input_delay: DEFAULT_INPUT_DELAY,
  });
  send(joinClient, {
    type: "match_found",
    match_protocol: match.match_protocol,
    p2p_protocol: match.p2p_protocol,
    opponent_framework_version: hostClient.framework_version,
    opponent_build_id: hostClient.build_id,
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
    p2p_auth_token: match.p2p_auth_token,
    map_key: shared.key,
    map_label: shared.label,
    map_sel: joinMapSel,
    seed,
    start_tick: 0,
    input_delay: DEFAULT_INPUT_DELAY,
  });

  console.log(`[match#${match.id}] ${source}/${queueName || "challenge"} ${a.username}(${a.framework_version} build=${a.build_id.toString(16)} p2p=${a.p2p_protocol}) vs ${b.username}(${b.framework_version} build=${b.build_id.toString(16)} p2p=${b.p2p_protocol}) map=${shared.key} host=${hostClient.username} join=${joinClient.username} udp_punch=required`);
  refreshFriendsForUsers([a.username, b.username]);
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

function relayAddressFor(peerEndpoint) {
  return {
    /* Clients deliberately replace this marker with their already-validated
     * control-server hostname. No separately configured public hostname is
     * needed on multi-homed deployments. */
    host: "relay",
    port: UDP_PORT,
    route: "relay",
    public_host: peerEndpoint.host,
    public_port: peerEndpoint.port,
    lan_host: "",
    lan_port: 0,
  };
}

function sendP2pPeerIfReady(match, username) {
  const peer = p2pPeerUsername(match, username);
  if (!peer) return;

  const client = connectedClient(username);
  const peerClient = connectedClient(peer);
  const endpoint = match.p2p_endpoints && match.p2p_endpoints.get(username);
  const peerEndpoint = match.p2p_endpoints && match.p2p_endpoints.get(peer);
  if (!client || !endpoint || !peerEndpoint) return;

  const peerAddress = match.force_relay
    ? relayAddressFor(peerEndpoint)
    : p2pAddressFor(client, endpoint, peerClient, peerEndpoint);
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
  console.log(`[p2p#${match.id}] sent peer to ${username} route=${peerAddress.route} peer=${peerAddress.host}:${peerAddress.port} public=${peerAddress.public_host}:${peerAddress.public_port}${peerAddress.lan_host ? ` lan=${peerAddress.lan_host}:${peerAddress.lan_port}` : ""}`);
}

function maybeSendP2pPeers(match) {
  if (!match || match.finished) return;
  sendP2pPeerIfReady(match, match.a);
  sendP2pPeerIfReady(match, match.b);
}

function registerP2pEndpoint(matchId, username, token, host, port, localPort, source) {
  if (!Number.isInteger(matchId) || !validUsername(username) || !token) {
    udpDiag(`[p2p] ignored ${source} probe: invalid identity match=${matchId} user=${username || "?"} from=${host || "?"}:${port || 0}`);
    return;
  }

  const match = activeMatches.get(matchId);
  if (!match || match.finished || !matchHasUser(match, username)) {
    udpDiag(`[p2p#${matchId}] ignored ${source} probe: no active match for ${username} from=${host || "?"}:${port || 0}`);
    return;
  }
  if (match.p2p_tokens[username] !== token) {
    udpDiag(`[p2p#${match.id}] ignored ${source} probe: bad token for ${username} from=${host || "?"}:${port || 0}`);
    return;
  }
  if (!host || !port) {
    udpDiag(`[p2p#${match.id}] ignored ${source} probe: missing source address for ${username}`);
    return;
  }

  const prev = match.p2p_endpoints.get(username);
  const changed = !prev || prev.host !== host || prev.port !== port ||
    prev.local_port !== localPort;
  const generation = prev
    ? (changed ? (prev.generation || 1) + 1 : (prev.generation || 1))
    : 1;
  if (prev) {
    const oldKey = `${prev.host}:${prev.port}`;
    const owner = relayEndpointIndex.get(oldKey);
    if (owner && owner.matchId === match.id && owner.username === username) {
      relayEndpointIndex.delete(oldKey);
    }
  }
  const endpoint = {
    host,
    port,
    local_port: localPort,
    seen_at: now(),
    generation,
    relay_window_at: prev ? prev.relay_window_at : 0,
    relay_packets: prev ? prev.relay_packets : 0,
    relay_bytes: prev ? prev.relay_bytes : 0,
  };
  match.p2p_endpoints.set(username, endpoint);
  relayEndpointIndex.set(`${host}:${port}`, { matchId: match.id, username });

  if (changed) {
    console.log(`[p2p#${match.id}] ${username} ${source}=${host}:${port} local=${localPort} generation=${generation}`);
  }
  if (P2P_RELAY_ENABLED && generation >= 2 && !match.force_relay) {
    match.force_relay = true;
    match.p2p_notified = {};
    console.log(`[relay#${match.id}] direct path did not establish; enabling bounded UDP relay`);
  }

  maybeSendP2pPeers(match);
}

function clearRelayEndpoints(match) {
  if (!match || !match.p2p_endpoints) return;
  for (const [username, endpoint] of match.p2p_endpoints) {
    const key = `${endpoint.host}:${endpoint.port}`;
    const owner = relayEndpointIndex.get(key);
    if (owner && owner.matchId === match.id && owner.username === username) {
      relayEndpointIndex.delete(key);
    }
  }
}

function clearTerminalRelay(match) {
  if (!match) return;
  if (match.relay_finish_timer) {
    clearTimeout(match.relay_finish_timer);
    match.relay_finish_timer = null;
  }
  terminalRelayMatches.delete(match.id);
  clearRelayEndpoints(match);
}

function retainTerminalRelay(match) {
  if (!match || !match.force_relay || !match.p2p_endpoints ||
      match.p2p_endpoints.size !== 2) {
    clearRelayEndpoints(match);
    return;
  }
  match.relay_finish_expires_at = now() + RELAY_FINISH_GRACE_MS;
  terminalRelayMatches.set(match.id, match);
  match.relay_finish_timer = setTimeout(() => {
    clearTerminalRelay(match);
  }, RELAY_FINISH_GRACE_MS);
  if (typeof match.relay_finish_timer.unref === "function") {
    match.relay_finish_timer.unref();
  }
  console.log(
    `[relay#${match.id}] retaining terminal route for ${RELAY_FINISH_GRACE_MS}ms native presentation grace`,
  );
}

function relayMatch(matchId) {
  const active = activeMatches.get(matchId);
  if (active && !active.finished) return active;
  const terminal = terminalRelayMatches.get(matchId);
  if (!terminal) return null;
  if (terminal.relay_finish_expires_at <= now()) {
    clearTerminalRelay(terminal);
    return null;
  }
  return terminal;
}

function relayRateAllowed(endpoint, byteLength) {
  const current = now();
  if (!endpoint.relay_window_at ||
      current - endpoint.relay_window_at >= 1000) {
    endpoint.relay_window_at = current;
    endpoint.relay_packets = 0;
    endpoint.relay_bytes = 0;
  }
  if (endpoint.relay_packets >= RELAY_PACKET_RATE_MAX ||
      endpoint.relay_bytes + byteLength > RELAY_BYTE_RATE_MAX) {
    return false;
  }
  endpoint.relay_packets++;
  endpoint.relay_bytes += byteLength;
  return true;
}

function handleRelayPacket(buf, rinfo) {
  if (!P2P_RELAY_ENABLED || !buf ||
      buf.length < GGPO_PACKET_MIN_BYTES ||
      buf.length > GGPO_PACKET_MAX_BYTES ||
      buf.readUInt32LE(0) !== GGPO_PACKET_MAGIC ||
      buf.readUInt16LE(4) !== P2P_PROTOCOL_VERSION) {
    return false;
  }
  const packetType = buf.readUInt16LE(6);
  const senderPlayer = buf.readUInt32LE(16);
  if (packetType < 1 || packetType > 9 || senderPlayer > 1) return true;
  const host = normalizeRemoteAddress(rinfo.address);
  const port = sanitizePort(rinfo.port, 0);
  const owner = relayEndpointIndex.get(`${host}:${port}`);
  if (!owner) return true;
  const match = relayMatch(owner.matchId);
  if (!match || !match.force_relay ||
      !matchHasUser(match, owner.username) ||
      !Array.isArray(match.player_by_index) ||
      match.player_by_index[senderPlayer] !== owner.username) {
    return true;
  }
  const source = match.p2p_endpoints.get(owner.username);
  const peerUsername = p2pPeerUsername(match, owner.username);
  const destination = match.p2p_endpoints.get(peerUsername);
  if (!source || !destination ||
      source.host !== host || source.port !== port ||
      !relayRateAllowed(source, buf.length)) {
    return true;
  }
  udpServer.send(buf, destination.port, destination.host, (err) => {
    if (err) udpDiag(`[relay#${match.id}] send to ${peerUsername} failed: ${err.message}`);
  });
  return true;
}

function sendUdpJson(rinfo, payload) {
  const line = `${JSON.stringify(payload)}\n`;
  udpServer.send(Buffer.from(line, "utf8"), rinfo.port, rinfo.address);
}

function handleUdpPingMessage(msg, rinfo) {
  const host = normalizeRemoteAddress(rinfo.address);
  const port = sanitizePort(rinfo.port, 0);
  sendUdpJson(rinfo, {
    type: "udp_pong",
    seq: msg.seq || 0,
    observed_host: host,
    observed_port: port,
    server_time: now(),
  });
  udpDiag(`[udp] ping from ${host}:${port}`);
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
        if (!clientsCanQueueMatch(casualQueue[i], casualQueue[j])) continue;
        if (!sharedMapChoices(casualQueue[i], casualQueue[j]).length) continue;
        if (usersBlockEachOther(casualQueue[i], casualQueue[j])) continue;
        if (makeMatch(casualQueue[i], casualQueue[j], "queue", "casual")) {
          changed = true;
          break;
        }
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
        if (!clientsCanQueueMatch(competitiveQueue[i], competitiveQueue[j])) continue;
        if (!canCompetitiveMatch(competitiveQueue[i], competitiveQueue[j])) continue;
        if (!sharedMapChoices(competitiveQueue[i], competitiveQueue[j], { competitive: true }).length) continue;
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
  client.authenticated_at = now();
  client.maps = defaultManifest();
  client.mapIndex = mapIndex(client.maps);
  onlineByUser.set(username, client);
  ensureUserShape(username);
  send(client, {
    type: "auth_ok",
    username,
    elo: publicElo(username),
    control_protocol: CONTROL_PROTOCOL_VERSION,
    match_protocol: MATCH_PROTOCOL_VERSION,
    p2p_protocol: P2P_PROTOCOL_VERSION,
    cap_p2p_auth: 1,
    cap_social_controls: 1,
    cap_private_rematch: 1,
    cap_p2p_relay: P2P_RELAY_ENABLED ? 1 : 0,
    cap_client_build_gate: 1,
  });
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
    blocked_users: [],
    muted_users: [],
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
  if (rec.ban) {
    const suffix = rec.ban.reason ? `: ${rec.ban.reason}` : "";
    return send(client, { type: "auth_fail", reason: `account banned${suffix}` });
  }
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
  if (Object.prototype.hasOwnProperty.call(msg, "control_protocol")) {
    client.control_protocol = sanitizeProtocolVersion(msg.control_protocol);
  }
  if (Object.prototype.hasOwnProperty.call(msg, "match_protocol")) {
    client.match_protocol = sanitizeProtocolVersion(msg.match_protocol);
  }
  if (Object.prototype.hasOwnProperty.call(msg, "p2p_protocol")) {
    client.p2p_protocol = sanitizeProtocolVersion(msg.p2p_protocol);
  }
  if (Object.prototype.hasOwnProperty.call(msg, "build_id")) {
    client.build_id = sanitizeFingerprint(msg.build_id);
  }
  if (Object.prototype.hasOwnProperty.call(msg, "game_exe_id")) {
    client.game_exe_id = sanitizeFingerprint(msg.game_exe_id);
  }
  if (Object.prototype.hasOwnProperty.call(msg, "framework_dll_id")) {
    client.framework_dll_id = sanitizeFingerprint(msg.framework_dll_id);
  }
  if (Object.prototype.hasOwnProperty.call(msg, "framework_version")) {
    client.framework_version = String(msg.framework_version || "unknown")
      .replace(/[^0-9A-Za-z._+-]/g, "")
      .slice(0, 32) || "unknown";
  }
  client.p2p_port = sanitizePort(msg.p2p_port, 0);
  client.lan_host = sanitizeHostHint(msg.lan_host);
  client.route_version = sanitizePort(msg.route_version, 0);
  console.log(`[maps] ${client.username || "anon"} maps=${client.maps.length} release=${client.framework_version} build=${client.build_id.toString(16)} exe=${client.game_exe_id.toString(16)} dll=${client.framework_dll_id.toString(16)} protocols=${client.control_protocol}/${client.match_protocol}/${client.p2p_protocol} port=${client.p2p_port}${client.lan_host ? ` lan=${client.lan_host}` : ""}${client.route_version ? ` route_v${client.route_version}` : ""}`);
  if (client.username && (!clientProtocolReady(client) || !clientBuildReady(client))) {
    removeFromQueues(client, "incompatible build");
    rejectUnsupportedClient(client);
  }
}

function sanitizePort(value, fallback) {
  const n = Number.parseInt(value, 10);
  if (!Number.isFinite(n) || n < 0 || n > 65535) return fallback;
  return n;
}

function sanitizeProtocolVersion(value) {
  return Number.isInteger(value) && value > 0 && value <= 65535 ? value : 0;
}

function sanitizeFingerprint(value) {
  return Number.isInteger(value) && value > 0 && value <= 0xffffffff ? value : 0;
}

function handleJoinQueue(client, msg) {
  if (!client.username) return sendError(client, "not logged in");
  if (rejectUnsupportedClient(client)) return;
  if (client.match_id && !activeMatches.has(client.match_id)) client.match_id = 0;
  if (client.match_id) return sendError(client, "already in a match");
  cancelRematchesForUser(client.username, "player joined matchmaking");
  const queue = String(msg.queue || "casual").toLowerCase() === "competitive" ? "competitive" : "casual";
  removeFromQueues(client, "changed");
  client.queue = queue;
  client.queue_joined_at = now();
  if (queue === "competitive") competitiveQueue.push(client);
  else casualQueue.push(client);
  send(client, { type: "queue_joined", queue });
  refreshFriendsFor(client.username);
  tryMatchmaking();
  /* Avoid a create/delete burst when the normal matcher consumes this player
   * immediately. The bot receives only the public account name and queue. */
  if (client.queue === queue) lfgBot.queueJoined(client.username, queue);
}

function handleLeaveQueue(client) {
  removeFromQueues(client, "left");
  send(client, { type: "queue_left" });
  if (client.username) refreshFriendsFor(client.username);
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
  if (usersBlockEachOther(client.username, target)) return sendError(client, "user is unavailable");
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
  if (usersBlockEachOther(client.username, from)) return sendError(client, "user is unavailable");
  if (!mine.friend_requests.includes(from)) return sendError(client, "friend request is no longer available");
  mine.friend_requests = mine.friend_requests.filter((u) => u !== from);
  if (!mine.friends.includes(from)) mine.friends.push(from);
  if (!other.friends.includes(client.username)) other.friends.push(client.username);
  saveDB();
  refreshFriendsForUsers([client.username, from]);
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
  mine.muted_users = mine.muted_users.filter((u) => u !== target);
  other.muted_users = other.muted_users.filter((u) => u !== client.username);
  saveDB();
  refreshFriendsForUsers([client.username, target]);
}

function expireChallengesBetween(left, right) {
  for (const [id, challenge] of challenges) {
    if (!(
      (challenge.from === left && challenge.to === right) ||
      (challenge.from === right && challenge.to === left)
    )) continue;
    challenges.delete(id);
    const otherUsername = challenge.from === left ? right : challenge.from;
    const otherClient = connectedClient(otherUsername);
    if (otherClient) {
      send(otherClient, {
        type: "challenge_expired",
        id,
        username: challenge.from === otherUsername ? challenge.to : challenge.from,
      });
    }
  }
}

function sendSocialSnapshots(left, right) {
  const leftClient = connectedClient(left);
  const rightClient = connectedClient(right);
  if (leftClient) sendFriendSnapshot(leftClient);
  if (rightClient) sendFriendSnapshot(rightClient);
}

function handleFriendMute(client, msg) {
  if (!client.username) return sendError(client, "not logged in");
  const target = normalizeUsername(msg.username);
  const mine = ensureUserShape(client.username);
  const other = ensureUserShape(target);
  if (!mine || !other) return sendError(client, "user not found");
  if (usersBlockEachOther(client.username, target)) return sendError(client, "user is unavailable");
  if (!mine.friends.includes(target)) return sendError(client, "you can only mute friends");
  const muted = msg.muted === true || Number(msg.muted) === 1;
  mine.muted_users = mine.muted_users.filter((u) => u !== target);
  if (muted) mine.muted_users.push(target);
  saveDB();
  send(client, {
    type: "social_update",
    action: muted ? "muted" : "unmuted",
    username: target,
  });
  sendFriendSnapshot(client);
}

function handleFriendBlock(client, msg) {
  if (!client.username) return sendError(client, "not logged in");
  const target = normalizeUsername(msg.username);
  if (!validUsername(target) || target === client.username) return sendError(client, "invalid username");
  const mine = ensureUserShape(client.username);
  const other = ensureUserShape(target);
  if (!mine || !other) return sendError(client, "user not found");

  mine.blocked_users = mine.blocked_users.filter((u) => u !== target);
  mine.blocked_users.push(target);
  mine.muted_users = mine.muted_users.filter((u) => u !== target);
  mine.friends = mine.friends.filter((u) => u !== target);
  other.friends = other.friends.filter((u) => u !== client.username);
  other.muted_users = other.muted_users.filter((u) => u !== client.username);
  mine.friend_requests = mine.friend_requests.filter((u) => u !== target);
  other.friend_requests = other.friend_requests.filter((u) => u !== client.username);
  expireChallengesBetween(client.username, target);
  cancelRematchesBetween(client.username, target);
  saveDB();
  send(client, { type: "social_update", action: "blocked", username: target });
  sendSocialSnapshots(client.username, target);
}

function handleFriendUnblock(client, msg) {
  if (!client.username) return sendError(client, "not logged in");
  const target = normalizeUsername(msg.username);
  const mine = ensureUserShape(client.username);
  if (!mine || !ensureUserShape(target)) return sendError(client, "user not found");
  mine.blocked_users = mine.blocked_users.filter((u) => u !== target);
  saveDB();
  send(client, { type: "social_update", action: "unblocked", username: target });
  sendFriendSnapshot(client);
}

function handleChallenge(client, msg) {
  if (!client.username) return sendError(client, "not logged in");
  if (rejectUnsupportedClient(client)) return;
  const target = normalizeUsername(msg.username);
  const mine = ensureUserShape(client.username);
  const other = ensureUserShape(target);
  if (!mine || !other) return sendError(client, "user not found");
  if (usersBlockEachOther(client.username, target)) return sendError(client, "user is unavailable");
  if (!mine.friends.includes(target)) return sendError(client, "you can only challenge friends");
  if (client.match_id) return sendError(client, "you are already in a match");
  const targetClient = connectedClient(target);
  if (!targetClient) return sendError(client, "friend is offline");
  if (targetClient.match_id) return sendError(client, "friend is already in a match");
  if (!clientsProtocolCompatible(client, targetClient)) {
    return sendError(client, "Friend is using an incompatible Eggnogg+ build. Both players must update.");
  }
  const existing = [...challenges.values()].find((c) =>
    c.from === client.username && c.to === target && c.expires_at > now());
  if (existing) {
    send(client, {
      type: "challenge_sent",
      username: target,
      id: existing.id,
      map_key: existing.map_key,
      map_label: existing.map_label,
      expires_in: Math.max(0, Math.ceil((existing.expires_at - now()) / 1000)),
    });
    return;
  }
  const selectedKey = String(msg.map_key || "").trim().toLowerCase();
  const selectedMap = sharedMapChoices(client, targetClient)
    .find((map) => map.key === selectedKey);
  if (!selectedKey || !selectedMap) {
    return sendError(client, "selected challenge map is no longer compatible");
  }
  const id = nextChallengeId++;
  const challenge = {
    id,
    from: client.username,
    to: target,
    map_key: selectedMap.key,
    map_label: selectedMap.label,
    created_at: now(),
    expires_at: now() + CHALLENGE_TTL_MS,
  };
  challenges.set(id, challenge);
  send(client, {
    type: "challenge_sent",
    username: target,
    id,
    map_key: challenge.map_key,
    map_label: challenge.map_label,
    expires_in: 300,
  });
  if (targetClient) sendFriendSnapshot(targetClient);
}

function handleChallengeMaps(client, msg) {
  if (!client.username) return sendError(client, "not logged in");
  if (rejectUnsupportedClient(client)) return;
  const target = normalizeUsername(msg.username);
  const requestId = Number.isInteger(msg.request_id) && msg.request_id > 0
    ? Math.min(msg.request_id, 0x7fffffff)
    : 0;
  const mine = ensureUserShape(client.username);
  const other = ensureUserShape(target);
  if (!mine || !other) return sendError(client, "user not found");
  if (usersBlockEachOther(client.username, target)) return sendError(client, "user is unavailable");
  if (!mine.friends.includes(target)) return sendError(client, "you can only challenge friends");
  if (client.match_id) return sendError(client, "you are already in a match");
  const targetClient = connectedClient(target);
  if (!targetClient) return sendError(client, "friend is offline");
  if (targetClient.match_id) return sendError(client, "friend is already in a match");
  if (!clientsProtocolCompatible(client, targetClient)) {
    return sendError(client, "Friend is using an incompatible Eggnogg+ build. Both players must update.");
  }

  const choices = sharedMapChoices(client, targetClient);
  send(client, {
    type: "challenge_maps_begin",
    username: target,
    request_id: requestId,
    count: choices.length,
  });
  for (const map of choices) {
    send(client, {
      type: "challenge_map_choice",
      username: target,
      request_id: requestId,
      key: map.key,
      label: map.label,
    });
  }
  send(client, {
    type: "challenge_maps_end",
    username: target,
    request_id: requestId,
    count: choices.length,
  });
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
  if (rejectUnsupportedClient(client)) return;
  const challenge = findChallenge(client, msg);
  if (!challenge || challenge.to !== client.username || challenge.expires_at <= now()) {
    return sendError(client, "challenge expired");
  }
  if (usersBlockEachOther(challenge.from, challenge.to)) {
    challenges.delete(challenge.id);
    sendFriendSnapshot(client);
    return sendError(client, "challenge expired");
  }
  const fromClient = connectedClient(challenge.from);
  if (!fromClient) {
    challenges.delete(challenge.id);
    sendFriendSnapshot(client);
    return sendError(client, "challenger is offline");
  }
  if (!clientsProtocolCompatible(client, fromClient)) {
    sendError(fromClient, "Friend is using an incompatible Eggnogg+ build. Both players must update.");
    return sendError(client, "Friend is using an incompatible Eggnogg+ build. Both players must update.");
  }
  if (client.match_id || fromClient.match_id) {
    return sendError(client, "challenge players are already in a match");
  }
  const selectedMap = sharedMapChoices(fromClient, client)
    .find((map) => map.key === challenge.map_key);
  if (!selectedMap) {
    challenges.delete(challenge.id);
    sendFriendSnapshot(client);
    sendFriendSnapshot(fromClient);
    sendError(fromClient, "selected challenge map is no longer compatible");
    return sendError(client, "selected challenge map is no longer compatible");
  }
  challenges.delete(challenge.id);
  send(fromClient, {
    type: "challenge_accepted",
    username: client.username,
    id: challenge.id,
    map_key: selectedMap.key,
    map_label: selectedMap.label,
  });
  sendFriendSnapshot(client);
  sendFriendSnapshot(fromClient);
  makeMatch(fromClient, client, "challenge", "", challenge.id, selectedMap.key);
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
  const rematch = createRematchForMatch(match);
  for (const username of [match.a, match.b]) {
    const c = connectedClient(username);
    const elo = publicElo(username);
    const rating = ratings[username] || {};
    if (c && c.match_id === match.id) {
      c.match_id = 0;
      const terminal = {
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
        rematch_available: rematch ? 1 : 0,
        rematch_expires_in: rematch ? rematchExpiresIn(rematch) : 0,
        rematch_unranked: 1,
      };
      /* The peer-disconnect observer can race a committed forfeit: the server
       * may have already awarded the observer a win when its queued local
       * "P2P disconnected" report arrives. Retain one exact terminal response
       * per connected participant so that retry is idempotent instead of
       * surfacing a misleading "invalid or stale match result" error. */
      c.last_match_terminal = terminal;
      send(c, terminal);
    }
  }
  activeMatches.delete(match.id);
  retainTerminalRelay(match);
  refreshFriendsForUsers([match.a, match.b]);
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
    if (c && c.match_id === match.id) {
      c.match_id = 0;
      const terminal = { type: "match_abort", match_id: match.id, reason };
      c.last_match_terminal = terminal;
      send(c, terminal);
    }
  }
  clearRelayEndpoints(match);
  activeMatches.delete(match.id);
  refreshFriendsForUsers([match.a, match.b]);
}

function replayTerminalMatch(client, msg) {
  let terminal = client && client.last_match_terminal;
  const matchId = msg && msg.match_id;
  if (!terminal || !Number.isInteger(matchId) || matchId <= 0 ||
      terminal.match_id !== matchId) {
    return false;
  }
  if (terminal.rematch_available) {
    const rematch = rematches.get(matchId);
    if (!rematch || rematch.expires_at <= now()) {
      terminal.rematch_available = 0;
      terminal.rematch_expires_in = 0;
    } else {
      terminal = {
        ...terminal,
        rematch_expires_in: rematchExpiresIn(rematch),
      };
    }
  }
  send(client, terminal);
  return true;
}

function handleRematchRequest(client, msg) {
  if (!client.username) return sendError(client, "not logged in");
  if (rejectUnsupportedClient(client)) return;
  const rematch = findClientRematch(client, msg);
  if (!rematch) {
    send(client, { type: "rematch_unavailable", match_id: msg && msg.match_id || 0 });
    return;
  }
  const otherUsername = client.username === rematch.a ? rematch.b : rematch.a;
  const otherClient = connectedClient(otherUsername);
  if (!otherClient || client.match_id || otherClient.match_id ||
      client.queue || otherClient.queue ||
      usersBlockEachOther(client.username, otherUsername)) {
    notifyRematchClosed(rematch, client.username, "rematch_unavailable", "rematch unavailable");
    return;
  }
  if (!clientsProtocolCompatible(client, otherClient)) {
    notifyRematchClosed(
      rematch,
      client.username,
      "rematch_unavailable",
      "opponent is using an incompatible Eggnogg+ build",
    );
    return;
  }

  rematch.accepted_by.add(client.username);
  if (rematch.accepted_by.size < 2) {
    send(client, {
      type: "rematch_waiting",
      match_id: rematch.match_id,
      expires_in: rematchExpiresIn(rematch),
    });
    send(otherClient, {
      type: "rematch_offer",
      match_id: rematch.match_id,
      from: client.username,
      map_label: rematch.map_label,
      expires_in: rematchExpiresIn(rematch),
      unranked: 1,
    });
    return;
  }

  removeRematch(rematch);
  send(client, { type: "rematch_starting", match_id: rematch.match_id });
  send(otherClient, { type: "rematch_starting", match_id: rematch.match_id });
  if (!makeMatch(client, otherClient, "rematch", "", 0, rematch.map_key)) {
    send(client, {
      type: "rematch_unavailable",
      match_id: rematch.match_id,
      reason: "the previous map is no longer compatible",
    });
    send(otherClient, {
      type: "rematch_unavailable",
      match_id: rematch.match_id,
      reason: "the previous map is no longer compatible",
    });
  }
}

function handleRematchDecline(client, msg) {
  if (!client.username) return sendError(client, "not logged in");
  const rematch = findClientRematch(client, msg);
  if (!rematch) {
    send(client, { type: "rematch_closed", match_id: msg && msg.match_id || 0 });
    return;
  }
  notifyRematchClosed(rematch, client.username, "rematch_declined", "rematch declined");
}

function matchForClientMessage(client, msg) {
  if (!client || !client.username || !client.match_id) return null;
  const matchId = msg && msg.match_id;
  if (!Number.isInteger(matchId) || matchId <= 0 || matchId !== client.match_id) return null;
  const match = activeMatches.get(matchId);
  if (!match || match.finished || !matchHasUser(match, client.username)) return null;
  return match;
}

function handleMatchStarted(client, msg) {
  const match = matchForClientMessage(client, msg);
  if (!match) return sendError(client, "invalid or stale match start");
  if (!match.started_by.has(client.username)) {
    match.started_by.add(client.username);
    console.log(`[match#${match.id}] gameplay ready ${client.username} (${match.started_by.size}/2)`);
  }
  if (match.committed) {
    send(client, { type: "match_started", match_id: match.id, committed: 1 });
    return;
  }
  if (match.started_by.size < 2) return;
  match.committed = true;
  match.committed_at = now();
  for (const username of [match.a, match.b]) {
    const c = connectedClient(username);
    if (c && c.match_id === match.id) {
      send(c, { type: "match_started", match_id: match.id, committed: 1 });
    }
  }
  console.log(`[match#${match.id}] gameplay committed`);
  refreshFriendsForUsers([match.a, match.b]);
}

function handleMatchAbort(client, msg) {
  const match = matchForClientMessage(client, msg);
  if (!match) return sendError(client, "invalid or stale match abort");
  const reasonText = String((msg && msg.reason) || "match setup aborted")
    .replace(/[\r\n\t]/g, " ")
    .slice(0, 160);
  if (!match.committed) {
    cancelMatch(match, reasonText || "match setup aborted");
    return;
  }
  const winner = client.username === match.a ? match.b : match.a;
  finishMatch(match, winner, reasonText || "opponent forfeited");
}

function handleMatchEnd(client, msg) {
  const match = matchForClientMessage(client, msg);
  if (!match) {
    if (replayTerminalMatch(client, msg)) return;
    return sendError(client, "invalid or stale match result");
  }
  if (!match.committed) {
    cancelMatch(match, "premature match result");
    return;
  }
  const result = String(msg.result || "").toLowerCase();
  if (result === "win" || result === "loss") {
    let winner = result === "win"
      ? client.username
      : (client.username === match.a ? match.b : match.a);
    if (Object.prototype.hasOwnProperty.call(msg, "winner_player")) {
      const winnerPlayer = msg.winner_player;
      if (!Number.isInteger(winnerPlayer) || winnerPlayer < 0 || winnerPlayer > 1 ||
          !Array.isArray(match.player_by_index) ||
          !validUsername(match.player_by_index[winnerPlayer])) {
        return sendError(client, "winner_player must be the synchronized player slot 0 or 1");
      }
      winner = match.player_by_index[winnerPlayer];
      const expectedResult = winner === client.username ? "win" : "loss";
      if (expectedResult !== result) {
        console.warn(
          `[match#${match.id}] ${client.username} local result=${result} disagreed with synchronized winner_player=${winnerPlayer}; using ${winner}`,
        );
      }
    }
    if (match.results.has(client.username)) return;
    match.transport_failures.delete(client.username);
    match.results.set(client.username, winner);
    if (match.results.size >= 2) {
      const winners = [...match.results.values()];
      const agreed = winners.every((w) => w === winners[0]);
      if (agreed) finishMatch(match, winners[0], "reported");
      else cancelMatch(match, "conflicting match reports; no contest");
      return;
    }
    send(client, { type: "match_report_ack", match_id: match.id, pending: 1 });
    armMatchResolutionTimer(match);
    return;
  }
  sendError(client, "match result must be win or loss");
}

function armMatchResolutionTimer(match) {
  if (!match || match.finished || match.result_timer) return;
  match.result_timer = setTimeout(() => {
    if (!activeMatches.has(match.id) || match.finished) return;
    if (match.results.size < 1 && match.transport_failures.size < 1) return;
    cancelMatch(
      match,
      match.transport_failures.size > 0
        ? "P2P connection failed; no contest"
        : "match result was not confirmed; no contest",
    );
  }, MATCH_REPORT_TIMEOUT_MS);
  if (typeof match.result_timer.unref === "function") match.result_timer.unref();
}

function handleMatchTransportFailure(client, msg) {
  const match = matchForClientMessage(client, msg);
  if (!match) {
    if (replayTerminalMatch(client, msg)) return;
    return sendError(client, "invalid or stale match transport failure");
  }
  if (!match.committed) {
    cancelMatch(match, "P2P connection failed before gameplay");
    return;
  }
  if (match.results.has(client.username)) {
    send(client, {
      type: "match_transport_failure_ack",
      match_id: match.id,
      pending: 1,
    });
    return;
  }
  const reason = String((msg && msg.reason) || "P2P transport failed")
    .replace(/[\r\n\t]/g, " ")
    .slice(0, 160);
  if (!match.transport_failures.has(client.username)) {
    match.transport_failures.set(client.username, reason);
    console.warn(
      `[match#${match.id}] ${client.username} reported transport failure ` +
      `(${match.transport_failures.size}/2): ${reason}`,
    );
  }
  send(client, {
    type: "match_transport_failure_ack",
    match_id: match.id,
    pending: 1,
  });
  if (match.transport_failures.size >= 2) {
    cancelMatch(match, "P2P connection failed; no contest");
    return;
  }
  armMatchResolutionTimer(match);
}

function dispatch(client, msg) {
  switch (msg.type) {
    case "server_info": sendServerInfo(client); break;
    case "register": handleRegister(client, msg); break;
    case "login": handleLogin(client, msg); break;
    case "map_manifest": handleMapManifest(client, msg); break;
    case "join_queue": handleJoinQueue(client, msg); break;
    case "leave_queue": handleLeaveQueue(client); break;
    case "friend_request": handleFriendRequest(client, msg); break;
    case "friend_accept": handleFriendAccept(client, msg); break;
    case "friend_decline": handleFriendDecline(client, msg); break;
    case "friend_remove": handleFriendRemove(client, msg); break;
    case "friend_mute": handleFriendMute(client, msg); break;
    case "friend_block": handleFriendBlock(client, msg); break;
    case "friend_unblock": handleFriendUnblock(client, msg); break;
    case "challenge_maps": handleChallengeMaps(client, msg); break;
    case "challenge": handleChallenge(client, msg); break;
    case "challenge_accept": handleChallengeAccept(client, msg); break;
    case "challenge_decline": handleChallengeDecline(client, msg); break;
    case "match_started": handleMatchStarted(client, msg); break;
    case "match_abort": handleMatchAbort(client, msg); break;
    case "match_end": handleMatchEnd(client, msg); break;
    case "match_transport_failure": handleMatchTransportFailure(client, msg); break;
    case "rematch_request": handleRematchRequest(client, msg); break;
    case "rematch_decline": handleRematchDecline(client, msg); break;
    case "ping": send(client, { type: "pong", seq: msg.seq || 0 }); break;
    default: sendError(client, "unknown message type");
  }
}

function destroyClient(client) {
  if (!clients.has(client)) return;
  clients.delete(client);
  removeFromQueues(client, "disconnected");
  if (client.username && onlineByUser.get(client.username) === client) {
    /* Remove presence ownership before any challenge/friend snapshot. Otherwise
     * one stale "online" snapshot can precede the final offline refresh. */
    onlineByUser.delete(client.username);
  }
  if (client.username) {
    cancelRematchesForUser(client.username, "opponent disconnected");
  }
  if (client.username) {
    for (const [id, challenge] of challenges) {
      if (challenge.from !== client.username && challenge.to !== client.username) continue;
      challenges.delete(id);
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
    refreshFriendsFor(client.username);
  }
  if (client.match_id) {
    const match = activeMatches.get(client.match_id);
    if (match) {
      const other = client.username === match.a ? match.b : match.a;
      const otherClient = connectedClient(other);
      if (!match.committed) cancelMatch(match, "opponent disconnected before gameplay");
      else if (otherClient) finishMatch(match, other, "opponent disconnected");
      else {
        clearRelayEndpoints(match);
        activeMatches.delete(match.id);
      }
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
const relayEndpointIndex = new Map();
const terminalRelayMatches = new Map();
const rematches = new Map();

function requireAdminUser(rawUsername) {
  const username = normalizeUsername(rawUsername);
  const rec = ensureUserShape(username);
  if (!validUsername(username) || !rec) throw new Error("user not found");
  return { username, rec };
}

function adminSnapshot() {
  const capturedAt = now();
  const usernames = Object.keys(db.users).sort();
  const friendships = [];
  const friendRequests = [];
  const blocks = [];
  const mutes = [];
  const friendshipKeys = new Set();

  const users = usernames.map((username) => {
    const rec = ensureUserShape(username);
    const rating = ensureRating(username);
    const client = connectedClient(username);
    const match = client && client.match_id
      ? activeMatches.get(client.match_id)
      : null;
    const opponent = match
      ? (match.a === username ? match.b : match.a)
      : "";

    for (const friend of rec ? rec.friends : []) {
      const pair = [username, friend].sort();
      const key = pair.join("\0");
      if (friendshipKeys.has(key)) continue;
      friendshipKeys.add(key);
      const other = ensureUserShape(friend);
      friendships.push({
        a: pair[0],
        b: pair[1],
        mutual: Boolean(other && other.friends.includes(username)),
      });
    }
    for (const from of rec ? rec.friend_requests : []) {
      friendRequests.push({ from, to: username });
    }
    for (const target of rec ? rec.blocked_users : []) {
      blocks.push({ from: username, to: target });
    }
    for (const target of rec ? rec.muted_users : []) {
      mutes.push({ from: username, to: target });
    }

    return {
      username,
      online: Boolean(client),
      presence: friendPresence(username),
      opponent,
      match_id: match ? match.id : 0,
      queue: client ? client.queue : "",
      banned: Boolean(rec && rec.ban),
      ban_reason: rec && rec.ban ? rec.ban.reason : "",
      elo: rating ? rating.elo : DEFAULT_ELO,
      mmr: rating ? rating.mmr : DEFAULT_MMR,
      friends: rec ? [...rec.friends].sort() : [],
      friend_requests: rec ? [...rec.friend_requests].sort() : [],
      blocked_users: rec ? [...rec.blocked_users].sort() : [],
      muted_users: rec ? [...rec.muted_users].sort() : [],
      created_at: rec ? rec.created_at : "",
      connected_at: client ? client.connected_at : 0,
      authenticated_at: client ? client.authenticated_at : 0,
      framework_version: client ? client.framework_version : "",
      protocols: client
        ? `${client.control_protocol}/${client.match_protocol}/${client.p2p_protocol}`
        : "",
      build_id: client && client.build_id
        ? client.build_id.toString(16).padStart(8, "0")
        : "",
      map_count: client && Array.isArray(client.maps) ? client.maps.length : 0,
      route_version: client ? client.route_version : 0,
    };
  });

  const queueEntry = (client) => ({
    username: client.username,
    elo: publicElo(client.username),
    joined_at: client.queue_joined_at,
    wait_ms: Math.max(0, capturedAt - client.queue_joined_at),
    map_count: Array.isArray(client.maps) ? client.maps.length : 0,
    framework_version: client.framework_version,
    build_id: client.build_id
      ? client.build_id.toString(16).padStart(8, "0")
      : "",
    rating_range: client.queue === "competitive"
      ? Math.round(competitiveRange(client))
      : 0,
  });

  return {
    generated_at: new Date(capturedAt).toISOString(),
    connections: clients.size,
    online: onlineByUser.size,
    queues: {
      casual: casualQueue.map(queueEntry),
      competitive: competitiveQueue.map(queueEntry),
    },
    matches: [...activeMatches.values()]
      .sort((a, b) => a.id - b.id)
      .map((match) => ({
        id: match.id,
        a: match.a,
        b: match.b,
        source: match.source,
        queue: match.queue,
        competitive: Boolean(match.competitive),
        challenge_id: match.challenge_id || 0,
        map_key: match.map ? match.map.key : "",
        map_label: match.map ? match.map.label : "",
        created_at: match.created_at,
        committed: Boolean(match.committed),
        committed_at: match.committed_at || 0,
        started_players: match.started_by ? match.started_by.size : 0,
        reported_players: match.results ? match.results.size : 0,
        rendezvous_players: match.p2p_endpoints ? match.p2p_endpoints.size : 0,
        route: match.force_relay
          ? "relay"
          : (match.p2p_endpoints && match.p2p_endpoints.size === 2
            ? "direct"
            : "negotiating"),
        protocols: `${match.control_protocol}/${match.match_protocol}/${match.p2p_protocol}`,
        build_id: match.build_id
          ? match.build_id.toString(16).padStart(8, "0")
          : "",
      })),
    challenges: [...challenges.values()]
      .sort((a, b) => a.created_at - b.created_at)
      .map((challenge) => ({
        id: challenge.id,
        from: challenge.from,
        to: challenge.to,
        map_key: challenge.map_key,
        map_label: challenge.map_label,
        created_at: challenge.created_at,
        expires_at: challenge.expires_at,
      })),
    rematches: [...rematches.values()]
      .sort((a, b) => a.created_at - b.created_at)
      .map((rematch) => ({
        match_id: rematch.match_id,
        a: rematch.a,
        b: rematch.b,
        map_key: rematch.map_key,
        map_label: rematch.map_label,
        created_at: rematch.created_at,
        expires_at: rematch.expires_at,
        accepted_by: [...rematch.accepted_by].sort(),
      })),
    friendships: friendships.sort((a, b) =>
      a.a.localeCompare(b.a) || a.b.localeCompare(b.b)),
    friend_requests: friendRequests.sort((a, b) =>
      a.to.localeCompare(b.to) || a.from.localeCompare(b.from)),
    blocks: blocks.sort((a, b) =>
      a.from.localeCompare(b.from) || a.to.localeCompare(b.to)),
    mutes: mutes.sort((a, b) =>
      a.from.localeCompare(b.from) || a.to.localeCompare(b.to)),
    users,
  };
}

function adminResetPassword(rawUsername, password) {
  const { username, rec } = requireAdminUser(rawUsername);
  const nextPassword = String(password || "");
  if (nextPassword.length < 4 || nextPassword.length > 256) {
    throw new Error("password must be 4..256 characters");
  }
  rec.salt = crypto.randomBytes(16).toString("hex");
  rec.hash = hashPassword(nextPassword, rec.salt);
  rec.password_reset_at = new Date().toISOString();
  saveDB();
  const client = connectedClient(username);
  if (client) {
    sendError(client, "Password reset by server administrator.");
    destroyClient(client);
  }
  return `Password reset for ${username}; existing sessions were disconnected.`;
}

function adminSetBan(rawUsername, banned, reason) {
  const { username, rec } = requireAdminUser(rawUsername);
  if (banned) {
    rec.ban = {
      reason: String(reason || "Banned by server administrator").trim().slice(0, 160),
      at: new Date().toISOString(),
    };
  } else {
    delete rec.ban;
  }
  saveDB();
  const client = connectedClient(username);
  if (banned && client) {
    sendError(client, rec.ban.reason || "Account banned.");
    destroyClient(client);
  }
  return banned
    ? `${username} was banned and disconnected.`
    : `${username} was unbanned.`;
}

function adminDisconnect(rawUsername) {
  const { username } = requireAdminUser(rawUsername);
  const client = connectedClient(username);
  if (!client) return `${username} is already offline.`;
  sendError(client, "Disconnected by server administrator.");
  destroyClient(client);
  return `${username} was disconnected.`;
}

function adminResetRating(rawUsername) {
  const { username } = requireAdminUser(rawUsername);
  const rating = setRating(username, DEFAULT_ELO, DEFAULT_MMR);
  saveRatings();
  const client = connectedClient(username);
  if (client) {
    send(client, { type: "rating_update", elo: rating.elo, elo_before: rating.elo });
  }
  return `${username}'s rating was reset to ${rating.elo}.`;
}

const adminServer = startAdminServerFromEnv(process.env, {
  snapshot: adminSnapshot,
  resetPassword: adminResetPassword,
  setBan: adminSetBan,
  disconnect: adminDisconnect,
  resetRating: adminResetRating,
});

const matchSweepTimer = setInterval(() => {
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
    if (match.finished || match.committed) continue;
    if (cutoff - match.created_at > MATCH_SETUP_STALE_MS) {
      cancelMatch(match, "stale match setup");
    }
  }
  for (const rematch of [...rematches.values()]) {
    if (rematch.expires_at > cutoff) continue;
    if (!removeRematch(rematch)) continue;
    for (const username of [rematch.a, rematch.b]) {
      const client = connectedClient(username);
      if (client) {
        send(client, {
          type: "rematch_expired",
          match_id: rematch.match_id,
        });
      }
    }
  }
  tryMatchmaking();
}, MATCH_SWEEP_INTERVAL_MS);

const server = net.createServer((socket) => {
  const client = {
    socket,
    buf: "",
    username: "",
    connected_at: now(),
    authenticated_at: 0,
    maps: defaultManifest(),
    mapIndex: mapIndex(defaultManifest()),
    framework_version: "unknown",
    control_protocol: 0,
    match_protocol: 0,
    p2p_protocol: 0,
    build_id: 0,
    game_exe_id: 0,
    framework_dll_id: 0,
    last_incompatible_build: "",
    p2p_port: 0,
    lan_host: "",
    route_version: 0,
    queue: "",
    queue_joined_at: 0,
    match_id: 0,
    last_match_terminal: null,
  };
  clients.add(client);
  socket.setEncoding("utf8");
  socket.setNoDelay(true);
  socket.setTimeout(CLIENT_IDLE_TIMEOUT_MS);
  sendServerInfo(client);

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
  if (!buf || buf.length > GGPO_PACKET_MAX_BYTES) return;
  if (handleRelayPacket(buf, rinfo)) return;
  let msg;
  try {
    msg = JSON.parse(buf.toString("utf8").trim());
  } catch (_) {
    return;
  }
  if (msg && msg.type === "udp_ping") {
    handleUdpPingMessage(msg, rinfo);
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

let shuttingDown = false;

function closeListener(listener) {
  if (!listener) return Promise.resolve();
  return new Promise((resolve) => {
    try {
      listener.close(() => resolve());
    } catch (_) {
      resolve();
    }
  });
}

async function orderlyShutdown(signal) {
  if (shuttingDown) return;
  shuttingDown = true;
  console.log(`Received ${signal}; retiring queue posts and shutting down.`);
  clearInterval(matchSweepTimer);
  for (const match of [...terminalRelayMatches.values()]) {
    clearTerminalRelay(match);
  }
  for (const client of [...clients]) destroyClient(client);

  const cleanup = Promise.allSettled([
    closeListener(server),
    closeListener(udpServer),
    closeListener(adminServer),
    closeListener(lfgRedirectServer),
    lfgBot.close({ retire: true }),
  ]);
  await Promise.race([
    cleanup,
    new Promise((resolve) => setTimeout(resolve, 10000)),
  ]);
  process.exit(0);
}

process.once("SIGINT", () => { void orderlyShutdown("SIGINT"); });
process.once("SIGTERM", () => { void orderlyShutdown("SIGTERM"); });
