"use strict";

const fs = require("node:fs");
const path = require("node:path");
const crypto = require("node:crypto");

// A missing store is a first launch; unreadable or malformed data is not.
// Fail startup before any persistence can overwrite the operator's evidence.
function loadUserStore(filename) {
  let text;
  try { text = fs.readFileSync(filename, "utf8"); }
  catch (err) {
    if (err.code === "ENOENT") return { users: Object.create(null) };
    throw err;
  }
  const store = JSON.parse(text);
  if (!store || typeof store !== "object" || Array.isArray(store) ||
      !store.users || typeof store.users !== "object" || Array.isArray(store.users)) {
    throw new Error(`Invalid user store: ${filename}`);
  }
  for (const [name, record] of Object.entries(store.users)) {
    if (!/^[a-z0-9_]{1,24}$/.test(name) || !record ||
        typeof record !== "object" || Array.isArray(record)) {
      throw new Error(`Invalid user record in ${filename}`);
    }
  }
  store.users = Object.assign(Object.create(null), store.users);
  return store;
}

// Replace the complete file only after its private sibling is flushed. This
// prevents partial JSON after interrupted writes; separate stores are still
// not one transaction, and directory durability depends on the filesystem.
function atomicWriteFile(filename, text) {
  fs.mkdirSync(path.dirname(filename), { recursive: true });
  const temp = `${filename}.${process.pid}.${crypto.randomBytes(8).toString("hex")}.tmp`;
  let fd;
  try {
    fd = fs.openSync(temp, "wx", 0o600);
    fs.writeFileSync(fd, text, "utf8");
    fs.fsyncSync(fd);
    fs.closeSync(fd);
    fd = undefined;
    fs.renameSync(temp, filename);
  } finally {
    if (fd !== undefined) fs.closeSync(fd);
    try { fs.unlinkSync(temp); } catch (err) { if (err.code !== "ENOENT") throw err; }
  }
}

module.exports = { loadUserStore, atomicWriteFile };
