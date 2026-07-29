"use strict";

const crypto = require("crypto");
const http = require("http");
const net = require("net");

const DEFAULT_ADMIN_PORT = 47779;
const SESSION_TTL_MS = 8 * 60 * 60 * 1000;
const SESSION_MAX = 64;
const MAX_BODY_BYTES = 64 * 1024;

function normalizeAddress(raw) {
  const value = String(raw || "");
  return value.startsWith("::ffff:") ? value.slice(7) : value;
}

function isPrivateBindHost(host) {
  const value = String(host || "").trim().toLowerCase();
  if (value === "localhost" || value === "127.0.0.1" || value === "::1") return true;
  if (net.isIPv4(value)) {
    const parts = value.split(".").map(Number);
    return parts[0] === 10 ||
      (parts[0] === 172 && parts[1] >= 16 && parts[1] <= 31) ||
      (parts[0] === 192 && parts[1] === 168) ||
      (parts[0] === 169 && parts[1] === 254);
  }
  if (net.isIPv6(value)) {
    return value.startsWith("fc") || value.startsWith("fd") ||
      value.startsWith("fe8") || value.startsWith("fe9") ||
      value.startsWith("fea") || value.startsWith("feb");
  }
  return false;
}

function html(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#39;");
}

function parseCookies(header) {
  const result = {};
  for (const pair of String(header || "").split(";")) {
    const split = pair.indexOf("=");
    if (split <= 0) continue;
    const key = pair.slice(0, split).trim();
    const value = pair.slice(split + 1).trim();
    if (key) result[key] = value;
  }
  return result;
}

function constantTimeTextEqual(left, right) {
  const a = crypto.createHash("sha256").update(String(left)).digest();
  const b = crypto.createHash("sha256").update(String(right)).digest();
  return crypto.timingSafeEqual(a, b);
}

function readRequestBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let size = 0;
    req.on("data", (chunk) => {
      size += chunk.length;
      if (size > MAX_BODY_BYTES) {
        reject(new Error("request body too large"));
        req.destroy();
        return;
      }
      chunks.push(chunk);
    });
    req.on("end", () => resolve(Buffer.concat(chunks).toString("utf8")));
    req.on("error", reject);
  });
}

