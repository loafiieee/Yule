"use strict";

const assert = require("node:assert/strict");
const test = require("node:test");
const {
  validateConfig,
  buildQueuePayload,
  DiscordLfgBot,
} = require("../online_server/discord_lfg_bot");

const CONFIG = {
  enabled: true,
  token: "abcDEF0123456789.token_value",
  channelId: "123456789012345678",
  publicBaseUrl: "https://example.test/yule",
  postDelayMs: 0,
};

test("configuration fails closed", () => {
  assert.deepEqual(validateConfig({ enabled: false }), { enabled: false });
  assert.throws(() => validateConfig({ ...CONFIG, token: "short" }), /token/);
  assert.throws(() => validateConfig({ ...CONFIG, channelId: "channel" }), /channel/);
  assert.throws(() => validateConfig({
    ...CONFIG,
    publicBaseUrl: "http://example.test/yule",
  }), /HTTPS/);
});

test("queue payload contains only public identity, queue, and HTTPS handoffs", () => {
  const payload = buildQueuePayload(
    "player_1", "competitive", CONFIG.publicBaseUrl,
  );
  const serialized = JSON.stringify(payload);
  assert.equal(payload.allowed_mentions.parse.length, 0);
  assert.match(serialized, /player_1/);
  assert.match(serialized, /Competitive/);
  assert.match(serialized, /https:\/\/example\.test\/yule\/challenge\/player_1/);
  assert.match(serialized, /https:\/\/example\.test\/yule\/queue\/competitive/);
  assert.equal(payload.components[0].components[0].label, "Join Competitive Queue");
  assert.equal(
    payload.components[0].components[0].url,
    "https://example.test/yule/queue/competitive",
  );
  assert.equal(payload.components[0].components[1].label, "Challenge player_1");
  assert.equal(
    payload.components[0].components[1].url,
    "https://example.test/yule/challenge/player_1",
  );
  for (const forbidden of [
    "match_id", "endpoint", "p2p", "auth_token", "rendezvous",
    "password", "server_secret", "peer_host", "peer_port",
  ]) {
    assert.equal(serialized.includes(forbidden), false, forbidden);
  }
});

function fakeDiscord() {
  const calls = [];
  let nextId = 100000000000000000n;
  return {
    calls,
    request: async (call) => {
      calls.push(call);
      if (call.method === "POST") {
        nextId += 1n;
        return {
          status: 201,
          headers: {},
          body: JSON.stringify({ id: String(nextId) }),
        };
      }
      return { status: 204, headers: {}, body: "" };
    },
  };
}

test("join posts once and leave edits the exact owned message", async () => {
  const discord = fakeDiscord();
  const logs = [];
  const bot = new DiscordLfgBot(CONFIG, {
    request: discord.request,
    delay: async () => {},
    log: (line) => logs.push(line),
  });
  assert.equal(bot.queueJoined("player_1", "casual"), true);
  assert.equal(bot.queueJoined("player_1", "casual"), true);
  await bot.flush();
  assert.equal(discord.calls.length, 1);
  assert.equal(discord.calls[0].method, "POST");
  assert.deepEqual(Object.keys(discord.calls[0]).sort(),
    ["body", "method", "path", "token"]);
  assert.equal(JSON.stringify(discord.calls[0].body).includes(CONFIG.token), false);

  assert.equal(bot.queueLeft("player_1"), true);
  await bot.flush();
  assert.equal(discord.calls.length, 2);
  assert.equal(discord.calls[1].method, "PATCH");
  assert.match(discord.calls[1].path, /\/messages\/100000000000000001$/);
  assert.deepEqual(discord.calls[1].body.components, []);
  assert.match(discord.calls[1].body.embeds[0].title, /Queue closed/);
  assert.equal(logs.some((line) => line.includes(CONFIG.token)), false);
});

test("matched post is edited now and deleted after one-day retention", async () => {
  const discord = fakeDiscord();
  const timers = new Map();
  let nextTimer = 1;
  const bot = new DiscordLfgBot(CONFIG, {
    request: discord.request,
    setTimer: (callback, ms) => {
      const id = nextTimer++;
      timers.set(id, { callback, ms });
      return id;
    },
    clearTimer: (id) => timers.delete(id),
  });
  bot.queueJoined("matched_user", "competitive");
  await bot.flush();
  bot.queueLeft("matched_user", "matched");
  await bot.flush();

  assert.deepEqual(discord.calls.map((call) => call.method), ["POST", "PATCH"]);
  assert.match(discord.calls[1].body.embeds[0].title, /Match found/);
  assert.equal(timers.size, 1);
  assert.equal([...timers.values()][0].ms, 24 * 60 * 60 * 1000);

  [...timers.values()][0].callback();
  await bot.flush();
  assert.deepEqual(discord.calls.map((call) => call.method),
    ["POST", "PATCH", "DELETE"]);
  assert.equal(discord.calls[2].body, undefined);
  assert.equal(discord.calls[2].path, discord.calls[1].path);
});

