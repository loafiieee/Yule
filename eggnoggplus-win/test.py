import math
import random
import threading
import time
from collections import deque
from dataclasses import dataclass
from pathlib import Path
from queue import Empty, Queue

import requests
import tkinter as tk
from tkinter import filedialog

import torch
import torch.nn as nn
import torch.nn.functional as F
import torch.optim as optim

BASE = "http://127.0.0.1:8765"

LEFT = 1 << 0
RIGHT = 1 << 1
UP = 1 << 2
DOWN = 1 << 3
JUMP = 1 << 4
ATTACK = 1 << 5

ACTION_MASKS = [
    0,
    LEFT,
    RIGHT,
    UP,
    DOWN,
    JUMP,
    ATTACK,
    LEFT | JUMP,
    RIGHT | JUMP,
    LEFT | ATTACK,
    RIGHT | ATTACK,
    UP | ATTACK,
    DOWN | ATTACK,
    JUMP | ATTACK,
    LEFT | DOWN,
    RIGHT | DOWN,
    LEFT | DOWN | JUMP,
    RIGHT | DOWN | JUMP,
    LEFT | UP,
    RIGHT | UP,
    LEFT | JUMP | ATTACK,
    RIGHT | JUMP | ATTACK,
]

BUTTON_LABELS = {
    LEFT: "L",
    RIGHT: "R",
    UP: "U",
    DOWN: "D",
    JUMP: "J",
    ATTACK: "A",
}


def _action_label(mask):
    if mask == 0:
        return "NONE"
    parts = [name for bit, name in BUTTON_LABELS.items() if (mask & bit) != 0]
    return "+".join(parts) if parts else "NONE"


ACTION_LABELS = [_action_label(mask) for mask in ACTION_MASKS]

OBS_KEYS = [
    "player_vx",
    "player_vy",
    "enemy_dx",
    "enemy_dy",
    "enemy_vx",
    "enemy_vy",
    "player_has_sword",
    "enemy_has_sword",
    "nearest_sword_dx",
    "nearest_sword_dy",
    "nearest_sword_visible",
    "player_x_norm",
    "player_y_norm",
    "player_has_go_arrow",
    "enemy_has_go_arrow",
    "player_dead",
    "enemy_dead",
    "go_arrow_known",
    "death_known",
    "player_goal_dir",
    "enemy_goal_dir",
    "goal_dir_known",
    "sim_time_scale_norm",
]

OBS_SIZE = len(OBS_KEYS)
ACTION_COUNT = len(ACTION_MASKS)
FRAME_SKIP = 2
STEP_DELAY = 0.016 * FRAME_SKIP

GAMMA = 0.995
LEARNING_RATE = 4e-4
BATCH_SIZE = 128
REPLAY_CAPACITY = 100_000
LEARN_START = 800
TARGET_SYNC_EVERY = 500
UPDATES_PER_STEP = 3
EPS_START = 1.0
EPS_END = 0.02
EPS_DECAY_STEPS = 120_000.0
PRIO_ALPHA = 0.6
PRIO_BETA_START = 0.4
PRIO_BETA_STEPS = 300_000.0
PRIO_EPS = 1e-3
SPEED_MIN = 0.05
SPEED_MAX = 100.0
SPEED_SYNC_EVERY_SEC = 0.25
STEP_DELAY_MIN_SEC = 0.004
TELEMETRY_LOG_EVERY_SEC = 1.0


def _as_float(value, default=0.0):
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def _as_int(value, default=0):
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def _as_bool(value, default=False):
    if isinstance(value, bool):
        return value
    if value is None:
        return default
    if isinstance(value, (int, float)):
        return value != 0
    if isinstance(value, str):
        text = value.strip().lower()
        if text in ("1", "true", "yes", "on", "y", "t"):
            return True
        if text in ("0", "false", "no", "off", "n", "f"):
            return False
    return default


def _goal_sign(value):
    v = _as_float(value, 0.0)
    if not math.isfinite(v):
        return 0.0
    if v > 0.25:
        return 1.0
    if v < -0.25:
        return -1.0
    return 0.0


def _sample_indices(total, target):
    if total <= 0:
        return []
    if total <= target:
        return list(range(total))
    if target <= 1:
        return [0]
    return [int(round(i * (total - 1) / float(target - 1))) for i in range(target)]


def _find_bool(state, keys):
    for key in keys:
        if key in state and state.get(key) is not None:
            return _as_bool(state.get(key), False), True
    return False, False


def _find_int(state, keys):
    for key in keys:
        if key in state and state.get(key) is not None:
            value = state.get(key)
            if isinstance(value, bool):
                continue
            try:
                return int(value), True
            except (TypeError, ValueError):
                continue
    return 0, False


