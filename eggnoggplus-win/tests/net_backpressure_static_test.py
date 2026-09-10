from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
NET = (ROOT / "ggpo_net.c").read_text(encoding="utf-8")
HEADER = (ROOT / "ggpo_net.h").read_text(encoding="utf-8")


def section(start: str, end: str) -> str:
    begin = NET.index(start)
    finish = NET.index(end, begin)
    return NET[begin:finish]


error_classifier = section(
    "static GgpoNetRawSendResult ggpo_net_note_socket_send_error",
    "static int ggpo_net_test_state_chunk_block_matches",
)
assert "error_code == WSAEWOULDBLOCK || error_code == WSAENOBUFS" in error_classifier
assert "GGPO_NET_RAW_SEND_WOULD_BLOCK" in error_classifier
assert "socket_would_block_events" in error_classifier
assert "socket_send_errors" in error_classifier

raw_send = section(
    "static GgpoNetRawSendResult ggpo_net_send_raw_bytes",
    "static int ggpo_net_queue_sim_packet",
)
raw_send = raw_send[raw_send.index("sent = sendto("):]
assert "sent != len" in raw_send
assert "g_net.packets_sent++" in raw_send
assert raw_send.index("g_net.packets_sent++") < raw_send.index(
    "return GGPO_NET_RAW_SEND_SENT"
)

queue_flush = section(
    "static void ggpo_net_flush_sim_queue",
    "static int ggpo_net_sign_packet",
)
assert "result == GGPO_NET_RAW_SEND_WOULD_BLOCK" in queue_flush
assert queue_flush.index("result == GGPO_NET_RAW_SEND_SENT") < queue_flush.index(
    "q->valid = 0"
)
would_block_branch = queue_flush[queue_flush.index("result == GGPO_NET_RAW_SEND_WOULD_BLOCK") :]
assert "break;" in would_block_branch

full_chunk = section(
    "static int ggpo_net_send_state_chunk_from",
    "static int ggpo_net_send_delta_state_chunk_from",
)
assert full_chunk.index("if (!ggpo_net_send_bytes") < full_chunk.index(
    "*inout_offset = offset + chunk"
)

delta_chunk = section(
    "static int ggpo_net_send_delta_state_chunk_from",
    "static int ggpo_net_send_state_chunk",
)
assert delta_chunk.index("if (!ggpo_net_send_bytes") < delta_chunk.index(
    "*inout_next_chunk = chunk_index + 1u"
)

service = section(
    "static int ggpo_net_service_transport",
    "#ifdef GGPO_NET_TEST\nint ggpo_net_test_service_input",
)
fresh_send = service.index("(void)ggpo_net_send_packet(heartbeat)")
queued_send = service.index("ggpo_net_flush_sim_queue()")
bulk_send = service.index("ggpo_net_send_state_sync_burst()")
correction_send = service.index("ggpo_net_send_correction_burst()")
assert fresh_send < queued_send < bulk_send < correction_send
assert "g_net.socket_backpressured_this_tick = 0" in service
assert "if (!g_net.socket_backpressured_this_tick)" in service

profile = section(
    "static int ggpo_net_send_cosmetic_profile",
    "static void ggpo_net_send_cosmetic_profile_periodic",
)
assert profile.index("if (!ggpo_net_send_bytes") < profile.index(
    "g_net.last_cosmetic_profile_send_tick = g_net.service_tick"
)

asset = section(
    "static void ggpo_net_send_cosmetic_asset_periodic",
    "static int ggpo_net_cosmetic_profiles_ready",
)
assert asset.index("!ggpo_net_send_cosmetic_asset_chunk") < asset.index(
    "g_net.local_cosmetic_asset_next_chunk ="
)

request = section(
    "static int ggpo_net_request_host_correction",
    "static const LuaGameStateRollbackSummary*",
)
assert request.index("if (!ggpo_net_send_packet") < request.index(
    "ggpo_net_saturating_increment(&g_net.correction_requests)"
)

assert "#define GGPO_NET_CORRECTION_BURST_CHUNKS 8" in NET
assert "tx pressure: would_block=%u send_error=%u deferred=%u" in NET
assert "ggpo_net_socket_would_block_count" in HEADER
assert "ggpo_net_test_force_state_chunk_would_block" in HEADER
assert "ggpo_net_test_get_correction_send_cursor" in HEADER

print("net_backpressure_static_test: all checks passed")
