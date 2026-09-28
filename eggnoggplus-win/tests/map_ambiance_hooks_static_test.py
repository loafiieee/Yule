import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HOOKS = (ROOT / "hooks.c").read_text(encoding="utf-8")

assert "#define ADDR_PARTICLES_DRAW_EX        0x418740u" in HOOKS
install = re.search(
    r"static const unsigned char expected\[\] = \{\s*0x55, 0xB9, 0x18, 0x00, 0x00, 0x00\s*\};.*?p_particles_draw_ex_trampoline =.*?trampoline;",
    HOOKS,
    re.DOTALL,
).group(0)
assert "ADDR_PARTICLES_DRAW_EX" in install
assert "sizeof(expected)" in install

wrapper = re.search(
    r"static void __cdecl hooked_particles_draw_ex\(.*?^}",
    HOOKS,
    re.DOTALL | re.MULTILINE,
).group(0)
assert wrapper.index("p_particles_draw_ex_trampoline") < wrapper.index(
    "draw_custom_ambiance_layer"
), "native particles must retain their place before custom lanes"
assert "camera_x" in wrapper and "camera_y" in wrapper and "layer" in wrapper
assert "draw_custom_ambiance_layer(layer)" in wrapper

renderer = re.search(
    r"static void draw_custom_ambiance_layer\(.*?^}",
    HOOKS,
    re.DOTALL | re.MULTILINE,
).group(0)
assert "custom_maps_pinned_ambiance" in renderer
assert "map_ambiance_render_next" in renderer
assert "particle.world_x - camera_x" in renderer
assert "camera_y - particle.world_y" in renderer
assert "camera_x = *g_camera_x" in renderer
assert "camera_y = *g_camera_y" in renderer
assert "Native layers 4 and 3 are invoked with screen-space camera arguments" in renderer
assert "render.layer = particle.blend" in renderer
assert "g_game_ticks" in renderer
assert "g_content_draw_ops" in renderer
assert 'LOG_INFO("[ambiance] active id=%s' in renderer
assert "content_bridge_draw_visual(&render, &g_content_draw_ops)" in renderer
assert "generated > 0u && queued == 0u" in renderer
assert "particle.scale_x == 0.0f || particle.scale_y == 0.0f" in renderer

print("custom ambiance native draw-hook checks: OK")
