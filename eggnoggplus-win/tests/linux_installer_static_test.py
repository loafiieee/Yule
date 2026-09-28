"""Packaging, syntax, and guard checks for the rewritten Linux installer."""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
LINUX = ROOT / "dist/installer/linux"
SHELL = LINUX / "install-linux.sh"
PYTHON = LINUX / "install-linux.py"
UNINSTALL = LINUX / "UNINSTALL-LINUX.sh"
BUILD = (ROOT / "tools/build_release.ps1").read_text(encoding="utf-8")
SOURCE = PYTHON.read_text(encoding="utf-8")

assert SHELL.read_text(encoding="utf-8").startswith("#!/usr/bin/env bash\n")
assert 'exec python3 "$script_dir/install-linux.py" "$@"' in SHELL.read_text(encoding="utf-8")
assert 'install-linux.sh" --uninstall' in UNINSTALL.read_text(encoding="utf-8")
for marker in (
    "non-local release URLs must use HTTPS", "size or SHA-256 mismatch",
    "item.get(\"overwrite\") is not True", "SDL2_real.dll",
    "game / \"maps\"", "game / \"mods\"", "yule://",
    "xdg-mime", "kbuildsycoca6", "Steam shortcut verification failed",
    "no signed-in userdata accounts", "flatpak", "snap",
    "Framework installed, but integration failed", "managed_files",
):
    assert marker in SOURCE, marker
assert "eval(" not in SOURCE and "shell=True" not in SOURCE
assert "install-linux.py" in BUILD
assert "create_linux_installer_zip.py" in BUILD

subprocess.run([sys.executable, "-m", "py_compile", str(PYTHON)], check=True)
bash = shutil.which("bash")
if bash:
    for path in (SHELL, UNINSTALL):
        result = subprocess.run([bash, "-n", str(path)], capture_output=True,
                                text=True, check=False, timeout=10)
        assert result.returncode == 0, result.stderr

package = ROOT / "dist/EGGNOGG+_framework_installer_linux.zip"
assert package.is_file()
with zipfile.ZipFile(package) as archive:
    assert archive.testzip() is None
    assert {"install-linux.sh", "install-linux.py", "UNINSTALL-LINUX.sh",
            "README.md", "YuleUpdater.exe", "assets/steam_icon.png"} <= set(archive.namelist())
    assert (archive.getinfo("install-linux.sh").external_attr >> 16) & 0o111
    packaged = archive.read("install-linux.py").decode("utf-8-sig").replace("\r\n", "\n")
    assert "Steam shortcut verification failed" in packaged
    assert "ARTWORK = {" in packaged
    without_art = lambda value: re.sub(r"(?s)# ==ARTWORK-BEGIN==.*?# ==ARTWORK-END==",
                                       "# embedded artwork", value).strip()
    assert without_art(packaged) == without_art(SOURCE), "Linux installer ZIP is stale"
    assert archive.read("install-linux.sh").decode("utf-8-sig").replace("\r\n", "\n").strip() == SHELL.read_text(encoding="utf-8").strip()

print("Linux Wine installer static/syntax/ZIP checks: OK")
