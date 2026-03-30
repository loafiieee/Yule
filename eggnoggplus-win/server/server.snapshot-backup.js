"use strict";

// Eggnogg+ Online server - single-authority mirror relay.
//
// Flow:
//   1. matchmake two peers and assign a shared map,
//   2. randomly choose one peer as the hidden authority,
//   3. wait for both peers to report the loaded map tiles,
//   4. abort if the maps differ,
//   5. authority simulates both players,
//   6. non-authority sends local input to the server,
//   7. server forwards that input to the authority,
//   8. authority streams full snapshots back through the server,
//   9. non-authority mirrors those snapshots.

const net = require("net");
const crypto = require("crypto");
const fs = require("fs");
const path = require("path");

const PORT = parseInt(process.env.PORT || "7878", 10);
const DB_FILE = process.env.DB || path.join(__dirname, "users.json");
const VANILLA_MAPS = 5;
const MAX_LINE_BYTES = 512 * 1024;
const VERBOSE = (process.env.VERBOSE || "1") !== "0";

let nextMatchId = 1;

function loadDB() {
  try {
    if (fs.existsSync(DB_FILE)) return JSON.parse(fs.readFileSync(DB_FILE, "utf8"));
  } catch (e) {
    console.error("[db] load error:", e.message);
  }
  return {};
}

function saveDB(db) {
  try {
    fs.writeFileSync(DB_FILE, JSON.stringify(db, null, 2), "utf8");
  } catch (e) {
    console.error("[db] save error:", e.message);
  }
}

function hashPassword(password, salt) {
  return crypto.createHash("sha256").update(`${salt}:${password}`).digest("hex");
}

function defaultManifest() {
  const maps = [];
  for (let i = 0; i < VANILLA_MAPS; i += 1) {
    maps.push({ key: `vanilla:${i}`, selector: i, label: `Vanilla ${i + 1}`, kind: "vanilla" });
  }
  return maps;
}

function sanitizeManifest(rawMaps) {
  const result = [];
  const seen = new Set();
  for (const item of (Array.isArray(rawMaps) ? rawMaps : [])) {
    if (!item || typeof item !== "object") continue;
    const key = typeof item.key === "string" ? item.key.trim().toLowerCase() : "";
    const selector = Number.parseInt(item.selector, 10);
    if (!key || !Number.isFinite(selector) || selector < 0 || selector > 4096) continue;
    if (seen.has(key)) continue;
    seen.add(key);
    result.push({
      key,
      selector,
      label: typeof item.label === "string" ? item.label : key,
      kind: typeof item.kind === "string" ? item.kind : (key.startsWith("vanilla:") ? "vanilla" : "custom"),
    });
  }
  return result.length > 0 ? result : defaultManifest();
}

function buildManifestIndex(maps) {
  const idx = new Map();
  for (const item of maps) idx.set(item.key, item);
  return idx;
}

function chooseSharedMap(p1, p2) {
  const leftMaps = p1.mapManifest || defaultManifest();
  const rightIndex = p2.mapManifestIndex || buildManifestIndex(defaultManifest());
  const shared = [];
  for (const left of leftMaps) {
    const right = rightIndex.get(left.key);
    if (!right) continue;
    shared.push({
      key: left.key,
      label: left.label || right.label || left.key,
      p1Selector: left.selector,
      p2Selector: right.selector,
    });
  }
  if (shared.length === 0) return null;
  return shared[Math.floor(Math.random() * shared.length)];
}

function logv(message) {
  if (VERBOSE) console.log(message);
}

function sanitizeHash(value) {
  return typeof value === "string" ? value.trim().slice(0, 64) : "";
}

function summarizeState(state) {
  if (!state || typeof state !== "object") return "state=nil";
  const p0 = state.player && typeof state.player === "object" ? state.player : {};
  const p1 = state.enemy && typeof state.enemy === "object" ? state.enemy : {};
  const entities = Array.isArray(state.entities) ? state.entities.length : 0;
  return `seq=${state.seq} room=${state.room_index} p0=(${Number(p0.x || 0).toFixed(2)},${Number(p0.y || 0).toFixed(2)}) p1=(${Number(p1.x || 0).toFixed(2)},${Number(p1.y || 0).toFixed(2)}) entities=${entities}`;
}