def _canonicalize_state(raw_state):
    state = dict(raw_state)

    player_index, player_index_known = _find_int(state, ["player_index"])
    if not player_index_known:
        player_index = 0
    enemy_index, enemy_index_known = _find_int(state, ["enemy_index"])
    if not enemy_index_known:
        enemy_index = (player_index + 1) & 1

    state["player_index"] = player_index
    state["enemy_index"] = enemy_index

    player_go = False
    enemy_go = False
    player_go_known = False
    enemy_go_known = False
    leader_idx, leader_known = _find_int(state, ["leader_player_index", "leader_index"])
    if leader_known:
        player_go = leader_idx == player_index
        enemy_go = leader_idx == enemy_index
        player_go_known = True
        enemy_go_known = True
    else:
        player_go, player_go_known = _find_bool(
            state,
            [
                "player_has_go_arrow",
                "player_go_arrow",
                "player_is_leader",
                "is_leader",
                "has_go_arrow",
            ],
        )
        enemy_go, enemy_go_known = _find_bool(
            state,
            [
                "enemy_has_go_arrow",
                "enemy_go_arrow",
                "enemy_is_leader",
            ],
        )
        if player_go_known and player_go and not enemy_go_known:
            enemy_go = False
            enemy_go_known = True
        elif enemy_go_known and enemy_go and not player_go_known:
            player_go = False
            player_go_known = True

    player_dead, player_dead_known = _find_bool(
        state,
        [
            "player_dead",
            "player_is_dead",
            "is_dead",
        ],
    )
    enemy_dead, enemy_dead_known = _find_bool(
        state,
        [
            "enemy_dead",
            "enemy_is_dead",
        ],
    )

    player_state, player_state_known = _find_int(state, ["player_state", "player_anim_state"])
    enemy_state, enemy_state_known = _find_int(state, ["enemy_state", "enemy_anim_state"])
    if player_state_known:
        player_dead = player_state == 8
        player_dead_known = True
    if enemy_state_known:
        enemy_dead = enemy_state == 8
        enemy_dead_known = True

    loser_idx, loser_known = _find_int(state, ["loser_player_index", "loser_index"])
    if loser_known:
        player_dead = loser_idx == player_index
        enemy_dead = loser_idx == enemy_index
        player_dead_known = True
        enemy_dead_known = True

    state["player_has_go_arrow"] = bool(player_go)
    state["enemy_has_go_arrow"] = bool(enemy_go)
    if "go_arrow_known" in raw_state and raw_state.get("go_arrow_known") is not None:
        state["go_arrow_known"] = _as_bool(raw_state.get("go_arrow_known"), False)
    else:
        state["go_arrow_known"] = bool(player_go_known and enemy_go_known)

    state["player_dead"] = bool(player_dead)
    state["enemy_dead"] = bool(enemy_dead)
    if "death_known" in raw_state and raw_state.get("death_known") is not None:
        state["death_known"] = _as_bool(raw_state.get("death_known"), False)
    else:
        state["death_known"] = bool(player_dead_known and enemy_dead_known)

    player_goal_dir = _goal_sign(state.get("player_goal_dir"))
    enemy_goal_dir = _goal_sign(state.get("enemy_goal_dir"))
    goal_dir_known = _as_bool(state.get("goal_dir_known"), False)

    if player_goal_dir == 0.0 and "goal_dir" in state:
        player_goal_dir = _goal_sign(state.get("goal_dir"))
    if enemy_goal_dir == 0.0 and "enemy_goal_dir" not in state and "goal_dir" in state:
        enemy_goal_dir = _goal_sign(state.get("goal_dir"))

    if player_goal_dir == 0.0 and state.get("player_has_go_arrow", False):
        enemy_dx = _as_float(state.get("enemy_dx"), 0.0)
        if abs(enemy_dx) > 1.0:
            player_goal_dir = -1.0 if enemy_dx > 0 else 1.0
    if enemy_goal_dir == 0.0 and state.get("enemy_has_go_arrow", False):
        enemy_dx = _as_float(state.get("enemy_dx"), 0.0)
        if abs(enemy_dx) > 1.0:
            enemy_goal_dir = 1.0 if enemy_dx > 0 else -1.0

    if player_goal_dir != 0.0 or enemy_goal_dir != 0.0:
        goal_dir_known = True

    state["player_goal_dir"] = player_goal_dir
    state["enemy_goal_dir"] = enemy_goal_dir
    state["goal_dir_known"] = goal_dir_known

    speed_enabled, speed_enabled_known = _find_bool(state, ["speed_enabled", "sim_speed_enabled"])
    if not speed_enabled_known:
        speed_enabled = True

    speed_multiplier = _as_float(state.get("speed_multiplier"), 1.0)
    if not math.isfinite(speed_multiplier):
        speed_multiplier = 1.0
    speed_multiplier = max(SPEED_MIN, min(SPEED_MAX, speed_multiplier))

    sim_time_scale = _as_float(
        state.get("sim_time_scale"),
        speed_multiplier if speed_enabled else 1.0,
    )
    if not math.isfinite(sim_time_scale) or sim_time_scale <= 0.0:
        sim_time_scale = speed_multiplier if speed_enabled else 1.0
    sim_time_scale = max(SPEED_MIN, min(SPEED_MAX, sim_time_scale))

    state["speed_enabled"] = bool(speed_enabled)
    state["speed_multiplier"] = speed_multiplier
    state["sim_time_scale"] = sim_time_scale

    return state


def _mirror_state(state):
    player_x = _as_float(state.get("player_x"), 0.0)
    player_y = _as_float(state.get("player_y"), 0.0)
    enemy_dx = _as_float(state.get("enemy_dx"), 0.0)
    enemy_dy = _as_float(state.get("enemy_dy"), 0.0)

    enemy_x = player_x + enemy_dx
    enemy_y = player_y + enemy_dy

    sword_dx = state.get("nearest_sword_dx")
    sword_dy = state.get("nearest_sword_dy")
    if sword_dx is None or sword_dy is None:
        mirrored_sword_dx = None
        mirrored_sword_dy = None
    else:
        mirrored_sword_dx = _as_float(sword_dx) - enemy_dx
        mirrored_sword_dy = _as_float(sword_dy) - enemy_dy

    return {
        "player_x": enemy_x,
        "player_y": enemy_y,
        "player_vx": _as_float(state.get("enemy_vx"), 0.0),
        "player_vy": _as_float(state.get("enemy_vy"), 0.0),
        "enemy_dx": -enemy_dx,
        "enemy_dy": -enemy_dy,
        "enemy_vx": _as_float(state.get("player_vx"), 0.0),
        "enemy_vy": _as_float(state.get("player_vy"), 0.0),
        "player_has_sword": bool(state.get("enemy_has_sword", False)),
        "enemy_has_sword": bool(state.get("player_has_sword", False)),
        "player_has_go_arrow": bool(state.get("enemy_has_go_arrow", False)),
        "enemy_has_go_arrow": bool(state.get("player_has_go_arrow", False)),
        "player_dead": bool(state.get("enemy_dead", False)),
        "enemy_dead": bool(state.get("player_dead", False)),
        "go_arrow_known": bool(state.get("go_arrow_known", False)),
        "death_known": bool(state.get("death_known", False)),
        "player_goal_dir": _goal_sign(state.get("enemy_goal_dir")),
        "enemy_goal_dir": _goal_sign(state.get("player_goal_dir")),
        "goal_dir_known": bool(state.get("goal_dir_known", False)),
        "speed_enabled": bool(state.get("speed_enabled", True)),
        "speed_multiplier": _as_float(state.get("speed_multiplier"), 1.0),
        "sim_time_scale": _as_float(state.get("sim_time_scale"), 1.0),
        "player_index": state.get("enemy_index", 1),
        "enemy_index": state.get("player_index", 0),
        "nearest_sword_dx": mirrored_sword_dx,
        "nearest_sword_dy": mirrored_sword_dy,
        "room_width": state.get("room_width"),
        "room_height": state.get("room_height"),
        "in_game": state.get("in_game", False),
    }


