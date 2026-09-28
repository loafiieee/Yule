from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "lua_manager.c").read_text(encoding="utf-8")
HOOKS = (ROOT / "hooks.c").read_text(encoding="utf-8")
GHIDRA = (ROOT / "ghidra" / "eggnoggplus.exe.c").read_text(encoding="utf-8")


def body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unterminated function: {signature}")


loader = body(SOURCE, "static int mod_assets_native_load_spritesheet(")
upload = body(SOURCE, "void lua_manager_before_atlas_upload(int atlas_ptr)")

# The checked-in reverse engineering establishes the two hazards this wrapper
# must isolate: the fifth file-loader argument limits emitted records rather
# than describing gutters, and the native record array contains 0x2000 slots.
assert "*param_2 = param_5;" in GHIDRA
assert "(iVar3 <= local_4c) && (*param_2 != 0)" in GHIDRA
assert "if (_spritecount < 0x2000)" in GHIDRA
assert "(param_1 & 0x1fff) * 0x1c" in GHIDRA
assert "MOD_ASSET_NATIVE_SPRITE_CAPACITY 0x2000" in SOURCE

# Decode to a known four-byte pixel layout and revalidate the exact grid before
# handing any memory to the fixed-address native atlas implementation.
decode = loader.index("source = p_stbi_load(")
compatibility_decode = loader.index("rgba_load_image_portable(")
geometry = loader.index("((region_w + padding) % (cell_w + padding))")
count = loader.index("sprite_count = columns * rows;")
expected = loader.index("sprite_count != expected_count")
capacity = loader.index(
    "sprite_count > MOD_ASSET_NATIVE_SPRITE_CAPACITY - before"
)
native_call = loader.index("p_atlas_add_spritesheet_from_rgba(")
assert ", &components, 4);" in loader
assert decode < compatibility_decode < geometry < count < expected < capacity < native_call
assert "decoded PNG through portable compatibility path" in loader

# Authored gutters are removed into a tight row-major sheet. The native helper
# receives a zero record limit so it emits every validated cell; passing the
# authored padding here would truncate the sheet after that many records.
assert "padding > 0 || source_x > 0 || source_y > 0" in loader
assert "source_y + row * (cell_h + padding) + pixel_y" in loader
assert "source_x + column * (cell_w + padding)" in loader
assert "row * cell_h + pixel_y" in loader
assert "column * cell_w" in loader
assert "p_atlas_add_spritesheet_from_rgba(atlas_ptr, cell_w, cell_h, 0," in loader

# Every registration path uses the safe wrapper. Map sheets provide the parser's
# expected count, while general mod sheets let the wrapper derive it after decode.
assert SOURCE.count("mod_assets_native_load_spritesheet(") == 3
assert "0, 0, 0, 0,\n                                                        sheet->flags," in upload
assert "sheet.source_x,\n                                                    sheet.source_y,\n                                                    sheet.source_w,\n                                                    sheet.source_h," in upload
assert "p_atlas_load_spritesheet" not in SOURCE

# The vanilla atlas size is part of the renderer contract. Custom content must
# never resize it globally: several draw paths normalize UVs as a 512x512 page.
assert "ADDR_ATLAS_CREATE_TEXTURE" not in HOOKS
assert "hooked_atlas_create_texture" not in HOOKS
assert "g_atlas_create_texture_detour" not in HOOKS
assert "MAP_ASSET_ATLAS_INDEX 15" in SOURCE
assert "p_atlas_create_texture((void*)(intptr_t)custom_atlas" in upload
assert "mod_assets_native_load_spritesheet(g_map_asset_atlas_ptr" in upload
assert "p_atlas_upload(g_map_asset_atlas_ptr, 0, 3)" in upload
assert "void lua_manager_draw_custom_atlas(void)" in SOURCE
assert "hooked_main_sprite_batches_draw" in HOOKS
assert "lua_manager_draw_custom_atlas();" in HOOKS

print("map_atlas_loader_static_test: all checks passed")
