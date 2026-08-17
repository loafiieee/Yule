from pathlib import Path
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[1]
INSTALLER = ROOT / "dist" / "installer" / "linux" / "install-linux.sh"
UNINSTALLER = ROOT / "dist" / "installer" / "linux" / "UNINSTALL-LINUX.sh"
RELEASE = (ROOT / "tools" / "build_release.ps1").read_text(encoding="utf-8")
SOURCE = INSTALLER.read_text(encoding="utf-8")

assert SOURCE.startswith("#!/usr/bin/env bash\n")
assert "set -Eeuo pipefail" in SOURCE
assert "--game-path" in SOURCE and "--wine-prefix" in SOURCE
assert "--uninstall" in SOURCE and "--skip-protocol" in SOURCE
assert "WINEPREFIX" in SOURCE and '"--yule-uri=$uri"' in SOURCE
assert "x-scheme-handler/yule" in SOURCE
assert "yule-eggnoggplus.desktop" in SOURCE
assert "xdg-mime query default" in SOURCE
assert "xdg-mime default yule-eggnoggplus.desktop" in SOURCE
assert "sha256sum" in SOURCE and "SHA-256 mismatch" in SOURCE
assert "item.get('overwrite') is not True" in SOURCE
assert "64 * 1024 * 1024" in SOURCE
assert "non-local release URLs must use HTTPS" in SOURCE
assert "SDL2_real.dll" in SOURCE and "ADOPTED_VANILLA" in SOURCE
assert "YuleUpdater.exe" in SOURCE
assert "managed_files" in SOURCE and "preserved modified managed file" in SOURCE
assert "install-linux.json" in SOURCE
assert "maps, mods, saves, and modified files were preserved" in SOURCE
assert "eval " not in SOURCE
assert "rm -rf /" not in SOURCE

assert UNINSTALLER.is_file()
assert 'install-linux.sh" --uninstall' in UNINSTALLER.read_text(encoding="utf-8")
for name in ("install-linux.sh", "UNINSTALL-LINUX.sh", "README.md"):
    assert f"$linuxInstallerSource '{name}'" in RELEASE
assert "EGGNOGG+_framework_installer_linux.zip" in RELEASE
assert "EGGNOGG+_framework_installer_windows.zip" in RELEASE

bash = shutil.which("bash")
if bash:
    for script in (INSTALLER, UNINSTALLER):
        checked = subprocess.run(
            [bash, "-n", script.relative_to(ROOT).as_posix()],
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
            timeout=10,
        )
        assert checked.returncode == 0, checked.stderr

print("Linux Wine installer static/syntax checks: OK")
