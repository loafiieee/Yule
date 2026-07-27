"use strict";

const http = require("http");

const USERNAME_RE = /^[a-z0-9_]{1,24}$/;
const PORT_RE = /^[0-9]{1,5}$/;

function normalizePublicBaseUrl(raw) {
  const text = String(raw || "");
  if (!text || text.length > 400 || /[\x00-\x20\\%]/.test(text)) {
    throw new Error("LFG public base URL is missing or contains forbidden characters");
  }
  let parsed;
  try {
    parsed = new URL(text);
  } catch (_) {
    throw new Error("LFG public base URL is invalid");
  }
  if (parsed.protocol !== "https:" || !parsed.hostname ||
      parsed.username || parsed.password || parsed.search || parsed.hash ||
      parsed.port && !PORT_RE.test(parsed.port)) {
    throw new Error("LFG public base URL must be an HTTPS URL without credentials, query, or fragment");
  }
  const schemeEnd = text.indexOf("://") + 3;
  const rawPathOffset = text.indexOf("/", schemeEnd);
  const rawPath = rawPathOffset >= 0 ? text.slice(rawPathOffset) : "";
  if (rawPath !== "/yule" && rawPath !== "/yule/") {
    throw new Error("LFG public base URL must use the strict /yule redirect path");
  }
  parsed.pathname = "/yule";
  return parsed.toString().replace(/\/$/, "");
}

function publicLfgLink(baseUrl, kind, value = "") {
  const base = normalizePublicBaseUrl(baseUrl);
  if (kind === "hub") return `${base}/hub`;
  if (kind === "requests") return `${base}/requests`;
  if (kind === "queue" && (value === "casual" || value === "competitive")) {
    return `${base}/queue/${value}`;
  }
  if (kind === "challenge" && USERNAME_RE.test(value)) {
    return `${base}/challenge/${value}`;
  }
  throw new Error("invalid public LFG route");
}

function parseRedirectTarget(rawTarget) {
  const target = String(rawTarget || "");
  if (!target || target.length > 160 || /[\x00-\x20\\%?#]/.test(target)) return null;
  if (target === "/yule/hub") return "yule://hub";
  if (target === "/yule/requests") return "yule://requests";
  let match = /^\/yule\/queue\/(casual|competitive)$/.exec(target);
  if (match) return `yule://queue/${match[1]}`;
  match = /^\/yule\/challenge\/([a-z0-9_]{1,24})$/.exec(target);
  if (match && USERNAME_RE.test(match[1])) return `yule://challenge/${match[1]}`;
  return null;
}

function redirectResponse(req, res) {
  const method = String(req.method || "").toUpperCase();
  if (method !== "GET" && method !== "HEAD") {
    res.writeHead(405, {
      "Allow": "GET, HEAD",
      "Cache-Control": "no-store",
      "Content-Type": "text/plain; charset=utf-8",
    });
    res.end(method === "HEAD" ? undefined : "Method not allowed.\n");
    return;
  }
  const route = parseRedirectTarget(req.url);
  if (!route) {
    res.writeHead(404, {
      "Cache-Control": "no-store",
      "Content-Type": "text/plain; charset=utf-8",
      "Referrer-Policy": "no-referrer",
      "X-Content-Type-Options": "nosniff",
    });
    res.end(method === "HEAD" ? undefined : "Unknown Yule link.\n");
    return;
  }
  const escaped = route.replace(/&/g, "&amp;").replace(/"/g, "&quot;");
  const body = [
    "<!doctype html><meta charset=\"utf-8\">",
    "<meta name=\"referrer\" content=\"no-referrer\">",
    "<title>Open Yule</title>",
    `<p><a rel="noreferrer" href="${escaped}">Open this link in Yule</a></p>`,
  ].join("");
  res.writeHead(302, {
    "Location": route,
    "Cache-Control": "no-store",
    "Content-Type": "text/html; charset=utf-8",
    "Content-Security-Policy": "default-src 'none'; style-src 'unsafe-inline'",
    "Referrer-Policy": "no-referrer",
    "X-Content-Type-Options": "nosniff",
    "Content-Length": Buffer.byteLength(body),
  });
  res.end(method === "HEAD" ? undefined : body);
}

function startRedirectServer(options = {}) {
  const port = Number.parseInt(String(options.port || "0"), 10);
  const host = String(options.host || "127.0.0.1");
  if (!Number.isInteger(port) || port < 1 || port > 65535) {
    throw new Error("LFG redirect port must be 1..65535");
  }
  if (host !== "127.0.0.1" && host !== "::1" && options.allowRemote !== true) {
    throw new Error("LFG redirect must bind loopback unless allowRemote is explicit");
  }
  const server = http.createServer(redirectResponse);
  server.listen(port, host);
  return server;
}

function startRedirectServerFromEnv(env = process.env) {
  if (!env.LFG_REDIRECT_PORT) return null;
  return startRedirectServer({
    port: env.LFG_REDIRECT_PORT,
    host: env.LFG_REDIRECT_HOST || "127.0.0.1",
    allowRemote: env.LFG_REDIRECT_ALLOW_REMOTE === "1",
  });
}

module.exports = {
  normalizePublicBaseUrl,
  publicLfgLink,
  parseRedirectTarget,
  redirectResponse,
  startRedirectServer,
  startRedirectServerFromEnv,
};