function page(title, body) {
  return `<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>${html(title)} · Eggnogg+ Admin</title>
  <style>
    :root{color-scheme:dark;--bg:#0e0a0b;--bg2:#150e10;--panel:#1a1214;--panel2:#221819;--ink:#f2e9e6;--muted:#b6a4a0;--faint:#83706c;--line:rgba(255,255,255,.10);--line2:rgba(255,255,255,.20);--ember:#ff5340;--ember-hi:#ff6d54;--ember-dk:#8f2b1e;--good:#78c894;--warn:#f0a879;--danger:#ff7770}
    *{box-sizing:border-box;border-radius:0}html{scroll-behavior:smooth;scroll-padding-top:88px}body{margin:0;background:radial-gradient(900px 450px at 18% -10%,rgba(143,43,30,.18),transparent 70%),var(--bg);color:var(--ink);font:14px/1.5 "Space Grotesk","Segoe UI",system-ui,sans-serif}
    a{color:inherit;text-decoration:none}a:hover{color:var(--ember-hi)}main{width:min(1440px,calc(100% - 28px));margin:18px auto 70px}
    h1,h2,h3,p{margin-top:0}.pixel,.section-label,th,.brand,.metric span,.pill,.mini-label,summary,button{font-family:"Cascadia Code",Consolas,monospace;text-transform:uppercase;letter-spacing:.055em}
    .admin-header{position:sticky;z-index:20;top:0;display:flex;align-items:center;gap:20px;padding:14px 16px;margin-bottom:16px;border:2px solid #000;background:rgba(26,18,20,.96);box-shadow:inset 0 0 0 2px rgba(255,255,255,.045)}
    .brand{font-size:12px;font-weight:800;color:var(--ink);white-space:nowrap}.brand b{color:var(--ember)}.admin-nav{display:flex;gap:5px;overflow-x:auto}.admin-nav a,.refresh{display:inline-block;padding:7px 9px;color:var(--muted);border:1px solid var(--line);font:10px "Cascadia Code",Consolas,monospace;text-transform:uppercase;white-space:nowrap}.admin-nav a:hover,.refresh:hover{color:var(--ink);border-color:var(--ember)}
    .snapshot{margin-left:auto;color:var(--faint);font-size:11px;text-align:right;white-space:nowrap}.refresh{margin-left:0;color:var(--ember)}
    .notice{padding:11px 13px;margin-bottom:16px;border:2px solid #000;border-left:5px solid var(--ember);background:#261416}.notice.error{border-left-color:var(--danger);background:#2a1114}
    .metrics{display:grid;grid-template-columns:repeat(7,minmax(110px,1fr));gap:8px;margin-bottom:16px}.metric{min-width:0;padding:13px 14px;border:2px solid #000;background:var(--panel);box-shadow:inset 0 0 0 2px rgba(255,255,255,.04)}.metric b{display:block;color:var(--ember);font-size:25px;line-height:1.1}.metric span{display:block;margin-top:5px;color:var(--muted);font-size:9px}
    .panel{padding:18px;margin-bottom:16px;border:2px solid #000;background:var(--panel);box-shadow:inset 0 0 0 2px rgba(255,255,255,.035)}.panel-head{display:flex;align-items:flex-start;justify-content:space-between;gap:16px;padding-bottom:12px;margin-bottom:12px;border-bottom:1px solid var(--line)}.panel-head h2{margin:0;font-size:18px}.panel-head p{margin:3px 0 0;color:var(--muted);font-size:12px}.section-label{margin-bottom:5px;color:var(--ember);font-size:9px;font-weight:800}
    .split{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}.stack{display:grid;gap:16px}.subpanel{min-width:0;padding:14px;border:1px solid var(--line);background:var(--bg2)}.subpanel h3{margin:0 0 10px;font-size:14px}.subpanel .count{color:var(--ember);font-family:monospace}
    .player-grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px}.player-card{padding:12px;border:1px solid var(--line);background:var(--bg2)}.player-card header{display:flex;justify-content:space-between;gap:8px;margin-bottom:9px}.player-name{font-weight:750;color:var(--ink)}.player-card dl{display:grid;grid-template-columns:auto 1fr;gap:4px 10px;margin:0;font-size:12px}.player-card dt{color:var(--faint)}.player-card dd{min-width:0;margin:0;color:var(--muted);overflow-wrap:anywhere}
    .pill{display:inline-block;padding:3px 6px;color:var(--muted);font-size:9px;border:1px solid var(--line2);background:#100b0c;white-space:nowrap}.pill.online,.pill.playing,.pill.direct{color:var(--good);border-color:rgba(120,200,148,.45)}.pill.queue,.pill.setup,.pill.negotiating{color:var(--warn);border-color:rgba(240,168,121,.45)}.pill.relay,.pill.banned,.pill.error{color:var(--danger);border-color:rgba(255,119,112,.5)}
    .table-wrap{max-width:100%;overflow:auto}table{width:100%;border-collapse:collapse;font-size:12px}th,td{padding:9px 8px;vertical-align:top;text-align:left;border-bottom:1px solid var(--line)}th{position:sticky;top:0;color:var(--faint);font-size:9px;background:var(--panel2);white-space:nowrap}tbody tr:hover{background:rgba(255,83,64,.035)}td strong{color:var(--ink)}.muted{color:var(--muted)}.faint{color:var(--faint)}.empty{padding:20px!important;color:var(--faint);text-align:center}.relation-list{display:flex;flex-wrap:wrap;gap:4px;max-width:32rem}.relation-list a,.relation-list span{padding:2px 5px;color:var(--muted);border:1px solid var(--line);font-size:11px}.relation-list a:hover{color:var(--ember);border-color:var(--ember)}
    .inline{display:flex;align-items:center;gap:7px;flex-wrap:wrap}form{margin:0}input,button{min-height:32px;padding:6px 8px;color:var(--ink);border:1px solid var(--line2);background:#100b0c;font:inherit}input{min-width:12rem}input:focus,button:focus,a:focus{outline:2px solid var(--ember);outline-offset:2px}button{color:var(--ink);font-size:9px;cursor:pointer}button:hover{border-color:var(--ember);background:#211316}button.danger{color:#ffd5d1;border-color:var(--ember-dk)}button.warn{color:#ffd9c6;border-color:#70402c}
    details{min-width:270px}summary{width:max-content;color:var(--ember);font-size:9px;cursor:pointer}.actions{display:grid;gap:8px;padding-top:10px}.actions form{padding-top:8px;border-top:1px solid var(--line)}
    .social-summary{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px;margin-bottom:12px}.social-stat{padding:10px;border:1px solid var(--line);background:var(--bg2)}.social-stat b{display:block;color:var(--ember);font-size:19px}.social-stat span{color:var(--faint);font-size:10px;text-transform:uppercase}
    @media(max-width:1120px){.metrics{grid-template-columns:repeat(4,1fr)}.player-grid{grid-template-columns:repeat(2,1fr)}.snapshot{display:none}}
    @media(max-width:820px){main{width:min(100% - 16px,1440px);margin-top:8px}.admin-header{position:static;align-items:flex-start;flex-wrap:wrap}.admin-nav{order:3;width:100%}.split{grid-template-columns:1fr}.metrics{grid-template-columns:repeat(2,1fr)}.player-grid{grid-template-columns:1fr}.social-summary{grid-template-columns:repeat(2,1fr)}.panel{padding:12px}.panel-head{align-items:stretch;flex-direction:column}input{min-width:0;width:100%}details{min-width:220px}}
  </style>
</head>
<body><main>${body}</main></body>
</html>`;
}

