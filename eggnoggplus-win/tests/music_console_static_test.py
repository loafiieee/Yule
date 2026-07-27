from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HOOKS = (ROOT / "hooks.c").read_text(encoding="utf-8")
CONSOLE = (ROOT / "CONSOLE.md").read_text(encoding="utf-8")
TRACK = (ROOT / "data" / "tune8.txt").read_text(encoding="utf-8")

for command in ("music.status", "music.scan", "music.rescan", "music.play"):
    assert f'"{command}"' in HOOKS, f"{command} must be registered"
    assert command in CONSOLE, f"{command} must be documented"

assert "ADDR_MAIN_TALLY_TUNES" in HOOKS
assert "p_main_tally_tunes()" in HOOKS
assert "ADDR_TUNE_COUNT" in HOOKS
assert "ADDR_FORCED_TUNE" in HOOKS
assert "Native discovery stops at the first gap" in HOOKS
assert "UNSUPPORTED_JAVASCRIPT" in HOOKS
assert "mod.audio.play_bytebeat" in HOOKS
assert "framework_tune_audio_callback" in HOOKS
assert "framework_tune_pump();" in HOOKS
assert "TryEnterCriticalSection" in HOOKS
assert "bytebeat_stream_create" in HOOKS
assert "bytebeat_stream_read" in HOOKS
assert "bytebeat_js_render(" not in HOOKS[HOOKS.index(
    "static void __cdecl framework_tune_audio_callback"
):HOOKS.index("static void framework_tune_deactivate")], (
    "the real-time mixer callback must never execute JavaScript"
)

# Vanilla requests a 22,050-Hz SDL mixer, which aliases 32/44.1-kHz formulas.
# The early audio-init detour must raise only that known request to the highest
# yule:bytebeat authoring rate and leave unrelated caller-selected rates alone.
assert "#define ADDR_MAD_INIT_AUDIO_STREAM    0x404240u" in HOOKS
assert "#define FRAMEWORK_AUDIO_VANILLA_RATE 22050" in HOOKS
assert "#define FRAMEWORK_AUDIO_OUTPUT_RATE  48000" in HOOKS
assert "framework_audio_requested_rate" in HOOKS
assert "requested_rate == FRAMEWORK_AUDIO_VANILLA_RATE" in HOOKS
assert "&g_mad_init_audio_stream_detour" in HOOKS
assert "&hooked_mad_init_audio_stream, 7" in HOOKS

# The authored song is retained verbatim and uses the isolated Dollchan engine.
assert "STEADY ON TIM, IT'S ONLY A BUDGET GAME" in TRACK
assert "yule:bytebeat" in TRACK and "engine=dollchan" in TRACK
assert "mode=bytebeat" in TRACK
assert "sample_rate=44100" in TRACK
assert "M=T=>" in TRACK and "random()" in TRACK
assert "bytebeat_js_create" in HOOKS
assert "BYTEBEAT_PLAYLIST_DOLLCHAN" in HOOKS
assert "BYTEBEAT_MAX_STREAM_OPERATIONS_PER_SECOND" in HOOKS
assert 'strstr(line, "=>")' in HOOKS
assert 'strstr(line, "random(")' in HOOKS

print("native/bounded/Dollchan music playlist and authoring controls: OK")
