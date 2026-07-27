import re
import shlex
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BUILD = (ROOT / "compile.sh").read_text(encoding="utf-8")

match = re.search(r"sources=\(\s*(.*?)\s*\)", BUILD, re.DOTALL)
assert match, "compile.sh is missing its sources array"

listed = set(shlex.split(match.group(1), comments=True, posix=True))
standalone_sources = {"updater_helper.c"}
expected = {path.name for path in ROOT.glob("*.c")} - standalone_sources

missing = sorted(expected - listed)
stale = sorted(listed - expected)
assert not missing, f"compile.sh omits root implementation files: {missing}"
assert not stale, f"compile.sh lists missing/non-root implementation files: {stale}"
assert BUILD.count("gcc -m32 -shared") == 1, "compile.sh must link the verified DLL once"
assert "cp -f build/SDL2_test.dll SDL2.dll" in BUILD, (
    "compile.sh must install the exact verified build artifact"
)
assert (
    "-DUPDATE_EXT_HELPER -municode -mwindows -static -static-libgcc" in BUILD
    and "-o build/YuleUpdater.exe updater_helper.c update_ext.c" in BUILD
    and "cp -f build/YuleUpdater.exe YuleUpdater.exe" in BUILD
), "compile.sh must build and install a self-contained one-shot updater"
assert "objdump -p build/YuleUpdater.exe" in BUILD
for replaceable_dll in (
    "SDL2.dll",
    "lua51.dll",
    "libgcc_s_dw2-1.dll",
    "libwinpthread-1.dll",
    "SDL2_mixer.dll",
):
    assert replaceable_dll in BUILD
toolchain_check = BUILD.index("command -v gcc")
build_dir_create = BUILD.index("mkdir -p build")
assert "/c/msys64/mingw32/bin" in BUILD, (
    "compile.sh must find the standard MinGW32 toolchain from non-MSYS shells"
)
assert toolchain_check < build_dir_create, (
    "compile.sh must reject a missing compiler before creating build output"
)
assert "EGGNOGGPLUS_SERIALIZER_TESTING" not in BUILD, (
    "release build must not enable the serializer's native-address test seam"
)

print(f"compile source list checks: OK ({len(listed)} implementation files)")
