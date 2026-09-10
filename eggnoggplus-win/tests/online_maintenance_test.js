"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const {backup, verify, restore, dataPaths} = require("../online_server/maintenance");
const {atomicWriteFile} = require("../online_server/storage");

function fixture(t) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "yule-maintenance-"));
  t.after(() => fs.rmSync(dir, {recursive: true, force: true}));
  const files = ["users.json", "ratings.json", "server_secret.key"].map(n => path.join(dir,n));
  fs.writeFileSync(files[0], '{"users":{"alice":{"hash":"fixture"}}}');
  fs.writeFileSync(files[1], '{"users":{"alice":{"elo":1234}}}');
  fs.writeFileSync(files[2], "0123456789abcdef0123456789abcdef");
  return {dir, files, snapshot: path.join(dir,"snapshot")};
}

test("offline backup verifies data and restores with a complete recovery copy", t => {
  const f = fixture(t);
  assert.throws(() => backup(f.snapshot, f.files), /Stop the server/);
  backup(f.snapshot, f.files, {offline:true});
  const original = verify(f.snapshot);
  assert.throws(() => backup(f.snapshot, f.files, {offline:true}));
  atomicWriteFile(f.files[0], '{"users":{"bob":{}}}');
  const recovery = path.join(f.dir,"recovery");
  restore(f.snapshot, f.files, recovery, {offline:true});
  assert.deepEqual(f.files.map(p => fs.readFileSync(p)), original);
  assert.ok(verify(recovery)[0].toString().includes("bob"));
});

test("tampered backup is rejected before any destination changes", t => {
  const f = fixture(t);
  backup(f.snapshot, f.files, {offline:true});
  const original = f.files.map(p => fs.readFileSync(p));
  fs.appendFileSync(path.join(f.snapshot,"users.json"), " ");
  assert.throws(() => restore(f.snapshot, f.files, path.join(f.dir,"recovery"), {offline:true}), /integrity/);
  assert.deepEqual(f.files.map(p => fs.readFileSync(p)), original);
  assert.equal(fs.existsSync(path.join(f.dir,"recovery")), false);
});

test("partial restore failure restores previous bytes and retains recovery snapshot", t => {
  const f = fixture(t);
  backup(f.snapshot, f.files, {offline:true});
  atomicWriteFile(f.files[0], '{"users":{"new_user":{}}}');
  const before = f.files.map(p => fs.readFileSync(p));
  let writes = 0;
  const recovery = path.join(f.dir,"recovery");
  assert.throws(() => restore(f.snapshot, f.files, recovery, {offline:true, write(file, bytes) {
    if (++writes === 2) throw new Error("simulated disk failure");
    atomicWriteFile(file, bytes);
  }}), /previous data restored/);
  assert.deepEqual(f.files.map(p => fs.readFileSync(p)), before);
  assert.deepEqual(verify(recovery), before);
  assert.throws(() => dataPaths({RATING_SECRET:"fixture"}), /file-backed/);
});


test("silent write corruption triggers verified recovery", t => {
  const f = fixture(t);
  backup(f.snapshot, f.files, {offline:true});
  atomicWriteFile(f.files[0], '{"users":{"before":{}}}');
  const before = f.files.map(p => fs.readFileSync(p));
  let writes = 0;
  assert.throws(() => restore(f.snapshot, f.files, path.join(f.dir,"recovery"), {
    offline:true, write(file, bytes) {
      atomicWriteFile(file, ++writes === 2 ? "corrupted" : bytes);
    },
  }), /previous data restored/);
  assert.deepEqual(f.files.map(p => fs.readFileSync(p)), before);
});

test("silent recovery corruption is never reported as recovered", t => {
  const f = fixture(t);
  backup(f.snapshot, f.files, {offline:true});
  let writes = 0;
  const recovery = path.join(f.dir,"recovery");
  assert.throws(() => restore(f.snapshot, f.files, recovery, {
    offline:true, write(file, bytes) {
      if (++writes === 2) throw new Error("disk");
      atomicWriteFile(file, writes === 3 ? "corrupted recovery" : bytes);
    },
  }), /recovery was incomplete/);
  assert.equal(verify(recovery).length, 3);
});
