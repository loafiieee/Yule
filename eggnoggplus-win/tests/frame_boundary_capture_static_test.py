"""Pin the one-canonical-capture-per-simulated-frame rollback pipeline."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
NET = (ROOT / "ggpo_net.c").read_text(encoding="utf-8")
LUA = (ROOT / "lua_manager.c").read_text(encoding="utf-8")


def c_function(source: str, name: str) -> str:
    match = re.search(
        rf"\b(?:static\s+)?(?:int|void)\s+{re.escape(name)}\s*\([^;]*?\)\s*\{{",
        source,
        re.S,
    )
    assert match, f"missing C function {name}"
    start = match.end()
    depth = 1
    pos = start
    while pos < len(source) and depth:
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
        pos += 1
    assert depth == 0, f"unterminated C function {name}"
    return source[start : pos - 1]


boundary = c_function(NET, "ggpo_net_frame_boundary")
assert "h->valid && h->frame == frame" in boundary
assert "lua_manager_game_state_save" in boundary
assert "lua_manager_game_state_canonicalize_rollback" in boundary
assert "lua_manager_game_state_analyze_canonical_rollback_blob" in boundary

live = c_function(NET, "ggpo_net_advance")
assert "ggpo_net_save_pre_state(g_net.frame" in live
assert "ggpo_ext_advance_frame(&inputs, arg0, NULL" in live
assert "ggpo_net_capture_post_state(" in live
assert "checksum = next_h->pre_checksum" in live

rollback = c_function(NET, "ggpo_net_apply_rollback_if_needed")
assert "ggpo_net_save_pre_state(f" in rollback
assert "ggpo_net_replay_frame(f, arg0, 1, NULL" in rollback
assert "ggpo_net_capture_post_state(" in rollback

correction = c_function(NET, "ggpo_net_apply_correction_and_replay")
assert "ggpo_net_seed_frame_boundary(" in correction
assert "ggpo_net_save_pre_state(frame" in correction
assert "ggpo_ext_advance_frame(&inputs, arg0, NULL" in correction
assert "ggpo_net_capture_post_state(" in correction

correction_inputs = c_function(NET, "ggpo_net_correction_inputs_for_frame")
assert "(!have_local || !have_remote)" in correction_inputs
assert "have_local && local_cmd != h->local_cmd" not in correction_inputs
assert "have_remote && remote_cmd != h->remote_cmd" not in correction_inputs
pin_correction_inputs = c_function(NET, "ggpo_net_pin_correction_inputs")
assert "ggpo_net_correction_span" in pin_correction_inputs
assert "ggpo_net_mutual_input_horizon" not in pin_correction_inputs
actionable_correction = c_function(NET, "ggpo_net_correction_actionable")
assert "GGPO_NET_CORRECTION_OFFER" in actionable_correction
assert "GGPO_NET_CORRECTION_READY" in actionable_correction

wrap_rebase = c_function(NET, "ggpo_net_test_rebase_active_frame")
assert "memcpy(g_net.apply_backup_state, source_blob, state_len)" in wrap_rebase
assert "g_net.state_epoch++" in wrap_rebase
assert "ggpo_net_clear_runtime_history()" in wrap_rebase
assert "g_net.frame = start_frame" in wrap_rebase
assert "ggpo_net_reset_checksum_channel(start_frame)" in wrap_rebase
assert "ggpo_net_store_input(g_net.remote_inputs" in wrap_rebase
assert "g_net.remote_contiguous_input_frame = start_frame - 1u" in wrap_rebase
assert "g_net.peer_acked_local_input_frame = start_frame - 1u" in wrap_rebase
assert "ggpo_net_seed_frame_boundary(start_frame" in wrap_rebase
assert "ggpo_net_seed_local_input_delay_from(start_frame" in wrap_rebase
assert wrap_rebase.index("memcpy(g_net.apply_backup_state") < wrap_rebase.index(
    "ggpo_net_clear_runtime_history()"
)
assert wrap_rebase.index("g_net.state_epoch++") < wrap_rebase.index(
    "ggpo_net_seed_frame_boundary(start_frame"
)

assert "ggpo_net_capture_history_summary" not in NET

analyzer = c_function(
    LUA, "lua_manager_game_state_analyze_canonical_rollback_blob"
)
assert "full_state_validate_rollback_blob_internal" in analyzer
internal = c_function(LUA, "full_state_validate_rollback_blob_internal")
assert "full_state_canonicalize_rollback_checksum_blob" in internal
assert "full_state_rollback_summary_from_canonical_blob" in internal
assert internal.index("full_state_canonicalize_rollback_checksum_blob") < internal.index(
    "full_state_rollback_summary_from_canonical_blob"
)

print("single frame-boundary capture static checks: OK")
