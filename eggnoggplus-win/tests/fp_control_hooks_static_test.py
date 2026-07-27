from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HOOKS = (ROOT / "hooks.c").read_text(encoding="utf-8")
EXT = (ROOT / "ggpo_ext.c").read_text(encoding="utf-8")
NET = (ROOT / "ggpo_net.c").read_text(encoding="utf-8")
FP = (ROOT / "fp_control.c").read_text(encoding="utf-8")
FP_HEADER = (ROOT / "fp_control.h").read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1:index]
    raise AssertionError(f"unterminated function: {signature}")


callback = function_body(HOOKS, "static int hooks_native_game_tick_callback")
guard = function_body(HOOKS, "static int hooks_run_native_game_tick")
simulate = function_body(HOOKS, "int hooks_simulate_game_ticks")
advance = function_body(HOOKS, "int hooks_advance_game_tick")
live = function_body(HOOKS, "static void __cdecl hooked_game_update(int arg0)")
ext_advance = function_body(EXT, "static int ggpo_ext_advance_frame_internal")
net_replay = function_body(NET, "static int ggpo_net_replay_frame")
net_live = function_body(NET, "int ggpo_net_advance(")
fp_run = function_body(FP, "int fp_control_run_canonical_tick")

# Preserve vanilla's MinGW `fninit` precision while pinning SSE underflow and
# rounding behavior. The per-tick guard changes only controls, not x87 stack or
# status, and restores both caller control registers after the callback.
assert "FP_CONTROL_CANONICAL_X87 UINT16_C(0x037f)" in FP_HEADER
assert "FP_CONTROL_CANONICAL_MXCSR UINT32_C(0x00001f80)" in FP_HEADER
assert '"fninit' not in FP.lower()
assert fp_run.index("fp_control_install_canonical();") < fp_run.index("callback(user)")
assert fp_run.index("callback(user)") < fp_run.index('"ldmxcsr %0"')
assert fp_run.index('"ldmxcsr %0"') < fp_run.index('"fldcw %0"')

# Exactly one native simulation and its deterministic content interactions live
# inside one callback invocation. Offline live, GGPO live, and replay callers
# all use that primitive; a replay/catch-up loop cannot span one FP scope.
assert callback.count("tick->update(tick->arg0);") == 1
assert callback.index("tick->update(tick->arg0);") < callback.index(
    "hooks_apply_content_tile_interactions();"
)
assert "fp_control_run_canonical_tick(hooks_native_game_tick_callback, &tick)" in guard
assert "hooks_run_native_game_tick(real_update, arg0, 1)" in simulate
assert "hooks_run_native_game_tick(real_update, arg0, 1)" in advance
assert "hooks_run_native_game_tick(real_update, arg0, 0)" in live
assert "hooks_simulate_game_ticks(1, arg0)" in ext_advance
assert "hooks_advance_game_tick(arg0, 1)" in ext_advance
assert "ggpo_ext_advance_frame(&inputs, arg0" in net_replay
assert "ggpo_ext_advance_frame(&inputs, arg0" in net_live
assert "_controlfp" not in NET
# Two live diagnostics and the automatic first-desync snapshot read the
# controls without mutating them.
assert NET.count("fp_control_get_x87_control()") == 3
assert NET.count("fp_control_get_mxcsr()") == 3
desync_capture = function_body(
    NET,
    "static void ggpo_net_capture_first_desync_repro(uint32_t frame,\n"
    "                                                uint32_t local_checksum,\n"
    "                                                uint32_t remote_checksum,\n"
    "                                                int remote_checksum_known) {",
)
assert "task->x87_control = fp_control_get_x87_control()" in desync_capture
assert "task->mxcsr = fp_control_get_mxcsr()" in desync_capture

print("fp_control_hooks_static_test: all checks passed")
