from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
HOOKS = (ROOT / "hooks.c").read_text(encoding="utf-8")
NET = (ROOT / "ggpo_net.c").read_text(encoding="utf-8")
NET_HEADER = (ROOT / "ggpo_net.h").read_text(encoding="utf-8")
SERIALIZER = (ROOT / "lua_manager.c").read_text(encoding="utf-8")

palette_start = HOOKS.index(
    "static const HooksPlayerColourDef k_extra_player_colours[]"
)
palette_end = HOOKS.index("};", palette_start)
palette = HOOKS[palette_start:palette_end]

solid_pattern = re.compile(
    r"\{\s*HOOKS_PLAYER_COLOUR_SOLID,\s*"
    r"([0-9.]+)f,\s*([0-9.]+)f,\s*([0-9.]+)f,\s*1\.0f,"
)
solid_colours = [
    tuple(float(channel) for channel in match.groups())
    for match in solid_pattern.finditer(palette)
]

assert len(solid_colours) >= 20
assert (0.20, 0.22, 0.25) in solid_colours

# Expanded colors are deliberate player choices, not transparency/shadow masks.
# Keep even the darkest entry visibly distinguishable from black in-game.
for red, green, blue in solid_colours:
    assert max(red, green, blue) >= 0.18
    assert 0.2126 * red + 0.7152 * green + 0.0722 * blue >= 0.10

# Online colors use a dedicated fixed-size authenticated tuple. Do not re-enable
# the old arbitrary profile/asset transport to move two palette IDs.
assert "#define GGPO_NET_ENABLE_COSMETICS 0" in NET
assert "#define GGPO_NET_PACKET_PALETTE 9u" in NET
assert "typedef struct GgpoNetPalettePacket" in NET
assert "GgpoNetPalettePacketSizeIsFixed" in NET
assert "ggpo_net_palette_ready_internal()" in NET
assert "if (!ggpo_net_palette_ready_internal()) return 0;" in NET
assert "GGPO_NET_PALETTE_MAX_ENTRIES 256u" in NET_HEADER

# Host captures its local P1 choice; join/deferred join capture local P2.
assert "configure_online_palette_for_player(0, source)" in HOOKS
assert HOOKS.count("configure_online_palette_for_player(1, source)") == 2

# The authoritative gameplay state is restored first, then the two presentation
# tuples are assigned to their server player slots before GAME can be entered.
pump_start = HOOKS.rindex("static void online_match_pump_launch(void)")
pump_end = HOOKS.index("static int menu_mode_total(void)", pump_start)
pump = HOOKS[pump_start:pump_end]
prepare_at = pump.index("ggpo_net_prepare_prematch_start")
palette_at = pump.index("online_apply_synchronized_player_palettes")
switch_at = pump.index("p_state_switch((void*)(uintptr_t)ADDR_GAME_STATE)")
assert prepare_at < palette_at < switch_at

# Native palette settings live outside the rollback serializer/checksum regions.
assert "0x448320" not in SERIALIZER
assert "ADDR_PLAYER_CLR_INDEX" not in SERIALIZER

print("player colour static checks: OK")
