import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HOOKS = (ROOT / "hooks.c").read_text(encoding="utf-8")
LUA = (ROOT / "lua_manager.c").read_text(encoding="utf-8")
HEADER = (ROOT / "lua_manager.h").read_text(encoding="utf-8")


assert "#define ADDR_SYN_UPDATE               0x405E40u" in HOOKS
syn_install = re.search(
    r"syn_update starts.*?^\s*custom_maps_init\(\);",
    HOOKS,
    re.DOTALL | re.MULTILINE,
).group(0)
for byte in ("0x55", "0x31", "0xD2", "0x57", "0x56"):
    assert byte in syn_install, f"syn_update prologue must pin {byte}"
assert "sizeof(expected)" in syn_install
assert "p_syn_update_trampoline" in syn_install

syn_hook = re.search(
    r"static void __cdecl hooked_syn_update\(.*?^}",
    HOOKS,
    re.DOTALL | re.MULTILINE,
).group(0)
assert syn_hook.index("p_syn_update_trampoline") < syn_hook.index(
    "framework_audio_scale_sample"
), "native synth must render before its output is scaled"
assert "g_framework_sfx_volume" in syn_hook

tune_callback = re.search(
    r"static void __cdecl framework_tune_audio_callback\(.*?^}",
    HOOKS,
    re.DOTALL | re.MULTILINE,
).group(0)
assert tune_callback.index("previous(samples") < tune_callback.index(
    "g_framework_music_volume"
), "native music must be produced before master scaling"
assert "framework_audio_scale_sample(g_framework_tune.held_left" in tune_callback
assert tune_callback.index("lua_manager_audio_mix_generated") > tune_callback.index(
    "g_framework_tune.held_right"
), "generated SFX must be mixed after music-only scaling"

for key in ('"music_volume"', '"sfx_volume"'):
    assert key in HOOKS, f"missing persisted setting {key}"
for label in ('"  Music volume"', '"  SFX volume"'):
    assert label in HOOKS, f"missing selectable volume row {label}"
assert "g_framework_music_volume,0,0)!=100" in HOOKS.replace(" ", "")
mad_init_hook = re.search(
    r"static void __cdecl hooked_mad_init_audio_stream\(.*?^}",
    HOOKS,
    re.DOTALL | re.MULTILINE,
).group(0)
assert "framework_audio_update_callback_ownership();" in mad_init_hook

assert "void lua_manager_set_master_audio_volumes" in LUA
assert "void lua_manager_set_master_audio_volumes" in HEADER
master_setter = re.search(
    r"void lua_manager_set_master_audio_volumes\(.*?^}",
    LUA,
    re.DOTALL | re.MULTILINE,
).group(0)
assert "p_mix_volume_channel" in master_setter
assert "audio_apply_master_music_volume" in master_setter

file_sfx = re.search(
    r"static int lua_audio_play_sfx\(.*?^}",
    LUA,
    re.DOTALL | re.MULTILINE,
).group(0)
assert "g_audio_channel_base_volumes[channel] = base_volume" in file_sfx
assert "g_audio_master_sfx_percent" in file_sfx

file_music = re.search(
    r"static int lua_audio_play_music\(.*?^}",
    LUA,
    re.DOTALL | re.MULTILINE,
).group(0)
assert "g_audio_music_play_volume = play_volume" in file_music
assert "audio_apply_master_music_volume" in file_music

generated = re.search(
    r"void lua_manager_audio_mix_generated\(.*?^}",
    LUA,
    re.DOTALL | re.MULTILINE,
).group(0)
assert "g_audio_master_sfx_percent" in generated
assert "added * (int)master_sfx" in generated

print("audio master-volume routing checks: OK")
