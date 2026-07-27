"use strict";

const assert = require("node:assert/strict");
const test = require("node:test");
const {
  normalizePublicBaseUrl,
  publicLfgLink,
  parseRedirectTarget,
  redirectResponse,
  startRedirectServer,
} = require("../online_server/lfg_redirect");

test("public base and generated links are strict HTTPS routes", () => {
  assert.equal(
    normalizePublicBaseUrl("https://example.test/yule/"),
    "https://example.test/yule",
  );
  assert.equal(
    publicLfgLink("https://example.test/yule", "challenge", "player_1"),
    "https://example.test/yule/challenge/player_1",
  );
  assert.equal(
    publicLfgLink("https://example.test/yule", "queue", "competitive"),
    "https://example.test/yule/queue/competitive",
  );
  for (const invalid of [
    "http://example.test/yule",
    "https://user@example.test/yule",
    "https://example.test/",
    "https://example.test/yule?token=private",
    "https://example.test/other",
    "https://example.test/yule/../yule",
    "https://example.test//yule",
    "https://example.test/yu%6ce",
    "https://example.test\\yule",
  ]) {
    assert.throws(() => normalizePublicBaseUrl(invalid));
  }
  assert.throws(() => publicLfgLink(
    "https://example.test/yule", "challenge", "Bad@Name",
  ));
  assert.throws(() => publicLfgLink(
    "https://example.test/yule", "queue", "ranked",
  ));
});

test("redirect parser exposes only completed canonical yule intents", () => {
  const accepted = new Map([
    ["/yule/hub", "yule://hub"],
    ["/yule/requests", "yule://requests"],
    ["/yule/queue/casual", "yule://queue/casual"],
    ["/yule/queue/competitive", "yule://queue/competitive"],
    ["/yule/challenge/player_1", "yule://challenge/player_1"],
  ]);
  for (const [target, expected] of accepted) {
    assert.equal(parseRedirectTarget(target), expected);
  }
  for (const rejected of [
    "",
    "/",
    "/yule",
    "/yule/queue/ranked",
    "/yule/challenge/BadName",
    "/yule/challenge/player_1?token=nope",
    "/yule/challenge/player%5f1",
    "/yule/match/123",
    "/yule/join/127.0.0.1",
    "/yule/queue/casual/extra",
    "/yule/hub/",
    "/yule//hub",
    "/yule/other/../hub",
    "/yule/./hub",
    "//attacker.test/yule/hub",
    "/yule\\hub",
  ]) {
    assert.equal(parseRedirectTarget(rejected), null, rejected);
  }
});

function fakeResponse() {
  return {
    status: 0,
    headers: {},
    body: "",
    writeHead(status, headers) {
      this.status = status;
      this.headers = headers;
    },
    end(body) {
      this.body = body || "";
    },
  };
}

test("HTTP handoff is no-store and method bounded", () => {
  const ok = fakeResponse();
  redirectResponse({ method: "GET", url: "/yule/queue/casual" }, ok);
  assert.equal(ok.status, 302);
  assert.equal(ok.headers.Location, "yule://queue/casual");
  assert.equal(ok.headers["Cache-Control"], "no-store");
  assert.equal(ok.headers["Referrer-Policy"], "no-referrer");
  assert.match(ok.body, /yule:\/\/queue\/casual/);

  const unknown = fakeResponse();
  redirectResponse({ method: "GET", url: "/yule/match/1" }, unknown);
  assert.equal(unknown.status, 404);

  const post = fakeResponse();
  redirectResponse({ method: "POST", url: "/yule/hub" }, post);
  assert.equal(post.status, 405);
  assert.equal(post.headers.Allow, "GET, HEAD");
});

test("redirect server refuses an accidental public bind", () => {
  assert.throws(() => startRedirectServer({
    port: 49123,
    host: "0.0.0.0",
  }), /loopback/);
});
