"""Pin automatic first-divergence snapshot preservation and its privacy boundary."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
NET = (ROOT / "ggpo_net.c").read_text(encoding="utf-8")


def section(start: str, end: str) -> str:
    begin = NET.index(start)
    finish = NET.index(end, begin)
    return NET[begin:finish]


capture = section(
    "static void ggpo_net_capture_first_desync_repro(uint32_t frame,\n"
    "                                                uint32_t local_checksum,\n"
    "                                                uint32_t remote_checksum,\n"
    "                                                int remote_checksum_known) {",
    "static void ggpo_net_mark_desync",
)
assert "uint32_t boundary_frame = frame + 1u" in capture
assert "!boundary->valid || boundary->frame != boundary_frame" in capture
assert 'const char* directory = "mods\\\\desync_repros"' in capture
assert "#ifdef GGPO_NET_TEST" in capture
assert 'getenv("EGGNOGGPLUS_TEST_REPRO_DIR")' in capture
assert "memcpy(task->state, blob, boundary->state_len)" in capture
assert "CreateThread(" in capture
assert capture.index("memcpy(task->state") < capture.index("CreateThread(")
assert "g_net.desync_repro_captured = 0" in capture

worker = section(
    "static DWORD WINAPI ggpo_net_desync_repro_worker",
    "static void ggpo_net_wait_desync_repro_worker",
)
assert "eggnoggplus-desync-repro-v1" in worker
assert "local_build_id=%08X" in worker
assert "remote_build_id=%08X" in worker
assert "state_layout_id=%08X" in worker
assert "deterministic_config_id=%08X" in worker
assert "sim_seed=%08X" in worker
assert "chaos_event_total=%u" in worker
assert "chaos_event_count=%u" in worker
assert "chaos_event_%04u=%u,%u,%u,%u,%u" in worker
assert "trace_%08X_f%08X_p%u.bin" in worker
assert "meta_%08X_f%08X_p%u.txt" in worker
assert "MOVEFILE_REPLACE_EXISTING" in worker
assert "MOVEFILE_WRITE_THROUGH" in worker
assert "SecureZeroMemory(task->state, task->state_len)" in worker
metadata_format = worker[
    worker.index('"format=eggnoggplus-desync-repro-v1')
    : worker.index("task->pair_tag", worker.index('"format=eggnoggplus-desync-repro-v1'))
]
for forbidden in ("session_id", "token", "password", "endpoint", "username", "address"):
    assert forbidden not in metadata_format

config_id = section(
    "static uint32_t ggpo_net_desync_config_id",
    "static DWORD WINAPI ggpo_net_desync_repro_worker",
)
for required in (
    "GGPO_NET_VERSION",
    "GGPO_NET_HISTORY_FRAMES",
    "g_net.state_layout_id",
    "g_net.state_size",
    "g_net.input_delay",
    "g_net.max_frame_advantage",
    "g_net.max_prediction",
    "g_net.correction_enabled",
):
    assert required in config_id

chaos_record = section(
    "static void ggpo_net_note_chaos_event",
    "static int ggpo_net_tick_reached",
)
assert "sequence % GGPO_NET_CHAOS_EVENT_CAP" in chaos_record
assert "event->service_tick = g_net.service_tick" in chaos_record
assert "event->packet_type = prefix->type" in chaos_record
assert "session_id" not in chaos_record

assert "g_net.sim_seed = ggpo_net_make_chaos_seed(g_net.session_id)" in NET
assert "GGPO_NET_CHAOS_DROP, signed_packet, len, 0u" in NET
assert "GGPO_NET_CHAOS_DELAY, data, len, delay_ticks" in NET
assert "GGPO_NET_CHAOS_QUEUE_DROP, data, len, delay_ticks" in NET

detector = section(
    "static void ggpo_net_recoverable_desync",
    "static void ggpo_net_track_remote_cmd",
)
assert "ggpo_net_capture_first_desync_repro(" in detector
assert detector.index("ggpo_net_capture_first_desync_repro(") < detector.index(
    "if (g_net.correction_enabled && (g_net.correction_active"
)

responder = section(
    "static int ggpo_net_prepare_host_correction",
    "static void ggpo_net_append_component",
)
assert "ggpo_net_capture_first_desync_repro(" in responder
assert responder.index("ggpo_net_capture_first_desync_repro(") < responder.index(
    "memcpy(g_net.correction_state"
)

stop = section(
    "static void ggpo_net_stop_internal",
    "void ggpo_net_stop(void)",
)
assert "ggpo_net_wait_desync_repro_worker(2000u)" in stop
assert stop.index("ggpo_net_wait_desync_repro_worker(2000u)") < stop.index(
    "free(g_net.state_blobs)"
)

waiter = section(
    "static void ggpo_net_wait_desync_repro_worker",
    "static void ggpo_net_capture_first_desync_repro",
)
assert "wait_result == WAIT_TIMEOUT && timeout_ms == 0u" in waiter
assert "GetExitCodeThread(worker, &exit_code)" in waiter
assert "first desync snapshot file write failed" in waiter
assert "first desync snapshot writer exceeded teardown deadline" in waiter


# A host-initiated correction may precede the joining peer's own detector.
offer = NET[NET.index('if (p->correction_phase == GGPO_NET_CORRECTION_OFFER) {'):]
offer = offer[:offer.index('if (!ggpo_net_correction_tuple_matches_packet(p)) {')]
assert offer.index('ggpo_net_capture_first_desync_repro(') < offer.index('ggpo_net_reset_recv_state();')
assert 'tick->valid && tick->frame == divergence' in offer
assert 'remote->valid && remote->frame == divergence' in offer
assert 'ggpo_net_dump_rng_ring(divergence)' in offer

print("desync_repro_static_test: all checks passed")
