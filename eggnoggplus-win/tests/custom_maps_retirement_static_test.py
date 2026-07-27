from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "custom_maps.c").read_text(encoding="utf-8")


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


swap = function_body("static void registry_swap_in")
mapgen = function_body("void custom_maps_handle_mapgen_init")
shutdown = function_body("void custom_maps_shutdown")
reclaim = function_body("static void free_unpinned_retired_registry_maps")

assert "old_maps == g_engine_pinned_registry_maps" in swap
assert "retire_registry_maps(old_maps, old_count);" in swap
assert "free_registry_map_array(old_maps, old_count);" in swap
assert "free_unpinned_retired_registry_maps();" in swap

assert "retired->maps == g_engine_pinned_registry_maps" in reclaim
assert "free_registry_map_array(retired->maps, retired->count);" in reclaim
assert "g_engine_pinned_registry_maps = g_custom_registry.maps;" in mapgen
assert mapgen.count("g_engine_pinned_registry_maps = NULL;") >= 3
assert mapgen.count("free_unpinned_retired_registry_maps();") >= 4

clear_pin = shutdown.index("g_engine_pinned_registry_maps = NULL;")
free_retired = shutdown.index("free_retired_registry_maps();")
assert clear_pin < free_retired

print("custom map retirement static checks: OK")
