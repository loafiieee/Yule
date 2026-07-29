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
    :root{color-scheme:dark;--bg:#0b1116;--panel:#121b23;--line:#263746;--text:#e7f0f4;--muted:#91a4af;--cyan:#65d4cd;--red:#ff777d;--amber:#f5c269}
    *{box-sizing:border-box} body{margin:0;background:var(--bg);color:var(--text);font:14px/1.45 system-ui,-apple-system,Segoe UI,sans-serif}
    a{color:var(--cyan)} main{width:min(1180px,calc(100% - 28px));margin:28px auto 70px}
    header{display:flex;align-items:center;justify-content:space-between;gap:16px;margin-bottom:20px}
    h1,h2{margin:0} h1{font-size:22px} h2{font-size:17px;margin-bottom:12px}
    .muted{color:var(--muted)} .panel{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:18px;margin-bottom:16px}
    .cards{display:grid;grid-template-columns:repeat(5,minmax(110px,1fr));gap:10px;margin-bottom:16px}
    .card{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:13px}.card b{display:block;font-size:22px;color:var(--cyan)}
    form{margin:0}.inline{display:flex;align-items:center;gap:7px;flex-wrap:wrap}
    input,button{border:1px solid var(--line);border-radius:5px;background:#0d151c;color:var(--text);padding:7px 9px;font:inherit}
    input:focus,button:focus{outline:2px solid var(--cyan);outline-offset:1px}button{cursor:pointer;background:#1a2a35}
    button:hover{border-color:var(--cyan)}button.danger{color:#ffd8da;border-color:#723c42}button.warn{color:#ffe8bd;border-color:#6f5a34}
    table{width:100%;border-collapse:collapse}th,td{text-align:left;vertical-align:top;border-bottom:1px solid var(--line);padding:10px 8px}
    th{color:var(--muted);font-size:12px;text-transform:uppercase;letter-spacing:.05em}.status{font-weight:600}.online{color:var(--cyan)}.banned{color:var(--red)}
    details{min-width:310px}summary{cursor:pointer;color:var(--cyan)}.actions{display:grid;gap:8px;margin-top:10px}
    .notice{border-left:3px solid var(--cyan);padding:9px 12px;background:#102128;margin-bottom:16px}.notice.error{border-color:var(--red);background:#271519}
    @media(max-width:850px){.cards{grid-template-columns:repeat(2,1fr)}table,thead,tbody,tr,th,td{display:block}thead{display:none}tr{padding:8px 0;border-bottom:1px solid var(--line)}td{border:0;padding:5px 0}details{min-width:0}}
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

function dashboardPage(snapshot, session, query) {
  const users = Array.isArray(snapshot.users) ? snapshot.users : [];
  const search = String(query.get("q") || "").trim().toLowerCase();
  const filtered = search
    ? users.filter((user) => String(user.username).toLowerCase().includes(search))
    : users;
  const notice = query.get("ok") || query.get("error") || "";
  const noticeClass = query.has("error") ? "notice error" : "notice";
  const rows = filtered.map((user) => {
    const username = String(user.username || "");
    const hidden = `<input type="hidden" name="csrf" value="${html(session.csrf)}"><input type="hidden" name="username" value="${html(username)}">`;
    const ban = user.banned
      ? `<span class="status banned">Banned</span><br><span class="muted">${html(user.ban_reason || "No reason")}</span>`
      : user.online
        ? `<span class="status online">Online</span>`
        : `<span class="status muted">Offline</span>`;
    return `<tr>
      <td><strong>${html(username)}</strong><br><span class="muted">created ${html(user.created_at || "unknown")}</span></td>
      <td>${ban}</td>
      <td>${html(user.elo)} / ${html(user.mmr)}</td>
      <td>${html(user.friends)} friends</td>
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
  return page("Dashboard", `<header>
    <div><h1>Eggnogg+ server admin</h1><div class="muted">Private LAN maintenance listener</div></div>
  </header>
  ${notice ? `<div class="${noticeClass}">${html(notice)}</div>` : ""}
  <section class="cards">
    <div class="card"><b>${html(snapshot.online || 0)}</b>online</div>
    <div class="card"><b>${html(users.length)}</b>accounts</div>
    <div class="card"><b>${html(snapshot.casual_queue || 0)}</b>casual queue</div>
    <div class="card"><b>${html(snapshot.competitive_queue || 0)}</b>ranked queue</div>
    <div class="card"><b>${html(snapshot.matches || 0)}</b>active matches</div>
  </section>
  <section class="panel">
    <div class="inline" style="justify-content:space-between;margin-bottom:12px">
      <h2>Accounts</h2>
      <form class="inline" method="get" action="/"><input name="q" value="${html(search)}" placeholder="Search username"><button type="submit">Search</button></form>
    </div>
    <div style="overflow-x:auto"><table><thead><tr><th>User</th><th>Status</th><th>Elo / MMR</th><th>Social</th><th>Actions</th></tr></thead><tbody>${rows || `<tr><td colspan="5" class="muted">No matching accounts.</td></tr>`}</tbody></table></div>
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
