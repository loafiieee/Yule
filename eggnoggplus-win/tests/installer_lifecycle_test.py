from __future__ import annotations

import hashlib
import atexit
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from threading import Thread
import uuid
import winreg


ROOT = Path(__file__).resolve().parents[1]
INSTALLER = ROOT / "dist" / "installer" / "windows" / "install.ps1"


class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, _format: str, *_args: object) -> None:
        pass


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def run_installer(
    game_exe: Path,
    game_dir: Path,
    manifest_dir: Path,
    original_root: Path,
    channel_url: str,
    protocol_root: str,
    *,
    uninstall: bool = False,
) -> subprocess.CompletedProcess[str]:
    powershell = shutil.which("powershell.exe") or shutil.which("powershell")
    if not powershell:
        raise RuntimeError("Windows PowerShell was not found")
    command = [
        powershell,
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        str(INSTALLER),
        "-NoReexec",
        "-Yes",
        "-OriginalRoot",
        str(original_root),
        "-SkipSteam",
        "-SkipShortcut",
        "-GamePath",
        str(game_exe),
        "-InstallDir",
        str(game_dir),
        "-ManifestDir",
        str(manifest_dir),
        "-ProtocolRegistryRoot",
        protocol_root,
        "-ChannelUrl",
        channel_url,
    ]
    if uninstall:
        command.append("-Uninstall")
    return subprocess.run(
        command,
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=30,
        check=False,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
    )


