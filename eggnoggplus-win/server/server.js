const fs = require("fs");
const path = require("path");
const net = require("net");
const crypto = require("crypto");

const HOST = process.env.HOST || "0.0.0.0";
const PORT = Number(process.env.PORT || 7878);
const USERS_PATH = path.join(__dirname, "users.json");

function log(...args) {
  console.log(new Date().toISOString(), ...args);
}

function safeSend(sock, msg) {
  if (!sock || sock.destroyed) {
    return false;
  }
  try {
    sock.write(JSON.stringify(msg) + "\n");
    return true;
  } catch (_err) {
    return false;
  }
}

function normalize(text) {
  return String(text || "").trim().toLowerCase();
}

function hashPassword(password) {
  return crypto.createHash("sha256").update(String(password || "")).digest("hex");
}

function ensureUsersFile() {
  if (!fs.existsSync(USERS_PATH)) {
    fs.writeFileSync(USERS_PATH, "{}\n", "utf8");
  }
}

function loadUsers() {
  ensureUsersFile();
  try {
    const raw = fs.readFileSync(USERS_PATH, "utf8");
    const parsed = JSON.parse(raw);
    if (parsed && typeof parsed === "object") {
      return parsed;
    }
  } catch (err) {
    log("failed to load users.json, starting empty:", err.message);
  }
  return {};
}

function saveUsers() {
  fs.writeFileSync(USERS_PATH, JSON.stringify(users, null, 2) + "\n", "utf8");
}

function defaultManifest() {
  const maps = [];
  for (let i = 0; i < 5; i += 1) {
    maps.push({
      key: `vanilla:${i}`,
      selector: i,
      label: `Vanilla ${i + 1}`,
    });
  }
  return maps;
}

function normalizeManifest(rawMaps) {
  const input = Array.isArray(rawMaps) ? rawMaps : defaultManifest();
  const byKey = new Map();
  for (const item of input) {
    const key = normalize(item && item.key);
    if (!key) {
      continue;
    }
    byKey.set(key, {
      key,
      selector: Number.isFinite(Number(item.selector)) ? Number(item.selector) : 0,
      label: String(item.label || key),
    });
  }
  if (byKey.size === 0) {
    for (const item of defaultManifest()) {
      byKey.set(item.key, item);
    }
  }
  return [...byKey.values()];
}

function chooseSharedMap(a, b) {
  const aMaps = normalizeManifest(a.manifest);
  const bMaps = normalizeManifest(b.manifest);
  const bByKey = new Map(bMaps.map((item) => [item.key, item]));
  const shared = [];

  for (const item of aMaps) {
    const other = bByKey.get(item.key);
    if (other) {
      shared.push({
        key: item.key,
        label: item.label || other.label || item.key,
        selectorA: item.selector,
        selectorB: other.selector,
      });
    }
  }

  if (shared.length === 0) {
    return null;
  }
  return shared[Math.floor(Math.random() * shared.length)];
}

function queueCount() {
  return queue.length;
}

function broadcastQueueCount() {
  const count = queueCount();
  for (const sock of clients) {
    if (sock.username) {
      safeSend(sock, { type: "queue_update", count });
    }
  }
}

function removeFromQueue(sock) {
  const idx = queue.indexOf(sock);
  if (idx >= 0) {
    queue.splice(idx, 1);
    broadcastQueueCount();
  }
}

class Match {
  constructor(a, b, sharedMap) {
    this.players = [a, b];
    this.map = sharedMap;
    this.ready = new Set();
    this.active = false;
    this.ended = false;

    a.match = this;
    b.match = this;
    a.matchRole = 0;
    b.matchRole = 1;

    safeSend(a, {
      type: "match_found",
      role: 0,
      map_key: sharedMap.key,
      map_sel: sharedMap.selectorA,
      map_label: sharedMap.label,
    });
    safeSend(b, {
      type: "match_found",
      role: 1,
      map_key: sharedMap.key,
      map_sel: sharedMap.selectorB,
      map_label: sharedMap.label,
    });
  }

  other(sock) {
    return this.players[0] === sock ? this.players[1] : this.players[0];
  }

  onReady(sock) {
    if (this.ended) {
      return;
    }
    this.ready.add(sock);
    if (this.ready.size < 2) {
      return;
    }
    this.active = true;
    const seed = crypto.randomBytes(4).readUInt32BE(0);
    safeSend(this.players[0], {
      type: "match_start",
      role: 0,
      authority_role: 0,
      seed: seed,
      map_key: this.map.key,
      map_sel: this.map.selectorA,
      map_label: this.map.label,
    });
    safeSend(this.players[1], {
      type: "match_start",
      role: 1,
      authority_role: 0,
      seed: seed,
      map_key: this.map.key,
      map_sel: this.map.selectorB,
      map_label: this.map.label,
    });
    log("match started", this.players[0].username, "vs", this.players[1].username, "map", this.map.key);
  }

