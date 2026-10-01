"use strict";

const NATIVE_UDP = 1;
const EOS_P2P = 2;
const ALL_CAPS = NATIVE_UDP | EOS_P2P;

function sanitizeTransportCaps(value, fallback = NATIVE_UDP) {
  return Number.isInteger(value) && value >= 0 && value <= ALL_CAPS
    ? value : fallback;
}

function verifiedEosPuid(client) {
  return typeof client?.verified_eos_puid === "string" &&
    /^[0-9a-f]{32}$/.test(client.verified_eos_puid);
}

/* A client claim alone cannot authorize EOS matchmaking. The PUID must have
 * been bound to this authenticated Yule account by the server first. */
function selectTransport(a, b) {
  const common = a.transport_caps & b.transport_caps;
  if ((common & EOS_P2P) && verifiedEosPuid(a) && verifiedEosPuid(b)) {
    return "eos_p2p";
  }
  if (common & NATIVE_UDP) return "native_udp";
  return null;
}

module.exports = {NATIVE_UDP, EOS_P2P, sanitizeTransportCaps, selectTransport};
