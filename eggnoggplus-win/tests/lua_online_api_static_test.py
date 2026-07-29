from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "lua_manager.c").read_text(encoding="utf-8")
DOCS = (ROOT / "docs-site" / "api-data.js").read_text(encoding="utf-8")


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


status = function_body("static int lua_online_status")
table = function_body("static void push_online_api_table")

assert table.count("lua_setfield") == 1
assert '"status"' in table
assert "cosmetic" not in table.lower()
assert "cosmetic" not in status.lower()

for removed in (
    "mod.online.set_cosmetic_profile",
    "mod.online.remote_cosmetic_profile",
    "mod.online.mark_cosmetic_profile_applied",
    "mod.online.set_cosmetic_asset",
    "mod.online.remote_cosmetic_asset",
    "mod.online.mark_cosmetic_asset_applied",
):
    assert removed not in DOCS

print("public mod.online cosmetics-surface removal checks: OK")
