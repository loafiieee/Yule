"use strict";

const fs = require("node:fs");
const path = require("node:path");
const crypto = require("node:crypto");
const {loadUserStore, atomicWriteFile} = require("./storage");
const FILES = ["users.json", "ratings.json", "server_secret.key"];
const digest = bytes => crypto.createHash("sha256").update(bytes).digest("hex");

function dataPaths(env = process.env) {
  if (env.RATING_SECRET) throw new Error("This tool backs up file-backed rating secrets; preserve RATING_SECRET through your service configuration instead.");
  return [env.DB || path.join(__dirname, FILES[0]),
    env.RATINGS || path.join(__dirname, FILES[1]),
    env.SECRET_FILE || path.join(__dirname, FILES[2])].map(p => path.resolve(p));
}

function validateData(paths) {
  loadUserStore(paths[0]);
  loadUserStore(paths[1]);
  // Missing stores are valid at server first boot, but not a complete snapshot.
  for (const file of paths) if (!fs.statSync(file).isFile()) throw new Error("Expected a regular data file");
  if (fs.readFileSync(paths[2], "utf8").trim().length < 16) throw new Error("Invalid rating secret");
}

function requireOffline(options) {
  if (!options?.offline) throw new Error("Stop the server first and pass --offline; data files are not a live transaction.");
}

function backup(directory, paths, options = {}) {
  requireOffline(options);
  validateData(paths);
  // Refuse existing destinations: an interrupted snapshot is never overwritten.
  fs.mkdirSync(directory, {mode: 0o700});
  const manifest = {version: 1, created_at: new Date().toISOString(), files: {}};
  for (let i = 0; i < FILES.length; i++) {
    const bytes = fs.readFileSync(paths[i]);
    atomicWriteFile(path.join(directory, FILES[i]), bytes);
    manifest.files[FILES[i]] = {size: bytes.length, sha256: digest(bytes)};
  }
  atomicWriteFile(path.join(directory, "manifest.json"), JSON.stringify(manifest, null, 2) + "\n");
  verify(directory);
  return manifest;
}

function verify(directory) {
  const manifest = JSON.parse(fs.readFileSync(path.join(directory, "manifest.json"), "utf8"));
  if (manifest.version !== 1 || !manifest.files ||
      JSON.stringify(Object.keys(manifest.files).sort()) !== JSON.stringify([...FILES].sort())) {
    throw new Error("Invalid backup manifest");
  }
  const paths = FILES.map(name => path.join(directory, name));
  const bytes = paths.map((file, i) => {
    const data = fs.readFileSync(file);
    const entry = manifest.files[FILES[i]];
    if (!entry || entry.size !== data.length || entry.sha256 !== digest(data)) {
      throw new Error(`Backup integrity check failed: ${FILES[i]}`);
    }
    return data;
  });
  validateData(paths);
  return bytes;
}

function restore(directory, paths, recoveryDirectory, options = {}) {
  requireOffline(options);
  const bytes = verify(directory); // validate everything before touching live paths
  if (new Set(paths.map(p => path.resolve(p))).size !== FILES.length) {
    throw new Error("Data destinations must be distinct");
  }
  // A complete verified recovery copy is mandatory. Restore targets an existing
  // stopped installation; for a new server, copy a verified snapshot manually.
  backup(recoveryDirectory, paths, options);
  const previous = verify(recoveryDirectory);
  const write = options.write || atomicWriteFile;
  try {
    for (let i = 0; i < FILES.length; i++) write(paths[i], bytes[i]);
    for (let i = 0; i < FILES.length; i++) {
      if (digest(fs.readFileSync(paths[i])) !== digest(bytes[i])) throw new Error("Restore verification failed");
    }
  } catch (error) {
    let recovered = true;
    for (let i = 0; i < FILES.length; i++) {
      try {
        write(paths[i], previous[i]);
        if (digest(fs.readFileSync(paths[i])) !== digest(previous[i])) recovered = false;
      } catch { recovered = false; }
    }
    throw new Error(recovered
      ? "Restore failed; previous data restored. Keep the server stopped and inspect the recovery snapshot."
      : "Restore failed and recovery was incomplete. Keep the server stopped; recover from the verified recovery snapshot.", {cause: error});
  }
}

if (require.main === module) {
  try {
    const [command, directory, ...rest] = process.argv.slice(2);
    if (command === "verify" && directory && rest.length === 0) {
      verify(directory);
    } else if (command === "backup" && directory && rest.length === 1 && rest[0] === "--offline") {
      backup(directory, dataPaths(), {offline: true});
    } else if (command === "restore" && directory && rest.length === 2 && rest[1] === "--offline") {
      restore(directory, dataPaths(), rest[0], {offline: true});
    } else throw new Error("Usage: node maintenance.js backup DIR --offline | verify DIR | restore DIR RECOVERY_DIR --offline");
    console.log(`${command}: verified successfully`);
  } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}

module.exports = {backup, verify, restore, dataPaths};
