#!/usr/bin/env python3
"""Pin the mod Lua content-registry native-visual contract.

lua_manager.c is coupled to fixed native game addresses, so this focused
source audit verifies the public parser/query bridge without linking it into a
standalone executable.
"""

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "lua_manager.c").read_text(encoding="utf-8")


def function_body(name: str) -> str:
    marker = f"{name}("
    start = SOURCE.find(marker)
    assert start >= 0, f"missing function {name}"
    brace = SOURCE.find("{", start)
    assert brace >= 0, f"missing body for {name}"
    depth = 0
    for position in range(brace, len(SOURCE)):
        if SOURCE[position] == "{":
            depth += 1
        elif SOURCE[position] == "}":
            depth -= 1
            if depth == 0:
                return SOURCE[brace + 1 : position]
    raise AssertionError(f"unterminated function {name}")


known_keys = function_body("lua_content_known_tile_key")
register = function_body("lua_content_tx_register_tile")
query = function_body("lua_content_push_tile")

assert '"native_visual"' in known_keys
assert (
    'lua_content_read_string(Ls, 2, "native_visual", 0,' in register
), "native_visual must remain an optional, strictly typed string"

accepted = re.findall(r'strcmp\(native_visual, "([^"]+)"\)', register)
assert accepted == ["replace", "underlay"], (
    "native_visual must accept only the two documented spellings"
)
assert '!native_visual[0] || strcmp(native_visual, "replace") == 0' in register
assert "input.flags |= CONTENT_TILE_NATIVE_VISUAL_UNDERLAY;" in register
assert (
    'return lua_content_fail(Ls, "tile.native_visual must be replace or underlay");'
    in register
)

assert "tile->flags & CONTENT_TILE_NATIVE_VISUAL_UNDERLAY" in query
assert '? "underlay" : "replace"' in query
assert 'lua_setfield(Ls, -2, "native_visual")' in query

print("lua_content_native_visual_static_test: all checks passed")
