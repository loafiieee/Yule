from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
HEADER = (ROOT / "mod_json.h").read_text(encoding="utf-8")
SOURCE = (ROOT / "mod_json.c").read_text(encoding="utf-8")
LUA = (ROOT / "lua_manager.c").read_text(encoding="utf-8")
API = (ROOT / "mod_api.c").read_text(encoding="utf-8")
BUILD = (ROOT / "compile.sh").read_text(encoding="utf-8")
RUNNER = (ROOT / "tests" / "run_core_native_tests.ps1").read_text(encoding="utf-8")
DOCS = (ROOT / "docs-site" / "api-data.js").read_text(encoding="utf-8")


for limit in (
    "#define MOD_JSON_MAX_INPUT_BYTES (1024u * 1024u)",
    "#define MOD_JSON_MAX_OUTPUT_BYTES (1024u * 1024u)",
    "#define MOD_JSON_MAX_STRING_BYTES (256u * 1024u)",
    "#define MOD_JSON_MAX_DEPTH 32",
    "#define MOD_JSON_MAX_NODES 65536",
):
    assert limit in HEADER

for guard in (
    "json_utf8_sequence",
    "high surrogate lacks low surrogate",
    "duplicate object key",
    "number is outside the finite double range",
    "table cycle detected",
    "sparse JSON arrays are not supported",
    "mixed string and numeric table keys are ambiguous",
    "maximum node count 65536 exceeded",
    "maximum depth 32 exceeded",
):
    assert guard in SOURCE

assert "qsort(keys, string_count" in SOURCE
assert '_create_locale(LC_NUMERIC, "C")' in SOURCE
assert "_snprintf_l(" in SOURCE
assert "_strtod_l(" in SOURCE
assert SOURCE.count("_free_locale(") >= 2
assert "lua_getmetatable" in SOURCE
assert "lua_setmetatable" in SOURCE
assert "luaL_load" not in SOURCE
assert "lua_pcall" not in SOURCE
assert "tostring" not in SOURCE

for field, function in (
    ("encode", "mod_json_lua_encode"),
    ("decode", "mod_json_lua_decode"),
    ("array", "mod_json_lua_array"),
    ("object", "mod_json_lua_object"),
    ("is_null", "mod_json_lua_is_null"),
):
    assert re.search(
        rf"lua_pushcfunction\(Ls,\s*{function}\);\s*"
        rf'lua_setfield\(Ls,\s*-2,\s*"{field}"\)',
        LUA,
    )
assert 'lua_setfield(Ls, -2, "null")' in LUA
assert 'lua_setfield(Ls, -2, "json")' in LUA
assert '"json.v1"' in API

for member in (
    "mod.json.null",
    "mod.json.encode",
    "mod.json.decode",
    "mod.json.array",
    "mod.json.object",
    "mod.json.is_null",
):
    assert member in DOCS

for source in ("lua_manager.c", "mod_api.c", "mod_json.c"):
    assert source in BUILD
assert "'tests\\mod_json_test.c', 'mod_json.c'" in RUNNER
assert "'tests\\mod_json_integration_static_test.py'" in RUNNER

print("bounded mod.json integration static checks: OK")
