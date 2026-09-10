"use strict";

const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const http = require("http");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");
const {
  adminLiveDashboard,
  isPrivateBindHost,
  startAdminServerFromEnv,
} = require("../online_server/admin_server");

function liveHarness() {
  const timers = new Map();
  const listeners = {};
  const status = { textContent: "" };
  let nextTimer = 0;
  const region = (name) => ({
    dataset: { live: name }, edits: [], open: false, focused: false, updates: 0,
    contains() { return this.focused; },
    querySelector() { return this.open; },
    querySelectorAll() { return this.edits; },
    replaceChildren() { this.updates++; },
  });
  const accounts = region("accounts");
  const players = region("players");
  const document = {
    hidden: false, activeElement: {},
    getElementById: () => status,
    querySelectorAll: () => [accounts, players],
    addEventListener: (name, fn) => { listeners[name] = fn; },
  };
  const harness = { document, accounts, players, status, timers, listeners,
    requests: [], response: {ok: true, status: 200, text: async () => "fixture"} };
  const context = vm.createContext({
    document, location: {href: "http://localhost/?q=audit#accounts"}, URL, AbortController,
    DOMParser: class { parseFromString() {
      return { querySelector: () => ({childNodes: []}) };
    } },
    setTimeout: (fn, delay) => { timers.set(++nextTimer, {fn, delay}); return nextTimer; },
    clearTimeout: (id) => timers.delete(id),
    fetch: async (url, options) => {
      harness.requests.push({url: String(url), options});
      if (harness.failure) throw new Error("offline");
      return harness.response;
    },
  });
  vm.runInContext(`(${adminLiveDashboard.toString()})();`, context);
  harness.tick = async () => {
    const timer = [...timers].find(([, value]) => value.delay === 2000);
    assert.ok(timer, "refresh must have exactly one scheduled retry");
    timers.delete(timer[0]);
    await timer[1].fn();
  };
  return harness;
}

test("live dashboard preserves focused, open, and edited maintenance while other regions refresh", async () => {
  const h = liveHarness();
  await h.tick();
  assert.equal(h.accounts.updates, 1);
  assert.equal(h.players.updates, 1);
  assert.equal(h.requests[0].url, "http://localhost/?q=audit&live=1");
  assert.equal(h.requests[0].options.redirect, "error");
  h.accounts.focused = true;
  await h.tick();
  h.accounts.focused = false;
  h.accounts.open = true;
  await h.tick();
  h.accounts.open = false;
  h.accounts.edits = [{value: "unsaved password", defaultValue: ""}];
  await h.tick();
  assert.equal(h.accounts.updates, 1);
  assert.equal(h.players.updates, 4);
  assert.match(h.status.textContent, /editing section paused/);
  h.accounts.edits = [];
  await h.tick();
  assert.equal(h.accounts.updates, 2);
  assert.equal(h.timers.size, 1);
});

test("live dashboard pauses hidden tabs, retries failures, and stops on expired sessions", async () => {
  const h = liveHarness();
  h.document.hidden = true;
  await h.tick();
  assert.equal(h.requests.length, 0);
  h.document.hidden = false;
  h.failure = true;
  await h.tick();
  assert.equal(h.players.updates, 0);
  assert.match(h.status.textContent, /retrying/);
  h.failure = false;
  await h.tick();
  assert.equal(h.players.updates, 1);
  h.response = {ok: false, status: 401};
  await h.tick();
  assert.match(h.status.textContent, /Refresh to reconnect/);
  assert.equal(h.timers.size, 0);
});

test("live dashboard does not overlap requests after visibility changes", async () => {
  const h = liveHarness();
  let release;
  h.response = new Promise((resolve) => { release = resolve; });
  const pending = h.tick();
  h.listeners.visibilitychange();
  assert.equal(h.requests.length, 1);
  release({ok: true, status: 200, text: async () => "fixture"});
  await pending;
  assert.equal(h.timers.size, 1);
  assert.equal(h.players.updates, 1);
});