def _encode_observation(state):
    room_w = max(_as_float(state.get("room_width"), 320.0), 1.0)
    room_h = max(_as_float(state.get("room_height"), 240.0), 1.0)

    sword_dx = state.get("nearest_sword_dx")
    sword_dy = state.get("nearest_sword_dy")
    sword_visible = 1.0 if sword_dx is not None and sword_dy is not None else 0.0
    sword_dx_norm = _as_float(sword_dx, 0.0) / room_w if sword_visible else 0.0
    sword_dy_norm = _as_float(sword_dy, 0.0) / room_h if sword_visible else 0.0
    go_arrow_known = 1.0 if state.get("go_arrow_known", False) else 0.0
    death_known = 1.0 if state.get("death_known", False) else 0.0
    player_goal_dir = _goal_sign(state.get("player_goal_dir"))
    enemy_goal_dir = _goal_sign(state.get("enemy_goal_dir"))
    goal_dir_known = 1.0 if state.get("goal_dir_known", False) else 0.0
    sim_time_scale = _as_float(state.get("sim_time_scale"), 1.0)
    if not math.isfinite(sim_time_scale) or sim_time_scale <= 0.0:
        sim_time_scale = 1.0
    sim_time_scale_norm = min(sim_time_scale, SPEED_MAX) / SPEED_MAX

    return [
        _as_float(state.get("player_vx"), 0.0) / 10.0,
        _as_float(state.get("player_vy"), 0.0) / 10.0,
        _as_float(state.get("enemy_dx"), 0.0) / room_w,
        _as_float(state.get("enemy_dy"), 0.0) / room_h,
        _as_float(state.get("enemy_vx"), 0.0) / 10.0,
        _as_float(state.get("enemy_vy"), 0.0) / 10.0,
        1.0 if state.get("player_has_sword", False) else 0.0,
        1.0 if state.get("enemy_has_sword", False) else 0.0,
        sword_dx_norm,
        sword_dy_norm,
        sword_visible,
        _as_float(state.get("player_x"), 0.0) / room_w,
        _as_float(state.get("player_y"), 0.0) / room_h,
        1.0 if state.get("player_has_go_arrow", False) else 0.0,
        1.0 if state.get("enemy_has_go_arrow", False) else 0.0,
        1.0 if state.get("player_dead", False) else 0.0,
        1.0 if state.get("enemy_dead", False) else 0.0,
        go_arrow_known,
        death_known,
        player_goal_dir,
        enemy_goal_dir,
        goal_dir_known,
        sim_time_scale_norm,
    ]


def _shape_reward(prev_state, next_state, action_mask):
    reward = -0.002

    room_w = max(_as_float(next_state.get("room_width"), 320.0), 1.0)
    prev_enemy_dx_abs = abs(_as_float(prev_state.get("enemy_dx"), 0.0))
    next_enemy_dx_abs = abs(_as_float(next_state.get("enemy_dx"), 0.0))
    reward += (prev_enemy_dx_abs - next_enemy_dx_abs) * 0.004

    prev_has_sword = bool(prev_state.get("player_has_sword", False))
    next_has_sword = bool(next_state.get("player_has_sword", False))
    if not prev_has_sword and next_has_sword:
        reward += 1.6
    if prev_has_sword and not next_has_sword:
        reward -= 1.4

    if not prev_has_sword:
        prev_sx = prev_state.get("nearest_sword_dx")
        prev_sy = prev_state.get("nearest_sword_dy")
        next_sx = next_state.get("nearest_sword_dx")
        next_sy = next_state.get("nearest_sword_dy")
        if (
            prev_sx is not None
            and prev_sy is not None
            and next_sx is not None
            and next_sy is not None
        ):
            prev_dist = math.hypot(_as_float(prev_sx), _as_float(prev_sy))
            next_dist = math.hypot(_as_float(next_sx), _as_float(next_sy))
            reward += (prev_dist - next_dist) * 0.015

    if next_state.get("enemy_has_sword", False) and not next_has_sword:
        reward -= 0.1

    player_x_prev = _as_float(prev_state.get("player_x"), 0.0)
    player_x_next = _as_float(next_state.get("player_x"), 0.0)
    enemy_x_prev = player_x_prev + _as_float(prev_state.get("enemy_dx"), 0.0)
    enemy_x_next = player_x_next + _as_float(next_state.get("enemy_dx"), 0.0)
    room_delta = _as_int(next_state.get("room_index"), 0) - _as_int(prev_state.get("room_index"), 0)
    player_progress_x = player_x_next - player_x_prev
    enemy_progress_x = enemy_x_next - enemy_x_prev

    player_has_go = bool(next_state.get("player_has_go_arrow", False))
    enemy_has_go = bool(next_state.get("enemy_has_go_arrow", False))
    player_idx = _as_int(next_state.get("player_index"), 0) & 1
    enemy_idx = _as_int(next_state.get("enemy_index"), (player_idx + 1)) & 1
    desired_player_dir = 1.0 if player_idx == 0 else -1.0
    desired_enemy_dir = 1.0 if enemy_idx == 0 else -1.0
    player_vx_next = _as_float(next_state.get("player_vx"), 0.0)
    enemy_vx_next = _as_float(next_state.get("enemy_vx"), 0.0)

    if player_has_go:
        reward += (player_progress_x / room_w) * desired_player_dir * 9.0
        reward += float(room_delta) * desired_player_dir * 3.5
        reward += (player_vx_next / 10.0) * desired_player_dir * 2.5
        if (player_vx_next * desired_player_dir) > 1.2:
            reward += 0.18
        elif (player_vx_next * desired_player_dir) < -0.8:
            reward -= 0.28
        if desired_player_dir > 0.0:
            if (action_mask & RIGHT) and not (action_mask & LEFT):
                reward += 0.08
            if (action_mask & LEFT):
                reward -= 0.12
        else:
            if (action_mask & LEFT) and not (action_mask & RIGHT):
                reward += 0.08
            if (action_mask & RIGHT):
                reward -= 0.12
        reward += 0.02

    if enemy_has_go:
        reward -= (enemy_progress_x / room_w) * desired_enemy_dir * 7.0
        reward -= float(room_delta) * desired_enemy_dir * 2.5
        reward -= (enemy_vx_next / 10.0) * desired_enemy_dir * 1.8
        if (enemy_vx_next * desired_enemy_dir) > 1.2:
            reward -= 0.14
        elif (enemy_vx_next * desired_enemy_dir) < -0.8:
            reward += 0.08
        reward -= 0.02

    if enemy_has_go and not player_has_go:
        reward += (prev_enemy_dx_abs - next_enemy_dx_abs) * 0.006

    prev_enemy_dead = bool(prev_state.get("enemy_dead", False))
    next_enemy_dead = bool(next_state.get("enemy_dead", False))
    prev_player_dead = bool(prev_state.get("player_dead", False))
    next_player_dead = bool(next_state.get("player_dead", False))
    if not prev_enemy_dead and next_enemy_dead:
        reward += 8.0
    if not prev_player_dead and next_player_dead:
        reward -= 8.0

    if action_mask == 0:
        reward -= 0.01
    if (action_mask & ATTACK) and next_enemy_dx_abs < (0.09 * room_w):
        reward += 0.05
    if abs(_as_float(next_state.get("player_vx"), 0.0)) < 0.05 and next_enemy_dx_abs > (0.18 * room_w):
        reward -= 0.01

    return max(-10.0, min(10.0, reward))


