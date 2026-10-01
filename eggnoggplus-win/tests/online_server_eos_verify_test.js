"use strict";

const assert = require("node:assert/strict");
const test = require("node:test");
const {parseVerifyOutput} = require("../online_server/eos_verify");
const fs = require("node:fs");
const vm = require("node:vm");

test("EOS SDK stdout diagnostics cannot hide or duplicate a PUID proof", () => {
  const account = "a".repeat(32);
  const puid = "b".repeat(32);
  const line = `${account} ${puid}\n`;
  assert.deepEqual(parseVerifyOutput(`Shutdown handler: initialize.\n${line}Shutdown handler: cleanup.\n`, puid),
                   {accountId: account, puid});
  assert.equal(parseVerifyOutput(line + line, puid), null);
  assert.equal(parseVerifyOutput(line, "c".repeat(32)), null);
  assert.equal(parseVerifyOutput(`${account} ${puid} trailing\n`, puid), null);
});

async function refreshProof({claimed, verified, proofAccount = "account", match = 42}) {
  const client = {username: "player", match_id: match, verified_eos_puid: verified};
  const replies = [];
  let verificationCalls = 0;
  const context = vm.createContext({
    eosVerificationsInFlight: 0, eosVerifierConfigured: () => true,
    ensureUserShape: () => ({account_id: "account"}),
    connectedClient: () => client,
    verifyConnectIdToken: async () => {
      verificationCalls++;
      return {accountId: proofAccount, puid: claimed};
    },
    send: (_client, reply) => replies.push(reply),
    sendError: (_client, message) => replies.push({type: "error", message}),
    tryMatchmaking: () => {},
  });
  const source = fs.readFileSync(require.resolve("../online_server/server.js"), "utf8");
  const start = source.indexOf("async function handleEosPuidProof(");
  const end = source.indexOf("\nfunction handleMapManifest(", start);
  assert.ok(start > 0 && end > start);
  vm.runInContext(source.slice(start, end), context);
  await context.handleEosPuidProof(client, {puid: claimed, id_token: "proof"});
  return {client, replies, verificationCalls, context};
}
test("active matches can refresh the same verified EOS identity", async () => {
  const result = await refreshProof({claimed: "puid-a", verified: "puid-a"});
  assert.equal(result.replies[0].type, "eos_puid_verified");
  assert.equal(result.verificationCalls, 1);
  assert.equal(result.context.eosVerificationsInFlight, 0);
  assert.equal(result.client.eos_verify_pending, false);
});
test("active matches cannot replace the EOS identity or use another Yule account", async () => {
  const substituted = await refreshProof({claimed: "puid-b", verified: "puid-a"});
  assert.equal(substituted.replies[0].type, "error");
  assert.equal(substituted.verificationCalls, 0);
  assert.equal(substituted.client.verified_eos_puid, "puid-a");
  const wrongAccount = await refreshProof({claimed: "puid-a", verified: "puid-a", proofAccount: "other"});
  assert.equal(wrongAccount.replies[0].type, "error");
  assert.equal(wrongAccount.client.verified_eos_puid, "puid-a");
});
