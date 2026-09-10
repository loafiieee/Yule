"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const {createAuditWriter, auditedAction} = require("../online_server/admin_audit");

test("audit preserves append history and records correlated intent/outcome without secrets", t => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "yule-audit-"));
  t.after(() => fs.rmSync(dir, {recursive:true, force:true}));
  const file = path.join(dir, "audit.jsonl");
  fs.writeFileSync(file, '{"old":true}\n');
  const write = createAuditWriter(file);
  assert.equal(auditedAction(write, "reset_password", "Player", "127.0.0.1", () => "secret-result"), "secret-result");
  assert.throws(() => auditedAction(write, "ban", "player", "127.0.0.1", () => {throw new Error("secret-error");}));
  const raw = fs.readFileSync(file, "utf8");
  const rows = raw.trim().split("\n").map(JSON.parse);
  assert.deepEqual(rows.map(r => r.status), [undefined,"requested","succeeded","requested","failed"]);
  assert.equal(rows[1].id, rows[2].id);
  assert.notEqual(rows[1].id, rows[3].id);
  assert.equal(rows[1].username, "player");
  assert.ok(!raw.includes("secret"));
});

test("audit failure blocks mutation and completion failure is explicitly distinguished", () => {
  let ran = false;
  assert.throws(() => auditedAction(() => {throw new Error("disk");}, "ban", "player", "local", () => {ran = true;}));
  assert.equal(ran, false);
  const records = [];
  assert.throws(() => auditedAction(row => {
    if (row.status === "succeeded") throw new Error("disk");
    records.push(row);
  }, "unban", "player", "local", () => {ran = true;}), /Maintenance completed/);
  assert.equal(ran, true);
  assert.equal(records.length, 1);
  assert.throws(() => auditedAction(() => {}, "unknown", "p", "local", () => {throw new Error("should not run");}), /unknown maintenance/);
});


test("an interrupted final record cannot absorb later audit entries", t => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "yule-audit-tail-"));
  t.after(() => fs.rmSync(dir, {recursive:true, force:true}));
  const file = path.join(dir,"audit.jsonl");
  const fragment = '{"status":"reque';
  fs.writeFileSync(file, fragment);
  auditedAction(createAuditWriter(file), "disconnect", "alice", "127.0.0.1", () => "done");
  const lines = fs.readFileSync(file,"utf8").trimEnd().split("\n");
  assert.equal(lines[0],fragment);
  assert.equal(JSON.parse(lines[1]).status,"requested");
  assert.equal(JSON.parse(lines[2]).status,"succeeded");
  assert.equal(lines.length,3);
});
