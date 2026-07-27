from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "bytebeat_js.c").read_text(encoding="utf-8")
CHAKRA = (ROOT / "bytebeat_chakra.c").read_text(encoding="utf-8")
HEADER = (ROOT / "bytebeat_js.h").read_text(encoding="utf-8")
BUILD = (ROOT / "compile.sh").read_text(encoding="utf-8")
RUNNER = (ROOT / "tests" / "run_core_native_tests.ps1").read_text(
    encoding="utf-8"
)
VENDOR = ROOT / "third_party" / "quickjs-ng"

assert (VENDOR / "quickjs-amalgam.c").is_file()
assert (VENDOR / "quickjs.h").is_file()
assert (VENDOR / "LICENSE").is_file()
assert "third_party/quickjs-ng/quickjs-amalgam.c" in BUILD
assert "quickjs-libc" not in BUILD

for contract in (
    "BYTEBEAT_JS_MAX_SOURCE (8u * 1024u * 1024u)",
    "BYTEBEAT_JS_MEMORY_LIMIT (64u * 1024u * 1024u)",
    "BYTEBEAT_JS_STACK_LIMIT (256u * 1024u)",
    "BYTEBEAT_JS_COMPILE_MIN_BUDGET_MS 100u",
    "BYTEBEAT_JS_COMPILE_MAX_BUDGET_MS 5000u",
    "BYTEBEAT_JS_CALLBACK_BUDGET_MS 50u",
    "BYTEBEAT_JS_CALLBACK_MAX_BUDGET_MS 250u",
    "BYTEBEAT_JS_BATCH_FRAMES 4096u",
):
    assert contract in HEADER

for call in (
    "JS_SetMemoryLimit",
    "JS_SetMaxStackSize",
    "JS_SetInterruptHandler",
    "bytebeat_js_begin_callback",
    "bytebeat_js_render",
    "JS_IsArray",
    "JS_NewTypedArray",
    "JS_GetTypedArrayBuffer",
):
    assert call in SOURCE

assert "QJS_BUILD_LIBC" not in BUILD
assert "bytebeat_chakra.c" in BUILD
for contract in (
    "GetSystemDirectoryW",
    'wcscat(system_path, L"\\\\Chakra.dll")',
    "JS_RUNTIME_ALLOW_SCRIPT_INTERRUPT",
    "JS_RUNTIME_DISABLE_FATAL_ON_OOM",
    "set_memory_limit",
    "CreateTimerQueueTimer",
):
    assert contract in CHAKRA
assert "bytebeat_chakra_create" in SOURCE
assert "bytebeat_js_render_budget_ms" in SOURCE
assert "tests\\bytebeat_js_test.c" in RUNNER
assert "third_party\\quickjs-ng\\quickjs-amalgam.c" in RUNNER

print("sandboxed Dollchan JavaScript build/security wiring checks: OK")
