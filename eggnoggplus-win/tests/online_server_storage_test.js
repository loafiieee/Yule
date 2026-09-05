"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const {loadUserStore, atomicWriteFile} = require("../online_server/storage");

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
