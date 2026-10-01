"use strict";
const assert = require("node:assert/strict");
const test = require("node:test");
const {listenerConfig} = require("../online_server/control_tls");
test("plaintext defaults to loopback and cannot bind publicly", () => {
  assert.equal(listenerConfig({}).host, "127.0.0.1");
  assert.equal(listenerConfig({HOST: "::1"}).encrypted, false);
  for (const host of ["0.0.0.0", "192.168.0.143", "localhost", "::"])
    assert.throws(() => listenerConfig({HOST: host}), /require TLS/);
  assert.throws(() => listenerConfig({TLS_CERT_FILE: "unused"}), /both/);
  assert.throws(() => listenerConfig({TLS_KEY_FILE: "unused"}), /both/);
});
