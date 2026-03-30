"use strict";

const INPUT = Object.freeze({
  LEFT: 1 << 0,
  RIGHT: 1 << 1,
  JUMP: 1 << 2,
  ATTACK: 1 << 3,
});
const INPUT_MASK = INPUT.LEFT | INPUT.RIGHT | INPUT.JUMP | INPUT.ATTACK;

const DEFAULTS = Object.freeze({
  historyLimit: 240,
  syncInterval: 4,
  syncWindow: 48,
  worldLeft: -120000,
  worldRight: 120000,
  groundY: 0,
  moveAccelGround: 720,
  moveAccelAir: 360,
  maxSpeed: 4800,
  friction: 900,
  gravity: 520,
  jumpVelocity: 7800,
  knockbackX: 5200,
  knockbackY: 4600,
  attackRangeX: 15000,
  attackRangeY: 10000,
  attackCooldown: 16,
  hitstun: 14,
  maxHealth: 3,
  respawnFrames: 45,
  spawnOffsetX: 48000,
});

function clamp(n, lo, hi) {
  return Math.max(lo, Math.min(hi, n));
}

function sign(n) {
  return n < 0 ? -1 : (n > 0 ? 1 : 0);
}

function mulberry32(seed) {
  let t = seed >>> 0;
  return function next() {
    t = (t + 0x6D2B79F5) >>> 0;
    let r = Math.imul(t ^ (t >>> 15), 1 | t);
    r ^= r + Math.imul(r ^ (r >>> 7), 61 | r);
    return ((r ^ (r >>> 14)) >>> 0) / 4294967296;
  };
}

function clonePlayer(p) {
  return {
    x: p.x | 0,
    y: p.y | 0,
    vx: p.vx | 0,
    vy: p.vy | 0,
    facing: p.facing | 0,
    grounded: !!p.grounded,
    attackCooldown: p.attackCooldown | 0,
    hitstun: p.hitstun | 0,
    health: p.health | 0,
    score: p.score | 0,
    respawnTimer: p.respawnTimer | 0,
  };
}

function cloneState(state) {
  return {
    frame: state.frame | 0,
    seed: state.seed | 0,
    winner: state.winner | 0,
    players: [clonePlayer(state.players[0]), clonePlayer(state.players[1])],
  };
}

function spawnPlayer(index, cfg) {
  const dir = index === 0 ? 1 : -1;
  return {
    x: (index === 0 ? -cfg.spawnOffsetX : cfg.spawnOffsetX) | 0,
    y: cfg.groundY | 0,
    vx: 0,
    vy: 0,
    facing: dir,
    grounded: true,
    attackCooldown: 0,
    hitstun: 0,
    health: cfg.maxHealth,
    score: 0,
    respawnTimer: 0,
  };
}

function createInitialState(seed, config = DEFAULTS) {
  return {
    frame: 0,
    seed: seed | 0,
    winner: -1,
    players: [spawnPlayer(0, config), spawnPlayer(1, config)],
  };
}

function playerBytes(p) {
  return [
    p.x | 0, p.y | 0, p.vx | 0, p.vy | 0,
    p.facing | 0, p.grounded ? 1 : 0,
    p.attackCooldown | 0, p.hitstun | 0,
    p.health | 0, p.score | 0, p.respawnTimer | 0,
  ];
}

function hashState(state) {
  let h = 2166136261 >>> 0;
  const values = [state.frame | 0, state.seed | 0, state.winner | 0]
    .concat(playerBytes(state.players[0]), playerBytes(state.players[1]));
  for (const v of values) {
    let n = v | 0;
    for (let i = 0; i < 4; i++) {
      h ^= (n & 0xff);
      h = Math.imul(h, 16777619) >>> 0;
      n >>>= 8;
    }
  }
  return (h >>> 0).toString(16).padStart(8, "0");
}