function summarize(obj) {
  if (!obj || typeof obj !== "object") return String(obj);
  const t = obj.type || "?";
  if (t === "sync_ready") return `${t} tile=${obj.tile_hash} room=${obj.room_index} authority=${obj.authority_role}`;
  if (t === "sync_begin") return `${t} authority=${obj.authority_role}`;
  if (t === "input") return `${t} seq=${obj.seq} cmd=${obj.cmd}`;
  if (t === "remote_input") return `${t} role=${obj.role} seq=${obj.seq} cmd=${obj.cmd}`;
  if (t === "snapshot") return `${t} seq=${obj.seq}`;
  if (t === "sync_error") return `${t} reason=${obj.reason}`;
  if (t === "ping" || t === "pong") return `${t} seq=${obj.seq}`;
  if (t === "match_found") return `${t} role=${obj.role} authority=${obj.authority_role} map=${obj.map_label}`;
  if (t === "auth_ok" || t === "auth_fail" || t === "queue_update" || t === "match_start" || t === "ready") return t;
  return t;
}

function send(client, obj) {
  try {
    const line = `${JSON.stringify(obj)}\n`;
    client.socket.write(line);
    client.txCount = (client.txCount || 0) + 1;
    client.txBytes = (client.txBytes || 0) + line.length;
    client.lastTxAt = Date.now();
    logv(`[tx] to=${client.username || "anon"} #${client.txCount} bytes=${line.length} ${summarize(obj)}`);
  } catch (_) {
    // ignore socket write failures
  }
}

function broadcastQueueCount() {
  let count = 0;
  for (const client of clients) {
    if (client.inQueue) count += 1;
  }
  const line = `${JSON.stringify({ type: "queue_update", count })}\n`;
  for (const client of clients) {
    if (!client.username) continue;
    try {
      client.socket.write(line);
    } catch (_) {
      // ignore
    }
  }
}

let userDB = loadDB();
const clients = new Set();
const queue = [];

class Match {
  constructor(p1, p2, sharedMap) {
    this.id = nextMatchId;
    nextMatchId += 1;
    this.players = [p1, p2];
    this.ready = [false, false];
    this.map = sharedMap;
    this.active = false;

    this.authorityRole = Math.random() < 0.5 ? 0 : 1;
    this.syncReady = [null, null];
    this.syncStarted = false;

    this.lastRemoteInputSeq = [-1, -1];
    this.lastRemoteInputAt = [0, 0];
    this.lastSnapshotSeq = -1;
    this.lastSnapshotAt = 0;

    this.inputRelayCount = 0;
    this.snapshotRelayCount = 0;
  }

  tag() {
    return `[match#${this.id}]`;
  }

  roleOf(client) {
    return this.players.indexOf(client);
  }

  authorityClient() {
    return this.players[this.authorityRole];
  }

  mirrorRole() {
    return 1 - this.authorityRole;
  }

  mirrorClient() {
    return this.players[this.mirrorRole()];
  }

  clearPlayers() {
    for (const player of this.players) {
      player.match = null;
      player.inQueue = false;
    }
  }

  notifyFound() {
    const [p0, p1] = this.players;
    send(p0, {
      type: "match_found",
      role: 0,
      authority_role: this.authorityRole,
      map_sel: this.map.p1Selector,
      map_key: this.map.key,
      map_label: this.map.label,
    });
    send(p1, {
      type: "match_found",
      role: 1,
      authority_role: this.authorityRole,
      map_sel: this.map.p2Selector,
      map_key: this.map.key,
      map_label: this.map.label,
    });
    console.log(`${this.tag()} found ${p0.username} vs ${p1.username} map=${this.map.label} authority_role=${this.authorityRole}`);
  }

  onReady(client) {
    const role = this.roleOf(client);
    if (role < 0) return;
    this.ready[role] = true;
    if (this.ready[0] && this.ready[1]) {
      this.active = true;
      this.syncReady = [null, null];
      this.syncStarted = false;
      this.lastRemoteInputSeq = [-1, -1];
      this.lastRemoteInputAt = [0, 0];
      this.lastSnapshotSeq = -1;
      this.lastSnapshotAt = 0;
      this.inputRelayCount = 0;
      this.snapshotRelayCount = 0;
      for (const player of this.players) {
        send(player, { type: "match_start", authority_role: this.authorityRole });
      }
      console.log(`${this.tag()} started authority_role=${this.authorityRole}`);
    }
  }