function sendHtml(res, status, body) {
  res.writeHead(status, {
    "Content-Type": "text/html; charset=utf-8",
    "Cache-Control": "no-store",
    "Content-Security-Policy": "default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'",
    "Referrer-Policy": "no-referrer",
    "X-Content-Type-Options": "nosniff",
    "X-Frame-Options": "DENY",
  });
  res.end(body);
}

function redirect(res, target, cookie) {
  const headers = {
    Location: target,
    "Cache-Control": "no-store",
  };
  if (cookie) headers["Set-Cookie"] = cookie;
  res.writeHead(303, headers);
  res.end();
}

function values(value) {
  return Array.isArray(value) ? value : [];
}

function durationLabel(milliseconds) {
  const seconds = Math.max(0, Math.floor(Number(milliseconds) / 1000) || 0);
  if (seconds < 60) return `${seconds}s`;
  const minutes = Math.floor(seconds / 60);
  if (minutes < 60) return `${minutes}m ${seconds % 60}s`;
  const hours = Math.floor(minutes / 60);
  return `${hours}h ${minutes % 60}m`;
}

function ageLabel(timestamp) {
  const value = Number(timestamp);
  return value > 0 ? durationLabel(Date.now() - value) : "unknown";
}

function playerLink(username) {
  const value = String(username || "");
  return value
    ? `<a class="player-name" href="#account-${html(value)}">${html(value)}</a>`
    : `<span class="faint">unknown</span>`;
}

function relationList(usernames, empty = "none") {
  const items = values(usernames);
  return items.length
    ? `<div class="relation-list">${items.map(playerLink).join("")}</div>`
    : `<span class="faint">${html(empty)}</span>`;
}

function presencePill(user) {
  if (user.banned) return `<span class="pill banned">banned</span>`;
  const presence = String(user.presence || (user.online ? "online" : "offline"));
  const labels = {
    in_match: ["playing", "playing"],
    match_setup: ["match setup", "setup"],
    queue_competitive: ["ranked queue", "queue"],
    queue_casual: ["casual queue", "queue"],
    online: ["online", "online"],
    offline: ["offline", ""],
  };
  const [label, className] = labels[presence] || [presence, ""];
  return `<span class="pill ${className}">${html(label)}</span>`;
}