function applyHorizontalMotion(player, input, cfg) {
  const left = (input & INPUT.LEFT) !== 0;
  const right = (input & INPUT.RIGHT) !== 0;
  let desired = 0;
  if (left && !right) desired = -1;
  else if (right && !left) desired = 1;

  if (desired !== 0) {
    const accel = player.grounded ? cfg.moveAccelGround : cfg.moveAccelAir;
    player.vx += desired * accel;
    player.vx = clamp(player.vx, -cfg.maxSpeed, cfg.maxSpeed);
    player.facing = desired;
  } else if (player.grounded) {
    if (player.vx > 0) player.vx = Math.max(0, player.vx - cfg.friction);
    else if (player.vx < 0) player.vx = Math.min(0, player.vx + cfg.friction);
  }
}

function resolveRespawn(player, index, cfg) {
  player.x = index === 0 ? -cfg.spawnOffsetX : cfg.spawnOffsetX;
  player.y = cfg.groundY;
  player.vx = 0;
  player.vy = 0;
  player.facing = index === 0 ? 1 : -1;
  player.grounded = true;
  player.attackCooldown = 0;
  player.hitstun = 0;
  player.health = cfg.maxHealth;
  player.respawnTimer = 0;
}

function inAttackArc(attacker, defender, cfg) {
  if (defender.respawnTimer > 0) return false;
  const dx = defender.x - attacker.x;
  const dy = Math.abs(defender.y - attacker.y);
  if (dy > cfg.attackRangeY) return false;
  if (attacker.facing >= 0) {
    return dx >= 0 && dx <= cfg.attackRangeX;
  }
  return dx <= 0 && dx >= -cfg.attackRangeX;
}

function stepState(prevState, frameInputs, overrides = {}) {
  const cfg = { ...DEFAULTS, ...overrides };
  const next = cloneState(prevState);
  next.frame = (prevState.frame + 1) | 0;
  next.winner = prevState.winner | 0;

  const justAttacked = [false, false];

  for (let i = 0; i < 2; i++) {
    const p = next.players[i];
    const input = (frameInputs[i] | 0) & INPUT_MASK;

    if (p.respawnTimer > 0) {
      p.respawnTimer -= 1;
      if (p.respawnTimer <= 0) resolveRespawn(p, i, cfg);
      continue;
    }

    if (p.hitstun <= 0) {
      applyHorizontalMotion(p, input, cfg);
      if ((input & INPUT.JUMP) && p.grounded) {
        p.vy = cfg.jumpVelocity;
        p.grounded = false;
      }
      if ((input & INPUT.ATTACK) && p.attackCooldown === 0) {
        p.attackCooldown = cfg.attackCooldown;
        justAttacked[i] = true;
      }
    }

    if (!p.grounded) {
      p.vy -= cfg.gravity;
    }

    p.x += p.vx;
    p.y += p.vy;

    if (p.x < cfg.worldLeft) {
      p.x = cfg.worldLeft;
      p.vx = 0;
    } else if (p.x > cfg.worldRight) {
      p.x = cfg.worldRight;
      p.vx = 0;
    }

    if (p.y <= cfg.groundY) {
      p.y = cfg.groundY;
      p.vy = 0;
      p.grounded = true;
    } else {
      p.grounded = false;
    }
  }

  for (let attackerIndex = 0; attackerIndex < 2; attackerIndex++) {
    if (!justAttacked[attackerIndex]) continue;
    const defenderIndex = 1 - attackerIndex;
    const attacker = next.players[attackerIndex];
    const defender = next.players[defenderIndex];
    if (!inAttackArc(attacker, defender, cfg)) continue;

    defender.health -= 1;
    defender.hitstun = cfg.hitstun;
    defender.vx = attacker.facing * cfg.knockbackX;
    defender.vy = cfg.knockbackY;
    defender.grounded = false;

    if (defender.health <= 0) {
      attacker.score += 1;
      defender.respawnTimer = cfg.respawnFrames;
      defender.vx = 0;
      defender.vy = 0;
      defender.hitstun = 0;
      next.winner = attacker.score >= 3 ? attackerIndex : -1;
    }
  }

  for (let i = 0; i < 2; i++) {
    const p = next.players[i];
    if (p.attackCooldown > 0) p.attackCooldown -= 1;
    if (p.hitstun > 0) p.hitstun -= 1;
  }

  return next;
}