  abort(reason) {
    console.warn(`${this.tag()} abort ${reason}`);
    for (const player of this.players) {
      send(player, { type: "sync_error", reason });
      send(player, { type: "match_end" });
    }
    this.active = false;
    this.clearPlayers();
    broadcastQueueCount();
  }

  onSyncReady(client, msg) {
    if (!this.active) return;
    const role = this.roleOf(client);
    if (role < 0) return;

    const ready = {
      tileHash: sanitizeHash(msg.tile_hash),
      roomIndex: Number.parseInt(msg.room_index, 10) || 0,
      authorityRole: Number.parseInt(msg.authority_role, 10) || this.authorityRole,
    };
    this.syncReady[role] = ready;
    console.log(`${this.tag()} sync_ready role=${role} user=${client.username} tile=${ready.tileHash} room=${ready.roomIndex}`);

    if (!this.syncReady[0] || !this.syncReady[1]) return;

    if (this.syncReady[0].tileHash !== this.syncReady[1].tileHash) {
      console.warn(`${this.tag()} tile_mismatch p0=${this.syncReady[0].tileHash} p1=${this.syncReady[1].tileHash}`);
      this.abort("Map mismatch between peers. Match aborted.");
      return;
    }

    this.syncStarted = true;
    for (const player of this.players) {
      send(player, {
        type: "sync_begin",
        authority_role: this.authorityRole,
      });
    }
    console.log(`${this.tag()} sync_begin authority_role=${this.authorityRole} tile=${this.syncReady[0].tileHash}`);
  }

  onInput(client, msg) {
    if (!this.active || !this.syncStarted) return;

    const role = this.roleOf(client);
    if (role < 0) return;

    const seq = Number.parseInt(msg.seq, 10);
    const cmd = Number.parseInt(msg.cmd, 10);
    if (!Number.isFinite(cmd) || cmd < 0 || cmd > 127) {
      console.warn(`${this.tag()} invalid input cmd from ${client.username}: ${msg.cmd}`);
      return;
    }
    if (Number.isFinite(seq) && seq > 0 && seq <= this.lastRemoteInputSeq[role]) {
      logv(`${this.tag()} drop stale input role=${role} seq=${seq} last=${this.lastRemoteInputSeq[role]}`);
      return;
    }

    if (Number.isFinite(seq) && seq > 0) this.lastRemoteInputSeq[role] = seq;
    this.lastRemoteInputAt[role] = Date.now();
    this.inputRelayCount += 1;

    // Relay to the OTHER player (both peers exchange inputs in P2P model).
    const other = this.players[1 - role];
    send(other, { type: "remote_input", role, seq, cmd });
    logv(`${this.tag()} relay_input role=${role} seq=${seq} cmd=${cmd}`);
  }

  onSnapshot(client, msg) {
    if (!this.active || !this.syncStarted) return;

    const role = this.roleOf(client);
    if (role !== this.authorityRole) {
      console.warn(`${this.tag()} snapshot rejected from non-authority ${client.username}`);
      return;
    }

    const seq = Number.parseInt(msg.seq, 10);
    const state = msg.state;
    if (!Number.isFinite(seq) || seq < 0) {
      console.warn(`${this.tag()} invalid snapshot seq from ${client.username}: ${msg.seq}`);
      return;
    }
    if (!state || typeof state !== "object") {
      console.warn(`${this.tag()} invalid snapshot state from ${client.username}`);
      return;
    }
    if (seq <= this.lastSnapshotSeq) {
      logv(`${this.tag()} drop stale snapshot seq=${seq} last=${this.lastSnapshotSeq}`);
      return;
    }

    this.lastSnapshotSeq = seq;
    this.lastSnapshotAt = Date.now();
    this.snapshotRelayCount += 1;

    send(this.mirrorClient(), {
      type: "snapshot",
      seq,
      state,
    });
    logv(`${this.tag()} relay_snapshot seq=${seq} ${summarizeState(state)}`);
  }

  end(initiator) {
    this.active = false;
    for (const player of this.players) {
      player.match = null;
      player.inQueue = false;
      if (player !== initiator) send(player, { type: "match_end" });
    }
    console.log(`${this.tag()} ended by ${initiator ? initiator.username : "?"} relayed_inputs=${this.inputRelayCount} relayed_snapshots=${this.snapshotRelayCount}`);
    broadcastQueueCount();
  }
}

