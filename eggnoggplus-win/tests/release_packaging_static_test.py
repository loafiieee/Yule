from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = (ROOT / "tools" / "build_release.ps1").read_text(encoding="utf-8")

preflight_end = SCRIPT.index("# --- channel tree")
preflight = SCRIPT[:preflight_end]

required = (
    "$Version -notmatch '^[0-9]+(?:\\.[0-9]+)*$'",
    "'#define\\s+FRAMEWORK_VERSION\\s+\"([^\"]+)\"'",
    "if ($sourceVersion -cne $Version)",
    "Refusing to advertise release $Version because update_ext.h still declares",
    "$built = Join-Path $GameDir 'build\\SDL2_test.dll'",
    "if (-not (Test-Path -LiteralPath $built -PathType Leaf))",
    "Refusing to package without the verified build\\SDL2_test.dll artifact.",
    "Get-FileHash -LiteralPath $sdl -Algorithm SHA256",
    "Get-FileHash -LiteralPath $built -Algorithm SHA256",
    "if ($deployedHash -ne $verifiedHash)",
    "Refusing to package a stale deployed SDL2.dll",
    "$builtUpdater = Join-Path $GameDir 'build\\YuleUpdater.exe'",
    "Refusing to package without the verified one-shot updater",
    "Get-FileHash -LiteralPath $updater -Algorithm SHA256",
    "Get-FileHash -LiteralPath $builtUpdater -Algorithm SHA256",
    "if ($updaterHash -ne $builtUpdaterHash)",
    "Refusing to package a stale YuleUpdater.exe",
    "$updaterImports",
    "Get-Command objdump",
    "'DLL Name:\\s*' + [regex]::Escape($replaceableImport)",
    "it imports replaceable payload",
    "'YULE_FRAMEWORK_VERSION=([0-9]+(?:\\.[0-9]+)*)'",
    "if ($binaryVersion -cne $Version)",
    "the deployed SDL2.dll was compiled",
    "$windowsInstallerSource = Join-Path $installerSourceRoot 'windows'",
    "$linuxInstallerSource = Join-Path $installerSourceRoot 'linux'",
    "$requiredInstallerSources",
    "missing installer source",
)
for text in required:
    assert text in preflight, f"release publisher is missing fail-closed check: {text}"

assert "shipping the DEPLOYED one" not in SCRIPT
assert "Copy-Item $built $sdl" not in SCRIPT
assert "Move-Item $built $sdl" not in SCRIPT
assert "Start-Process" not in SCRIPT
assert "Copy-Item" not in preflight and "Move-Item" not in preflight
WINDOWS = ROOT / "dist" / "installer" / "windows"
LINUX = ROOT / "dist" / "installer" / "linux"
assert "$windowsInstallerSource 'UNINSTALL.bat'" in SCRIPT
assert (WINDOWS / "UNINSTALL.bat").is_file()
for linux_installer_file in (
    "install-linux.sh",
    "UNINSTALL-LINUX.sh",
    "README.md",
):
    assert (LINUX / linux_installer_file).is_file()
    assert f"$linuxInstallerSource '{linux_installer_file}'" in SCRIPT
assert "assets\\steam_icon.png" in SCRIPT
assert SCRIPT.count("Copy-Item $updater") == 2
assert "EGGNOGG+_framework_installer_windows.zip" in SCRIPT
assert "EGGNOGG+_framework_installer_linux.zip" in SCRIPT
assert ".installer-staging-" in SCRIPT
assert "Set-Content -LiteralPath (Join-Path $windowsStage 'install.ps1')" in SCRIPT
assert "'YuleUpdater.exe'" not in SCRIPT[SCRIPT.index("$ReleaseFiles = @"):SCRIPT.index("# --- sanity")], (
    "the one-shot updater must not be included in its own replaceable update manifest"
)
assert "Publish releases\\$Version first" in SCRIPT
assert "Publish releases\\latest.json atomically LAST" in SCRIPT
assert "publish both installer zips" in SCRIPT

UPDATE_SOURCE = (ROOT / "update_ext.c").read_text(encoding="utf-8")
assert '"YULE_FRAMEWORK_VERSION=" FRAMEWORK_VERSION' in UPDATE_SOURCE

print("release packaging fail-closed static checks: OK")
