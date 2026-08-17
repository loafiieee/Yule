from pathlib import Path
import json
import re


ROOT = Path(__file__).resolve().parents[1]
HEADER = (ROOT / "mod_api.h").read_text(encoding="utf-8")
COMPAT = (ROOT / "mod_api.c").read_text(encoding="utf-8")
LUA = (ROOT / "lua_manager.c").read_text(encoding="utf-8")
LUA_HEADER = (ROOT / "lua_manager.h").read_text(encoding="utf-8")
HOOKS = (ROOT / "hooks.c").read_text(encoding="utf-8")
BUILD = (ROOT / "compile.sh").read_text(encoding="utf-8")
RUNNER = (ROOT / "tests" / "run_core_native_tests.ps1").read_text(encoding="utf-8")
DOCS = (ROOT / "docs-site" / "api-data.js").read_text(encoding="utf-8")
SCHEMA = json.loads((ROOT / "mod.schema.json").read_text(encoding="utf-8"))


assert "#define MOD_API_MAJOR 1" in HEADER
assert re.search(r"#define MOD_API_REVISION\s+[1-9][0-9]*", HEADER)
assert "#define MOD_API_VERSION MOD_API_MAJOR" in HEADER
assert "MOD_API_REQUIRED_CAPABILITIES_MAX 32" in HEADER

capabilities = re.findall(r'^\s+"([a-z][a-z0-9._-]*)",?$', COMPAT, re.MULTILINE)
assert len(capabilities) >= 10
assert capabilities == sorted(capabilities)
assert len(capabilities) == len(set(capabilities))
for required in (
    "api.capabilities",
    "console.commands",
    "content.tiles.v1",
    "fs.pick_file",
    "map.lua.v1",
    "storage.v1",
):
    assert required in capabilities

for manifest_key in ('"api_version"', '"api_revision"', '"api_requires"'):
    assert manifest_key in LUA
assert "json_cursor_parse_capability_array" in LUA
assert "mod_api_capability_name_valid" in LUA
assert "mod_manifest_api_compatible" in LUA
assert LUA.count("mod_manifest_api_compatible(") >= 3  # definition, scan, load
assert "&manifest->api_requires.ids[0][0]" in LUA
assert "manifest->api_revision" in LUA
assert "only for local testing" in LUA

for field in ('"major"', '"revision"', '"capabilities"', '"has"', '"require"'):
    assert field in LUA
assert "static void push_api_api_table" in LUA
assert "push_api_api_table(Ls)" in LUA
assert 'lua_setfield(Ls, -2, "api")' in LUA
assert '"framework_api_revision"' in LUA
assert '"api_revision"' in LUA

for declaration in (
    "lua_manager_framework_api_revision(void)",
    "lua_manager_framework_api_capability_count(void)",
):
    assert declaration in LUA_HEADER
    assert declaration in LUA
assert "major=%d revision=%d capabilities=%d" in HOOKS

properties = SCHEMA["properties"]
assert properties["api_version"]["minimum"] == 1
assert properties["api_revision"]["minimum"] == 0
assert properties["api_requires"]["maxItems"] == 32
assert properties["api_requires"]["uniqueItems"] is True
assert properties["api_requires"]["items"]["pattern"] == "^[a-z][a-z0-9._-]*$"

for member in (
    "mod.api.major",
    "mod.api.revision",
    "mod.api.capabilities",
    "mod.api.has",
    "mod.api.require",
):
    assert member in DOCS
assert "Deprecated functions remain" in (
    ROOT / "MODDING.md"
).read_text(encoding="utf-8")

for source in ("lua_manager.c", "mod_api.c"):
    assert source in BUILD
assert "'tests\\mod_api_test.c', 'mod_api.c'" in RUNNER
assert "'tests\\mod_api_integration_static_test.py'" in RUNNER

print("mod API manifest/runtime integration static checks: OK")
