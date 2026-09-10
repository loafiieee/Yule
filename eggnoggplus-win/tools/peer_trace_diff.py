#!/usr/bin/env python3
"""Validate and compare Eggnogg+ canonical rollback trace streams.

Each record is little-endian:
    uint32 frame
    uint32 canonical_state_size
    uint32 finalized_post_frame_checksum
    uint8  canonical_state[canonical_state_size]

The tool deliberately reports hashes and the first differing byte offset, never
the state bytes themselves. Recognized canonical EGG0 v9/v10/v11/v12 states also report
component hashes and a schema location for the first difference. Exit 0 means
identical, 1 means a valid divergence, and 2 means invalid input or usage.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import struct
import sys
from typing import BinaryIO


HEADER = struct.Struct("<III")
UINT32_MASK = 0xFFFFFFFF
DEFAULT_MAX_STATE_BYTES = 64 * 1024 * 1024
EGG0_MAGIC = 0x30474745
EGG0_VERSION = 9
EGG0_HEADER_SIZE_V9 = 0x25688
EGG0_SCALAR_HEADER_SIZE_V9 = 252
EGG0_MAP_SCRIPT_END_V9 = 29700
EGG0_TRANSIENT_END_V9 = 30852
EGG0_THING_INFO_END_V9 = 31044
EGG0_ROOM_INFO_END_V9 = 48520
PLAYER_SIZE_V9 = 0x15C
THING_SIZE_V9 = 0x15C
THING_COUNT_MAX_V9 = 128


class TraceFormatError(ValueError):
    """A trace is structurally invalid."""


@dataclass(frozen=True)
class Record:
    frame: int
    state_len: int
    post_checksum: int
    state_sha256: str
    state: bytes

    def public(self) -> dict[str, int | str]:
        return {
            "frame": self.frame,
            "state_len": self.state_len,
            "post_checksum": self.post_checksum,
            "state_sha256": self.state_sha256,
        }


@dataclass(frozen=True)
class Egg0View:
    state: bytes
    thing_count: int
    player_size: int
    thing_size: int
    tilemap_w: int
    tilemap_h: int
    tilemap_bytes: int
    payload_offset: int
    player0_offset: int
    player1_offset: int
    things_offset: int
    tilemap_offset: int


HEADER_FIELDS_V9 = (
    ("magic", 0, 4),
    ("version", 4, 4),
    ("thing_count", 8, 4),
    ("player_size", 12, 4),
    ("thing_size", 16, 4),
    ("active_room", 20, 4),
    ("start_countdown", 24, 4),
    ("end_countdown", 28, 4),
    ("game_level", 32, 4),
    ("rng_seed", 36, 4),
    ("native_game_ticks", 40, 4),
    ("leader_mode", 44, 4),
    ("crowd_sound_last_tick", 48, 4),
    ("waterfall_fx_present", 52, 4),
    ("framework_tick_count", 56, 8),
    ("leader_raw", 64, 4),
    ("waterfall_fx_raw", 68, 4),
    ("map_selector", 72, 4),
    ("map_mode", 76, 4),
    ("round_end_any", 80, 4),
    ("score_target", 84, 4),
    ("armed_respawn_limit", 88, 4),
    ("score_p0", 92, 4),
    ("score_p1", 96, 4),
    ("chant_step", 100, 4),
    ("chant_timer", 104, 4),
    ("crowd_timer", 108, 4),
    ("native_thing_count", 112, 4),
    ("things_allocated", 116, 4),
    ("thing_latest", 120, 4),
    ("game_do_lerp_colours", 124, 4),
    ("waterfall_count", 128, 4),
    ("game_started", 132, 4),
    ("lerp_time", 136, 4),
    ("game_old_active_room", 140, 4),
    ("resumed", 144, 4),
    ("freeze", 148, 4),
    ("player_mode0", 152, 4),
    ("player_mode1", 156, 4),
    ("seed", 160, 4),
    ("loser_mode", 164, 4),
    ("loser_raw", 168, 4),
    ("score_shudder0", 172, 4),
    ("score_shudder1", 176, 4),
    ("roomdef_count", 180, 4),
    ("room_w", 184, 4),
    ("room_pixel_w", 188, 4),
    ("map_w", 192, 4),
    ("map_h", 196, 4),
    ("tilemap_w", 200, 4),
    ("tilemap_h", 204, 4),
    ("tile_w", 208, 4),
    ("tile_h", 212, 4),
    ("tilemap_pixels_w", 216, 4),
    ("tilemap_pixels_h", 220, 4),
    ("tilemap_bytes", 224, 4),
    ("camera_x", 228, 4),
    ("camera_y", 232, 4),
    ("camera_shake", 236, 4),
    ("camera_shake_decay", 240, 4),
    ("game_w", 244, 4),
    ("game_h", 248, 4),
)

PLAYER_FIELDS_V9 = (
    ("thing_slot", 0x00, 4),
    ("sprite_index", 0x04, 4),
    ("has_sword", 0x11, 1),
    ("x", 0x24, 4),
    ("y", 0x28, 4),
    ("prev_x", 0x2C, 4),
    ("prev_y", 0x30, 4),
    ("vx", 0x34, 4),
    ("vy", 0x38, 4),
    ("action_blob", 0x54, 0x24),
    ("event_flags", 0x70, 4),
    ("prev_event_flags", 0x74, 4),
    ("state_id", 0x78, 4),
    ("pending_event_flags", 0x84, 4),
    ("state_timer", 0x8C, 4),
    ("anim_phase", 0x94, 4),
    ("facing_sign", 0x98, 1),
    ("room", 0x9B, 1),
    ("prev_cmd_bits", 0x9E, 1),
    ("cmd_bits", 0x9F, 1),
    ("jump_buffer", 0xA0, 1),
    ("attack_buffer", 0xA1, 1),
    ("prev_collision", 0xAC, 1),
    ("collision_flags", 0xAD, 1),
    ("render_rgba", 0xD8, 0x20),
    ("anim_ptr", 0x158, 4),
)

THING_FIELDS_V9 = (
    ("active", 0x00, 1),
    ("type", 0x01, 1),
    ("head_blob", 0x02, 0x22),
    ("x", 0x24, 4),
    ("y", 0x28, 4),
    ("prev_x", 0x2C, 4),
    ("prev_y", 0x30, 4),
    ("vx", 0x34, 4),
    ("vy", 0x38, 4),
    ("motion_blob", 0x3C, 0x18),
    ("action_blob", 0x54, 0x24),
    ("state_id", 0x78, 4),
    ("flags", 0x80, 4),
    ("room", 0xC0, 4),
    ("tail_blob", 0xC4, 0x08),
)


def positive_int(value: str) -> int:
    try:
        parsed = int(value, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be an integer") from exc
    if parsed <= 0 or parsed > UINT32_MASK:
        raise argparse.ArgumentTypeError("must be in the range 1..4294967295")
    return parsed


def read_record(
    stream: BinaryIO,
    *,
    record_index: int,
    previous_frame: int | None,
    max_state_bytes: int,
) -> Record | None:
    header = stream.read(HEADER.size)
    if not header:
        return None
    if len(header) != HEADER.size:
        raise TraceFormatError(
            f"record {record_index}: truncated header "
            f"({len(header)}/{HEADER.size} bytes)"
        )
    frame, state_len, post_checksum = HEADER.unpack(header)
    if state_len == 0:
        raise TraceFormatError(f"record {record_index}: zero-length canonical state")
    if state_len > max_state_bytes:
        raise TraceFormatError(
            f"record {record_index}: state length {state_len} exceeds "
            f"the {max_state_bytes}-byte safety limit"
        )
    if previous_frame is not None:
        expected = (previous_frame + 1) & UINT32_MASK
        if frame != expected:
            raise TraceFormatError(
                f"record {record_index}: nonconsecutive frame {frame} "
                f"(expected {expected})"
            )
    state = stream.read(state_len)
    if len(state) != state_len:
        raise TraceFormatError(
            f"record {record_index} frame {frame}: truncated state "
            f"({len(state)}/{state_len} bytes)"
        )
    return Record(
        frame=frame,
        state_len=state_len,
        post_checksum=post_checksum,
        state_sha256=hashlib.sha256(state).hexdigest(),
        state=state,
    )


def first_byte_difference(left: bytes, right: bytes) -> int | None:
    for offset, (left_byte, right_byte) in enumerate(zip(left, right)):
        if left_byte != right_byte:
            return offset
    if len(left) != len(right):
        return min(len(left), len(right))
    return None


def _u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def _i32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<i", data, offset)[0]


def parse_egg0_v9(state: bytes) -> Egg0View | None:
    """Recognize the exact checked-in 32-bit EGG0 v9 canonical layout.

    Counts and payload offsets are validated from the blob itself. Unknown
    versions/layouts deliberately fall back to the generic byte-level report.
    """

    delta = 260 if len(state) >= 8 and _u32(state, 4) in (10, 11, 12) else 0
    if len(state) < (EGG0_HEADER_SIZE_V9 + delta + (4 if delta else 0)):
        return None
    if _u32(state, 0) != EGG0_MAGIC or _u32(state, 4) not in (9, 10, 11, 12):
        return None
    thing_count = _u32(state, 8)
    player_size = _u32(state, 12)
    thing_size = _u32(state, 16)
    tilemap_w = _i32(state, 200)
    tilemap_h = _i32(state, 204)
    tilemap_bytes = _u32(state, 224)
    if (
        thing_count > THING_COUNT_MAX_V9
        or player_size != PLAYER_SIZE_V9
        or thing_size != THING_SIZE_V9
        or tilemap_w < 0
        or tilemap_h < 0
        or tilemap_bytes % 4 != 0
    ):
        return None
    payload_bytes = player_size * 2 + thing_size * thing_count + tilemap_bytes
    base_size = EGG0_HEADER_SIZE_V9 + delta + (4 if delta else 0) + payload_bytes
    if base_size > len(state):
        return None
    if base_size != len(state):
        if _u32(state, 4) not in (11, 12):
            return None
        tail = state[base_size:]
        map_bytes = 29708
        content_header = 48 if _u32(state, 4) == 12 else 16
        package_at = content_header + map_bytes
        world_at = package_at + 72
        magic = b"YMC2" if content_header == 48 else b"YMC1"
        if len(tail) < world_at + 32 or tail[:4] != magic or tail[content_header-12:content_header] != bytes(12):
            return None
        if tail[content_header:package_at] != state[252:252 + map_bytes]:
            return None
        if tail[package_at:package_at + 4] != b"YEP1" or tail[world_at:world_at + 4] != b"YEW1":
            return None
        if tail[package_at + 68:world_at] != bytes(4) or _u32(tail, world_at + 4) != 1:
            return None
        if any(c not in b"0123456789abcdef" for c in tail[package_at + 4:package_at + 68]):
            return None
        capacity = _u32(tail, world_at + 8)
        types = _u32(tail, world_at + 24)
        if not 1 <= capacity <= 4096 or not 1 <= types <= 4096:
            return None
        if len(tail) != world_at + 32 + capacity * 32 + types * 520:
            return None
    if tilemap_w and tilemap_h and tilemap_w * tilemap_h * 4 > tilemap_bytes:
        return None
    player0_offset = (EGG0_HEADER_SIZE_V9 + delta + (4 if delta else 0))
    player1_offset = player0_offset + player_size
    things_offset = player1_offset + player_size
    tilemap_offset = things_offset + thing_count * thing_size
    return Egg0View(
        state=state,
        thing_count=thing_count,
        player_size=player_size,
        thing_size=thing_size,
        tilemap_w=tilemap_w,
        tilemap_h=tilemap_h,
        tilemap_bytes=tilemap_bytes,
        payload_offset=(EGG0_HEADER_SIZE_V9 + delta + (4 if delta else 0)),
        player0_offset=player0_offset,
        player1_offset=player1_offset,
        things_offset=things_offset,
        tilemap_offset=tilemap_offset,
    )


def _named_range(
    offset: int, fields: tuple[tuple[str, int, int], ...]
) -> tuple[str, int] | None:
    for name, start, size in fields:
        if start <= offset < start + size:
            return name, offset - start
    return None


def _map_script_location(relative: int) -> dict[str, int | str]:
    if relative >= 29448:
        if relative < 29452:
            return {"component": "map_script", "field": "timer_count", "component_byte": relative}
        timer, byte = divmod(relative - 29452, 8)
        return {"component": "map_script", "field": "timer_remaining" if byte < 4 else "timer_interval",
                "timer": timer, "field_byte": byte % 4, "component_byte": relative}
    header_fields = (
        ("magic", 0, 4),
        ("version", 4, 2),
        ("header_size", 6, 2),
        ("total_size", 8, 4),
        ("checksum", 12, 4),
        ("script_id", 16, 8),
        ("tick", 24, 8),
        ("rng_state", 32, 4),
        ("flags", 36, 4),
        ("state_count", 40, 2),
        ("override_count", 42, 2),
        ("contact_count", 44, 2),
        ("velocity_limit_count", 46, 2),
        ("reserved", 48, 16),
    )
    found = _named_range(relative, header_fields)
    if found:
        return {
            "component": "map_script",
            "field": found[0],
            "field_byte": found[1],
            "component_byte": relative,
        }
    if 64 <= relative < 128:
        rel = relative - 64
        return {
            "component": "map_script",
            "field": "lifecycle_generation",
            "index": rel // 4,
            "field_byte": rel % 4,
            "component_byte": relative,
        }
    if 128 <= relative < 7296:
        rel = relative - 128
        index, entry_byte = divmod(rel, 112)
        entry_fields = (
            ("in_use", 0, 1),
            ("type", 1, 1),
            ("key_len", 2, 1),
            ("string_len", 3, 1),
            ("bool_value", 4, 1),
            ("reserved", 5, 3),
            ("number_value", 8, 8),
            ("key", 16, 32),
            ("string_value", 48, 64),
        )
        entry = _named_range(entry_byte, entry_fields)
        return {
            "component": "map_script",
            "field": f"state[{index}].{entry[0] if entry else 'raw'}",
            "field_byte": entry[1] if entry else entry_byte,
            "component_byte": relative,
        }
    if 7296 <= relative < 14464:
        rel = relative - 7296
        index, entry_byte = divmod(rel, 28)
        fields = (
            ("in_use_reserved", 0, 4),
            ("cell_index", 4, 4),
            ("sprite_index", 8, 4),
            ("offset_x_q", 12, 4),
            ("offset_y_q", 16, 4),
            ("expires_after_tick", 20, 8),
        )
        entry = _named_range(entry_byte, fields)
        return {
            "component": "map_script",
            "field": f"overrides[{index}].{entry[0] if entry else 'raw'}",
            "field_byte": entry[1] if entry else entry_byte,
            "component_byte": relative,
        }
    if 14464 <= relative < 28800:
        rel = relative - 14464
        index, entry_byte = divmod(rel, 56)
        fields = (
            ("identity", 0, 4),
            ("object_id", 4, 4),
            ("lifecycle_id", 8, 4),
            ("cell_index", 12, 4),
            ("tile_x", 16, 4),
            ("tile_y", 20, 4),
            ("object_x", 24, 4),
            ("object_y", 28, 4),
            ("object_vx", 32, 4),
            ("object_vy", 36, 4),
            ("room_scope_reserved", 40, 8),
            ("last_seen_tick", 48, 8),
        )
        entry = _named_range(entry_byte, fields)
        return {
            "component": "map_script",
            "field": f"contacts[{index}].{entry[0] if entry else 'raw'}",
            "field_byte": entry[1] if entry else entry_byte,
            "component_byte": relative,
        }
    rel = relative - 28800
    index, entry_byte = divmod(rel, 36)
    fields = (
        ("identity", 0, 4),
        ("object_id", 4, 4),
        ("lifecycle_id", 8, 4),
        ("min_vx", 12, 4),
        ("max_vx", 16, 4),
        ("min_vy", 20, 4),
        ("max_vy", 24, 4),
        ("expires_after_tick", 28, 8),
    )
    entry = _named_range(entry_byte, fields)
    return {
        "component": "map_script",
        "field": f"velocity_limits[{index}].{entry[0] if entry else 'raw'}",
        "field_byte": entry[1] if entry else entry_byte,
        "component_byte": relative,
    }


def egg0_location(view: Egg0View, offset: int) -> dict[str, int | str]:
    delta = 260 if _u32(view.state, 4) in (10, 11, 12) else 0
    if offset < 0 or offset >= len(view.state):
        return {"component": "outside_state", "state_byte": offset}
    if offset < EGG0_SCALAR_HEADER_SIZE_V9:
        found = _named_range(offset, HEADER_FIELDS_V9)
        return {
            "component": "header",
            "field": found[0] if found else "padding",
            "field_byte": found[1] if found else offset,
            "state_byte": offset,
        }
    if offset < (EGG0_MAP_SCRIPT_END_V9 + delta):
        result = _map_script_location(offset - EGG0_SCALAR_HEADER_SIZE_V9)
        result["state_byte"] = offset
        return result
    if offset < (EGG0_TRANSIENT_END_V9 + delta):
        return {
            "component": "transient_game_state",
            "component_byte": offset - (EGG0_MAP_SCRIPT_END_V9 + delta),
            "state_byte": offset,
        }
    if offset < (EGG0_THING_INFO_END_V9 + delta):
        return {
            "component": "thing_info_state",
            "component_byte": offset - (EGG0_TRANSIENT_END_V9 + delta),
            "state_byte": offset,
        }
    if offset < (EGG0_ROOM_INFO_END_V9 + delta):
        return {
            "component": "room_info_state",
            "component_byte": offset - (EGG0_THING_INFO_END_V9 + delta),
            "state_byte": offset,
        }
    if offset < (EGG0_HEADER_SIZE_V9 + delta + (4 if delta else 0)):
        return {
            "component": "particle_state",
            "component_byte": offset - (EGG0_ROOM_INFO_END_V9 + delta),
            "state_byte": offset,
        }
    if offset < view.player1_offset:
        rel = offset - view.player0_offset
        found = _named_range(rel, PLAYER_FIELDS_V9)
        return {
            "component": "player0",
            "field": found[0] if found else "raw",
            "field_byte": found[1] if found else rel,
            "component_byte": rel,
            "state_byte": offset,
        }
    if offset < view.things_offset:
        rel = offset - view.player1_offset
        found = _named_range(rel, PLAYER_FIELDS_V9)
        return {
            "component": "player1",
            "field": found[0] if found else "raw",
            "field_byte": found[1] if found else rel,
            "component_byte": rel,
            "state_byte": offset,
        }
    if offset < view.tilemap_offset:
        rel = offset - view.things_offset
        index, entity_byte = divmod(rel, view.thing_size)
        found = _named_range(entity_byte, THING_FIELDS_V9)
        return {
            "component": "things",
            "entity_index": index,
            "field": found[0] if found else "raw",
            "field_byte": found[1] if found else entity_byte,
            "entity_byte": entity_byte,
            "state_byte": offset,
        }
    if offset >= view.tilemap_offset + view.tilemap_bytes:
        return {"component": "managed_content", "component_byte": offset - view.tilemap_offset - view.tilemap_bytes, "state_byte": offset}
    rel = offset - view.tilemap_offset
    tile_index, cell_byte = divmod(rel, 4)
    result: dict[str, int | str] = {
        "component": "tilemap",
        "tile_index": tile_index,
        "cell_byte": cell_byte,
        "state_byte": offset,
    }
    if view.tilemap_w > 0:
        result["tile_x"] = tile_index % view.tilemap_w
        result["tile_y"] = tile_index // view.tilemap_w
    return result


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def egg0_component_bytes(view: Egg0View) -> dict[str, bytes]:
    delta = 260 if _u32(view.state, 4) in (10, 11, 12) else 0
    return {
        "header": view.state[:EGG0_SCALAR_HEADER_SIZE_V9],
        "map_script": view.state[
            EGG0_SCALAR_HEADER_SIZE_V9:(EGG0_MAP_SCRIPT_END_V9 + delta)
        ],
        "transient_game_state": view.state[
            (EGG0_MAP_SCRIPT_END_V9 + delta):(EGG0_TRANSIENT_END_V9 + delta)
        ],
        "thing_info_state": view.state[
            (EGG0_TRANSIENT_END_V9 + delta):(EGG0_THING_INFO_END_V9 + delta)
        ],
        "room_info_state": view.state[
            (EGG0_THING_INFO_END_V9 + delta):(EGG0_ROOM_INFO_END_V9 + delta)
        ],
        "particle_state": view.state[(EGG0_ROOM_INFO_END_V9 + delta):(EGG0_HEADER_SIZE_V9 + delta + (4 if delta else 0))],
        "player0": view.state[view.player0_offset:view.player1_offset],
        "player1": view.state[view.player1_offset:view.things_offset],
        "things": view.state[view.things_offset:view.tilemap_offset],
        "managed_content": view.state[view.tilemap_offset + view.tilemap_bytes:],
        "tilemap": view.state[
            view.tilemap_offset:view.tilemap_offset + view.tilemap_bytes
        ],
    }


def compare_egg0_schema(
    left_state: bytes, right_state: bytes, first_byte: int | None
) -> dict[str, object] | None:
    left = parse_egg0_v9(left_state)
    right = parse_egg0_v9(right_state)
    if left is None or right is None:
        return None
    left_components = egg0_component_bytes(left)
    right_components = egg0_component_bytes(right)
    left_hashes = {name: _sha256(data) for name, data in left_components.items()}
    right_hashes = {name: _sha256(data) for name, data in right_components.items()}
    changed_components = [
        name
        for name in left_components
        if left_hashes[name] != right_hashes[name]
    ]
    shared_entities = min(left.thing_count, right.thing_count)
    changed_entities = [
        index
        for index in range(shared_entities)
        if left.state[
            left.things_offset + index * left.thing_size:
            left.things_offset + (index + 1) * left.thing_size
        ]
        != right.state[
            right.things_offset + index * right.thing_size:
            right.things_offset + (index + 1) * right.thing_size
        ]
    ]
    changed_entities.extend(
        range(shared_entities, max(left.thing_count, right.thing_count))
    )
    shared_tiles = min(left.tilemap_bytes, right.tilemap_bytes) // 4
    changed_tile_count = sum(
        left.state[left.tilemap_offset + index * 4:left.tilemap_offset + index * 4 + 4]
        != right.state[
            right.tilemap_offset + index * 4:right.tilemap_offset + index * 4 + 4
        ]
        for index in range(shared_tiles)
    )
    changed_tile_count += abs(left.tilemap_bytes - right.tilemap_bytes) // 4
    report: dict[str, object] = {
        "format": "EGG0",
        "version": _u32(left.state, 4),
        "right_version": _u32(right.state, 4),
        "changed_components": changed_components,
        "changed_entities": changed_entities,
        "changed_tile_count": changed_tile_count,
        "left_component_sha256": left_hashes,
        "right_component_sha256": right_hashes,
    }
    if first_byte is not None:
        left_location = egg0_location(left, first_byte)
        right_location = egg0_location(right, first_byte)
        if left_location == right_location:
            report["first_difference"] = left_location
        else:
            report["left_first_difference"] = left_location
            report["right_first_difference"] = right_location
    return report


def compare_traces(
    left_path: Path,
    right_path: Path,
    *,
    max_state_bytes: int,
) -> dict[str, object]:
    record_index = 0
    left_previous: int | None = None
    right_previous: int | None = None
    first_frame: int | None = None
    last_frame: int | None = None

    with left_path.open("rb") as left_stream, right_path.open("rb") as right_stream:
        while True:
            try:
                left = read_record(
                    left_stream,
                    record_index=record_index,
                    previous_frame=left_previous,
                    max_state_bytes=max_state_bytes,
                )
            except TraceFormatError as exc:
                raise TraceFormatError(f"left trace: {exc}") from exc
            try:
                right = read_record(
                    right_stream,
                    record_index=record_index,
                    previous_frame=right_previous,
                    max_state_bytes=max_state_bytes,
                )
            except TraceFormatError as exc:
                raise TraceFormatError(f"right trace: {exc}") from exc

            if left is None and right is None:
                if record_index == 0:
                    raise TraceFormatError("both traces are empty")
                return {
                    "status": "identical",
                    "records": record_index,
                    "first_frame": first_frame,
                    "last_frame": last_frame,
                }
            if left is None or right is None:
                present = right if left is None else left
                return {
                    "status": "divergent",
                    "record": record_index,
                    "frame": present.frame if present else None,
                    "differences": ["record_count"],
                    "first_byte": None,
                    "left": left.public() if left else None,
                    "right": right.public() if right else None,
                }

            if first_frame is None:
                first_frame = left.frame
            left_previous = left.frame
            right_previous = right.frame
            last_frame = left.frame

            differences: list[str] = []
            if left.frame != right.frame:
                differences.append("frame")
            byte_offset = first_byte_difference(left.state, right.state)
            if byte_offset is not None:
                differences.append(
                    "state_length"
                    if left.state_len != right.state_len
                    and byte_offset == min(left.state_len, right.state_len)
                    else "state_byte"
                )
            if left.post_checksum != right.post_checksum:
                differences.append("post_checksum")
            if differences:
                report: dict[str, object] = {
                    "status": "divergent",
                    "record": record_index,
                    "frame": left.frame if left.frame == right.frame else None,
                    "differences": differences,
                    "first_byte": byte_offset,
                    "left": left.public(),
                    "right": right.public(),
                }
                schema = compare_egg0_schema(left.state, right.state, byte_offset)
                if schema is not None:
                    report["schema"] = schema
                return report
            record_index += 1


def print_plain(report: dict[str, object]) -> None:
    if report["status"] == "identical":
        print(
            "IDENTICAL "
            f"records={report['records']} "
            f"first_frame={report['first_frame']} "
            f"last_frame={report['last_frame']}"
        )
        return

    differences = ",".join(report["differences"])
    print(
        "DIVERGENT "
        f"record={report['record']} "
        f"frame={report['frame']} "
        f"differences={differences} "
        f"first_byte={report['first_byte']}"
    )
    for side in ("left", "right"):
        record = report[side]
        if record is None:
            print(f"{side}=EOF")
        else:
            print(
                f"{side} "
                f"frame={record['frame']} "
                f"state_len={record['state_len']} "
                f"post_checksum={record['post_checksum']} "
                f"state_sha256={record['state_sha256']}"
            )
    schema = report.get("schema")
    if isinstance(schema, dict):
        changed = ",".join(str(item) for item in schema["changed_components"])
        location = schema.get("first_difference")
        if isinstance(location, dict):
            location_text = ".".join(
                str(location[key])
                for key in ("component", "field")
                if key in location
            )
            if "entity_index" in location:
                location_text += f"[{location['entity_index']}]"
            if "tile_index" in location:
                location_text += (
                    f"[{location['tile_index']}]"
                    f"({location.get('tile_x','?')},{location.get('tile_y','?')})"
                )
        else:
            location_text = "layout-dependent"
        print(
            f"schema=EGG0/v{schema['version']} "
            f"changed_components={changed or 'none'} "
            f"changed_entities={schema['changed_entities']} "
            f"changed_tiles={schema['changed_tile_count']} "
            f"first={location_text}"
        )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Validate and compare two Eggnogg+ canonical rollback trace streams "
            "without printing state contents."
        )
    )
    parser.add_argument("left", type=Path, help="first peer trace")
    parser.add_argument("right", type=Path, help="second peer trace")
    parser.add_argument(
        "--max-state-bytes",
        type=positive_int,
        default=DEFAULT_MAX_STATE_BYTES,
        help=f"per-record safety limit (default: {DEFAULT_MAX_STATE_BYTES})",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="emit one machine-readable JSON report",
    )
    args = parser.parse_args(argv)

    try:
        report = compare_traces(
            args.left,
            args.right,
            max_state_bytes=args.max_state_bytes,
        )
    except (OSError, TraceFormatError) as exc:
        if args.json:
            print(json.dumps({"status": "invalid", "error": str(exc)}, sort_keys=True))
        else:
            print(f"INVALID: {exc}", file=sys.stderr)
        return 2

    if args.json:
        print(json.dumps(report, sort_keys=True))
    else:
        print_plain(report)
    return 0 if report["status"] == "identical" else 1


if __name__ == "__main__":
    raise SystemExit(main())
