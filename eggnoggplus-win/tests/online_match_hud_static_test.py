from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "hooks.c").read_text(encoding="utf-8")
CONFIG = (ROOT / "mods" / "online_hub.cfg").read_text(encoding="utf-8")


def function_body(marker: str) -> str:
    start = SOURCE.index(marker)
    brace = SOURCE.index("{", start)
    depth = 0
    for pos in range(brace, len(SOURCE)):
        if SOURCE[pos] == "{":
            depth += 1
        elif SOURCE[pos] == "}":
            depth -= 1
            if depth == 0:
                return SOURCE[brace + 1 : pos]
    raise AssertionError(f"unterminated function: {marker}")


draw = function_body("static void online_draw_match_network_hud")
pre_swap = function_body("void hooks_online_on_pre_swap")
console = function_body("static void console_run_ggpo_net")
load = function_body("static void online_hub_load(void) {")
save = function_body("static void online_hub_save(void) {")

assert "ONLINE_SETTING_MATCH_HUD" in SOURCE
assert "int match_hud;" in SOURCE
assert "g_online_cfg.match_hud = 0;" in SOURCE
assert '"match_hud"' in load
assert '"match_hud=%d\\n"' in save
assert "match_hud=0" in CONFIG
assert '{ ONLINE_SETTING_MATCH_HUD, "Match Network HUD" }' in SOURCE

assert "if (!g_online_cfg.match_hud) return;" in draw
assert "ggpo_net_rtt_ticks()" in draw
assert "ggpo_net_input_delay()" in draw
assert "ggpo_net_rollback_count()" in draw
assert "UINT64_C(1000)" in draw
assert "/ 60u" in draw
assert "online_hub_draw_rect" in draw
assert "online_hub_draw_border" in draw
assert "online_hub_text_alpha" in draw

assert "online_draw_match_network_hud();" in pre_swap
game_gate = pre_swap.index("ADDR_GAME_STATE")
hud_call = pre_swap.index("online_draw_match_network_hud();")
assert game_gate < hud_call

assert '_stricmp(action, "hud")' in console
assert "console_try_parse_bool" in console
assert "online_hub_save();" in console
assert "Usage: ggpo.net hud [on|off]" in console

print("online match network HUD static checks: OK")
