import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LUA = (ROOT / "lua_manager.c").read_text(encoding="utf-8")
HEADER = (ROOT / "bytebeat_ext.h").read_text(encoding="utf-8")
BUILD = (ROOT / "compile.sh").read_text(encoding="utf-8")
RUNNER = (ROOT / "tests" / "run_core_native_tests.ps1").read_text(encoding="utf-8")

for api in (
    "play_bytebeat",
    "bytebeat_info",
    "stop_sfx",
    "clear_generated",
    "status",
):
    assert f'"{api}"' in LUA, f"mod.audio.{api} must be registered"

assert "AUDIO_BYTEBEAT_CACHE_MAX     16" in LUA
assert "AUDIO_BYTEBEAT_PCM_MAX_BYTES (4u * 1024u * 1024u)" in LUA
assert "BYTEBEAT_MAX_RENDER_OPERATIONS 8388608u" in HEADER
assert "BYTEBEAT_MAX_STREAM_OPERATIONS_PER_SECOND 25165824u" in HEADER
assert "BYTEBEAT_DEFAULT_PLAYLIST_SAMPLE_RATE 8000u" in HEADER
assert "BYTEBEAT_MAX_SAMPLES 262144u" in HEADER
assert "BYTEBEAT_MAX_NODES 512u" in HEADER
assert "BYTEBEAT_MAX_EXPRESSION 1024u" in HEADER

generate = re.search(
    r"static AudioChunkCacheEntry\* mod_audio_generate_chunk\(.*?^}",
    LUA,
    re.DOTALL | re.MULTILINE,
).group(0)
assert "bytebeat_render_pcm16" in generate
assert "generated_pcm = pcm" in generate
assert "SDL_RWFromConstMem" not in generate
assert "CreateFile" not in generate and "fopen" not in generate

play = re.search(
    r"static int lua_audio_play_bytebeat\(.*?^}",
    LUA,
    re.DOTALL | re.MULTILINE,
).group(0)
assert "audio_bytebeat_parse_request" in play
assert "mod_audio_find_generated_chunk" in play
assert "mod_audio_generate_chunk" in play
assert "audio_generated_play" in play
assert "audio_runtime_ensure_ready" not in play
assert "SDL2_mixer.dll" not in play
assert 'lua_setfield(Ls, -2, "pcm_bytes")' in LUA
assert 'lua_setfield(Ls, -2, "backend")' in LUA

assert "lua_manager_audio_mix_generated" in LUA
assert "TryEnterCriticalSection(&g_audio_generated_lock)" in LUA
assert "AUDIO_GENERATED_VOICE_MAX    32" in LUA
assert "lua_manager_audio_mix_generated(samples, frame_count, output_rate)" in (
    ROOT / "hooks.c"
).read_text(encoding="utf-8")

assert "audio_generated_chunks" in (ROOT / "lua_manager.h").read_text(encoding="utf-8")
assert "diag.audio_generated_chunks" in (ROOT / "hooks.c").read_text(encoding="utf-8")
assert "bytebeat_ext.c" in BUILD
assert "bytebeat_ext_test.c" in RUNNER
assert "bytebeat_lua_static_test.py" in RUNNER

print("bytebeat Lua/cache/resource wiring checks: OK")