function removeFromQueue(client) {
  const idx = queue.indexOf(client);
  if (idx >= 0) {
    queue.splice(idx, 1);
    client.inQueue = false;
  }
}

function tryMatchmake() {
  let matched = true;
  while (matched) {
    matched = false;
    for (let i = 0; i < queue.length && !matched; i += 1) {
      const p1 = queue[i];
      for (let j = i + 1; j < queue.length; j += 1) {
        const p2 = queue[j];
        const shared = chooseSharedMap(p1, p2);
        if (!shared) continue;
        queue.splice(j, 1);
        queue.splice(i, 1);
        p1.inQueue = false;
        p2.inQueue = false;
        const match = new Match(p1, p2, shared);
        p1.match = match;
        p2.match = match;
        match.notifyFound();
        matched = true;
        break;
      }
    }
  }
  broadcastQueueCount();
}

function handleRegister(client, msg) {
  const username = (msg.username || "").trim().toLowerCase();
  const password = msg.password || "";
  if (!username || !password) return send(client, { type: "auth_fail", reason: "missing fields" });
  if (username.length > 24) return send(client, { type: "auth_fail", reason: "username too long" });
  if (!/^[a-z0-9_]+$/.test(username)) return send(client, { type: "auth_fail", reason: "username: a-z 0-9 _ only" });
  if (userDB[username]) return send(client, { type: "auth_fail", reason: "username taken" });

  const salt = crypto.randomBytes(16).toString("hex");
  userDB[username] = { hash: hashPassword(password, salt), salt };
  saveDB(userDB);
  client.username = username;
  send(client, { type: "auth_ok", username });
  console.log(`[auth] registered ${username}`);
  broadcastQueueCount();
}

function handleLogin(client, msg) {
  const username = (msg.username || "").trim().toLowerCase();
  const password = msg.password || "";
  const rec = userDB[username];
  if (!rec) return send(client, { type: "auth_fail", reason: "unknown user" });
  if (hashPassword(password, rec.salt) !== rec.hash) {
    return send(client, { type: "auth_fail", reason: "wrong password" });
  }

  for (const other of clients) {
    if (other !== client && other.username === username) {
      send(other, { type: "error", message: "Logged in from another location" });
      destroyClient(other);
    }
  }

  client.username = username;
  send(client, { type: "auth_ok", username });
  console.log(`[auth] login ${username}`);
  broadcastQueueCount();
}

function handleMapManifest(client, msg) {
  client.mapManifest = sanitizeManifest(msg.maps);
  client.mapManifestIndex = buildManifestIndex(client.mapManifest);
  client.mapManifestSerial = typeof msg.serial === "string" ? msg.serial : "";
  console.log(`[maps] ${client.username || "anon"} manifest=${client.mapManifest.length}`);
}

function handleJoinQueue(client) {
  if (!client.username) return send(client, { type: "error", message: "not logged in" });
  if (client.match) return send(client, { type: "error", message: "already in a match" });
  if (client.inQueue) return;
  if (!client.mapManifest || client.mapManifest.length === 0) {
    client.mapManifest = defaultManifest();
    client.mapManifestIndex = buildManifestIndex(client.mapManifest);
  }
  client.inQueue = true;
  queue.push(client);
  console.log(`[queue] ${client.username} joined queue=${queue.length}`);
  broadcastQueueCount();
  tryMatchmake();
}

function handleLeaveQueue(client) {
  removeFromQueue(client);
  console.log(`[queue] ${client.username || "?"} left`);
  broadcastQueueCount();
}

function handleReady(client) {
  if (client.match) client.match.onReady(client);
}

function handleSyncReady(client, msg) {
  if (!client.match || !client.match.active) {
    console.warn(`[sync_ready] dropped from ${client.username || "anon"}: no active match`);
    return;
  }
  client.match.onSyncReady(client, msg);
}

function handleInput(client, msg) {
  if (!client.match || !client.match.active) {
    console.warn(`[input] dropped from ${client.username || "anon"}: no active match`);
    return;
  }
  client.match.onInput(client, msg);
}

function handleSnapshot(client, msg) {
  if (!client.match || !client.match.active) {
    console.warn(`[snapshot] dropped from ${client.username || "anon"}: no active match`);
    return;
  }
  client.match.onSnapshot(client, msg);
}

function handlePing(client, msg) {
  send(client, { type: "pong", seq: msg.seq });
}

function handlePong(_client, _msg) {
  // no-op
}