function trimMap(map, minFrame) {
  for (const key of map.keys()) {
    if (key < minFrame) map.delete(key);
  }
}

class LatencyModel {
  constructor(baseFrames, jitterFrames = 0, seed = 1) {
    this.baseFrames = Math.max(1, baseFrames | 0);
    this.jitterFrames = Math.max(0, jitterFrames | 0);
    this.rng = mulberry32(seed | 0);
  }

  sample() {
    if (this.jitterFrames <= 0) return this.baseFrames;
    const delta = Math.floor(this.rng() * (this.jitterFrames * 2 + 1)) - this.jitterFrames;
    return Math.max(1, this.baseFrames + delta);
  }
}

class NetworkHarness {
  constructor() {
    this.now = 0;
    this.queue = [];
    this.order = 0;
  }

  send(target, message, latencyFrames) {
    const delay = Math.max(1, latencyFrames | 0);
    this.queue.push({
      at: this.now + delay,
      order: this.order++,
      target,
      message: JSON.parse(JSON.stringify(message)),
    });
  }

  deliverDue(frame) {
    this.now = frame | 0;
    this.queue.sort((a, b) => (a.at - b.at) || (a.order - b.order));
    while (this.queue.length > 0 && this.queue[0].at <= this.now) {
      const item = this.queue.shift();
      item.target.onMessage(item.message);
    }
  }

  flush(frames = 1) {
    for (let i = 0; i < frames; i++) {
      this.deliverDue(this.now + 1);
    }
  }
}

class RollbackMatchServer {
  constructor(options = {}) {
    this.cfg = { ...DEFAULTS, ...(options.config || {}) };
    this.seed = options.seed | 0;
    this.historyLimit = options.historyLimit || this.cfg.historyLimit;
    this.syncInterval = options.syncInterval || this.cfg.syncInterval;
    this.syncWindow = options.syncWindow || this.cfg.syncWindow;
    this.frame = 0;
    this.state = createInitialState(this.seed, this.cfg);
    this.stateHistory = new Map([[0, cloneState(this.state)]]);
    this.inputs = [new Map(), new Map()];
    this.lastKnownInput = [0, 0];
    this.lastPredictedInput = [0, 0];
    this.dirtyFrame = null;
    this.clients = [];
    this.network = null;
    this.metrics = {
      rollbacks: 0,
      resimulatedFrames: 0,
      predictedFrames: 0,
      corrections: 0,
      syncsSent: 0,
    };
  }

  attach(network, clients) {
    this.network = network;
    this.clients = clients.slice();
  }

  submitInput(playerIndex, frame, input) {
    const player = playerIndex | 0;
    const f = frame | 0;
    const value = (input | 0) & INPUT_MASK;
    const existing = this.inputs[player].get(f);
    if (existing) {
      if (existing.input !== value) {
        if (f <= this.frame) {
          this.dirtyFrame = this.dirtyFrame == null ? f : Math.min(this.dirtyFrame, f);
          this.metrics.corrections += 1;
        }
        existing.input = value;
      }
      existing.predicted = false;
    } else {
      this.inputs[player].set(f, { input: value, predicted: false });
      if (f <= this.frame) {
        this.dirtyFrame = this.dirtyFrame == null ? f : Math.min(this.dirtyFrame, f);
        this.metrics.corrections += 1;
      }
    }
    if (f >= this.frame - this.historyLimit) {
      this.lastKnownInput[player] = value;
    }
  }

  getInput(playerIndex, frame) {
    const existing = this.inputs[playerIndex].get(frame);
    if (existing) {
      if (!existing.predicted) this.lastKnownInput[playerIndex] = existing.input;
      return existing.input;
    }

    const predicted = this.lastKnownInput[playerIndex] | 0;
    this.inputs[playerIndex].set(frame, { input: predicted, predicted: true });
    this.lastPredictedInput[playerIndex] = predicted;
    this.metrics.predictedFrames += 1;
    return predicted;
  }