  relay(from, msg) {
    if (this.ended || !this.active) {
      return;
    }
    const other = this.other(from);
    const type = String(msg.type || "");
    if (type === "input") {
      safeSend(other, {
        type: "remote_input",
        role: from.matchRole,
        seq: Number(msg.seq || msg.frame || 0),
        frame: Number(msg.frame || msg.seq || 0),
        cmd: Number(msg.cmd || 0),
      });
    } else if (type === "snapshot") {
      safeSend(other, {
        type: "remote_snapshot",
        seq: Number(msg.seq || 0),
        data: msg.data,
      });
    }
  }

  end(leaver, reason) {
    if (this.ended) {
      return;
    }
    this.ended = true;
    this.active = false;
    for (const sock of this.players) {
      sock.match = null;
      sock.matchRole = null;
      if (sock !== leaver) {
        safeSend(sock, { type: "match_end", reason: reason || "Opponent disconnected." });
      }
    }
  }
}

function tryMatchmake() {
  while (queue.length >= 2) {
    const a = queue.shift();
    const b = queue.shift();
    if (!a || !b || a.destroyed || b.destroyed || !a.username || !b.username) {
      continue;
    }

    const sharedMap = chooseSharedMap(a, b);
    if (!sharedMap) {
      safeSend(a, { type: "error", message: "No shared maps with opponent." });
      safeSend(b, { type: "error", message: "No shared maps with opponent." });
      continue;
    }

    new Match(a, b, sharedMap);
    broadcastQueueCount();
  }
}

function handleAuth(sock, msg, isRegister) {
  const username = normalize(msg.username);
  const password = String(msg.password || "");
  if (!username || !password) {
    safeSend(sock, { type: "auth_fail", reason: "Missing username or password." });
    return;
  }

  const existing = users[username];
  if (isRegister) {
    if (existing) {
      safeSend(sock, { type: "auth_fail", reason: "User already exists." });
      return;
    }
    users[username] = { password_hash: hashPassword(password) };
    saveUsers();
  } else {
    if (!existing || existing.password_hash !== hashPassword(password)) {
      safeSend(sock, { type: "auth_fail", reason: "Bad username or password." });
      return;
    }
  }

  sock.username = username;
  safeSend(sock, { type: "auth_ok", username });
  safeSend(sock, { type: "queue_update", count: queueCount() });
  log("auth ok", username);
}

function handleMessage(sock, msg) {
  const type = String(msg && msg.type || "");

  if (type === "register") {
    handleAuth(sock, msg, true);
    return;
  }

  if (type === "login") {
    handleAuth(sock, msg, false);
    return;
  }

  if (type === "ping") {
    safeSend(sock, { type: "pong", seq: msg.seq });
    return;
  }

  if (!sock.username) {
    safeSend(sock, { type: "error", message: "Not authenticated." });
    return;
  }

  if (type === "map_manifest") {
    sock.manifest = normalizeManifest(msg.maps);
    return;
  }

  if (type === "join_queue") {
    if (sock.match) {
      return;
    }
    removeFromQueue(sock);
    queue.push(sock);
    broadcastQueueCount();
    tryMatchmake();
    return;
  }

  if (type === "leave_queue") {
    removeFromQueue(sock);
    return;
  }

  if (type === "ready") {
    if (sock.match) {
      sock.match.onReady(sock);
    }
    return;
  }

  if (type === "input" || type === "snapshot") {
    if (sock.match) {
      sock.match.relay(sock, msg);
    }
    return;
  }

  if (type === "match_end") {
    if (sock.match) {
      sock.match.end(sock, "Opponent left the match.");
    }
    return;
  }
}

const users = loadUsers();
const clients = new Set();
const queue = [];

const server = net.createServer((sock) => {
  sock.setEncoding("utf8");
  sock.buffer = "";
  sock.username = null;
  sock.manifest = defaultManifest();
  sock.match = null;
  sock.matchRole = null;

  clients.add(sock);
  log("client connected");

  sock.on("data", (chunk) => {
    sock.buffer += chunk;
    while (true) {
      const nl = sock.buffer.indexOf("\n");
      if (nl < 0) {
        break;
      }
      const line = sock.buffer.slice(0, nl).trim();
      sock.buffer = sock.buffer.slice(nl + 1);
      if (!line) {
        continue;
      }

      let msg = null;
      try {
        msg = JSON.parse(line);
      } catch (_err) {
        safeSend(sock, { type: "error", message: "Bad JSON." });
        continue;
      }

      handleMessage(sock, msg);
    }
  });

  sock.on("close", () => {
    clients.delete(sock);
    removeFromQueue(sock);
    if (sock.match) {
      sock.match.end(sock, "Opponent disconnected.");
    }
    log("client disconnected", sock.username || "(anonymous)");
  });

  sock.on("error", () => {
    sock.destroy();
  });
});

server.listen(PORT, HOST, () => {
  log(`server listening on ${HOST}:${PORT}`);
});
