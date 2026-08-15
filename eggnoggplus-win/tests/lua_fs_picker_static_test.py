from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
MANAGER = (ROOT / "lua_manager.c").read_text(encoding="utf-8")
SOURCE = (ROOT / "mod_fs.c").read_text(encoding="utf-8")
HEADER = (ROOT / "mod_fs.h").read_text(encoding="utf-8")
BUILD = (ROOT / "compile.sh").read_text(encoding="utf-8")
RUNNER = (ROOT / "tests" / "run_core_native_tests.ps1").read_text(encoding="utf-8")
DOCS = (ROOT / "docs-site" / "api-data.js").read_text(encoding="utf-8")


def function_body(name: str, next_name: str) -> str:
    match = re.search(
        rf"static int {re.escape(name)}\s*\([^)]*\)\s*\{{(?P<body>.*?)"
        rf"\n\}}\n\n(?:/\*.*?\*/\s*)?static int {re.escape(next_name)}\s*\(",
        SOURCE,
        flags=re.DOTALL,
    )
    assert match, f"could not isolate {name}"
    return match.group("body")


picker = function_body("lua_fs_pick_file", "lua_fs_pick_character_file")
dialog = function_body("fs_picker_run", "lua_fs_pick_file")

assert '#define FS_PICKER_MAX_FILTERS 16' in SOURCE
assert '#define FS_PICKER_MAX_PATTERNS 16' in SOURCE
assert '#define FS_PICKER_FILTER_WCHARS 4096' in SOURCE
assert '#define FS_PICKER_PATH_WCHARS 32768' in SOURCE

for option in ('"title"', '"filters"', '"patterns"', '"allow_all"', '"filter_index"'):
    assert option in picker, f"missing pick_file option {option}"

assert 'fs_picker_owner_guard(L, "mod.fs.pick_file")' in picker
assert "filter_count > FS_PICKER_MAX_FILTERS" in picker
assert "pattern_count > FS_PICKER_MAX_PATTERNS" in picker
assert "fs_picker_pattern_valid" in picker
assert "filter_index > (DWORD)filter_count" in picker

assert "GetOpenFileNameW" in dialog
assert "GetOpenFileNameA" not in dialog
assert "MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS" in SOURCE
assert "WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS" in dialog
for flag in (
    "OFN_EXPLORER",
    "OFN_FILEMUSTEXIST",
    "OFN_PATHMUSTEXIST",
    "OFN_NOCHANGEDIR",
):
    assert flag in dialog
assert "CommDlgExtendedError()" in dialog
assert '"cancelled"' in dialog

assert "fs_register_owner_function(" in SOURCE
assert 'lua_fs_pick_file, "pick_file"' in SOURCE
assert "mod_fs_lua_push_api(Ls, &mod->enabled)" in MANAGER
assert "void mod_fs_lua_push_api" in HEADER
assert "mod_fs.c" in BUILD

# API v1 compatibility remains callable, but new documentation leads with the
# general picker and labels the old helper as deprecated.
assert 'lua_fs_pick_character_file, "pick_character_file"' in SOURCE
assert 'lua_fs_pick_folder, "pick_folder"' in SOURCE
assert "SHBrowseForFolderW" in SOURCE
assert "SHGetPathFromIDListW" in SOURCE
assert "FILE_ATTRIBUTE_REPARSE_POINT" in SOURCE
assert "if (is_directory)" in SOURCE
assert "continue;" in SOURCE
assert "mod.fs.pick_file" in DOCS
assert "deprecated" in DOCS.lower()

assert "tests\\lua_fs_picker_static_test.py" in RUNNER

print("Lua filesystem picker static checks: OK")
