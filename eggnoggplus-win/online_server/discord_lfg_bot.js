"use strict";

const https = require("https");
const {
  normalizePublicBaseUrl,
  publicLfgLink,
} = require("./lfg_redirect");

const USERNAME_RE = /^[a-z0-9_]{1,24}$/;
const SNOWFLAKE_RE = /^[1-9][0-9]{0,19}$/;
const TOKEN_RE = /^[A-Za-z0-9._-]{20,256}$/;
const API_HOST = "discord.com";
const API_PREFIX = "/api/v10";
const RESPONSE_MAX = 64 * 1024;
const REQUEST_TIMEOUT_MS = 10000;
const TRACKED_MAX = 2048;
const PENDING_CREATE_MAX = 4096;
const DEFAULT_POST_DELAY_MS = 2000;
const POST_DELAY_MAX_MS = 30000;
const DEFAULT_MATCH_DELETE_MS = 24 * 60 * 60 * 1000;
const MATCH_DELETE_MAX_MS = 7 * 24 * 60 * 60 * 1000;

function validateConfig(config) {
  if (!config || config.enabled !== true) return { enabled: false };
  const token = String(config.token || "");
  const channelId = String(config.channelId || "");
  if (!TOKEN_RE.test(token)) throw new Error("Discord LFG bot token is missing or malformed");
  if (!SNOWFLAKE_RE.test(channelId)) throw new Error("Discord LFG channel ID is malformed");
  return {
    enabled: true,
    token,
    channelId,
    publicBaseUrl: normalizePublicBaseUrl(config.publicBaseUrl),
    postDelayMs: Number.isInteger(config.postDelayMs)
      ? Math.max(0, Math.min(POST_DELAY_MAX_MS, config.postDelayMs))
      : DEFAULT_POST_DELAY_MS,
    matchDeleteMs: Number.isInteger(config.matchDeleteMs)
      ? Math.max(0, Math.min(MATCH_DELETE_MAX_MS, config.matchDeleteMs))
      : DEFAULT_MATCH_DELETE_MS,
  };
}

function buildQueuePayload(username, queue, baseUrl) {
  if (!USERNAME_RE.test(username)) throw new Error("invalid LFG username");
  if (queue !== "casual" && queue !== "competitive") {
    throw new Error("invalid LFG queue");
  }
  const queueLabel = queue === "competitive" ? "Competitive" : "Casual";
  return {
    allowed_mentions: { parse: [], users: [], roles: [], replied_user: false },
    embeds: [{
      title: `${queueLabel} player looking for a match`,
      description: `**${username}** joined the ${queueLabel.toLowerCase()} queue.`,
      color: queue === "competitive" ? 0xf0b429 : 0x4fb6e9,
      fields: [
        { name: "Queue", value: queueLabel, inline: true },
        { name: "How to play", value: "Challenge this player or join the same public queue.", inline: false },
      ],
      footer: { text: "Only public queue and challenge links are included." },
      timestamp: new Date().toISOString(),
    }],
    components: [{
      type: 1,
      components: [
        {
          type: 2,
          style: 5,
          label: `Challenge ${username}`,
          url: publicLfgLink(baseUrl, "challenge", username),
        },
        {
          type: 2,
          style: 5,
          label: `Join ${queueLabel} Queue`,
          url: publicLfgLink(baseUrl, "queue", queue),
        },
      ],
    }],
  };
}

function buildRetiredPayload(username, queue, reason = "left") {
  if (!USERNAME_RE.test(username)) throw new Error("invalid LFG username");
  if (queue !== "casual" && queue !== "competitive") {
    throw new Error("invalid LFG queue");
  }
  const queueLabel = queue === "competitive" ? "Competitive" : "Casual";
  const states = {
    matched: {
      title: "Match found",
      description: `**${username}** found a match and is no longer waiting.`,
      color: 0x57f287,
    },
    disconnected: {
      title: "Player went offline",
      description: `**${username}** left the ${queueLabel.toLowerCase()} queue.`,
      color: 0x747f8d,
    },
    changed: {
      title: "Queue changed",
      description: `**${username}** is no longer waiting in this ${queueLabel.toLowerCase()} queue.`,
      color: 0x747f8d,
    },
    shutdown: {
      title: "Queue closed",
      description: `**${username}** is no longer waiting in the ${queueLabel.toLowerCase()} queue.`,
      color: 0x747f8d,
    },
    left: {
      title: "Queue closed",
      description: `**${username}** left the ${queueLabel.toLowerCase()} queue.`,
      color: 0x747f8d,
    },
  };
  const state = states[reason] || states.left;
  return {
    allowed_mentions: { parse: [], users: [], roles: [], replied_user: false },
    embeds: [{
      title: state.title,
      description: state.description,
      color: state.color,
      fields: [{ name: "Queue", value: queueLabel, inline: true }],
      footer: { text: "This LFG post is no longer active." },
      timestamp: new Date().toISOString(),
    }],
    components: [],
  };
}