test("leave racing create edits the late message", async () => {
  let resolveCreate;
  const calls = [];
  const create = new Promise((resolve) => { resolveCreate = resolve; });
  const bot = new DiscordLfgBot(CONFIG, {
    request: async (call) => {
      calls.push(call);
      if (call.method === "POST") return create;
      return { status: 204, headers: {}, body: "" };
    },
    delay: async () => {},
  });
  bot.queueJoined("racer", "competitive");
  bot.queueLeft("racer");
  resolveCreate({
    status: 201,
    headers: {},
    body: JSON.stringify({ id: "222222222222222222" }),
  });
  await bot.flush();
  assert.deepEqual(calls.map((call) => call.method), ["POST", "PATCH"]);
  assert.deepEqual(calls[1].body.components, []);
});

test("a sub-two-second queue stay never creates a Discord message", async () => {
  const discord = fakeDiscord();
  const timers = new Map();
  let nextTimer = 1;
  const bot = new DiscordLfgBot({ ...CONFIG, postDelayMs: 2000 }, {
    request: discord.request,
    setTimer: (callback, ms) => {
      const id = nextTimer++;
      timers.set(id, { callback, ms });
      return id;
    },
    clearTimer: (id) => timers.delete(id),
  });
  assert.equal(bot.queueJoined("quick_match", "casual"), true);
  assert.equal(timers.size, 1);
  assert.equal([...timers.values()][0].ms, 2000);
  assert.equal(bot.queueLeft("quick_match", "matched"), true);
  assert.equal(timers.size, 0);
  await bot.flush();
  assert.equal(discord.calls.length, 0);
});

test("the delayed post is created once only after its timer fires", async () => {
  const discord = fakeDiscord();
  let timer = null;
  const bot = new DiscordLfgBot({ ...CONFIG, postDelayMs: 2000 }, {
    request: discord.request,
    setTimer: (callback, ms) => {
      timer = { callback, ms };
      return 1;
    },
    clearTimer: () => { timer = null; },
  });
  assert.equal(bot.queueJoined("patient_user", "competitive"), true);
  assert.equal(discord.calls.length, 0);
  assert.equal(timer.ms, 2000);
  timer.callback();
  await bot.flush();
  assert.deepEqual(discord.calls.map((call) => call.method), ["POST"]);
});

test("429 response uses server retry_after instead of hardcoded route rates", async () => {
  const calls = [];
  const delays = [];
  const bot = new DiscordLfgBot(CONFIG, {
    request: async (call) => {
      calls.push(call);
      if (calls.length === 1) {
        return {
          status: 429,
          headers: {},
          body: JSON.stringify({ retry_after: 0.25, global: false }),
        };
      }
      return {
        status: 201,
        headers: {},
        body: JSON.stringify({ id: "333333333333333333" }),
      };
    },
    delay: async (ms) => { delays.push(ms); },
  });
  bot.queueJoined("retry_user", "casual");
  await bot.flush();
  assert.equal(calls.length, 2);
  assert.deepEqual(delays, [250]);
});

test("invalid lifecycle values never reach Discord", async () => {
  const discord = fakeDiscord();
  const bot = new DiscordLfgBot(CONFIG, { request: discord.request });
  assert.equal(bot.queueJoined("Bad@Name", "casual"), false);
  assert.equal(bot.queueJoined("valid_name", "private"), false);
  assert.equal(bot.queueLeft("Bad@Name"), false);
  await bot.flush();
  assert.equal(discord.calls.length, 0);
});

test("orderly close retires active owned posts", async () => {
  const discord = fakeDiscord();
  const bot = new DiscordLfgBot(CONFIG, { request: discord.request });
  bot.queueJoined("closing_user", "casual");
  await bot.flush();
  await bot.close({ retire: true });
  assert.deepEqual(discord.calls.map((call) => call.method), ["POST", "PATCH"]);
  assert.match(discord.calls[1].body.embeds[0].title, /Queue closed/);
  assert.equal(bot.enabled, false);
});

test("pending create work is bounded without dropping owned cleanup", async () => {
  const resolvers = [];
  let nextId = 400000000000000000n;
  const logs = [];
  const bot = new DiscordLfgBot(CONFIG, {
    pendingCreateMax: 2,
    request: (call) => {
      if (call.method !== "POST") {
        return Promise.resolve({ status: 204, headers: {}, body: "" });
      }
      return new Promise((resolve) => {
        resolvers.push(() => {
          nextId += 1n;
          resolve({
            status: 201,
            headers: {},
            body: JSON.stringify({ id: String(nextId) }),
          });
        });
      });
    },
    log: (line) => logs.push(line),
  });
  assert.equal(bot.queueJoined("bounded_1", "casual"), true);
  assert.equal(bot.queueJoined("bounded_2", "casual"), true);
  assert.equal(bot.queueJoined("bounded_3", "casual"), false);
  assert.equal(bot.records.has("bounded_3"), false);
  assert.equal(logs.some((line) => line.includes("pending-request limit")), true);

  while (bot.pendingTasks > 0) {
    if (resolvers.length) resolvers.shift()();
    await new Promise((resolve) => setImmediate(resolve));
  }
  await bot.flush();
  await bot.close({ retire: true });
  assert.equal(bot.pendingTasks, 0);
});
