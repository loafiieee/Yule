"use strict";

const assert = require("node:assert/strict");
const http = require("node:http");
const test = require("node:test");
const {createEosUserInfoService, startEosUserInfoServerFromEnv, USERINFO_PATH} = require("../online_server/eos_userinfo");

test("EOS UserInfo resolves short lived bearer tokens to stable account subjects", async () => {
  let now = 1000;
  const service = createEosUserInfoService(() => now);
  const server = http.createServer(service.handle);
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const endpoint = `http://127.0.0.1:${server.address().port}${USERINFO_PATH}`;
  try {
    const accountId = "a".repeat(32);
    const {token, expiresAt} = service.issue(accountId, "yule_player");
    assert.equal(expiresAt, 181000);
    assert.equal(token.length, 43);
    const ok = await fetch(endpoint, {headers: {Authorization: `Bearer ${token}`}});
    assert.equal(ok.status, 200);
    assert.deepEqual(await ok.json(), {sub: accountId, nickname: "yule_player"});
    assert.equal(ok.headers.get("cache-control"), "no-store");
    assert.equal((await fetch(endpoint)).status, 401);
    assert.equal((await fetch(endpoint, {headers: {Authorization: `Bearer ${"b".repeat(43)}`}})).status, 401);
    assert.equal((await fetch(endpoint + "/other", {headers: {Authorization: `Bearer ${token}`}})).status, 404);
    now = expiresAt;
    assert.equal((await fetch(endpoint, {headers: {Authorization: `Bearer ${token}`}})).status, 401);
  } finally {
    await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
  }
});

test("EOS UserInfo refuses malformed account identities", () => {
  const service = createEosUserInfoService();
  assert.throws(() => service.issue("player", "yule_player"), /Invalid Yule account identity/);
  assert.throws(() => service.issue("a".repeat(32), "invalid name"), /Invalid Yule account identity/);
});

test("EOS UserInfo listener cannot bind to a public interface", () => {
  assert.throws(() => startEosUserInfoServerFromEnv({
    EOS_USERINFO_PORT: "47781", EOS_USERINFO_HOST: "0.0.0.0",
  }), /loopback/);
  assert.equal(startEosUserInfoServerFromEnv({}), null);
});