function request(port, method, path, body = "", cookie = "", hostHeader = "") {
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
        ...(hostHeader ? { Host: hostHeader } : {}),
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
      generated_at: "2026-07-28T00:05:00.000Z",
      connections: 3,
      online: 3,
      queues: {
        casual: [],
        competitive: [{
          username: "player_3",
          elo: 980,
          joined_at: Date.now() - 3000,
          wait_ms: 3000,
          map_count: 5,
          framework_version: "1.0",
          build_id: "00c0ffee",
          rating_range: 174,
        }],
      },
      matches: [{
        id: 14,
        a: "player_1",
        b: "player_2",
        source: "queue",
        queue: "competitive",
        competitive: true,
        map_key: "vanilla:2",
        map_label: "The Fountain",
        created_at: Date.now() - 8000,
        committed: true,
        started_players: 2,
        reported_players: 0,
        rendezvous_players: 2,
        route: "direct",
        protocols: "3/3/17",
        build_id: "00c0ffee",
      }],
      challenges: [{
        id: 9,
        from: "player_3",
        to: "player_1",
        map_key: "vanilla:1",
        map_label: "The Mine",
        created_at: Date.now() - 1000,
        expires_at: Date.now() + 299000,
      }],
      rematches: [],
      friendships: [{ a: "player_1", b: "player_2", mutual: true }],
      friend_requests: [{ from: "player_2", to: "player_1" }],
      blocks: [],
      mutes: [],
      users: [{
        username: "player_1",
        online: true,
        presence: "in_match",
        opponent: "player_2",
        match_id: 14,
        queue: "",
        banned: false,
        ban_reason: "",
        elo: 1010,
        mmr: 1005,
        friends: ["player_2"],
        friend_requests: ["player_3"],
        blocked_users: [],
        muted_users: [],
        created_at: "2026-07-28T00:00:00.000Z",
        connected_at: Date.now() - 10000,
        authenticated_at: Date.now() - 9000,
        framework_version: "1.0",
        protocols: "3/3/17",
        build_id: "00c0ffee",
        map_count: 5,
        route_version: 2,
      }, {
        username: "player_2",
        online: true,
        presence: "in_match",
        opponent: "player_1",
        match_id: 14,
        queue: "",
        banned: false,
        ban_reason: "",
        elo: 990,
        mmr: 995,
        friends: ["player_1"],
        friend_requests: [],
        blocked_users: [],
        muted_users: [],
        created_at: "2026-07-28T00:01:00.000Z",
        connected_at: Date.now() - 9000,
        authenticated_at: Date.now() - 8000,
        framework_version: "1.0",
        protocols: "3/3/17",
        build_id: "00c0ffee",
        map_count: 5,
        route_version: 2,
      }, {
        username: "player_3",
        online: true,
        presence: "queue_competitive",
        opponent: "",
        match_id: 0,
        queue: "competitive",
        banned: false,
        ban_reason: "",
        elo: 980,
        mmr: 985,
        friends: [],
        friend_requests: [],
        blocked_users: [],
        muted_users: [],
        created_at: "2026-07-28T00:02:00.000Z",
        connected_at: Date.now() - 5000,
        authenticated_at: Date.now() - 4500,
        framework_version: "1.0",
        protocols: "3/3/17",
        build_id: "00c0ffee",
        map_count: 5,
        route_version: 2,
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
  const auditRecords = [];
  const server = startAdminServerFromEnv({
    ADMIN_HOST: "127.0.0.1",
    ADMIN_PORT: "0",
  }, api, {
    allowEphemeralPort: true,
    audit: record => auditRecords.push(record),
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
  assert.match(dashboard.body, /Active matches/);
  assert.match(dashboard.body, /The Fountain/);
  assert.match(dashboard.body, /direct/);
  assert.match(dashboard.body, /Friend pairs/);
  assert.match(dashboard.body, /mutual/);
  assert.match(dashboard.body, /Pending requests/);
  assert.match(dashboard.headers["content-security-policy"], /frame-ancestors 'none'/);
  assert.equal(dashboard.headers["cache-control"], "no-store");
  assert.match(dashboard.body, /src="\/live.js" defer/);
  assert.match(dashboard.headers["content-security-policy"], /script-src 'self'; connect-src 'self'/);
  const script = await request(port, "GET", "/live.js", "", cookie);
  assert.equal(script.status, 200);
  assert.match(script.headers["content-type"], /text\/javascript/);
  assert.equal(script.headers["cache-control"], "no-store");
  const expiredLive = await request(port, "GET", "/?live=1");
  assert.equal(expiredLive.status, 401);
  assert.equal(expiredLive.headers["set-cookie"], undefined);
  const oldSnapshot = api.snapshot;
  api.snapshot = () => ({ ...oldSnapshot(), online: 7 });
  const live = await request(port, "GET", "/?live=1&q=player_1", "", cookie);
  assert.equal(live.status, 200);
  assert.match(live.body, /<b>7<\/b><span>authenticated/);
  assert.match(live.body, /id="account-player_1"/);
  assert.doesNotMatch(live.body, /id="account-player_2"/);
  api.snapshot = oldSnapshot;
  const csrf = dashboard.body.match(/name="csrf" value="([0-9a-f]+)"/)[1];

  const rejected = await request(
    port, "POST", "/action",
    "csrf=nope&action=disconnect&username=player_1", cookie,
  );
  assert.equal(rejected.status, 403);
  assert.deepEqual(calls, []);
  assert.deepEqual(auditRecords, []);

  const action = await request(
    port, "POST", "/action",
    `csrf=${csrf}&action=ban&username=player_1&reason=testing`, cookie,
  );
  assert.equal(action.status, 303);
  assert.deepEqual(calls, [["ban", "player_1", true, "testing"]]);
  assert.deepEqual(auditRecords.map(r => r.status), ["requested", "succeeded"]);
  assert.equal(auditRecords[0].id, auditRecords[1].id);
  assert.equal(auditRecords[0].actor, "127.0.0.1");
  assert.ok(!JSON.stringify(auditRecords).includes("testing"));
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


test("admin rejects DNS rebinding hostnames before granting sessions", async (t) => {
  const server = startAdminServerFromEnv({ ADMIN_PORT: "0", ADMIN_HOST: "127.0.0.1" },
    { snapshot: () => ({}) }, { allowEphemeralPort: true, log() {} });
  t.after(() => new Promise(resolve => server.close(resolve)));
  await new Promise(resolve => server.once("listening", resolve));
  const response = await request(server.address().port, "GET", "/", "", "", "attacker.example");
  assert.equal(response.status, 403);
  assert.equal(response.headers["set-cookie"], undefined);
});