  rollbackIfNeeded() {
    if (this.dirtyFrame == null) return;
    const start = Math.max(1, this.dirtyFrame | 0);
    const latest = this.frame | 0;
    const baseState = this.stateHistory.get(start - 1);
    if (!baseState) {
      throw new Error(`missing server history for rollback frame ${start - 1}`);
    }
    this.metrics.rollbacks += 1;
    this.metrics.resimulatedFrames += (latest - start + 1);

    let state = cloneState(baseState);
    for (let f = start; f <= latest; f++) {
      const frameInputs = [this.getInput(0, f), this.getInput(1, f)];
      state = stepState(state, frameInputs, this.cfg);
      this.stateHistory.set(f, cloneState(state));
    }
    this.state = state;
    this.dirtyFrame = null;
  }

  tick() {
    this.rollbackIfNeeded();
    const nextFrame = this.frame + 1;
    const frameInputs = [this.getInput(0, nextFrame), this.getInput(1, nextFrame)];
    this.state = stepState(this.state, frameInputs, this.cfg);
    this.frame = nextFrame;
    this.stateHistory.set(this.frame, cloneState(this.state));

    const minKeep = Math.max(0, this.frame - this.historyLimit);
    trimMap(this.stateHistory, minKeep);
    trimMap(this.inputs[0], minKeep + 1);
    trimMap(this.inputs[1], minKeep + 1);

    if (this.network && (this.frame % this.syncInterval === 0)) {
      this.broadcastSync(false);
    }
  }

  buildSyncPayload(force = false) {
    const start = Math.max(1, this.frame - this.syncWindow + 1);
    const frames = [];
    for (let f = start; f <= this.frame; f++) {
      const p0 = this.inputs[0].get(f);
      const p1 = this.inputs[1].get(f);
      frames.push({
        frame: f,
        p0: p0 ? p0.input : 0,
        p1: p1 ? p1.input : 0,
      });
    }
    return {
      type: "sync",
      force: !!force,
      serverFrame: this.frame,
      hash: hashState(this.state),
      snapshot: cloneState(this.state),
      inputsStart: start,
      inputs: frames,
    };
  }

  broadcastSync(force = false) {
    const payload = this.buildSyncPayload(force);
    for (const client of this.clients) {
      this.network.send(client, payload, client.serverLatency.sample());
      this.metrics.syncsSent += 1;
    }
  }

  onMessage(message) {
    if (!message || message.type !== "input") return;
    this.submitInput(message.playerIndex, message.frame, message.input);
  }
}

class RollbackClient {
  constructor(options = {}) {
    this.playerIndex = options.playerIndex | 0;
    this.otherIndex = 1 - this.playerIndex;
    this.controller = options.controller;
    this.cfg = { ...DEFAULTS, ...(options.config || {}) };
    this.network = null;
    this.server = null;
    this.clientLatency = options.clientLatency instanceof LatencyModel
      ? options.clientLatency
      : new LatencyModel(3, 0, 1 + this.playerIndex);
    this.serverLatency = options.serverLatency instanceof LatencyModel
      ? options.serverLatency
      : new LatencyModel(3, 0, 101 + this.playerIndex);
    this.state = createInitialState(options.seed | 0, this.cfg);
    this.localFrame = 0;
    this.stateHistory = new Map([[0, cloneState(this.state)]]);
    this.localInputs = new Map();
    this.remoteInputs = new Map();
    this.metrics = {
      rollbacks: 0,
      maxRollbackDistance: 0,
      predictedRemoteFrames: 0,
      authoritativeCorrections: 0,
      hashCorrections: 0,
    };
    this.latestAuthoritativeFrame = 0;
    this.latestAuthoritativeHash = hashState(this.state);
  }

  attach(network, server) {
    this.network = network;
    this.server = server;
  }

  currentHash() {
    return hashState(this.state);
  }

  getLocalInput(frame) {
    const cached = this.localInputs.get(frame);
    if (cached != null) return cached;
    const value = (this.controller(this.state, this.playerIndex, frame) | 0) & INPUT_MASK;
    this.localInputs.set(frame, value);
    return value;
  }

