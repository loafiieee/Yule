from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / "lua_manager.c").read_text(encoding="utf-8")
for name, registration, count in (
    ("lua_font_unregister_glyph", "font_regs", "font_reg_count"),
    ("lua_texture_unregister", "texture_regs", "texture_reg_count"),
):
    start = source.index("static int " + name + "(")
    end = source.index("\n}\n", start)
    body = source[start:end]
    assert "mod_from_upvalue" in body and "!mod->enabled" in body
    assert "mod_is_gameplay_suspended(mod)" in body
    assert f"mod->{registration}" in body and f"mod->{count}--" in body
    assert "rebuild_registered_assets_from_enabled_mods" in body
    assert f"lua_pushcclosure(Ls, {name}, 1)" in source
assert "mod_texture_reg_record(mod, canonical, rel_path)" in source
print("owner-scoped asset removal API wiring: OK")
