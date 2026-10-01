"use strict";
const fs = require("node:fs");
const net = require("node:net");
const tls = require("node:tls");

function listenerConfig(env = process.env) {
  const certificate = env.TLS_CERT_FILE;
  const key = env.TLS_KEY_FILE;
  if (Boolean(certificate) !== Boolean(key)) {
    throw new Error("Set both TLS_CERT_FILE (full chain) and TLS_KEY_FILE");
  }
  const encrypted = Boolean(certificate && key);
  const host = env.HOST || (encrypted ? "0.0.0.0" : "127.0.0.1");
  if (!encrypted && host !== "127.0.0.1" && host !== "::1") {
    throw new Error("Public control listeners require TLS_CERT_FILE and TLS_KEY_FILE");
  }
  const options = encrypted ? {
    cert: fs.readFileSync(certificate),
    key: fs.readFileSync(key),
    minVersion: "TLSv1.2",
    handshakeTimeout: 10000,
  } : null;
  return {host, encrypted, options};
}

function createControlServer(config, onConnection) {
  if (!config.encrypted) return net.createServer(onConnection);
  const server = tls.createServer(config.options, onConnection);
  // Invalid handshakes never reach the account protocol or crash the listener.
  server.on("tlsClientError", (_error, socket) => socket.destroy());
  return server;
}
module.exports = {listenerConfig, createControlServer};