function emptyRow(columns, message) {
  return `<tr><td class="empty" colspan="${columns}">${html(message)}</td></tr>`;
}

function dashboardPage(snapshot, session, query) {
  const users = values(snapshot.users);
  const matches = values(snapshot.matches);
  const challenges = values(snapshot.challenges);
  const rematches = values(snapshot.rematches);
  const friendships = values(snapshot.friendships);
  const friendRequests = values(snapshot.friend_requests);
  const blocks = values(snapshot.blocks);
  const mutes = values(snapshot.mutes);
  const casualQueue = values(snapshot.queues && snapshot.queues.casual);
  const competitiveQueue = values(snapshot.queues && snapshot.queues.competitive);
  const userIndex = new Map(users.map((user) => [String(user.username), user]));
  const search = String(query.get("q") || "").trim().toLowerCase();
  const filtered = search
    ? users.filter((user) => String(user.username).toLowerCase().includes(search))
    : users;
  const notice = query.get("ok") || query.get("error") || "";
  const noticeClass = query.has("error") ? "notice error" : "notice";
  const onlineUsers = users.filter((user) => user.online);

  const playerCards = onlineUsers.map((user) => {
    const context = user.opponent
      ? `vs ${html(user.opponent)} · match #${html(user.match_id)}`
      : user.queue
        ? `${html(user.queue)} queue`
        : "online hub";
    return `<article class="player-card">
      <header>${playerLink(user.username)}${presencePill(user)}</header>
      <dl>
        <dt>Activity</dt><dd>${context}</dd>
        <dt>Rating</dt><dd>${html(user.elo)} Elo / ${html(user.mmr)} MMR</dd>
        <dt>Client</dt><dd>${html(user.framework_version || "unknown")} · protocols ${html(user.protocols || "unknown")}</dd>
        <dt>Build</dt><dd>${html(user.build_id || "unknown")} · ${html(user.map_count || 0)} maps</dd>
        <dt>Connected</dt><dd>${html(ageLabel(user.connected_at))}</dd>
      </dl>
    </article>`;
  }).join("");

  const matchRows = matches.map((match) => {
    const state = match.committed ? "playing" : "setup";
    const mode = match.competitive
      ? "ranked"
      : (match.source === "challenge" ? "challenge" : (match.queue || match.source || "casual"));
    return `<tr>
      <td><strong>#${html(match.id)}</strong><br><span class="pill ${state}">${state}</span></td>
      <td>${playerLink(match.a)} <span class="faint">vs</span> ${playerLink(match.b)}</td>
      <td><strong>${html(match.map_label || match.map_key || "unknown")}</strong><br><span class="faint">${html(match.map_key || "")}</span></td>
      <td>${html(mode)}<br><span class="faint">${html(match.source || "")}</span></td>
      <td><span class="pill ${html(match.route)}">${html(match.route)}</span><br><span class="faint">${html(match.rendezvous_players || 0)}/2 rendezvous · ${html(match.started_players || 0)}/2 started · ${html(match.reported_players || 0)}/2 reports</span></td>
      <td>${html(ageLabel(match.created_at))}<br><span class="faint">protocols ${html(match.protocols || "unknown")} · build ${html(match.build_id || "unknown")}</span></td>
    </tr>`;
  }).join("");

  const queueRows = (queueName, entries) => entries.map((entry) => `<tr>
    <td><span class="pill queue">${html(queueName)}</span></td>
    <td>${playerLink(entry.username)}</td>
    <td>${html(entry.elo)}${entry.rating_range ? ` <span class="faint">±${html(entry.rating_range)}</span>` : ""}</td>
    <td>${html(durationLabel(entry.wait_ms))}</td>
    <td>${html(entry.map_count || 0)} maps</td>
    <td>${html(entry.framework_version || "unknown")}<br><span class="faint">${html(entry.build_id || "unknown")}</span></td>
  </tr>`).join("");
  const allQueueRows = queueRows("casual", casualQueue) +
    queueRows("ranked", competitiveQueue);

  const challengeRows = challenges.map((challenge) => `<tr>
    <td>#${html(challenge.id)}</td>
    <td>${playerLink(challenge.from)} <span class="faint">→</span> ${playerLink(challenge.to)}</td>
    <td>${html(challenge.map_label || challenge.map_key || "unknown")}<br><span class="faint">${html(challenge.map_key || "")}</span></td>
    <td>${html(ageLabel(challenge.created_at))}</td>
    <td>${html(durationLabel(Number(challenge.expires_at) - Date.now()))}</td>
  </tr>`).join("");

  const rematchRows = rematches.map((rematch) => `<tr>
    <td>#${html(rematch.match_id)}</td>
    <td>${playerLink(rematch.a)} <span class="faint">vs</span> ${playerLink(rematch.b)}</td>
    <td>${html(rematch.map_label || rematch.map_key || "unknown")}</td>
    <td>${relationList(rematch.accepted_by, "waiting for both")}</td>
    <td>${html(durationLabel(Number(rematch.expires_at) - Date.now()))}</td>
  </tr>`).join("");

  const friendshipRows = friendships.map((friendship) => {
    const left = userIndex.get(String(friendship.a)) || {};
    const right = userIndex.get(String(friendship.b)) || {};
    return `<tr>
      <td>${playerLink(friendship.a)} ${presencePill(left)}</td>
      <td>${playerLink(friendship.b)} ${presencePill(right)}</td>
      <td>${friendship.mutual
        ? `<span class="pill online">mutual</span>`
        : `<span class="pill error">one-sided data</span>`}</td>
    </tr>`;
  }).join("");
  const requestRows = friendRequests.map((request) => `<tr>
    <td>${playerLink(request.from)}</td>
    <td>${playerLink(request.to)}</td>
  </tr>`).join("");
  const blockRows = blocks.map((block) => `<tr><td>${playerLink(block.from)}</td><td>${playerLink(block.to)}</td></tr>`).join("");
  const muteRows = mutes.map((mute) => `<tr><td>${playerLink(mute.from)}</td><td>${playerLink(mute.to)}</td></tr>`).join("");

  const accountRows = filtered.map((user) => {
    const username = String(user.username || "");
    const hidden = `<input type="hidden" name="csrf" value="${html(session.csrf)}"><input type="hidden" name="username" value="${html(username)}">`;
    const activity = user.opponent
      ? `<br><span class="muted">vs ${html(user.opponent)} · match #${html(user.match_id)}</span>`
      : user.queue
        ? `<br><span class="muted">${html(user.queue)} queue</span>`
        : "";
    const banReason = user.banned
      ? `<br><span class="muted">${html(user.ban_reason || "No reason")}</span>`
      : "";
    const clientDetails = user.online
      ? `${html(user.framework_version || "unknown")} · ${html(user.protocols || "unknown")}<br>
        <span class="faint">build ${html(user.build_id || "unknown")} · route v${html(user.route_version || 0)} · ${html(user.map_count || 0)} maps · connected ${html(ageLabel(user.connected_at))}</span>`
      : `<span class="faint">not connected</span>`;
    return `<tr id="account-${html(username)}">
      <td>${playerLink(username)}<br><span class="faint">created ${html(user.created_at || "unknown")}</span></td>
      <td>${presencePill(user)}${activity}${banReason}</td>
      <td>${html(user.elo)} / ${html(user.mmr)}</td>
      <td>
        <span class="mini-label">Friends</span>${relationList(user.friends)}
        ${values(user.friend_requests).length ? `<br><span class="mini-label">Requests</span>${relationList(user.friend_requests)}` : ""}
        ${values(user.blocked_users).length ? `<br><span class="mini-label">Blocked</span>${relationList(user.blocked_users)}` : ""}
        ${values(user.muted_users).length ? `<br><span class="mini-label">Muted</span>${relationList(user.muted_users)}` : ""}
      </td>
      <td>${clientDetails}</td>
      <td><details><summary>Maintenance</summary><div class="actions">
        <form class="inline" method="post" action="/action">${hidden}<input type="hidden" name="action" value="reset_password"><input name="password" type="password" minlength="4" maxlength="256" required placeholder="New password"><button class="warn" type="submit">Reset password</button></form>
        ${user.banned
          ? `<form class="inline" method="post" action="/action">${hidden}<input type="hidden" name="action" value="unban"><button type="submit">Unban</button></form>`
          : `<form class="inline" method="post" action="/action">${hidden}<input type="hidden" name="action" value="ban"><input name="reason" maxlength="160" placeholder="Ban reason"><button class="danger" type="submit">Ban</button></form>`}
        <form class="inline" method="post" action="/action">${hidden}<input type="hidden" name="action" value="disconnect"><button type="submit">Disconnect</button></form>
        <form class="inline" method="post" action="/action">${hidden}<input type="hidden" name="action" value="reset_rating"><button class="warn" type="submit">Reset rating</button></form>
      </div></details></td>
    </tr>`;
  }).join("");

  return page("Dashboard", `<header class="admin-header">
    <div class="brand"><b>YULE</b> // SERVER</div>
    <nav class="admin-nav" aria-label="Dashboard sections">
      <a href="#players">Players</a><a href="#matches">Matches</a><a href="#queues">Queues</a><a href="#social">Social</a><a href="#accounts">Accounts</a>
    </nav>
    <div class="snapshot">Private LAN snapshot<br>${html(snapshot.generated_at || "just now")}</div>
    <a class="refresh" href="/">Refresh</a>
  </header>
  ${notice ? `<div class="${noticeClass}">${html(notice)}</div>` : ""}
  <section class="metrics" aria-label="Server totals">
    <div class="metric"><b>${html(snapshot.online || 0)}</b><span>authenticated</span></div>
    <div class="metric"><b>${html(snapshot.connections || snapshot.online || 0)}</b><span>connections</span></div>
    <div class="metric"><b>${html(users.length)}</b><span>accounts</span></div>
    <div class="metric"><b>${html(matches.length)}</b><span>active matches</span></div>
    <div class="metric"><b>${html(casualQueue.length)}</b><span>casual queue</span></div>
    <div class="metric"><b>${html(competitiveQueue.length)}</b><span>ranked queue</span></div>
    <div class="metric"><b>${html(challenges.length)}</b><span>challenges</span></div>
  </section>

  <section class="panel" id="players">
    <div class="panel-head"><div><div class="section-label">Live presence</div><h2>Connected players</h2><p>Current activity, opponent, client compatibility, and connection age.</p></div></div>
    <div class="player-grid">${playerCards || `<div class="empty">Nobody is authenticated right now.</div>`}</div>
  </section>

  <section class="panel" id="matches">
    <div class="panel-head"><div><div class="section-label">Gameplay</div><h2>Active matches</h2><p>Authoritative server phase and transport negotiation. No authentication tokens or raw endpoints are shown.</p></div></div>
    <div class="table-wrap"><table><thead><tr><th>Match</th><th>Players</th><th>Map</th><th>Mode</th><th>Network</th><th>Runtime</th></tr></thead><tbody>${matchRows || emptyRow(6, "No active matches.")}</tbody></table></div>
  </section>

  <div class="split" id="queues">
    <section class="panel">
      <div class="panel-head"><div><div class="section-label">Matchmaking</div><h2>Queues</h2><p>Order, rating window, wait, content pool, and client build.</p></div></div>
      <div class="table-wrap"><table><thead><tr><th>Queue</th><th>Player</th><th>Elo / range</th><th>Wait</th><th>Content</th><th>Client</th></tr></thead><tbody>${allQueueRows || emptyRow(6, "Both queues are empty.")}</tbody></table></div>
    </section>
    <div class="stack">
      <section class="panel">
        <div class="panel-head"><div><div class="section-label">Direct invites</div><h2>Challenges</h2></div></div>
        <div class="table-wrap"><table><thead><tr><th>ID</th><th>Players</th><th>Map</th><th>Age</th><th>Expires</th></tr></thead><tbody>${challengeRows || emptyRow(5, "No pending challenges.")}</tbody></table></div>
      </section>
      <section class="panel">
        <div class="panel-head"><div><div class="section-label">Post-match</div><h2>Rematch windows</h2></div></div>
        <div class="table-wrap"><table><thead><tr><th>Match</th><th>Players</th><th>Map</th><th>Accepted</th><th>Expires</th></tr></thead><tbody>${rematchRows || emptyRow(5, "No open rematch windows.")}</tbody></table></div>
      </section>
    </div>
  </div>

  <section class="panel" id="social">
    <div class="panel-head"><div><div class="section-label">Social graph</div><h2>Player relationships</h2><p>Friendships are deduplicated into pairs; one-sided rows flag inconsistent stored data.</p></div></div>
    <div class="social-summary">
      <div class="social-stat"><b>${html(friendships.length)}</b><span>friendships</span></div>
      <div class="social-stat"><b>${html(friendRequests.length)}</b><span>pending requests</span></div>
      <div class="social-stat"><b>${html(blocks.length)}</b><span>blocks</span></div>
      <div class="social-stat"><b>${html(mutes.length)}</b><span>mutes</span></div>
    </div>
    <div class="split">
      <div class="subpanel"><h3>Friend pairs <span class="count">${html(friendships.length)}</span></h3><div class="table-wrap"><table><thead><tr><th>Player</th><th>Friend</th><th>Integrity</th></tr></thead><tbody>${friendshipRows || emptyRow(3, "No friendships stored.")}</tbody></table></div></div>
      <div class="stack">
        <div class="subpanel"><h3>Pending requests <span class="count">${html(friendRequests.length)}</span></h3><div class="table-wrap"><table><thead><tr><th>From</th><th>To</th></tr></thead><tbody>${requestRows || emptyRow(2, "No pending requests.")}</tbody></table></div></div>
        <div class="split">
          <div class="subpanel"><h3>Blocks <span class="count">${html(blocks.length)}</span></h3><div class="table-wrap"><table><thead><tr><th>Player</th><th>Blocked</th></tr></thead><tbody>${blockRows || emptyRow(2, "No blocks.")}</tbody></table></div></div>
          <div class="subpanel"><h3>Mutes <span class="count">${html(mutes.length)}</span></h3><div class="table-wrap"><table><thead><tr><th>Player</th><th>Muted</th></tr></thead><tbody>${muteRows || emptyRow(2, "No mutes.")}</tbody></table></div></div>
        </div>
      </div>
    </div>
  </section>

  <section class="panel" id="accounts">
    <div class="panel-head">
      <div><div class="section-label">Accounts</div><h2>Directory and maintenance</h2><p>Ratings, complete social lists, live build details, bans, and existing maintenance actions.</p></div>
      <form class="inline" method="get" action="/"><input name="q" value="${html(search)}" placeholder="Search username"><button type="submit">Search</button>${search ? `<a class="refresh" href="/#accounts">Clear</a>` : ""}</form>
    </div>
    <div class="table-wrap"><table><thead><tr><th>User</th><th>Status</th><th>Elo / MMR</th><th>Relationships</th><th>Client</th><th>Actions</th></tr></thead><tbody>${accountRows || emptyRow(6, "No matching accounts.")}</tbody></table></div>
  </section>`);
}

