"""Regression tests for the secret-safe canonical peer-trace diff CLI."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "peer_trace_diff.py"
LUA_MANAGER = (ROOT / "lua_manager.c").read_text(encoding="utf-8")
HEADER = struct.Struct("<III")
EGG0_HEADER_SIZE = 0x25688
PLAYER_SIZE = 0x15C
THING_SIZE = 0x15C

for contract in (
    "sizeof(FullStateBlobHeader) == 0x25790u",
    "offsetof(FullStateBlobHeader, map_script_state) == 252u",
    "offsetof(FullStateBlobHeader, transient_game_state) == 29960u",
    "offsetof(FullStateBlobHeader, thing_info_state) == 31112u",
    "offsetof(FullStateBlobHeader, room_info_state) == 31304u",
    "offsetof(FullStateBlobHeader, particle_state) == 48780u",
):
    assert contract in LUA_MANAGER


def trace_record(frame: int, checksum: int, state: bytes) -> bytes:
    return HEADER.pack(frame, len(state), checksum) + state


def egg0_state(*, thing_count: int = 3, tilemap_w: int = 4, tilemap_h: int = 2) -> bytes:
    tilemap_bytes = tilemap_w * tilemap_h * 4
    state = bytearray(
        EGG0_HEADER_SIZE
        + PLAYER_SIZE * 2
        + THING_SIZE * thing_count
        + tilemap_bytes
    )
    struct.pack_into("<I", state, 0, 0x30474745)
    struct.pack_into("<I", state, 4, 9)
    struct.pack_into("<I", state, 8, thing_count)
    struct.pack_into("<I", state, 12, PLAYER_SIZE)
    struct.pack_into("<I", state, 16, THING_SIZE)
    struct.pack_into("<i", state, 200, tilemap_w)
    struct.pack_into("<i", state, 204, tilemap_h)
    struct.pack_into("<I", state, 224, tilemap_bytes)
    return bytes(state)


def run_tool(*args: object) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(TOOL), *(str(arg) for arg in args)],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=10,
    )


with tempfile.TemporaryDirectory(prefix="eggnoggplus_trace_diff_") as temp_name:
    temp = Path(temp_name)
    left = temp / "left.bin"
    right = temp / "right.bin"

    wrap_trace = b"".join(
        (
            trace_record(0xFFFFFFFE, 11, b"alpha"),
            trace_record(0xFFFFFFFF, 12, b"bravo"),
            trace_record(0, 13, b"charlie"),
        )
    )
    left.write_bytes(wrap_trace)
    right.write_bytes(wrap_trace)
    identical = run_tool(left, right)
    assert identical.returncode == 0, identical
    assert (
        "IDENTICAL records=3 first_frame=4294967294 last_frame=0"
        in identical.stdout
    )
    assert identical.stderr == ""

    left.write_bytes(b"")
    right.write_bytes(b"")
    empty = run_tool(left, right)
    assert empty.returncode == 2, empty
    assert "both traces are empty" in empty.stderr

    left_state = b"abcdef"
    right_state = b"abXdef"
    left.write_bytes(trace_record(9, 100, left_state))
    right.write_bytes(trace_record(9, 101, right_state))
    divergent = run_tool("--json", left, right)
    assert divergent.returncode == 1, divergent
    report = json.loads(divergent.stdout)
    assert report["status"] == "divergent"
    assert report["record"] == 0
    assert report["frame"] == 9
    assert report["differences"] == ["state_byte", "post_checksum"]
    assert report["first_byte"] == 2
    assert report["left"]["state_sha256"] == hashlib.sha256(left_state).hexdigest()
    assert report["right"]["state_sha256"] == hashlib.sha256(right_state).hexdigest()
    assert "state" not in report["left"]
    assert "schema" not in report

    # v10 inserts a timer block before the old transient boundary; keep v9 tests below.
    old = egg0_state()
    current = bytearray(old[:29700] + bytes(260) + old[29700:EGG0_HEADER_SIZE] + bytes(4) + old[EGG0_HEADER_SIZE:])
    struct.pack_into("<I", current, 4, 10)
    altered = bytearray(current)
    altered[29704] = 1
    left.write_bytes(trace_record(29, 200, bytes(current)))
    right.write_bytes(trace_record(29, 201, bytes(altered)))
    timer_diff = run_tool("--json", left, right)
    assert timer_diff.returncode == 1, timer_diff
    timer_schema = json.loads(timer_diff.stdout)["schema"]
    assert timer_schema["version"] == 10
    assert timer_schema["first_difference"]["field"] == "timer_remaining"
    assert timer_schema["first_difference"]["timer"] == 0
    altered = bytearray(current)
    altered[EGG0_HEADER_SIZE + 264 + PLAYER_SIZE + 0x34] = 1
    right.write_bytes(trace_record(29, 201, bytes(altered)))
    current_player = json.loads(run_tool("--json", left, right).stdout)["schema"]
    assert current_player["first_difference"]["component"] == "player1"

    # v11 preserves all native offsets; an optional managed extension is hashed separately.
    version11 = bytearray(current)
    struct.pack_into("<I", version11, 4, 11)
    extension = bytearray(b"YMC1" + bytes(12) + version11[252:252+29708])
    extension += b"YEP1" + b"a"*64 + bytes(4)
    world = bytearray(32 + 8*32 + 520)
    world[:4] = b"YEW1"
    struct.pack_into("<I", world, 4, 1)
    struct.pack_into("<I", world, 8, 8)
    struct.pack_into("<I", world, 24, 1)
    version11 += extension + world
    changed11 = bytearray(version11)
    changed11[-1] = 1
    left.write_bytes(trace_record(29, 200, bytes(version11)))
    right.write_bytes(trace_record(29, 201, bytes(changed11)))
    entity_schema = json.loads(run_tool("--json", left, right).stdout)["schema"]
    assert entity_schema["version"] == 11
    assert entity_schema["changed_components"] == ["managed_content"]
    assert entity_schema["first_difference"]["component"] == "managed_content"

    version12 = bytearray(current)
    struct.pack_into("<I", version12, 4, 12)
    extension12 = b"YMC2" + bytes(range(32)) + bytes(12) + version12[252:252+29708]
    extension12 += b"YEP1" + b"a"*64 + bytes(4) + world
    version12 += extension12
    changed12 = bytearray(version12)
    changed12[-1] = 1
    left.write_bytes(trace_record(29, 200, bytes(version12)))
    right.write_bytes(trace_record(29, 201, bytes(changed12)))
    current_schema = json.loads(run_tool("--json", left, right).stdout)["schema"]
    assert current_schema["version"] == 12
    assert current_schema["changed_components"] == ["managed_content"]

    # Recognized production EGG0/v9 blobs receive secret-safe component hashes
    # and exact schema locations in addition to the generic byte offset.
    canonical = egg0_state()
    changed = bytearray(canonical)
    changed[36] = 0xA5
    left.write_bytes(trace_record(30, 200, canonical))
    right.write_bytes(trace_record(30, 201, bytes(changed)))
    header_diff = run_tool("--json", left, right)
    assert header_diff.returncode == 1, header_diff
    header_report = json.loads(header_diff.stdout)
    schema = header_report["schema"]
    assert schema["format"] == "EGG0"
    assert schema["version"] == 9
    assert schema["changed_components"] == ["header"]
    assert schema["first_difference"]["component"] == "header"
    assert schema["first_difference"]["field"] == "rng_seed"
    assert schema["first_difference"]["field_byte"] == 0
    assert schema["left_component_sha256"]["header"] != schema[
        "right_component_sha256"
    ]["header"]
    assert "state" not in schema

    player1_vx = EGG0_HEADER_SIZE + PLAYER_SIZE + 0x34
    changed = bytearray(canonical)
    changed[player1_vx + 1] = 0x7F
    right.write_bytes(trace_record(30, 201, bytes(changed)))
    player_diff = run_tool("--json", left, right)
    player_schema = json.loads(player_diff.stdout)["schema"]
    assert player_schema["changed_components"] == ["player1"]
    assert player_schema["first_difference"]["component"] == "player1"
    assert player_schema["first_difference"]["field"] == "vx"
    assert player_schema["first_difference"]["field_byte"] == 1

    things_offset = EGG0_HEADER_SIZE + PLAYER_SIZE * 2
    entity_state = things_offset + THING_SIZE * 2 + 0x78
    changed = bytearray(canonical)
    changed[entity_state] = 3
    right.write_bytes(trace_record(30, 201, bytes(changed)))
    entity_diff = run_tool("--json", left, right)
    entity_schema = json.loads(entity_diff.stdout)["schema"]
    assert entity_schema["changed_components"] == ["things"]
    assert entity_schema["changed_entities"] == [2]
    assert entity_schema["first_difference"]["entity_index"] == 2
    assert entity_schema["first_difference"]["field"] == "state_id"

    tilemap_offset = things_offset + THING_SIZE * 3
    tile_byte = tilemap_offset + 5 * 4 + 2
    changed = bytearray(canonical)
    changed[tile_byte] = 0x40
    right.write_bytes(trace_record(30, 201, bytes(changed)))
    tile_diff = run_tool(left, right)
    assert tile_diff.returncode == 1, tile_diff
    assert "changed_components=tilemap" in tile_diff.stdout
    assert "changed_tiles=1" in tile_diff.stdout
    assert "first=tilemap[5](1,1)" in tile_diff.stdout
    tile_json = run_tool("--json", left, right)
    tile_schema = json.loads(tile_json.stdout)["schema"]
    assert tile_schema["first_difference"]["tile_index"] == 5
    assert tile_schema["first_difference"]["tile_x"] == 1
    assert tile_schema["first_difference"]["tile_y"] == 1
    assert tile_schema["first_difference"]["cell_byte"] == 2

    map_state_number = 252 + 128 + 3 * 112 + 10
    changed = bytearray(canonical)
    changed[map_state_number] = 0x11
    right.write_bytes(trace_record(30, 201, bytes(changed)))
    map_diff = run_tool("--json", left, right)
    map_schema = json.loads(map_diff.stdout)["schema"]
    assert map_schema["changed_components"] == ["map_script"]
    assert (
        map_schema["first_difference"]["field"]
        == "state[3].number_value"
    )
    assert map_schema["first_difference"]["field_byte"] == 2

    left.write_bytes(trace_record(9, 100, left_state))
    right.write_bytes(trace_record(9, 101, left_state))
    checksum = run_tool(left, right)
    assert checksum.returncode == 1, checksum
    assert "differences=post_checksum" in checksum.stdout
    assert "first_byte=None" in checksum.stdout

    right.write_bytes(trace_record(10, 100, left_state))
    frame = run_tool("--json", left, right)
    assert frame.returncode == 1, frame
    frame_report = json.loads(frame.stdout)
    assert frame_report["differences"] == ["frame"]
    assert frame_report["frame"] is None

    right.write_bytes(trace_record(9, 100, left_state + b"!"))
    length = run_tool(left, right)
    assert length.returncode == 1, length
    assert "differences=state_length" in length.stdout
    assert "first_byte=6" in length.stdout

    right.write_bytes(
        trace_record(9, 100, left_state)
        + trace_record(10, 102, b"second")
    )
    count = run_tool(left, right)
    assert count.returncode == 1, count
    assert "differences=record_count" in count.stdout
    assert "left=EOF" in count.stdout

    left.write_bytes(trace_record(0xFFFFFFFF, 1, b"a") + trace_record(0, 2, b"b"))
    right.write_bytes(trace_record(0xFFFFFFFF, 1, b"a") + trace_record(1, 2, b"b"))
    sequence = run_tool(left, right)
    assert sequence.returncode == 2, sequence
    assert "right trace: record 1: nonconsecutive frame 1 (expected 0)" in sequence.stderr

    left.write_bytes(b"\x00\x01")
    right.write_bytes(b"")
    header = run_tool(left, right)
    assert header.returncode == 2, header
    assert "truncated header" in header.stderr

    left.write_bytes(HEADER.pack(4, 5, 7) + b"abc")
    body = run_tool(left, right)
    assert body.returncode == 2, body
    assert "truncated state (3/5 bytes)" in body.stderr

    left.write_bytes(HEADER.pack(4, 0, 7))
    zero = run_tool(left, right)
    assert zero.returncode == 2, zero
    assert "zero-length canonical state" in zero.stderr

    left.write_bytes(trace_record(4, 7, b"abc"))
    oversized = run_tool("--max-state-bytes", 2, left, right)
    assert oversized.returncode == 2, oversized
    assert "exceeds the 2-byte safety limit" in oversized.stderr

print("peer trace diff tests: OK")