function handleMatchEnd(client) {
  if (client.match) client.match.end(client);
}

function dispatchMessage(client, msg) {
  switch (msg.type) {
    case "register": handleRegister(client, msg); break;
    case "login": handleLogin(client, msg); break;
    case "map_manifest": handleMapManifest(client, msg); break;
    case "join_queue": handleJoinQueue(client); break;
    case "leave_queue": handleLeaveQueue(client); break;
    case "ready": handleReady(client); break;
    case "sync_ready": handleSyncReady(client, msg); break;
    case "input": handleInput(client, msg); break;
    case "snapshot": handleSnapshot(client, msg); break;
    case "ping": handlePing(client, msg); break;
    case "pong": handlePong(client, msg); break;
    case "match_end": handleMatchEnd(client); break;
    case "sync_begin":
    case "remote_input":
    case "lock_ready":
    case "lock_begin":
    case "input_update":
    case "frame_step":
    case "frame_hash":
    case "state_request":
    case "state_detail":
    case "state_resolve":
    case "resolve_ack":
      console.warn(`[proto] deprecated/client-only message from ${client.username || "anon"}: ${msg.type}`);
      break;
    default:
      send(client, { type: "error", message: "unknown message type" });
  }
}

function destroyClient(client) {
  if (!clients.has(client)) return;
  clients.delete(client);
  removeFromQueue(client);
  if (client.match) client.match.end(client);
  try {
    client.socket.destroy();
  } catch (_) {
    // ignore
  }
  if (client.username) {
    console.log(`[conn] disconnected ${client.username} rx=${client.rxCount || 0} tx=${client.txCount || 0}`);
  }
  broadcastQueueCount();
}

setInterval(() => {
  const now = Date.now();
  for (const client of clients) {
    if (!client.match || !client.match.active || !client.match.syncStarted) continue;
    const match = client.match;
    const role = match.roleOf(client);
    const inputIdle = match.lastRemoteInputAt[role] ? (now - match.lastRemoteInputAt[role]) : -1;
    if (inputIdle > 1500) {
      console.warn(`${match.tag()} input_idle role=${role} user=${client.username} idle_ms=${inputIdle}`);
    }
  }
}, 1000);

const server = net.createServer((socket) => {
  const client = {
    socket,
    buf: "",
    username: null,
    inQueue: false,
    match: null,
    mapManifest: defaultManifest(),
    mapManifestIndex: buildManifestIndex(defaultManifest()),
    mapManifestSerial: "",
    rxCount: 0,
    txCount: 0,
    rxBytes: 0,
    txBytes: 0,
    lastRxAt: 0,
    lastTxAt: 0,
  };
  clients.add(client);
  console.log(`[conn] new connection from ${socket.remoteAddress}:${socket.remotePort}`);

  socket.setEncoding("utf8");
  socket.setNoDelay(true);
  socket.setTimeout(120000);

  socket.on("data", (chunk) => {
    client.buf += chunk;
    let nl;
    while ((nl = client.buf.indexOf("\n")) !== -1) {
      const raw = client.buf.slice(0, nl);
      client.buf = client.buf.slice(nl + 1);
      const line = raw.trim();
      if (!line) continue;
      if (line.length > MAX_LINE_BYTES) {
        send(client, { type: "error", message: "message too long" });
        destroyClient(client);
        return;
      }

      let msg;
      try {
        msg = JSON.parse(line);
      } catch (_) {
        send(client, { type: "error", message: "invalid JSON" });
        continue;
      }
      if (!msg || typeof msg !== "object" || !msg.type) continue;

      client.rxCount = (client.rxCount || 0) + 1;
      client.rxBytes = (client.rxBytes || 0) + line.length;
      client.lastRxAt = Date.now();
      logv(`[rx] from=${client.username || "anon"} #${client.rxCount} bytes=${line.length} ${summarize(msg)}`);
      dispatchMessage(client, msg);
    }
  });

  socket.on("close", () => destroyClient(client));
  socket.on("error", () => destroyClient(client));
  socket.on("timeout", () => destroyClient(client));
});

server.listen(PORT, () => {
  console.log(`Eggnogg+ Online server listening on port ${PORT}`);
  console.log(`DB file: ${DB_FILE}`);
  console.log("Press Ctrl+C to stop.\n");
});

server.on("error", (err) => {
  console.error("Server error:", err.message);
  process.exit(1);
});
