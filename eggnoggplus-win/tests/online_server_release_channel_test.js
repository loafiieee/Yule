"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const {releaseChannel, acceptsReleaseChannel} = require("../online_server/release_channel");

test("stable compatibility and explicit beta isolation", () => {
  assert.equal(releaseChannel({}), "stable");
  assert.equal(releaseChannel({RELEASE_CHANNEL: "beta"}), "beta");
  assert.throws(() => releaseChannel({RELEASE_CHANNEL: "production"}));
  assert.equal(acceptsReleaseChannel("stable", undefined), true);
  assert.equal(acceptsReleaseChannel("stable", "stable"), true);
  assert.equal(acceptsReleaseChannel("stable", "beta"), false);
  assert.equal(acceptsReleaseChannel("beta", "beta"), true);
  for (const value of [undefined, null, "", "stable", {}, ["beta"]]) {
    assert.equal(acceptsReleaseChannel("beta", value), false);
  }
});