@dataclass
class Transition:
    obs: tuple
    action: int
    reward: float
    next_obs: tuple
    done: bool


class ReplayBuffer:
    def __init__(
        self,
        capacity,
        alpha=PRIO_ALPHA,
        beta_start=PRIO_BETA_START,
        beta_steps=PRIO_BETA_STEPS,
    ):
        self.data = deque(maxlen=capacity)
        self.priorities = deque(maxlen=capacity)
        self.alpha = alpha
        self.beta_start = beta_start
        self.beta_steps = max(1.0, float(beta_steps))
        self.max_priority = 1.0

    def add(self, obs, action, reward, next_obs, done):
        row = Transition(tuple(obs), int(action), float(reward), tuple(next_obs), bool(done))
        self.data.append(row)
        self.priorities.append(self.max_priority)

    def _beta(self, step_count):
        t = max(0.0, float(step_count))
        mix = min(1.0, t / self.beta_steps)
        return self.beta_start + (1.0 - self.beta_start) * mix

    def sample(self, batch_size, step_count):
        n = len(self.data)
        if n == 0:
            return None

        raw = [max(float(p), PRIO_EPS) for p in self.priorities]
        scaled = [p ** self.alpha for p in raw]
        total = sum(scaled)
        if total <= 0.0:
            scaled = [1.0] * n
            total = float(n)

        idxs = random.choices(range(n), weights=scaled, k=batch_size)
        batch = [self.data[i] for i in idxs]

        obs = [row.obs for row in batch]
        actions = [row.action for row in batch]
        rewards = [row.reward for row in batch]
        next_obs = [row.next_obs for row in batch]
        dones = [row.done for row in batch]
        probs = [scaled[i] / total for i in idxs]
        beta = self._beta(step_count)
        weights = [(n * max(p, 1e-12)) ** (-beta) for p in probs]
        max_w = max(weights) if weights else 1.0
        if max_w <= 0.0:
            max_w = 1.0
        weights = [w / max_w for w in weights]

        return obs, actions, rewards, next_obs, dones, idxs, weights

    def update_priorities(self, idxs, td_errors):
        for idx, err in zip(idxs, td_errors):
            if idx < 0 or idx >= len(self.priorities):
                continue
            p = abs(float(err)) + PRIO_EPS
            self.priorities[idx] = p
            if p > self.max_priority:
                self.max_priority = p

    def __len__(self):
        return len(self.data)


class QNet(nn.Module):
    def __init__(self, obs_size, action_count):
        super().__init__()
        self.net = nn.Sequential(
            nn.Linear(obs_size, 256),
            nn.ReLU(),
            nn.Linear(256, 256),
            nn.ReLU(),
            nn.Linear(256, action_count),
        )

    def forward(self, x):
        return self.net(x)


