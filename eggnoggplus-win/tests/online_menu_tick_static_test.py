"""Structural guards for single-owner online simulation behind in-game menus."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "hooks.c").read_text(encoding="utf-8")


def function_body(name: str) -> str:
    marker = f"{name}("
    search_from = 0
    while True:
        start = SOURCE.find(marker, search_from)
        assert start >= 0, f"missing definition for {name}"
        paren = start + len(name)
        depth = 0
        end_paren = -1
        for pos in range(paren, len(SOURCE)):
            if SOURCE[pos] == "(":
                depth += 1
            elif SOURCE[pos] == ")":
                depth -= 1
                if depth == 0:
                    end_paren = pos
                    break
        assert end_paren >= 0, f"unterminated signature for {name}"
        body_start = end_paren + 1
        while body_start < len(SOURCE) and SOURCE[body_start].isspace():
            body_start += 1
        if body_start < len(SOURCE) and SOURCE[body_start] == "{":
            break
        search_from = end_paren + 1

    depth = 0
    for pos in range(body_start, len(SOURCE)):
        if SOURCE[pos] == "{":
            depth += 1
        elif SOURCE[pos] == "}":
            depth -= 1
            if depth == 0:
                return SOURCE[body_start : pos + 1]
    raise AssertionError(f"unterminated body for {name}")


# Vanilla has separate states for each player's input-remapping page. These are
# not aliases of either Options state, so omitting them freezes an online peer as
# soon as the controls page opens.
assert "#define ADDR_REMAP_STATE2             0x4483B8u" in SOURCE
assert "#define ADDR_REMAP_STATE1             0x4483C8u" in SOURCE

button_owner = function_body("online_state_ticks_via_button_update")
for state in (
    "ADDR_OPTIONS_STATE_PAUSED",
    "ADDR_OPTIONS_STATE",
    "ADDR_REMAP_STATE1",
    "ADDR_REMAP_STATE2",
    "g_mods_state",
    "g_mods_entry_state",
):
    assert state in button_owner
assert "g_console_state" not in button_owner

all_menus = function_body("online_state_is_ingame_menu")
assert "online_state_ticks_via_button_update(st)" in all_menus
assert "g_console_state" in all_menus

# The native remap pages, Options, and Mods all reach the common button-update
# hook. It must have one online advance call after the native menu update and use
# the owner-specific classifier. GAME owns its normal tick elsewhere.
button_update = function_body("hooked_main_update_with_buttons")
assert button_update.count("online_advance_net_gameplay_tick(arg0)") == 1
assert button_update.index("real_update(arg0)") < button_update.index(
    "online_advance_net_gameplay_tick(arg0)"
)
assert "online_state_ticks_via_button_update(after_update)" in button_update

# Console deliberately does not use the native button updater and therefore has
# one independent owner. MODS explicitly delegates to the common hook and must
# not advance a second time from its state update.
console_update = function_body("console_update")
assert console_update.count("online_advance_net_gameplay_tick(0)") == 1
mods_update = function_body("mods_update")
assert "p_main_update_with_buttons(0);" in mods_update
assert "online_advance_net_gameplay_tick" not in mods_update

# Every supported menu neutralizes the local gameplay command, is accepted by
# the active-match monitor, and may temporarily run the native paused update.
advance = function_body("online_advance_net_gameplay_tick")
assert "online_state_is_ingame_menu(state_ptr)" in advance
assert "raw0 = 0u" in advance and "raw1 = 0u" in advance
allowed = function_body("online_match_state_allowed")
assert "online_state_is_ingame_menu(state_ptr)" in allowed
can_simulate = function_body("hooks_can_run_simulated_game_update")
assert "online_state_is_ingame_menu(state_ptr)" in can_simulate

print("online menu tick static checks: OK")
