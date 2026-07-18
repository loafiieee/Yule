import re
import shlex
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BUILD = (ROOT / "compile.sh").read_text(encoding="utf-8")

match = re.search(r"sources=\(\s*(.*?)\s*\)", BUILD, re.DOTALL)
assert match, "compile.sh is missing its sources array"

listed = set(shlex.split(match.group(1), comments=True, posix=True))
expected = {path.name for path in ROOT.glob("*.c")}

missing = sorted(expected - listed)
stale = sorted(listed - expected)
assert not missing, f"compile.sh omits root implementation files: {missing}"
assert not stale, f"compile.sh lists missing/non-root implementation files: {stale}"
assert BUILD.count("gcc -m32 -shared") == 1, "compile.sh must link the verified DLL once"
assert "cp -f build/SDL2_test.dll SDL2.dll" in BUILD, (
    "compile.sh must install the exact verified build artifact"
)

print(f"compile source list checks: OK ({len(listed)} implementation files)")
