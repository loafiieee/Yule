"use strict";

const {spawn} = require("node:child_process");
const path = require("node:path");

const PUID_PATTERN = /^[0-9a-f]{32}$/;
const JWT_PATTERN = /^[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+$/;

function configured(env = process.env) {
  return path.isAbsolute(env.EOS_VERIFY_BINARY || "") &&
    !!env.EOS_PRODUCT_ID && !!env.EOS_SANDBOX_ID &&
    !!env.EOS_DEPLOYMENT_ID && !!env.EOS_CLIENT_ID &&
    !!env.EOS_CLIENT_SECRET;
}

function parseVerifyOutput(output, puid) {
  const matches = output.split(/\r?\n/).map((line) =>
    /^([0-9a-f]{32}) ([0-9a-f]{32})$/.exec(line)).filter(Boolean);
  if (matches.length !== 1 || matches[0][2] !== puid) return null;
  return {accountId: matches[0][1], puid: matches[0][2]};
}

function verifyConnectIdToken(puid, jwt, env = process.env) {
  if (!configured(env) || !PUID_PATTERN.test(puid) ||
      typeof jwt !== "string" || jwt.length < 64 || jwt.length > 16000 ||
      !JWT_PATTERN.test(jwt)) {
    return Promise.reject(new Error("EOS verification input is unavailable"));
  }
  return new Promise((resolve, reject) => {
    const child = spawn(env.EOS_VERIFY_BINARY, [puid], {
      env: {
        ...(process.platform === "win32" ? {
          SystemRoot: process.env.SystemRoot || process.env.SYSTEMROOT,
          TEMP: process.env.TEMP,
          TMP: process.env.TMP,
        } : {}),
        EOS_PRODUCT_ID: env.EOS_PRODUCT_ID,
        EOS_SANDBOX_ID: env.EOS_SANDBOX_ID,
        EOS_DEPLOYMENT_ID: env.EOS_DEPLOYMENT_ID,
        EOS_CLIENT_ID: env.EOS_CLIENT_ID,
        EOS_CLIENT_SECRET: env.EOS_CLIENT_SECRET,
      },
      stdio: ["pipe", "pipe", "ignore"],
      windowsHide: true,
    });
    let output = "";
    let settled = false;
    const timer = setTimeout(() => child.kill(), 17000);
    function finish(error, value) {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      if (error) reject(error);
      else resolve(value);
    }
    child.on("error", () => finish(new Error("EOS verifier could not start")));
    child.stdout.on("data", (chunk) => {
      output += chunk.toString("ascii");
      if (output.length > 2048) child.kill();
    });
    child.on("close", (code) => {
      // EOS itself can emit shutdown diagnostics on stdout. Only one exact
      // account/PUID result line from our helper is accepted.
      const proof = code === 0 ? parseVerifyOutput(output, puid) : null;
      if (!proof) {
        finish(new Error("EOS Connect ID token was rejected"));
      } else {
        finish(null, proof);
      }
    });
    child.stdin.on("error", () => {});
    child.stdin.end(jwt + "\n");
  });
}

module.exports = {configured, parseVerifyOutput, verifyConnectIdToken};
