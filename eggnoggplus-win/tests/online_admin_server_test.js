"use strict";

const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const http = require("http");
const path = require("node:path");
const test = require("node:test");
const {
  isPrivateBindHost,
  startAdminServerFromEnv,
} = require("../online_server/admin_server");

function request(port, method, path, body = "", cookie = "") {
  return new Promise((resolve, reject) => {
    const req = http.request({
      host: "127.0.0.1",
      port,
      method,
      path,
      headers: {
        "Content-Type": "application/x-www-form-urlencoded",
        "Content-Length": Buffer.byteLength(body),
        ...(cookie ? { Cookie: cookie } : {}),
      },
    }, (res) => {
      const chunks = [];
      res.on("data", (chunk) => chunks.push(chunk));
      res.on("end", () => resolve({
        status: res.statusCode,
        headers: res.headers,
        body: Buffer.concat(chunks).toString("utf8"),
      }));
    });
    req.on("error", reject);
    req.end(body);
  });
}

test("admin bind validation accepts only loopback/private literals by default", () => {
  for (const host of [
    "127.0.0.1", "::1", "10.0.0.4", "172.16.4.2",
    "172.31.255.1", "192.168.1.20", "fd00::20",
  ]) {
    assert.equal(isPrivateBindHost(host), true, host);
  }
  for (const host of [
    "0.0.0.0", "::", "8.8.8.8", "172.32.0.1",
    "example.com", "203.0.113.8",
  ]) {
    assert.equal(isPrivateBindHost(host), false, host);
  }
});

test("admin UI grants a private client session, requires CSRF, and dispatches actions", async (t) => {
  const calls = [];
  const api = {
    snapshot: () => ({
      online: 1,
      casual_queue: 0,
      competitive_queue: 1,
      matches: 1,
      users: [{
        username: "player_1",
        online: true,
        banned: false,
        ban_reason: "",
        elo: 1010,
        mmr: 1005,
        friends: 2,
        created_at: "2026-07-28T00:00:00.000Z",
      }],
    }),
    resetPassword: (username, password) => {
      calls.push(["reset_password", username, password]);
      return "password reset";
    },
    setBan: (username, banned, reason) => {
      calls.push(["ban", username, banned, reason]);
      return "ban updated";
    },
    disconnect: (username) => {
      calls.push(["disconnect", username]);
      return "disconnected";
    },
    resetRating: (username) => {
      calls.push(["reset_rating", username]);
      return "rating reset";
    },
  };
  const server = startAdminServerFromEnv({
    ADMIN_HOST: "127.0.0.1",
    ADMIN_PORT: "0",
  }, api, {
    allowEphemeralPort: true,
    log: () => {},
  });
  assert.ok(server);
  t.after(() => new Promise((resolve) => server.close(resolve)));
  if (!server.listening) {
    await new Promise((resolve) => server.once("listening", resolve));
  }
  const port = server.address().port;

  const sessionStart = await request(port, "GET", "/");
  assert.equal(sessionStart.status, 303);
  const cookie = sessionStart.headers["set-cookie"][0].split(";")[0];
  assert.match(cookie, /^yule_admin=[0-9a-f]{64}$/);

  const dashboard = await request(port, "GET", "/", "", cookie);
  assert.equal(dashboard.status, 200);
  assert.match(dashboard.body, /player_1/);
  assert.match(dashboard.body, /1010/);
  assert.match(dashboard.headers["content-security-policy"], /frame-ancestors 'none'/);
  assert.equal(dashboard.headers["cache-control"], "no-store");
  const csrf = dashboard.body.match(/name="csrf" value="([0-9a-f]+)"/)[1];

  const rejected = await request(
    port, "POST", "/action",
    "csrf=nope&action=disconnect&username=player_1", cookie,
  );
  assert.equal(rejected.status, 403);
  assert.deepEqual(calls, []);

  const action = await request(
    port, "POST", "/action",
    `csrf=${csrf}&action=ban&username=player_1&reason=testing`, cookie,
  );
  assert.equal(action.status, 303);
  assert.deepEqual(calls, [["ban", "player_1", true, "testing"]]);
});

test("admin UI can be disabled explicitly", () => {
  const api = { snapshot: () => ({ users: [] }) };
  assert.equal(startAdminServerFromEnv({
    ADMIN_ENABLED: "0",
  }, api, { log: () => {} }), null);
});

test("direct execution explains that the admin listener belongs to server.js", () => {
  const result = spawnSync(process.execPath, [
    path.join(__dirname, "..", "online_server", "admin_server.js"),
  ], { encoding: "utf8" });
  assert.equal(result.status, 1);
  assert.match(result.stderr, /not a standalone process/);
  assert.match(result.stderr, /configure ADMIN_HOST\/ADMIN_PORT/);
});
