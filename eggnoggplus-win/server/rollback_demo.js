"use strict";

const { runPrototypeMatch } = require("./rollback_core");

const result = runPrototypeMatch({
  seed: 424242,
  frames: 480,
  settleFrames: 48,
  client0ToServer: 3,
  serverToClient0: 3,
  client1ToServer: 5,
  serverToClient1: 5,
  client0Jitter: 1,
  server0Jitter: 1,
  client1Jitter: 2,
  server1Jitter: 2,
});

console.log("Rollback prototype demo");
console.log("=======================");
console.log(`Converged: ${result.ok}`);
console.log(`Frames:    ${result.frames}`);
console.log(`Hashes:    server=${result.hashes.server} c0=${result.hashes.client0} c1=${result.hashes.client1}`);
console.log("Server metrics:", JSON.stringify(result.metrics.server, null, 2));
console.log("Client 0 metrics:", JSON.stringify(result.metrics.clients[0], null, 2));
console.log("Client 1 metrics:", JSON.stringify(result.metrics.clients[1], null, 2));
console.log("Final scores:", result.serverState.players.map((p) => p.score).join(" - "));

process.exit(result.ok ? 0 : 1);