function startAdminServerFromEnv(env, api, options = {}) {
  const log = options.log || ((line) => console.log(`[admin] ${line}`));
  const host = String(env.ADMIN_HOST || "127.0.0.1").trim();
  const port = Number.parseInt(env.ADMIN_PORT || `${DEFAULT_ADMIN_PORT}`, 10);
  const allowWildcard = env.ADMIN_ALLOW_WILDCARD === "1";
  const secureCookie = env.ADMIN_COOKIE_SECURE === "1";

  if (env.ADMIN_ENABLED === "0") {
    log("disabled by ADMIN_ENABLED=0");
    return null;
  }
  if (!Number.isInteger(port) ||
      port < (options.allowEphemeralPort ? 0 : 1) ||
      port > 65535) {
    log("disabled (ADMIN_PORT must be 1..65535)");
    return null;
  }
  if (!isPrivateBindHost(host) && !allowWildcard) {
    log(`disabled (ADMIN_HOST ${host} is not a private literal address; set the machine's LAN IP)`);
    return null;
  }
  if (!api || typeof api.snapshot !== "function") {
    log("disabled (maintenance API is unavailable)");
    return null;
  }

  const sessions = new Map();

  function activeSession(req) {
    const token = parseCookies(req.headers.cookie).yule_admin;
    const session = token ? sessions.get(token) : null;
    const ip = normalizeAddress(req.socket.remoteAddress);
    if (!session || session.expires_at <= Date.now() || session.ip !== ip) {
      if (token) sessions.delete(token);
      return null;
    }
    session.expires_at = Date.now() + SESSION_TTL_MS;
    return session;
  }

  function newSessionCookie(ip) {
    for (const [oldToken, oldSession] of sessions) {
      if (oldSession.expires_at <= Date.now()) sessions.delete(oldToken);
    }
    while (sessions.size >= SESSION_MAX) {
      sessions.delete(sessions.keys().next().value);
    }
    const token = crypto.randomBytes(32).toString("hex");
    sessions.set(token, {
      csrf: crypto.randomBytes(24).toString("hex"),
      ip,
      expires_at: Date.now() + SESSION_TTL_MS,
    });
    return `yule_admin=${token}; Path=/; HttpOnly; SameSite=Strict${secureCookie ? "; Secure" : ""}; Max-Age=${SESSION_TTL_MS / 1000}`;
  }

  const server = http.createServer(async (req, res) => {
    const ip = normalizeAddress(req.socket.remoteAddress);
    const session = activeSession(req);

    try {
      const url = new URL(req.url || "/", "http://admin.invalid");
      if (!isPrivateBindHost(ip)) {
        return sendHtml(res, 403, page("Forbidden", "<div class=\"notice error\">Admin access is limited to private LAN addresses.</div>"));
      }
      if (req.method === "GET" && url.pathname === "/") {
        if (!session) return redirect(res, "/", newSessionCookie(ip));
        return sendHtml(res, 200, dashboardPage(api.snapshot(), session, url.searchParams));
      }

      if (!session) {
        return redirect(res, "/");
      }

      if (req.method === "POST" && url.pathname === "/action") {
        const form = new URLSearchParams(await readRequestBody(req));
        if (!constantTimeTextEqual(form.get("csrf") || "", session.csrf)) {
          return sendHtml(res, 403, page("Forbidden", "<div class=\"notice error\">Invalid request token.</div>"));
        }
        const action = String(form.get("action") || "");
        const username = String(form.get("username") || "");
        let message = "";
        if (action === "reset_password") {
          message = api.resetPassword(username, String(form.get("password") || ""));
        } else if (action === "ban") {
          message = api.setBan(username, true, String(form.get("reason") || ""));
        } else if (action === "unban") {
          message = api.setBan(username, false, "");
        } else if (action === "disconnect") {
          message = api.disconnect(username);
        } else if (action === "reset_rating") {
          message = api.resetRating(username);
        } else {
          throw new Error("unknown maintenance action");
        }
        log(`${ip} ${action} ${username}`);
        return redirect(res, `/?ok=${encodeURIComponent(message || "Maintenance action completed.")}`);
      }

      sendHtml(res, 404, page("Not found", "<div class=\"notice error\">Not found.</div>"));
    } catch (err) {
      log(`request failed from ${ip}: ${err.message}`);
      redirect(res, `/?error=${encodeURIComponent(err.message || "Maintenance action failed.")}`);
    }
  });

  server.on("error", (err) => {
    log(`listener error: ${err.message}`);
  });
  server.listen(port, host, () => {
    const address = server.address();
    log(`listening on http://${address.address}:${address.port}`);
  });
  return server;
}

module.exports = {
  isPrivateBindHost,
  startAdminServerFromEnv,
};

if (require.main === module) {
  console.error("admin_server.js is not a standalone process.");
  console.error("It is started by server.js; configure ADMIN_HOST/ADMIN_PORT on the eggnogg service and restart that service.");
  process.exitCode = 1;
}
