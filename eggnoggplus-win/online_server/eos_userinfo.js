"use strict";

const crypto = require("node:crypto");
const http = require("node:http");

const TOKEN_TTL_MS = 3 * 60 * 1000;
const MAX_PENDING_TOKENS = 4096;
const USERINFO_PATH = "/eos/userinfo";

function createEosUserInfoService(clock = Date.now) {
  const tokens = new Map();

  function prune() {
    const cutoff = clock();
    for (const [token, entry] of tokens) {
      if (entry.expiresAt <= cutoff) tokens.delete(token);
    }
  }

  function issue(accountId, displayName) {
    if (!/^[0-9a-f]{32}$/.test(accountId) ||
        !/^[a-z0-9_]{1,24}$/.test(displayName)) {
      throw new Error("Invalid Yule account identity for EOS Connect");
    }
    prune();
    if (tokens.size >= MAX_PENDING_TOKENS) {
      throw new Error("EOS Connect token queue is full");
    }
    const token = crypto.randomBytes(32).toString("base64url");
    const expiresAt = clock() + TOKEN_TTL_MS;
    tokens.set(token, {accountId, displayName, expiresAt});
    return {token, expiresAt};
  }

  function handle(req, res) {
    res.setHeader("Cache-Control", "no-store");
    res.setHeader("Content-Type", "application/json; charset=utf-8");
    if (req.url !== USERINFO_PATH || req.method !== "GET") {
      res.writeHead(404).end("{}");
      return;
    }
    const header = req.headers.authorization;
    const match = typeof header === "string" && /^Bearer ([A-Za-z0-9_-]{43})$/.exec(header);
    if (!match) {
      res.writeHead(401, {"WWW-Authenticate": "Bearer"}).end("{}");
      return;
    }
    const entry = tokens.get(match[1]);
    if (!entry || entry.expiresAt <= clock()) {
      if (entry) tokens.delete(match[1]);
      res.writeHead(401, {"WWW-Authenticate": "Bearer"}).end("{}");
      return;
    }
    res.writeHead(200).end(JSON.stringify({sub: entry.accountId, nickname: entry.displayName}));
  }

  return {issue, handle};
}

function startEosUserInfoServerFromEnv(env = process.env) {
  if (!env.EOS_USERINFO_PORT) return null;
  const port = Number(env.EOS_USERINFO_PORT);
  if (!Number.isInteger(port) || port < 1 || port > 65535) {
    throw new Error("EOS_USERINFO_PORT must be a TCP port");
  }
  const host = env.EOS_USERINFO_HOST || "127.0.0.1";
  if (host !== "127.0.0.1" && host !== "::1") {
    throw new Error("EOS_USERINFO_HOST must be a loopback address");
  }
  const service = createEosUserInfoService();
  const server = http.createServer(service.handle);
  let listening = false;
  server.requestTimeout = 5000;
  server.headersTimeout = 5000;
  server.on("error", (err) => {
    listening = false;
    console.error(`[eos] UserInfo listener: ${err.message}`);
  });
  server.on("close", () => { listening = false; });
  server.listen(port, host, () => {
    listening = true;
    console.log(`[eos] UserInfo listening on ${host}:${port}`);
  });
  return {
    issue(accountId, displayName) {
      if (!listening) throw new Error("EOS Connect UserInfo listener is unavailable");
      return service.issue(accountId, displayName);
    },
    server,
  };
}

module.exports = {createEosUserInfoService, startEosUserInfoServerFromEnv, USERINFO_PATH};
