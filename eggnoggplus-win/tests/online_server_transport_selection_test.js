"use strict";

const assert = require("node:assert/strict");
const test = require("node:test");
const {
  NATIVE_UDP, EOS_P2P, sanitizeTransportCaps, selectTransport,
} = require("../online_server/transport_selection");

test("EOS wins only when both peers advertise it and have server-verified PUIDs", () => {
  const a = {transport_caps: NATIVE_UDP | EOS_P2P, verified_eos_puid: "a".repeat(32)};
  const b = {transport_caps: NATIVE_UDP | EOS_P2P, verified_eos_puid: "b".repeat(32)};
  assert.equal(selectTransport(a, b), "eos_p2p");
  assert.equal(selectTransport(a, {...b, verified_eos_puid: ""}), "native_udp");
  assert.equal(selectTransport(a, {...b, transport_caps: NATIVE_UDP}), "native_udp");
  assert.equal(selectTransport({...a, transport_caps: EOS_P2P}, {...b, transport_caps: NATIVE_UDP}), null);
  assert.equal(selectTransport({...a, transport_caps: EOS_P2P}, {...b, verified_eos_puid: ""}), null);
});

test("transport support is independent of the gameplay protocol version", () => {
  assert.equal(sanitizeTransportCaps(undefined), NATIVE_UDP);
  assert.equal(sanitizeTransportCaps(EOS_P2P), EOS_P2P);
  assert.equal(sanitizeTransportCaps(NATIVE_UDP | EOS_P2P), NATIVE_UDP | EOS_P2P);
  assert.equal(sanitizeTransportCaps(99, 0), 0);
});
