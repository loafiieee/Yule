from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "lua_manager.c").read_text(encoding="utf-8")
header = (ROOT / "map_script.h").read_text(encoding="utf-8")

assert '#include "map_script.h"' in source
assert "#define FULL_STATE_BLOB_VERSION     9u" in source
assert "MapScriptSnapshot map_script_state;" in source
assert "FULL_STATE_FIELD_RANGE(map_script_state);" in source
assert "#define MAP_SCRIPT_API_VERSION            UINT32_C(7)" in header
assert "#define MAP_SCRIPT_SNAPSHOT_VERSION       5u" in header
assert "int32_t offset_x_q;" in header
assert "int32_t offset_y_q;" in header
assert "uint8_t object_kind;" in header
assert "uint8_t contact_scope;" in header
assert "MAP_SCRIPT_CONTACT_SCOPE_BINDING = 1" in header

capture_start = source.index("static int full_state_capture_into")
apply_validation_start = source.index("static int full_state_validate_apply_blob",
                                      capture_start)
apply_start = source.index("static int full_state_apply_blob",
                           apply_validation_start)
capture = source[capture_start:apply_validation_start]
assert "map_script_snapshot_save(&hdr->map_script_state" in capture

apply_validation = source[apply_validation_start:apply_start]
assert "map_script_snapshot_validate(&hdr->map_script_state" in apply_validation

apply_end = source.index("static uint32_t full_state_crc32", apply_start)
apply = source[apply_start:apply_end]
validate_at = apply.index("full_state_validate_apply_blob(src, src_len")
load_at = apply.index("map_script_snapshot_load(&hdr->map_script_state")
native_write_at = apply.index("*p_game_active_room = hdr->active_room")
assert validate_at < load_at < native_write_at

header_validate_start = source.index("static int full_state_validate_blob_header")
header_validate_end = source.index("static void full_state_zero_player_render_colours",
                                   header_validate_start)
header_validate = source[header_validate_start:header_validate_end]
assert "map_script_snapshot_validate(&hdr->map_script_state" in header_validate

fingerprint_start = source.index("uint32_t lua_manager_game_state_layout_fingerprint")
fingerprint = source[fingerprint_start:fingerprint_start + 5000]
assert "layout_schema_version = 6u" in fingerprint
assert "MAP_SCRIPT_API_VERSION" in fingerprint
assert "MAP_SCRIPT_SNAPSHOT_VERSION" in fingerprint
assert "sizeof(MapScriptSnapshot)" in fingerprint

print("map_script_rollback_static_test: all checks passed")