function defaultDelay(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function discordRequest({ token, method, path, body }) {
  return new Promise((resolve, reject) => {
    const payload = body === undefined ? null : Buffer.from(JSON.stringify(body), "utf8");
    const req = https.request({
      protocol: "https:",
      hostname: API_HOST,
      port: 443,
      method,
      path: `${API_PREFIX}${path}`,
      headers: {
        "Authorization": `Bot ${token}`,
        "User-Agent": "DiscordBot (https://loafiieee.com/yule, 1.0)",
        "Accept": "application/json",
        ...(payload ? {
          "Content-Type": "application/json",
          "Content-Length": payload.length,
        } : {}),
      },
    }, (res) => {
      const chunks = [];
      let length = 0;
      res.on("data", (chunk) => {
        length += chunk.length;
        if (length > RESPONSE_MAX) {
          req.destroy(new Error("Discord response exceeded 64 KiB"));
          return;
        }
        chunks.push(chunk);
      });
      res.on("end", () => resolve({
        status: Number(res.statusCode || 0),
        headers: res.headers || {},
        body: Buffer.concat(chunks).toString("utf8"),
      }));
    });
    req.setTimeout(REQUEST_TIMEOUT_MS, () => {
      req.destroy(new Error("Discord request timed out"));
    });
    req.on("error", reject);
    if (payload) req.write(payload);
    req.end();
  });
}

function safeJson(text) {
  if (!text) return {};
  try {
    const value = JSON.parse(text);
    return value && typeof value === "object" ? value : {};
  } catch (_) {
    return {};
  }
}

class DiscordLfgBot {
  constructor(config, dependencies = {}) {
    this.config = validateConfig(config);
    this.request = dependencies.request || discordRequest;
    this.delay = dependencies.delay || defaultDelay;
    this.setTimer = dependencies.setTimer || setTimeout;
    this.clearTimer = dependencies.clearTimer || clearTimeout;
    this.log = dependencies.log || (() => {});
    this.records = new Map();
    this.retiredDeleteTimers = new Set();
    this.serial = Promise.resolve();
    this.closed = false;
    this.pendingTasks = 0;
    this.pendingCreateMax = Number.isInteger(dependencies.pendingCreateMax)
      ? Math.max(1, Math.min(PENDING_CREATE_MAX, dependencies.pendingCreateMax))
      : PENDING_CREATE_MAX;
  }

  get enabled() {
    return this.config.enabled === true && !this.closed;
  }

  enqueue(task, options = {}) {
    if (!this.enabled) return Promise.resolve(null);
    if (options.critical !== true &&
        this.pendingTasks >= this.pendingCreateMax) {
      this.log("Discord LFG post skipped: pending-request limit reached");
      return null;
    }
    this.pendingTasks++;
    const run = this.serial.then(task, task);
    this.serial = run.catch((err) => {
      this.log(`Discord LFG request failed: ${err && err.message ? err.message : "unknown error"}`);
      return null;
    }).finally(() => {
      this.pendingTasks--;
    });
    return run;
  }

  async api(method, path, body, attempt = 0) {
    const response = await this.request({
      token: this.config.token,
      method,
      path,
      body,
    });
    if (response.status === 429 && attempt < 3) {
      const parsed = safeJson(response.body);
      const headerSeconds = Number.parseFloat(
        String(response.headers && response.headers["retry-after"] || ""),
      );
      const bodySeconds = Number(parsed.retry_after);
      const seconds = Number.isFinite(bodySeconds) ? bodySeconds : headerSeconds;
      const boundedMs = Math.max(50, Math.min(60000,
        Number.isFinite(seconds) ? Math.ceil(seconds * 1000) : 1000));
      await this.delay(boundedMs);
      return this.api(method, path, body, attempt + 1);
    }
    if (response.status >= 500 && response.status <= 599 && attempt < 2) {
      await this.delay(500 * (2 ** attempt));
      return this.api(method, path, body, attempt + 1);
    }
    if (response.status < 200 || response.status > 299) {
      throw new Error(`Discord API ${method} returned HTTP ${response.status}`);
    }
    return safeJson(response.body);
  }

  scheduleMatchedDelete(messageId) {
    if (!SNOWFLAKE_RE.test(messageId) || this.config.matchDeleteMs <= 0) return;
    let timer = null;
    timer = this.setTimer(() => {
      this.retiredDeleteTimers.delete(timer);
      this.enqueue(async () => {
        await this.api(
          "DELETE",
          `/channels/${this.config.channelId}/messages/${messageId}`,
        );
        this.log(`Discord matched LFG post deleted after retention (${messageId})`);
      }, { critical: true });
    }, this.config.matchDeleteMs);
    this.retiredDeleteTimers.add(timer);
    if (timer && typeof timer.unref === "function") timer.unref();
  }

  async retireMessage(username, queue, reason, messageId) {
    await this.api(
      "PATCH",
      `/channels/${this.config.channelId}/messages/${messageId}`,
      buildRetiredPayload(username, queue, reason),
    );
    if (reason === "matched") this.scheduleMatchedDelete(messageId);
  }

  queueJoined(username, queue) {
    if (!this.enabled || !USERNAME_RE.test(username) ||
        (queue !== "casual" && queue !== "competitive")) {
      return false;
    }
    if (!this.records.has(username) && this.records.size >= TRACKED_MAX) {
      this.log("Discord LFG post skipped: tracked-user limit reached");
      return false;
    }
    const prior = this.records.get(username);
    if (prior && prior.active && prior.queue === queue) return true;
    if (prior) this.queueLeft(username, "changed");
    const generation = (prior ? prior.generation : 0) + 1;
    const record = {
      generation,
      active: true,
      queue,
      messageId: "",
      timer: null,
    };
    this.records.set(username, record);
    const payload = buildQueuePayload(username, queue, this.config.publicBaseUrl);
    const create = () => {
      record.timer = null;
      if (!record.active || this.records.get(username) !== record) return false;
      const queued = this.enqueue(async () => {
      const created = await this.api(
        "POST", `/channels/${this.config.channelId}/messages`, payload,
      );
      const messageId = String(created.id || "");
      if (!SNOWFLAKE_RE.test(messageId)) {
        throw new Error("Discord create-message response omitted a valid message ID");
      }
      const current = this.records.get(username);
      if (!current || current !== record || !current.active) {
        await this.retireMessage(
          username, queue, record.retireReason || "left", messageId,
        );
        return;
      }
      current.messageId = messageId;
      this.log(`Discord LFG post created for ${username}/${queue}`);
      });
      if (!queued && this.records.get(username) === record) {
        this.records.delete(username);
      }
      return !!queued;
    };
    if (this.config.postDelayMs > 0) {
      record.timer = this.setTimer(create, this.config.postDelayMs);
      if (record.timer && typeof record.timer.unref === "function") {
        record.timer.unref();
      }
    } else {
      return create();
    }
    return true;
  }

  queueLeft(username, reason = "left") {
    if (!USERNAME_RE.test(username)) return false;
    const record = this.records.get(username);
    if (!record) return true;
    record.active = false;
    record.retireReason = reason;
    record.generation++;
    this.records.delete(username);
    if (record.timer !== null) {
      this.clearTimer(record.timer);
      record.timer = null;
    }
    if (this.enabled && SNOWFLAKE_RE.test(record.messageId)) {
      const messageId = record.messageId;
      this.enqueue(async () => {
        await this.retireMessage(username, record.queue, reason, messageId);
        this.log(`Discord LFG post updated for ${username} (${reason})`);
      }, { critical: true });
    }
    return true;
  }

  async flush() {
    await this.serial;
  }

  async close(options = {}) {
    if (this.closed) return;
    const retire = options.retire !== false;
    if (retire) {
      for (const username of [...this.records.keys()]) {
        this.queueLeft(username, "shutdown");
      }
      await this.flush();
    }
    this.closed = true;
    for (const timer of this.retiredDeleteTimers) this.clearTimer(timer);
    this.retiredDeleteTimers.clear();
    for (const record of this.records.values()) record.active = false;
    this.records.clear();
  }
}

function createDiscordLfgBotFromEnv(env = process.env, dependencies = {}) {
  return new DiscordLfgBot({
    enabled: env.DISCORD_LFG_ENABLED === "1",
    token: env.DISCORD_LFG_BOT_TOKEN,
    channelId: env.DISCORD_LFG_CHANNEL_ID,
    publicBaseUrl: env.DISCORD_LFG_PUBLIC_BASE_URL,
    postDelayMs: env.DISCORD_LFG_POST_DELAY_MS === undefined
      ? DEFAULT_POST_DELAY_MS
      : Number.parseInt(String(env.DISCORD_LFG_POST_DELAY_MS), 10),
    matchDeleteMs: env.DISCORD_LFG_MATCH_DELETE_MS === undefined
      ? DEFAULT_MATCH_DELETE_MS
      : Number.parseInt(String(env.DISCORD_LFG_MATCH_DELETE_MS), 10),
  }, dependencies);
}

module.exports = {
  validateConfig,
  buildQueuePayload,
  buildRetiredPayload,
  discordRequest,
  DiscordLfgBot,
  createDiscordLfgBotFromEnv,
};