class SelfPlayTrainer:
    def __init__(self, status_callback):
        self.status_callback = status_callback
        self.stop_event = threading.Event()
        self.model_lock = threading.Lock()
        self.control_lock = threading.Lock()
        self.viz_lock = threading.Lock()

        self.device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
        self.policy_net = QNet(OBS_SIZE, ACTION_COUNT).to(self.device)
        self.target_net = QNet(OBS_SIZE, ACTION_COUNT).to(self.device)
        self.target_net.load_state_dict(self.policy_net.state_dict())
        self.target_net.eval()

        self.optimizer = optim.Adam(self.policy_net.parameters(), lr=LEARNING_RATE)
        self.replay = ReplayBuffer(REPLAY_CAPACITY)
        self.session = requests.Session()

        self.step_count = 0
        self.episode_count = 0
        self.update_count = 0
        self.epsilon = EPS_START
        self.loss_ema = None
        self.reward_ema = None
        self.speed_multiplier = 1.0
        self.speed_enabled = True
        self.sim_time_scale = 1.0
        self.desired_speed_multiplier = 1.0
        self.desired_speed_enabled = True
        self.speed_dirty = True
        self.last_speed_sync = 0.0
        self.last_telemetry_log = 0.0
        self.expected_telemetry_keys = {
            "player_has_sword",
            "enemy_has_sword",
            "player_has_go_arrow",
            "enemy_has_go_arrow",
            "player_dead",
            "enemy_dead",
            "go_arrow_known",
            "death_known",
            "player_goal_dir",
            "enemy_goal_dir",
            "goal_dir_known",
            "speed_enabled",
            "speed_multiplier",
            "sim_time_scale",
        }
        self.speed_sync_error = ""
        self.viz_obs = [0.0] * OBS_SIZE
        self.viz_q = [0.0] * ACTION_COUNT
        self.viz_action = 0
        self.viz_step = 0

    def _get_state(self):
        try:
            return self.session.get(f"{BASE}/state", timeout=0.2).json()
        except Exception:
            return None

    def set_speed_control(self, speed_multiplier=None, speed_enabled=None):
        with self.control_lock:
            if speed_multiplier is not None:
                clamped = max(SPEED_MIN, min(SPEED_MAX, _as_float(speed_multiplier, 1.0)))
                if abs(self.desired_speed_multiplier - clamped) > 1e-6:
                    self.desired_speed_multiplier = clamped
                    self.speed_dirty = True
            if speed_enabled is not None:
                enabled = bool(speed_enabled)
                if self.desired_speed_enabled != enabled:
                    self.desired_speed_enabled = enabled
                    self.speed_dirty = True

    def _sync_speed(self, force=False):
        now = time.time()
        if not force and (now - self.last_speed_sync) < SPEED_SYNC_EVERY_SEC:
            return

        with self.control_lock:
            desired_speed = self.desired_speed_multiplier
            desired_enabled = self.desired_speed_enabled
            dirty = self.speed_dirty

        if not force and not dirty:
            self.last_speed_sync = now
            return

        params = {
            "value": f"{desired_speed:.6g}",
            "enabled": 1 if desired_enabled else 0,
        }
        try:
            resp = self.session.get(f"{BASE}/speed", params=params, timeout=0.2)
            payload = resp.json()
            if isinstance(payload, dict) and payload.get("ok", False):
                self.speed_multiplier = max(
                    SPEED_MIN,
                    min(SPEED_MAX, _as_float(payload.get("speed_multiplier"), desired_speed)),
                )
                self.speed_enabled = _as_bool(payload.get("speed_enabled"), desired_enabled)
                self.sim_time_scale = max(
                    SPEED_MIN,
                    min(SPEED_MAX, _as_float(payload.get("sim_time_scale"), self.sim_time_scale)),
                )
                with self.control_lock:
                    self.speed_dirty = False
                self.speed_sync_error = ""
            else:
                self.speed_sync_error = "speed endpoint returned non-ok payload"
        except Exception:
            self.speed_sync_error = "speed endpoint unavailable"

        self.last_speed_sync = now

    def _send_input(self, player, mask):
        try:
            self.session.get(
                f"{BASE}/input",
                params={
                    "player": int(player),
                    "mask": int(mask),
                    "frames": -1,
                    "replace": 1,
                },
                timeout=0.08,
            )
        except Exception:
            pass

    def _clear_inputs(self):
        for player in (0, 1):
            try:
                self.session.get(
                    f"{BASE}/input/clear",
                    params={"player": player},
                    timeout=0.08,
                )
            except Exception:
                pass

    def _step_delay(self):
        sim_scale = max(1.0, _as_float(self.sim_time_scale, 1.0))
        return max(STEP_DELAY_MIN_SEC, STEP_DELAY / sim_scale)

    def _print_telemetry(self, raw_state, state):
        now = time.time()
        if (now - self.last_telemetry_log) < TELEMETRY_LOG_EVERY_SEC:
            return
        self.last_telemetry_log = now

        missing = [key for key in sorted(self.expected_telemetry_keys) if key not in raw_state]
        if missing:
            print("[telemetry] missing keys:", ", ".join(missing), flush=True)

        sword_dx = state.get("nearest_sword_dx")
        sword_dy = state.get("nearest_sword_dy")
        if sword_dx is None or sword_dy is None:
            sword_text = "none"
        else:
            sword_text = f"{_as_float(sword_dx, 0.0):.2f},{_as_float(sword_dy, 0.0):.2f}"

        print(
            "[telemetry]",
            f"in_game={int(_as_bool(state.get('in_game'), False))}",
            f"room={_as_int(state.get('room_index'), -1)}",
            f"p_idx={_as_int(state.get('player_index'), 0)}",
            f"dx={_as_float(state.get('enemy_dx'), 0.0):.2f}",
            f"dy={_as_float(state.get('enemy_dy'), 0.0):.2f}",
            f"pv=({_as_float(state.get('player_vx'), 0.0):.2f},{_as_float(state.get('player_vy'), 0.0):.2f})",
            f"ev=({_as_float(state.get('enemy_vx'), 0.0):.2f},{_as_float(state.get('enemy_vy'), 0.0):.2f})",
            f"hold={int(_as_bool(state.get('player_has_sword'), False))}/{int(_as_bool(state.get('enemy_has_sword'), False))}",
            f"sword={sword_text}",
            f"go={int(_as_bool(state.get('player_has_go_arrow'), False))}/{int(_as_bool(state.get('enemy_has_go_arrow'), False))}",
            f"dead={int(_as_bool(state.get('player_dead'), False))}/{int(_as_bool(state.get('enemy_dead'), False))}",
            f"known_go={int(_as_bool(state.get('go_arrow_known'), False))}",
            f"known_dead={int(_as_bool(state.get('death_known'), False))}",
            f"gdir={_goal_sign(state.get('player_goal_dir')):.0f}/{_goal_sign(state.get('enemy_goal_dir')):.0f}",
            f"leader={state.get('leader_player_index', 'n/a')}",
            f"loser={state.get('loser_player_index', 'n/a')}",
            f"speed={_as_float(state.get('sim_time_scale'), 1.0):.2f}x",
            f"sync={'ok' if not self.speed_sync_error else self.speed_sync_error}",
            flush=True,
        )

    def _choose_action(self, obs):
        obs_tensor = torch.tensor([obs], dtype=torch.float32, device=self.device)
        with self.model_lock:
            with torch.no_grad():
                q_values = self.policy_net(obs_tensor).squeeze(0)

        greedy_action = int(torch.argmax(q_values).item())
        if random.random() < self.epsilon:
            action = random.randrange(ACTION_COUNT)
        else:
            action = greedy_action
        return action, q_values.detach().cpu().tolist()

    def _update_viz_cache(self, obs, q_values, action):
        with self.viz_lock:
            self.viz_obs = list(obs)
            self.viz_q = list(q_values)
            self.viz_action = int(action)
            self.viz_step = int(self.step_count)

    def _train_batch(self):
        if len(self.replay) < max(BATCH_SIZE, LEARN_START):
            return None

        batch = self.replay.sample(BATCH_SIZE, self.step_count)
        if batch is None:
            return None
        obs, actions, rewards, next_obs, dones, idxs, is_weights = batch

        obs_tensor = torch.tensor(obs, dtype=torch.float32, device=self.device)
        action_tensor = torch.tensor(actions, dtype=torch.int64, device=self.device).unsqueeze(1)
        reward_tensor = torch.tensor(rewards, dtype=torch.float32, device=self.device)
        next_obs_tensor = torch.tensor(next_obs, dtype=torch.float32, device=self.device)
        done_tensor = torch.tensor(dones, dtype=torch.float32, device=self.device)
        is_weight_tensor = torch.tensor(is_weights, dtype=torch.float32, device=self.device)

        with self.model_lock:
            q_values = self.policy_net(obs_tensor).gather(1, action_tensor).squeeze(1)
            with torch.no_grad():
                next_actions = self.policy_net(next_obs_tensor).argmax(1, keepdim=True)
                next_q_values = self.target_net(next_obs_tensor).gather(1, next_actions).squeeze(1)
                q_target = reward_tensor + GAMMA * (1.0 - done_tensor) * next_q_values

            td_error = q_values - q_target
            per_sample_loss = F.smooth_l1_loss(q_values, q_target, reduction="none")
            loss = (is_weight_tensor * per_sample_loss).mean()
            self.optimizer.zero_grad(set_to_none=True)
            loss.backward()
            nn.utils.clip_grad_norm_(self.policy_net.parameters(), 5.0)
            self.optimizer.step()

            td_abs = td_error.detach().abs().cpu().tolist()
            self.replay.update_priorities(idxs, td_abs)

            if self.step_count % TARGET_SYNC_EVERY == 0:
                self.target_net.load_state_dict(self.policy_net.state_dict())

        return float(loss.item())

    def _push_status(self, message):
        self.status_callback(
            {
                "message": message,
                "device": str(self.device),
                "steps": self.step_count,
                "episodes": self.episode_count,
                "updates": self.update_count,
                "epsilon": self.epsilon,
                "buffer": len(self.replay),
                "loss_ema": self.loss_ema,
                "reward_ema": self.reward_ema,
                "speed_enabled": self.speed_enabled,
                "speed_multiplier": self.speed_multiplier,
                "sim_time_scale": self.sim_time_scale,
                "speed_sync_error": self.speed_sync_error,
            }
        )

    def export_model(self, model_path):
        target_path = Path(model_path)
        script_path = target_path.with_suffix(".pt")

        with self.model_lock:
            cpu_state_dict = {
                key: value.detach().cpu() for key, value in self.policy_net.state_dict().items()
            }

        payload = {
            "model_state_dict": cpu_state_dict,
            "obs_keys": OBS_KEYS,
            "action_masks": ACTION_MASKS,
            "steps": self.step_count,
            "episodes": self.episode_count,
            "exported_at_epoch": time.time(),
        }
        torch.save(payload, target_path)

        scripted_model = QNet(OBS_SIZE, ACTION_COUNT)
        scripted_model.load_state_dict(cpu_state_dict)
        scripted_model.eval()
        torch.jit.script(scripted_model).save(str(script_path))

        return str(target_path), str(script_path)

    def get_visualization_data(self, max_hidden=18):
        with self.viz_lock:
            obs = list(self.viz_obs)
            selected_action = int(self.viz_action)
            step = int(self.viz_step)
            epsilon = float(self.epsilon)

        if len(obs) != OBS_SIZE:
            obs = [0.0] * OBS_SIZE

        obs_tensor = torch.tensor([obs], dtype=torch.float32, device=self.device)

        with self.model_lock:
            with torch.no_grad():
                fc1 = self.policy_net.net[0]
                fc2 = self.policy_net.net[2]
                fc3 = self.policy_net.net[4]

                h1 = F.relu(fc1(obs_tensor)).squeeze(0)
                h2 = F.relu(fc2(h1.unsqueeze(0))).squeeze(0)
                q = fc3(h2.unsqueeze(0)).squeeze(0)

                h1_idx = _sample_indices(fc1.out_features, max_hidden)
                h2_idx = _sample_indices(fc2.out_features, max_hidden)

                w1 = fc1.weight[h1_idx, :].detach().cpu().tolist()  # [h1][in]
                w2 = fc2.weight[h2_idx][:, h1_idx].detach().cpu().tolist()  # [h2][h1]
                w3 = fc3.weight[:, h2_idx].detach().cpu().tolist()  # [out][h2]

                h1_sel = h1[h1_idx].detach().cpu().tolist()
                h2_sel = h2[h2_idx].detach().cpu().tolist()
                q_vals = q.detach().cpu().tolist()

        return {
            "step": step,
            "epsilon": epsilon,
            "obs_keys": OBS_KEYS,
            "obs": obs,
            "h1": h1_sel,
            "h2": h2_sel,
            "q": q_vals,
            "selected_action": selected_action,
            "action_labels": ACTION_LABELS,
            "w1": w1,
            "w2": w2,
            "w3": w3,
        }

    def run(self):
        self._sync_speed(force=True)
        self._push_status("trainer started")
        while not self.stop_event.is_set():
            self._sync_speed(force=False)
            current_raw = self._get_state()
            if current_raw is None:
                self._push_status("waiting for telemetry_http /state")
                time.sleep(0.2)
                continue
            current = _canonicalize_state(current_raw)
            self.speed_enabled = _as_bool(current.get("speed_enabled"), self.speed_enabled)
            self.speed_multiplier = max(
                SPEED_MIN,
                min(SPEED_MAX, _as_float(current.get("speed_multiplier"), self.speed_multiplier)),
            )
            self.sim_time_scale = max(
                SPEED_MIN,
                min(SPEED_MAX, _as_float(current.get("sim_time_scale"), self.sim_time_scale)),
            )
            self._print_telemetry(current_raw, current)

            if not current.get("in_game", False):
                self._clear_inputs()
                self._push_status("in menu, waiting for round")
                time.sleep(0.1)
                continue

            obs0_state = current
            obs1_state = _mirror_state(current)
            obs0 = _encode_observation(obs0_state)
            obs1 = _encode_observation(obs1_state)

            self.epsilon = EPS_END + (EPS_START - EPS_END) * math.exp(
                -float(self.step_count) / EPS_DECAY_STEPS
            )

            action0, q_values0 = self._choose_action(obs0)
            action1, _ = self._choose_action(obs1)
            self._update_viz_cache(obs0, q_values0, action0)

            self._send_input(0, ACTION_MASKS[action0])
            self._send_input(1, ACTION_MASKS[action1])
            time.sleep(self._step_delay())

            next_state_raw = self._get_state()
            if next_state_raw is None:
                continue
            next_state = _canonicalize_state(next_state_raw)

            done = not next_state.get("in_game", False)
            next0_state = next_state if not done else obs0_state
            next1_state = _mirror_state(next_state) if not done else obs1_state
            next_obs0 = _encode_observation(next0_state)
            next_obs1 = _encode_observation(next1_state)

            reward0 = _shape_reward(obs0_state, next0_state, ACTION_MASKS[action0])
            reward1 = _shape_reward(obs1_state, next1_state, ACTION_MASKS[action1])

            self.replay.add(obs0, action0, reward0, next_obs0, done)
            self.replay.add(obs1, action1, reward1, next_obs1, done)

            self.step_count += 1
            if done:
                self.episode_count += 1
                self._clear_inputs()

            step_reward = 0.5 * (reward0 + reward1)
            if self.reward_ema is None:
                self.reward_ema = step_reward
            else:
                self.reward_ema = self.reward_ema * 0.98 + step_reward * 0.02

            for _ in range(UPDATES_PER_STEP):
                loss = self._train_batch()
                if loss is None:
                    break
                self.update_count += 1
                if self.loss_ema is None:
                    self.loss_ema = loss
                else:
                    self.loss_ema = self.loss_ema * 0.98 + loss * 0.02

            if self.step_count % 25 == 0:
                self._push_status("training self-play")

        self._clear_inputs()
        self._push_status("trainer stopped")

    def stop(self):
        self.stop_event.set()


