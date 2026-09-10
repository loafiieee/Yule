"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const {loadUserStore, atomicWriteFile, updateUserRecord} = require("../online_server/storage");

test("stores reject damaged existing files and have no inherited users", t => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "eggnogg-store-"));
  t.after(() => fs.rmSync(dir, {recursive: true, force: true}));
  const file = path.join(dir, "fixture.json");
  assert.equal(loadUserStore(file).users.constructor, undefined);
  for (const raw of ['{', '[]', '{}', '{"users":[]}', '{"users":{"a":null}}']) {
    fs.writeFileSync(file, raw);
    assert.throws(() => loadUserStore(file));
    assert.equal(fs.readFileSync(file, "utf8"), raw);
  }
  const raw = '{"users":{"__proto__":{"hash":"fixture"}}}';
  atomicWriteFile(file, raw);
  assert.equal(loadUserStore(file).users.__proto__.hash, "fixture");
  assert.equal(Object.getPrototypeOf(loadUserStore(file).users), null);
  assert.deepEqual(fs.readdirSync(dir), ["fixture.json"]);
});

test("failed atomic replacement preserves destination and cleans temporary file", t => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "eggnogg-store-"));
  t.after(() => fs.rmSync(dir, {recursive: true, force: true}));
  const destination = path.join(dir, "existing");
  fs.mkdirSync(destination);
  fs.writeFileSync(path.join(destination, "keep"), "original");
  assert.throws(() => atomicWriteFile(destination, "replacement"));
  assert.equal(fs.readFileSync(path.join(destination, "keep"), "utf8"), "original");
  assert.deepEqual(fs.readdirSync(dir), ["existing"]);
});


test("record updates publish only after persistence and preserve unrelated users", t => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "yule-record-"));
  t.after(() => fs.rmSync(dir, {recursive:true, force:true}));
  const file = path.join(dir, "users.json");
  atomicWriteFile(file, '{"users":{"alice":{"friends":["bob"]},"bob":{}}}');
  const store = loadUserStore(file);
  const alice = store.users.alice, bob = store.users.bob;
  updateUserRecord(file, store, "alice", rec => { rec.ban = {reason:"fixture"}; }, (target, bytes) => {
    assert.strictEqual(store.users.alice, alice);
    assert.equal(store.users.alice.ban, undefined);
    atomicWriteFile(target, bytes);
  });
  assert.strictEqual(store.users.bob, bob);
  assert.notStrictEqual(store.users.alice, alice);
  assert.equal(loadUserStore(file).users.alice.ban.reason, "fixture");
  const current = store.users.alice;
  const previousBytes = fs.readFileSync(file);
  assert.throws(() => updateUserRecord(file, store, "alice", rec => {
    rec.friends.push("charlie"); delete rec.ban;
  }, () => { throw new Error("disk failure"); }), /disk failure/);
  assert.strictEqual(store.users.alice, current);
  assert.deepEqual(current.friends, ["bob"]);
  assert.ok(current.ban);
  assert.deepEqual(fs.readFileSync(file), previousBytes);
  assert.throws(() => updateUserRecord(file, store, "new_user", () => ({elo:1000}), () => {
    throw new Error("disk failure");
  }));
  assert.equal(store.users.new_user, undefined);
});
