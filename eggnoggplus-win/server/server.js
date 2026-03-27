// server.js  –  Eggnogg+ Online server
// Pure Node.js, no dependencies.  Run with:  node server.js
//
// Protocol:  newline-delimited JSON over raw TCP.
//
// Client → Server messages:
//   { type:"register", username, password }
//   { type:"login",    username, password }
//   { type:"join_queue" }
//   { type:"leave_queue" }
//   { type:"ready" }                         – client ready to start match
//   { type:"input",   tick, cmd }            – gameplay input (forwarded)
//   { type:"match_end" }                     – client reports match finished
//
// Server → Client messages:
//   { type:"auth_ok",      username }
//   { type:"auth_fail",    reason }
//   { type:"queue_update", count }
//   { type:"match_found",  role, map_sel }   – role 0=P1, 1=P2
//   { type:"match_start" }                   – both players ready, go
//   { type:"remote_input", tick, cmd }       – forwarded from opponent
//   { type:"match_end" }
//   { type:"error",        message }

"use strict";

const net    = require("net");
const crypto = require("crypto");
const fs     = require("fs");
const path   = require("path");

// ── Config ──────────────────────────────────────────────────────────────────

const PORT        = parseInt(process.env.PORT  || "7878", 10);
const DB_FILE     = process.env.DB   || path.join(__dirname, "users.json");
const VANILLA_MAPS = 5;   // map selector indices 0–4

// ── Persistence (flat JSON file) ─────────────────────────────────────────────
// Schema: { "username": { hash: "sha256hex", salt: "hex" }, ... }