class TrainerApp:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("Eggnogg+ Self-Play Trainer")
        self.root.geometry("1200x880")
        self.root.resizable(True, True)

        self.status_queue = Queue()
        self.status_var = tk.StringVar(value="Initializing trainer...")
        self.stats_var = tk.StringVar(value="")
        self.loss_var = tk.StringVar(value="loss_ema: n/a")
        self.reward_var = tk.StringVar(value="reward_ema: n/a")
        self.speed_status_var = tk.StringVar(value="sim_speed: 1.00x | trainer_speed: 1.00x | enabled: on")
        self.viz_status_var = tk.StringVar(value="network: waiting for model activity")
        self.export_var = tk.StringVar(value="")
        self.speed_var = tk.DoubleVar(value=1.0)
        self.speed_enabled_var = tk.BooleanVar(value=True)

        self.trainer = None

        self._build_ui()

        self.trainer = SelfPlayTrainer(self.status_queue.put)
        self._apply_speed_controls()
        self.worker = threading.Thread(target=self.trainer.run, daemon=True)
        self.worker.start()

        self.root.after(100, self._drain_status_queue)
        self.root.after(200, self._update_network_viz)
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    def _build_ui(self):
        frame = tk.Frame(self.root, padx=12, pady=12)
        frame.pack(fill="both", expand=True)

        tk.Label(frame, textvariable=self.status_var, anchor="w", font=("Consolas", 10, "bold")).pack(
            fill="x", pady=(0, 8)
        )
        tk.Label(frame, textvariable=self.stats_var, anchor="w", font=("Consolas", 10)).pack(
            fill="x", pady=(0, 4)
        )
        tk.Label(frame, textvariable=self.loss_var, anchor="w", font=("Consolas", 10)).pack(
            fill="x", pady=(0, 4)
        )
        tk.Label(frame, textvariable=self.reward_var, anchor="w", font=("Consolas", 10)).pack(
            fill="x", pady=(0, 12)
        )

        speed_box = tk.LabelFrame(frame, text="Simulation Speed", padx=8, pady=8)
        speed_box.pack(fill="x", pady=(0, 12))

        tk.Checkbutton(
            speed_box,
            text="Enable speed scaling",
            variable=self.speed_enabled_var,
            command=self._on_speed_toggle,
            font=("Consolas", 10),
        ).pack(anchor="w", pady=(0, 4))

        tk.Scale(
            speed_box,
            from_=SPEED_MIN,
            to=SPEED_MAX,
            resolution=0.05,
            orient="horizontal",
            variable=self.speed_var,
            command=self._on_speed_slider,
            length=1040,
            showvalue=True,
            label="Target speed multiplier (x)",
            font=("Consolas", 9),
        ).pack(fill="x")

        preset_row = tk.Frame(speed_box)
        preset_row.pack(fill="x", pady=(4, 2))
        for value in (1.0, 5.0, 10.0, 20.0, 50.0, 100.0):
            tk.Button(
                preset_row,
                text=f"{value:.0f}x",
                width=7,
                command=lambda v=value: self._set_speed_preset(v),
            ).pack(side="left", padx=(0, 6))

        tk.Label(speed_box, textvariable=self.speed_status_var, anchor="w", font=("Consolas", 10)).pack(
            fill="x", pady=(6, 0)
        )

        tk.Button(
            frame,
            text="Export Torch7 Model (.t7)",
            command=self._export_model,
            width=28,
            height=2,
        ).pack(anchor="w")

        tk.Label(frame, textvariable=self.export_var, anchor="w", font=("Consolas", 9)).pack(
            fill="x", pady=(8, 0)
        )

        viz_box = tk.LabelFrame(frame, text="Neural Network (Live)", padx=8, pady=8)
        viz_box.pack(fill="both", expand=True, pady=(10, 0))
        tk.Label(viz_box, textvariable=self.viz_status_var, anchor="w", font=("Consolas", 9)).pack(
            fill="x", pady=(0, 6)
        )
        self.viz_canvas = tk.Canvas(
            viz_box,
            bg="#0d1117",
            highlightthickness=1,
            highlightbackground="#2a3441",
            width=1120,
            height=420,
        )
        self.viz_canvas.pack(fill="both", expand=True)

    def _apply_speed_controls(self):
        if self.trainer is None:
            return
        self.trainer.set_speed_control(
            speed_multiplier=self.speed_var.get(),
            speed_enabled=self.speed_enabled_var.get(),
        )

    def _on_speed_slider(self, _value=None):
        self._apply_speed_controls()

    def _on_speed_toggle(self):
        self._apply_speed_controls()

    def _set_speed_preset(self, value):
        self.speed_var.set(value)
        self._apply_speed_controls()

    def _layer_positions(self, x, count, top, bottom):
        if count <= 0:
            return []
        if count == 1:
            return [(x, 0.5 * (top + bottom))]
        step = (bottom - top) / float(count - 1)
        return [(x, top + i * step) for i in range(count)]

    def _weight_color(self, w):
        mag = min(1.0, abs(float(w)) * 3.0)
        level = int(40 + 215 * mag)
        if w >= 0.0:
            return f"#1e{level:02x}7a"
        return f"#{level:02x}4a3a"

    def _value_color(self, v, vmax):
        if vmax <= 1e-8:
            vmax = 1.0
        ratio = min(1.0, abs(float(v)) / vmax)
        level = int(50 + 205 * ratio)
        if v >= 0.0:
            return f"#2b{level:02x}ff"
        return f"#{level:02x}8a3f"

    def _draw_network_graph(self, data):
        c = self.viz_canvas
        c.delete("all")

        width = max(320, int(c.winfo_width() or 1120))
        height = max(220, int(c.winfo_height() or 420))
        margin_x = 70
        top = 36
        bottom = height - 28

        x_input = margin_x
        x_h1 = int(width * 0.34)
        x_h2 = int(width * 0.62)
        x_out = width - margin_x

        obs = data["obs"]
        h1 = data["h1"]
        h2 = data["h2"]
        q = data["q"]
        w1 = data["w1"]
        w2 = data["w2"]
        w3 = data["w3"]
        action_idx = int(data["selected_action"])
        action_labels = data["action_labels"]

        pos_in = self._layer_positions(x_input, len(obs), top, bottom)
        pos_h1 = self._layer_positions(x_h1, len(h1), top, bottom)
        pos_h2 = self._layer_positions(x_h2, len(h2), top, bottom)
        pos_out = self._layer_positions(x_out, len(q), top, bottom)

        for j, (_, y2) in enumerate(pos_h1):
            for i, (_, y1) in enumerate(pos_in):
                w = w1[j][i]
                if abs(w) < 0.08:
                    continue
                c.create_line(x_input, y1, x_h1, y2, fill=self._weight_color(w), width=1)

        for j, (_, y2) in enumerate(pos_h2):
            for i, (_, y1) in enumerate(pos_h1):
                w = w2[j][i]
                if abs(w) < 0.08:
                    continue
                c.create_line(x_h1, y1, x_h2, y2, fill=self._weight_color(w), width=1)

        for j, (_, y2) in enumerate(pos_out):
            for i, (_, y1) in enumerate(pos_h2):
                w = w3[j][i]
                if abs(w) < 0.06:
                    continue
                c.create_line(x_h2, y1, x_out, y2, fill=self._weight_color(w), width=1)

        obs_max = max([abs(v) for v in obs] + [1e-6])
        h1_max = max([abs(v) for v in h1] + [1e-6])
        h2_max = max([abs(v) for v in h2] + [1e-6])
        q_max = max([abs(v) for v in q] + [1e-6])

        for i, (x, y) in enumerate(pos_in):
            color = self._value_color(obs[i], obs_max)
            c.create_oval(x - 4, y - 4, x + 4, y + 4, fill=color, outline="#cad3de", width=1)

        for i, (x, y) in enumerate(pos_h1):
            color = self._value_color(h1[i], h1_max)
            c.create_oval(x - 5, y - 5, x + 5, y + 5, fill=color, outline="#cad3de", width=1)

        for i, (x, y) in enumerate(pos_h2):
            color = self._value_color(h2[i], h2_max)
            c.create_oval(x - 5, y - 5, x + 5, y + 5, fill=color, outline="#cad3de", width=1)

        for i, (x, y) in enumerate(pos_out):
            color = self._value_color(q[i], q_max)
            if i == action_idx:
                c.create_oval(x - 7, y - 7, x + 7, y + 7, fill=color, outline="#ffd166", width=2)
            else:
                c.create_oval(x - 5, y - 5, x + 5, y + 5, fill=color, outline="#cad3de", width=1)

        c.create_text(x_input, 14, text="Input", fill="#d0d7de", font=("Consolas", 10, "bold"))
        c.create_text(x_h1, 14, text="Hidden 1", fill="#d0d7de", font=("Consolas", 10, "bold"))
        c.create_text(x_h2, 14, text="Hidden 2", fill="#d0d7de", font=("Consolas", 10, "bold"))
        c.create_text(x_out, 14, text="Output Q", fill="#d0d7de", font=("Consolas", 10, "bold"))

        if 0 <= action_idx < len(action_labels):
            action_text = action_labels[action_idx]
        else:
            action_text = "n/a"
        self.viz_status_var.set(
            f"step={data['step']} | epsilon={data['epsilon']:.3f} | selected_action={action_idx} ({action_text})"
        )

    def _update_network_viz(self):
        try:
            if self.trainer is None:
                self.viz_status_var.set("network: trainer not started")
            else:
                data = self.trainer.get_visualization_data()
                self._draw_network_graph(data)
        except Exception as exc:
            self.viz_status_var.set(f"network render error: {exc}")
        self.root.after(220, self._update_network_viz)

    def _drain_status_queue(self):
        newest = None
        try:
            while True:
                newest = self.status_queue.get_nowait()
        except Empty:
            pass

        if newest:
            self.status_var.set(f"status: {newest['message']} | device: {newest['device']}")
            self.stats_var.set(
                "steps: {steps} | episodes: {episodes} | updates: {updates} | epsilon: {epsilon:.3f} | replay: {buffer} | sim: {sim_time_scale:.2f}x".format(
                    **newest
                )
            )
            enabled_text = "on" if newest.get("speed_enabled", True) else "off"
            sync_text = newest.get("speed_sync_error", "")
            if sync_text:
                sync_text = f" | sync: {sync_text}"
            self.speed_status_var.set(
                f"sim_speed: {newest.get('sim_time_scale', 1.0):.2f}x | trainer_speed: {newest.get('speed_multiplier', 1.0):.2f}x | enabled: {enabled_text}{sync_text}"
            )
            if newest["loss_ema"] is None:
                self.loss_var.set("loss_ema: n/a")
            else:
                self.loss_var.set(f"loss_ema: {newest['loss_ema']:.5f}")

            if newest["reward_ema"] is None:
                self.reward_var.set("reward_ema: n/a")
            else:
                self.reward_var.set(f"reward_ema: {newest['reward_ema']:.5f}")

        self.root.after(100, self._drain_status_queue)

    def _export_model(self):
        default_name = time.strftime("eggnogg_selfplay_%Y%m%d_%H%M%S.t7")
        path = filedialog.asksaveasfilename(
            title="Export model",
            initialfile=default_name,
            defaultextension=".t7",
            filetypes=[("Torch7 model", "*.t7"), ("All files", "*.*")],
        )
        if not path:
            return

        try:
            t7_path, pt_path = self.trainer.export_model(path)
            self.export_var.set(
                f"exported: {Path(t7_path).name} (checkpoint), {Path(pt_path).name} (TorchScript)"
            )
        except Exception as exc:
            self.export_var.set(f"export failed: {exc}")

    def _on_close(self):
        if self.trainer is not None:
            self.trainer.stop()
        if hasattr(self, "worker") and self.worker is not None:
            self.worker.join(timeout=2.0)
        self.root.destroy()

    def run(self):
        self.root.mainloop()


if __name__ == "__main__":
    TrainerApp().run()
