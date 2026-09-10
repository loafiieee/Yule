"use strict";

const fs = require("node:fs");
const path = require("node:path");
const crypto = require("node:crypto");
const ACTIONS = new Set(["reset_password", "ban", "unban", "disconnect", "reset_rating"]);

function createAuditWriter(filename) {
  return record => {
    fs.mkdirSync(path.dirname(filename), {recursive: true});
    const line = JSON.stringify(record) + "\n";
    const fd = fs.openSync(filename, "a+", 0o600);
    try {
      // Preserve an interrupted final record as evidence, but isolate it so
      // future complete records remain individually parseable.
      const size = fs.fstatSync(fd).size;
      if (size) {
        const tail = Buffer.alloc(1);
        if (fs.readSync(fd, tail, 0, 1, size - 1) !== 1) throw new Error("Cannot inspect audit tail");
        if (tail[0] !== 10) fs.writeFileSync(fd, "\n", "utf8");
      }
      fs.writeFileSync(fd, line, "utf8");
      fs.fsyncSync(fd);
    } finally { fs.closeSync(fd); }
  };
}

function auditedAction(write, action, username, actor, operation) {
  if (!ACTIONS.has(action)) throw new Error("unknown maintenance action");
  const base = {
    version: 1, id: crypto.randomUUID(), action,
    username: /^[a-z0-9_]{1,24}$/i.test(username) ? username.toLowerCase() : null,
    actor: String(actor).slice(0, 64),
  };
  const emit = status => write({...base, at: new Date().toISOString(), status});
  // The durable intent precedes mutation. Never include form fields, passwords,
  // cookies, ban reasons, result messages, or arbitrary exception text.
  emit("requested");
  let result;
  try { result = operation(); }
  catch (error) {
    try { emit("failed"); }
    catch { throw new Error("Maintenance failed; audit outcome could not be recorded."); }
    throw error;
  }
  try { emit("succeeded"); }
  catch { throw new Error("Maintenance completed, but its audit outcome could not be recorded. Do not retry blindly."); }
  return result;
}

module.exports = {createAuditWriter, auditedAction};