function loadDB() {
    try {
        if (fs.existsSync(DB_FILE))
            return JSON.parse(fs.readFileSync(DB_FILE, "utf8"));
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
    return crypto.createHash("sha256")
                 .update(salt + ":" + password)
                 .digest("hex");
}

let userDB = loadDB();

// ── Client registry ──────────────────────────────────────────────────────────

// client object fields:
//   socket, buf, username (null if not authed),
//   inQueue (bool), match (ref to Match or null)

const clients = new Set();

function send(client, obj) {
    try {
        client.socket.write(JSON.stringify(obj) + "\n");
    } catch (_) {}
}

function broadcastQueueCount() {
    let count = 0;
    for (const c of clients) if (c.inQueue) count++;
    const msg = JSON.stringify({ type: "queue_update", count }) + "\n";
    for (const c of clients) {
        if (c.username) {
            try { c.socket.write(msg); } catch (_) {}
        }
    }
}

// ── Match ────────────────────────────────────────────────────────────────────

class Match {
    constructor(p1, p2) {
        this.players  = [p1, p2];             // [P1 client, P2 client]
        this.ready    = [false, false];
        this.map_sel  = Math.floor(Math.random() * VANILLA_MAPS);
        this.active   = false;
    }

    roleOf(client) {
        return this.players.indexOf(client);  // 0 or 1
    }

    opponent(client) {
        const r = this.roleOf(client);
        return r >= 0 ? this.players[1 - r] : null;
    }

    notifyFound() {
        for (let r = 0; r < 2; r++) {
            send(this.players[r], {
                type:    "match_found",
                role:    r,
                map_sel: this.map_sel,
            });
        }
        const p1 = this.players[0].username;
        const p2 = this.players[1].username;
        console.log(`[match] found: ${p1} vs ${p2}  map=${this.map_sel}`);
    }

    onReady(client) {
        const r = this.roleOf(client);
        if (r < 0) return;
        this.ready[r] = true;
        if (this.ready[0] && this.ready[1]) {
            this.active = true;
            for (const p of this.players) send(p, { type: "match_start" });
            console.log(`[match] started: ${this.players[0].username} vs ${this.players[1].username}`);
        }
    }

    forwardInput(from, tick, cmd) {
        const opp = this.opponent(from);
        if (opp) send(opp, { type: "remote_input", tick, cmd });
    }

    end(initiator) {
        this.active = false;
        for (const p of this.players) {
            p.match = null;
            p.inQueue = false;
            if (p !== initiator) send(p, { type: "match_end" });
        }
        const name = initiator ? initiator.username : "?";
        console.log(`[match] ended (by ${name})`);
        broadcastQueueCount();
    }
}

// ── Queue ────────────────────────────────────────────────────────────────────

const queue = [];   // ordered array of authed clients waiting for a match

function tryMatchmake() {
    while (queue.length >= 2) {
        const p1 = queue.shift();
        const p2 = queue.shift();
        p1.inQueue = false;
        p2.inQueue = false;
        const m = new Match(p1, p2);
        p1.match = m;
        p2.match = m;
        m.notifyFound();
    }
    broadcastQueueCount();
}

function removeFromQueue(client) {
    const idx = queue.indexOf(client);
    if (idx >= 0) { queue.splice(idx, 1); client.inQueue = false; }
}

// ── Message handlers ─────────────────────────────────────────────────────────

function handleRegister(client, msg) {
    const u = (msg.username || "").trim().toLowerCase();
    const p = msg.password  || "";
    if (!u || !p)          return send(client, { type:"auth_fail", reason:"missing fields" });
    if (u.length > 24)     return send(client, { type:"auth_fail", reason:"username too long" });
    if (!/^[a-z0-9_]+$/.test(u))
                           return send(client, { type:"auth_fail", reason:"username: a-z 0-9 _ only" });
    if (userDB[u])         return send(client, { type:"auth_fail", reason:"username taken" });

    const salt = crypto.randomBytes(16).toString("hex");
    userDB[u]  = { hash: hashPassword(p, salt), salt };
    saveDB(userDB);
    client.username = u;
    send(client, { type:"auth_ok", username: u });
    console.log(`[auth] registered: ${u}`);
    broadcastQueueCount();
}

function handleLogin(client, msg) {
    const u = (msg.username || "").trim().toLowerCase();
    const p = msg.password  || "";
    const rec = userDB[u];
    if (!rec) return send(client, { type:"auth_fail", reason:"unknown user" });

    const expected = hashPassword(p, rec.salt);
    if (expected !== rec.hash)
        return send(client, { type:"auth_fail", reason:"wrong password" });

    // Check if already connected from another socket
    for (const c of clients) {
        if (c !== client && c.username === u) {
            send(c, { type:"error", message:"Logged in from another location" });
            destroyClient(c);
        }
    }

    client.username = u;
    send(client, { type:"auth_ok", username: u });
    console.log(`[auth] login: ${u}`);
    broadcastQueueCount();
}

function handleJoinQueue(client) {
    if (!client.username) return send(client, { type:"error", message:"not logged in" });
    if (client.match)     return send(client, { type:"error", message:"already in a match" });
    if (client.inQueue)   return;
    client.inQueue = true;
    queue.push(client);
    console.log(`[queue] ${client.username} joined (queue=${queue.length})`);
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

function handleInput(client, msg) {
    if (client.match && client.match.active) {
        const tick = parseInt(msg.tick, 10) || 0;
        const cmd  = parseInt(msg.cmd,  10) || 0;
        // Basic plausibility: cmd is a 7-bit mask
        if (cmd < 0 || cmd > 127) return;
        client.match.forwardInput(client, tick, cmd);
    }
}

function handleMatchEnd(client) {
    if (client.match) client.match.end(client);
}

function dispatchMessage(client, msg) {
    switch (msg.type) {
        case "register":  handleRegister(client, msg);  break;
        case "login":     handleLogin(client, msg);     break;
        case "join_queue":  handleJoinQueue(client);    break;
        case "leave_queue": handleLeaveQueue(client);   break;
        case "ready":     handleReady(client);          break;
        case "input":     handleInput(client, msg);     break;
        case "match_end": handleMatchEnd(client);       break;
        default:
            send(client, { type:"error", message:"unknown message type" });
    }
}

// ── Connection lifecycle ──────────────────────────────────────────────────────

function destroyClient(client) {
    if (!clients.has(client)) return;
    clients.delete(client);
    removeFromQueue(client);
    if (client.match) client.match.end(client);
    try { client.socket.destroy(); } catch (_) {}
    if (client.username)
        console.log(`[conn] disconnected: ${client.username}`);
    broadcastQueueCount();
}

const server = net.createServer((socket) => {
    const client = {
        socket,
        buf:      "",
        username: null,
        inQueue:  false,
        match:    null,
    };
    clients.add(client);
    const addr = socket.remoteAddress + ":" + socket.remotePort;
    console.log(`[conn] new connection from ${addr}`);

    socket.setEncoding("utf8");
    socket.setNoDelay(true);

    socket.on("data", (chunk) => {
        client.buf += chunk;
        let nl;
        while ((nl = client.buf.indexOf("\n")) !== -1) {
            const line = client.buf.slice(0, nl).trim();
            client.buf = client.buf.slice(nl + 1);
            if (!line) continue;
            // Hard cap to prevent memory abuse
            if (line.length > 2048) {
                send(client, { type:"error", message:"message too long" });
                destroyClient(client);
                return;
            }
            let msg;
            try { msg = JSON.parse(line); } catch (_) {
                send(client, { type:"error", message:"invalid JSON" });
                continue;
            }
            if (typeof msg !== "object" || !msg.type) continue;
            dispatchMessage(client, msg);
        }
    });

    socket.on("close", ()  => destroyClient(client));
    socket.on("error", ()  => destroyClient(client));
    socket.on("timeout", () => destroyClient(client));
    socket.setTimeout(120_000); // 2-min idle timeout
});

server.listen(PORT, () => {
    console.log(`Eggnogg+ Online server listening on port ${PORT}`);
    console.log(`DB file: ${DB_FILE}`);
    console.log("Press Ctrl+C to stop.\n");
});

server.on("error", (e) => {
    console.error("Server error:", e.message);
    process.exit(1);
});