  getRemoteInput(frame) {
    const cached = this.remoteInputs.get(frame);
    if (cached != null) return cached;
    const previous = this.remoteInputs.get(frame - 1) || 0;
    this.remoteInputs.set(frame, previous);
    this.metrics.predictedRemoteFrames += 1;
    return previous;
  }

  simulateFrame(frame) {
    const localInput = this.getLocalInput(frame);
    const remoteInput = this.getRemoteInput(frame);
    const inputs = this.playerIndex === 0
      ? [localInput, remoteInput]
      : [remoteInput, localInput];
    this.state = stepState(this.state, inputs, this.cfg);
    this.localFrame = frame;
    this.stateHistory.set(frame, cloneState(this.state));

    const minKeep = Math.max(0, this.localFrame - this.cfg.historyLimit);
    trimMap(this.stateHistory, minKeep);
    trimMap(this.localInputs, minKeep + 1);
    trimMap(this.remoteInputs, minKeep + 1);
  }

  tick() {
    const nextFrame = this.localFrame + 1;
    const localInput = this.getLocalInput(nextFrame);
    this.network.send(this.server, {
      type: "input",
      playerIndex: this.playerIndex,
      frame: nextFrame,
      input: localInput,
    }, this.clientLatency.sample());
    this.simulateFrame(nextFrame);
  }

  reconcileFromSnapshot(snapshot, authoritativeInputs, serverHash, forced) {
    const baseFrame = snapshot.frame | 0;
    const localHashAtBase = this.stateHistory.has(baseFrame)
      ? hashState(this.stateHistory.get(baseFrame))
      : null;

    let needsRollback = !!forced;
    if (!needsRollback && localHashAtBase !== serverHash) {
      needsRollback = true;
      this.metrics.hashCorrections += 1;
    }

    for (const row of authoritativeInputs) {
      const frame = row.frame | 0;
      const remoteValue = this.playerIndex === 0 ? (row.p1 | 0) : (row.p0 | 0);
      const localValue = this.playerIndex === 0 ? (row.p0 | 0) : (row.p1 | 0);
      if ((this.localInputs.get(frame) ?? localValue) !== localValue) {
        this.localInputs.set(frame, localValue);
        needsRollback = true;
        this.metrics.authoritativeCorrections += 1;
      }
      const prevRemote = this.remoteInputs.get(frame);
      if (prevRemote == null || prevRemote !== remoteValue) {
        this.remoteInputs.set(frame, remoteValue);
        if (prevRemote != null && prevRemote !== remoteValue) {
          needsRollback = true;
          this.metrics.authoritativeCorrections += 1;
        }
      }
    }

    this.latestAuthoritativeFrame = baseFrame;
    this.latestAuthoritativeHash = serverHash;

    if (!needsRollback) return;

    const distance = Math.max(0, this.localFrame - baseFrame);
    this.metrics.rollbacks += 1;
    this.metrics.maxRollbackDistance = Math.max(this.metrics.maxRollbackDistance, distance);

    this.state = cloneState(snapshot);
    this.stateHistory.set(baseFrame, cloneState(this.state));
    for (let frame = baseFrame + 1; frame <= this.localFrame; frame++) {
      const localInput = this.getLocalInput(frame);
      const remoteInput = this.remoteInputs.get(frame) ?? this.getRemoteInput(frame);
      const inputs = this.playerIndex === 0
        ? [localInput, remoteInput]
        : [remoteInput, localInput];
      this.state = stepState(this.state, inputs, this.cfg);
      this.stateHistory.set(frame, cloneState(this.state));
    }
  }

  onMessage(message) {
    if (!message || message.type !== "sync") return;
    this.reconcileFromSnapshot(message.snapshot, message.inputs, message.hash, !!message.force);
  }
}