def main() -> int:
    protocol_suffix = rf"Software\YuleInstallerTests\{uuid.uuid4().hex}\yule"
    protocol_root = "HKCU:\\" + protocol_suffix

    def delete_registry_tree(path: str) -> None:
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path, 0,
                                winreg.KEY_READ | winreg.KEY_WRITE) as key:
                children: list[str] = []
                index = 0
                while True:
                    try:
                        children.append(winreg.EnumKey(key, index))
                        index += 1
                    except OSError:
                        break
            for child in children:
                delete_registry_tree(path + "\\" + child)
            winreg.DeleteKey(winreg.HKEY_CURRENT_USER, path)
        except FileNotFoundError:
            pass

    atexit.register(delete_registry_tree, protocol_suffix)

    with tempfile.TemporaryDirectory(prefix="eggnoggplus installer ") as temp_name:
        temp = Path(temp_name)
        game = temp / "selected game"
        manifest_dir = temp / "manifest"
        channel = temp / "channel"
        installer_payload = temp / "installer payload"
        game.mkdir()
        channel.mkdir()
        installer_payload.mkdir()

        game_exe = game / "eggnoggplus.exe"
        vanilla_sdl = b"vanilla SDL fixture"
        proxy_sdl = b"proxy fixture with modframework marker"
        runtime_name = "libgcc_s_dw2-1.dll"
        runtime_bytes = b"managed runtime fixture"
        updater_name = "YuleUpdater.exe"
        updater_bytes = b"one-shot updater fixture"
        (installer_payload / updater_name).write_bytes(updater_bytes)
        game_exe.write_bytes(b"fake game executable")
        (game / "SDL2.dll").write_bytes(vanilla_sdl)
        (game / "keep-user-file.txt").write_text("keep me", encoding="utf-8")
        (channel / "SDL2.dll").write_bytes(proxy_sdl)
        (channel / runtime_name).write_bytes(runtime_bytes)

        server = ThreadingHTTPServer(
            ("127.0.0.1", 0),
            lambda *args, **kwargs: QuietHandler(
                *args, directory=str(channel), **kwargs
            ),
        )
        base = f"http://127.0.0.1:{server.server_port}/"
        latest = {
            "channel_version": 1,
            "version": "installer-test",
            "base": base,
            "files": [
                {
                    "path": "SDL2.dll",
                    "sha256": sha256(proxy_sdl),
                    "size": len(proxy_sdl),
                    "overwrite": True,
                },
                {
                    "path": runtime_name,
                    "sha256": sha256(runtime_bytes),
                    "size": len(runtime_bytes),
                    "overwrite": True,
                },
            ],
        }
        (channel / "latest.json").write_text(
            json.dumps(latest), encoding="utf-8"
        )
        server_thread = Thread(target=server.serve_forever, daemon=True)
        server_thread.start()
        try:
            installed = run_installer(
                game_exe,
                game,
                manifest_dir,
                installer_payload,
                base + "latest.json",
                protocol_root,
            )
        finally:
            server.shutdown()
            server.server_close()
            server_thread.join(timeout=5)

        if installed.returncode != 0:
            raise AssertionError(
                f"installer failed\nOUT:\n{installed.stdout}\nERR:\n{installed.stderr}"
            )
        if (
            not (game / "SDL2.dll").is_file()
            or not (game / "SDL2_real.dll").is_file()
            or not (game / runtime_name).is_file()
            or (game / "SDL2.dll").read_bytes() != proxy_sdl
            or (game / "SDL2_real.dll").read_bytes() != vanilla_sdl
            or (game / runtime_name).read_bytes() != runtime_bytes
            or (game / updater_name).read_bytes() != updater_bytes
        ):
            raise AssertionError(
                "verified install did not adopt vanilla SDL and sync files\n"
                f"OUT:\n{installed.stdout}\nERR:\n{installed.stderr}"
            )
        manifest_path = manifest_dir / "install.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
        managed = {entry["path"]: entry for entry in manifest["managed_files"]}
        if (
            manifest["manifest_version"] != 2
            or manifest["installer_version"] != 2
            or Path(manifest["game_executable"]) != game_exe
            or Path(manifest["updater_executable"]) != game / updater_name
            or not manifest["deep_link_protocol"]["applied"]
            or str(game_exe) not in manifest["deep_link_protocol"]["command"]
            or "--yule-uri=%1" not in manifest["deep_link_protocol"]["command"]
            or set(managed) != {"SDL2.dll", runtime_name, updater_name}
        ):
            raise AssertionError("v2 uninstall manifest was incomplete")
        try:
            with winreg.OpenKey(
                winreg.HKEY_CURRENT_USER,
                protocol_suffix + r"\shell\open\command",
            ) as protocol_key:
                installed_protocol_command = winreg.QueryValueEx(
                    protocol_key, ""
                )[0]
        except OSError as exc:
            raise AssertionError("installer did not register the isolated protocol") from exc
        if installed_protocol_command != manifest["deep_link_protocol"]["command"]:
            raise AssertionError("registered protocol command did not match its receipt")

        modified_runtime = b"user replaced this runtime"
        (game / runtime_name).write_bytes(modified_runtime)
        removed = run_installer(
            game_exe,
            game,
            manifest_dir,
            installer_payload,
            base + "latest.json",
            protocol_root,
            uninstall=True,
        )
        if removed.returncode != 0:
            raise AssertionError(
                f"uninstaller failed\nOUT:\n{removed.stdout}\nERR:\n{removed.stderr}"
            )
        if (
            (game / "SDL2.dll").read_bytes() != vanilla_sdl
            or (game / "SDL2_real.dll").exists()
            or (game / runtime_name).read_bytes() != modified_runtime
            or (game / updater_name).exists()
            or not (game / "keep-user-file.txt").is_file()
            or manifest_path.exists()
            or (game / "install.json").exists()
        ):
            raise AssertionError(
                "uninstall did not restore vanilla or preserve modified/user files"
            )
        try:
            winreg.OpenKey(winreg.HKEY_CURRENT_USER, protocol_suffix).Close()
            raise AssertionError("uninstall did not remove its exact protocol handler")
        except FileNotFoundError:
            pass

        # A missing vanilla forwarder is a hard preflight failure: no framework
        # file or user data may be removed in this state.
        (game / "SDL2.dll").write_bytes(proxy_sdl)
        blocked = run_installer(
            game_exe,
            game,
            manifest_dir,
            installer_payload,
            base + "latest.json",
            protocol_root,
            uninstall=True,
        )
        if (
            blocked.returncode == 0
            or (game / "SDL2.dll").read_bytes() != proxy_sdl
            or not (game / "keep-user-file.txt").is_file()
            or "SDL2_real.dll is missing" not in blocked.stdout
        ):
            raise AssertionError("unsafe half-install uninstall did not fail closed")

        offline_game = temp / "offline fresh game"
        offline_manifest = temp / "offline manifest"
        offline_game.mkdir()
        offline_exe = offline_game / "eggnoggplus.exe"
        offline_exe.write_bytes(b"fake offline game")
        (offline_game / "SDL2.dll").write_bytes(vanilla_sdl)
        offline = run_installer(
            offline_exe,
            offline_game,
            offline_manifest,
            installer_payload,
            base + "latest.json",
            protocol_root,
        )
        if (
            offline.returncode != 0
            or (offline_game / "SDL2.dll").read_bytes() != vanilla_sdl
            or (offline_game / "SDL2_real.dll").exists()
            or "restored the original vanilla SDL2.dll" not in offline.stdout
        ):
            raise AssertionError(
                "incomplete fresh install did not roll back to vanilla\n"
                f"OUT:\n{offline.stdout}\nERR:\n{offline.stderr}"
            )

    delete_registry_tree(protocol_suffix)
    print("installer lifecycle checks: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
