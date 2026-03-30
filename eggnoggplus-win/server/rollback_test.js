"use strict";

const { runPrototypeMatch } = require("./rollback_core");

const scenarios = [
  {
    name: "balanced-low-latency",
    seed: 1001,
    frames: 360,
    settleFrames: 40,
    client0ToServer: 2,
    serverToClient0: 2,
    client1ToServer: 2,
    serverToClient1: 2,
  },
  {
    name: "asymmetric-jitter",
    seed: 2002,
    frames: 420,
    settleFrames: 50,
    client0ToServer: 3,
    serverToClient0: 4,
    client1ToServer: 6,
    serverToClient1: 5,
    client0Jitter: 1,
    server0Jitter: 1,
    client1Jitter: 2,
    server1Jitter: 2,
  },
  {
    name: "high-latency",
    seed: 3003,
    frames: 540,
    settleFrames: 72,
    client0ToServer: 7,
    serverToClient0: 7,
    client1ToServer: 9,
    serverToClient1: 9,
    client0Jitter: 2,
    server0Jitter: 2,
    client1Jitter: 3,
    server1Jitter: 3,
  },
];

let failures = 0;
for (const scenario of scenarios) {
  const result = runPrototypeMatch(scenario);
  const summary = `${scenario.name}: converged=${result.ok} hashes=${result.hashes.server}/${result.hashes.client0}/${result.hashes.client1}`;
  console.log(summary);
  console.log(`  server rollbacks=${result.metrics.server.rollbacks} predicted=${result.metrics.server.predictedFrames} corrections=${result.metrics.server.corrections}`);
  console.log(`  client0 rollbacks=${result.metrics.clients[0].rollbacks} max=${result.metrics.clients[0].maxRollbackDistance}`);
  console.log(`  client1 rollbacks=${result.metrics.clients[1].rollbacks} max=${result.metrics.clients[1].maxRollbackDistance}`);
  if (!result.ok) failures += 1;
}

if (failures > 0) {
  console.error(`\n${failures} rollback prototype scenario(s) failed.`);
  process.exit(1);
}

console.log("\nAll rollback prototype scenarios passed.");