function makeBotController(seed, styleOffset = 0) {
  const rng = mulberry32((seed + styleOffset * 9973) | 0);
  let strafeFlipAt = 0;
  let strafeDir = 1;
  return function botController(state, playerIndex, frame) {
    const me = state.players[playerIndex];
    const them = state.players[1 - playerIndex];
    if (me.respawnTimer > 0) return 0;

    if (frame >= strafeFlipAt) {
      strafeFlipAt = frame + 18 + Math.floor(rng() * 35);
      strafeDir = rng() < 0.5 ? -1 : 1;
    }

    const dx = them.x - me.x;
    const dy = them.y - me.y;
    let input = 0;

    if (Math.abs(dx) > 18000) {
      input |= dx > 0 ? INPUT.RIGHT : INPUT.LEFT;
    } else if (Math.abs(dx) < 8000) {
      input |= dx > 0 ? INPUT.LEFT : INPUT.RIGHT;
    } else if ((frame + styleOffset) % 11 < 5) {
      input |= strafeDir > 0 ? INPUT.RIGHT : INPUT.LEFT;
    }

    if (me.grounded && them.y > me.y + 6000 && Math.abs(dx) < 42000) {
      input |= INPUT.JUMP;
    } else if (me.grounded && Math.abs(dx) < 26000 && ((frame + styleOffset * 3) % 47 === 0)) {
      input |= INPUT.JUMP;
    }

    if (Math.abs(dx) < 16500 && Math.abs(dy) < 12000) {
      const phase = (frame + styleOffset * 5) % 9;
      if (phase <= 2) input |= INPUT.ATTACK;
    }

    return input & INPUT_MASK;
  };
}

function runPrototypeMatch(options = {}) {
  const seed = options.seed == null ? 1337 : (options.seed | 0);
  const config = { ...DEFAULTS, ...(options.config || {}) };
  config.historyLimit = options.historyLimit || config.historyLimit;

  const network = new NetworkHarness();
  const server = new RollbackMatchServer({
    seed,
    config,
    historyLimit: config.historyLimit,
    syncInterval: options.syncInterval || config.syncInterval,
    syncWindow: options.syncWindow || config.syncWindow,
  });

  const client0 = new RollbackClient({
    seed,
    config,
    playerIndex: 0,
    controller: options.controller0 || makeBotController(seed, 0),
    clientLatency: new LatencyModel(options.client0ToServer || 3, options.client0Jitter || 0, seed ^ 0x1111),
    serverLatency: new LatencyModel(options.serverToClient0 || 3, options.server0Jitter || 0, seed ^ 0x2222),
  });
  const client1 = new RollbackClient({
    seed,
    config,
    playerIndex: 1,
    controller: options.controller1 || makeBotController(seed, 1),
    clientLatency: new LatencyModel(options.client1ToServer || 4, options.client1Jitter || 0, seed ^ 0x3333),
    serverLatency: new LatencyModel(options.serverToClient1 || 4, options.server1Jitter || 0, seed ^ 0x4444),
  });

  server.attach(network, [client0, client1]);
  client0.attach(network, server);
  client1.attach(network, server);

  const totalFrames = options.frames || 360;
  for (let frame = 1; frame <= totalFrames; frame++) {
    network.deliverDue(frame);
    client0.tick();
    client1.tick();
    server.tick();
  }

  server.broadcastSync(true);
  const settleFrames = options.settleFrames || 40;
  for (let i = 0; i < settleFrames; i++) {
    network.deliverDue(network.now + 1);
  }

  const serverHash = hashState(server.state);
  const client0Hash = client0.currentHash();
  const client1Hash = client1.currentHash();

  return {
    ok: serverHash === client0Hash && serverHash === client1Hash,
    seed,
    frames: totalFrames,
    hashes: { server: serverHash, client0: client0Hash, client1: client1Hash },
    serverState: cloneState(server.state),
    clientStates: [cloneState(client0.state), cloneState(client1.state)],
    metrics: {
      server: server.metrics,
      clients: [client0.metrics, client1.metrics],
    },
  };
}

module.exports = {
  INPUT,
  DEFAULTS,
  LatencyModel,
  NetworkHarness,
  RollbackMatchServer,
  RollbackClient,
  createInitialState,
  cloneState,
  hashState,
  makeBotController,
  runPrototypeMatch,
  stepState,
};
