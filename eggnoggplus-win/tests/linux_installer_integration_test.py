from __future__ import annotations

import hashlib
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import tempfile
from threading import Thread
import zipfile


ROOT = Path(__file__).resolve().parents[1]
INSTALLER = ROOT / "dist" / "installer" / "linux" / "install-linux.sh"


class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, _format: str, *_args: object) -> None:
        pass


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    if os.name != "posix":
        print("Linux Wine installer integration test: skipped on non-POSIX host")
        return 0

    with tempfile.TemporaryDirectory(prefix="yule linux installer ") as name:
        temp = Path(name)
        package = temp / "installer"
        package.mkdir()
        with zipfile.ZipFile(ROOT / "dist/EGGNOGG+_framework_installer_linux.zip") as archive:
            archive.extractall(package)
        installer = package / "install-linux.sh"
        home = temp / "home"
        source = temp / "source game"
        installed = temp / "installed game"
        channel = temp / "channel"
        fake_bin = temp / "bin"
        prefix = temp / "wine prefix"
        for directory in (home, source, channel, fake_bin, prefix):
            directory.mkdir(parents=True)
        steam_root = home / ".local" / "share" / "Steam"
        steam_config = steam_root / "userdata" / "123456" / "config"
        steam_config.mkdir(parents=True)
        malformed_vdf = steam_config / "shortcuts.vdf"
        malformed_vdf.write_bytes(b"\x03broken\0")

        vanilla = b"vanilla SDL"
        proxy = b"verified modframework proxy\0YULE_CHANNEL_SWITCH=1\0YULE_FRAMEWORK_VERSION=99.1\0"
        runtime = b"verified runtime"
        (source / "eggnoggplus.exe").write_bytes(b"fake Windows game")
        (source / "SDL2.dll").write_bytes(vanilla)
        (source / "keep-user-file.txt").write_text("keep", encoding="utf-8")
        (channel / "SDL2.dll").write_bytes(proxy)
        (channel / "libgcc_s_dw2-1.dll").write_bytes(runtime)

        xdg_state = temp / "xdg-handler"
        xdg_state.write_text("previous.desktop\n", encoding="utf-8")
        (fake_bin / "wine-test").write_text(
            "#!/usr/bin/env bash\n"
            "printf '%s\\n' \"$WINEPREFIX\" \"$@\" > \"$YULE_WINE_TRACE\"\n",
            encoding="utf-8",
        )
        (fake_bin / "xdg-mime").write_text(
            "#!/usr/bin/env bash\n"
            "set -eu\n"
            "if [ \"$1\" = query ]; then cat \"$YULE_XDG_STATE\"; exit 0; fi\n"
            "if [ \"${YULE_XDG_REJECT:-0}\" = 1 ]; then exit 4; fi\n"
            "if [ \"$1\" = default ]; then printf '%s\\n' \"$2\" > \"$YULE_XDG_STATE\"; exit 0; fi\n"
            "exit 2\n",
            encoding="utf-8",
        )
        (fake_bin / "update-desktop-database").write_text(
            "#!/usr/bin/env bash\nexit 0\n", encoding="utf-8"
        )
        (fake_bin / "desktop-file-validate").write_text(
            "#!/usr/bin/env bash\n"
            "grep -q '^Exec=' \"$1\" && grep -q '^Path=' \"$1\" && "
            "! grep -q '^Path=\"' \"$1\"\n",
            encoding="utf-8",
        )
        kde_cache_state = temp / "kde-cache-refreshed"
        (fake_bin / "kbuildsycoca6").write_text(
            "#!/usr/bin/env bash\n"
            "set -eu\n"
            "[ \"${1:-}\" = --noincremental ]\n"
            ": > \"$YULE_KDE_CACHE_STATE\"\n",
            encoding="utf-8",
        )
        for executable in fake_bin.iterdir():
            executable.chmod(0o755)

        server = ThreadingHTTPServer(
            ("127.0.0.1", 0),
            lambda *args, **kwargs: QuietHandler(
                *args, directory=str(channel), **kwargs
            ),
        )
        base = f"http://127.0.0.1:{server.server_port}/"
        latest = {
            "channel_version": 1,
            "version": "99.1",
            "release_channel": "stable",
            "channel_switch": 1,
            "base": base,
            "files": [
                {
                    "path": "SDL2.dll",
                    "sha256": digest(proxy),
                    "size": len(proxy),
                    "overwrite": True,
                },
                {
                    "path": "libgcc_s_dw2-1.dll",
                    "sha256": digest(runtime),
                    "size": len(runtime),
                    "overwrite": True,
                },
            ],
        }
        (channel / "latest.json").write_text(
            json.dumps(latest), encoding="utf-8"
        )
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()

        env = os.environ.copy()
        env.update(
            {
                "HOME": str(home),
                "XDG_DATA_HOME": str(home / "data"),
                "XDG_STATE_HOME": str(home / "state"),
                "XDG_BIN_HOME": str(home / "bin"),
                "YULE_XDG_STATE": str(xdg_state),
                "YULE_KDE_CACHE_STATE": str(kde_cache_state),
                "YULE_WINE_TRACE": str(temp / "wine-trace"),
                "PATH": str(fake_bin) + os.pathsep + env.get("PATH", ""),
            }
        )
        command = [
            "bash",
            str(installer),
            "--game-path",
            str(source / "eggnoggplus.exe"),
            "--install-dir",
            str(installed),
            "--wine-prefix",
            str(prefix),
            "--wine-bin",
            "wine-test",
            "--channel-url",
            base + "latest.json",
        ]
        try:
            rejected = subprocess.run(
                command,
                cwd=ROOT,
                env={**env, "YULE_XDG_REJECT": "1"},
                stdin=subprocess.DEVNULL,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=30,
                check=False,
            )
            assert rejected.returncode == 2, rejected.stdout + rejected.stderr
            assert "xdg-mime rejected yule:// registration" in rejected.stdout
            failed_receipt = json.loads(
                (home / "state" / "yule" / "install-linux.json").read_text(encoding="utf-8")
            )
            assert failed_receipt["steps"]["deep_links"] == "failed"
            assert failed_receipt["steps"]["framework"] == "ok"
            assert failed_receipt["steps"]["steam"] == "failed"
            assert "unsupported field type" in rejected.stdout
            assert malformed_vdf.read_bytes() == b"\x03broken\0"
            assert failed_receipt["desktop"]["file_sha256"]
            malformed_vdf.unlink()
            repaired_command = command.copy()
            repaired_command[repaired_command.index(str(source / "eggnoggplus.exe"))] = str(
                installed / "eggnoggplus.exe"
            )
            result = subprocess.run(
                repaired_command,
                cwd=ROOT,
                env=env,
                stdin=subprocess.DEVNULL,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=30,
                check=False,
            )
            empty_steam = temp / "empty-steam"
            (empty_steam / "userdata").mkdir(parents=True)
            no_accounts = subprocess.run(
                repaired_command,
                cwd=ROOT,
                env={**env, "STEAM_ROOT": str(empty_steam)},
                stdin=subprocess.DEVNULL,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=30,
                check=False,
            )
            assert no_accounts.returncode == 2, no_accounts.stdout + no_accounts.stderr
            assert "no signed-in userdata accounts" in no_accounts.stdout
            flatpak_steam = home / ".var/app/com.valvesoftware.Steam/data/Steam"
            (flatpak_steam / "userdata/654321/config").mkdir(parents=True)
            sandboxed = subprocess.run(
                repaired_command, cwd=ROOT,
                env={**env, "STEAM_ROOT": str(flatpak_steam)},
                stdin=subprocess.DEVNULL, text=True, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, timeout=30, check=False,
            )
            assert sandboxed.returncode == 2, sandboxed.stdout + sandboxed.stderr
            assert "uses flatpak sandboxing" in sandboxed.stdout
            assert not (flatpak_steam / "userdata/654321/config/shortcuts.vdf").exists()
            result = subprocess.run(
                repaired_command, cwd=ROOT, env=env, stdin=subprocess.DEVNULL,
                text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                timeout=30, check=False,
            )
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=5)
        assert result.returncode == 0, result.stdout + result.stderr

        receipt_path = home / "state" / "yule" / "install-linux.json"
        receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
        wrapper = home / "bin" / "yule-eggnoggplus"
        desktop = home / "data" / "applications" / "yule-eggnoggplus.desktop"
        assert (installed / "mods").is_dir()
        assert (installed / "maps").is_dir()
        user_map = installed / "maps" / "user-map"
        user_map.mkdir()
        (user_map / "data.map").write_bytes(b"user map content")
        assert (installed / "SDL2.dll").read_bytes() == proxy
        assert json.loads((installed / "mods/update_channel.json").read_text())["release_channel"] == "stable"
        assert (installed / "SDL2_real.dll").read_bytes() == vanilla
        assert (installed / "libgcc_s_dw2-1.dll").read_bytes() == runtime
        assert (installed / "YuleUpdater.exe").is_file()
        assert (installed / "keep-user-file.txt").is_file()
        assert receipt["platform"] == "linux-wine"
        assert receipt["wine_prefix"] == str(prefix)
        assert receipt["adopted_vanilla"] is True
        assert wrapper.is_file() and os.access(wrapper, os.X_OK)
        assert '"--yule-uri=$uri"' in wrapper.read_text(encoding="utf-8")
        uri = "yule://queue/casual?x=1&y=2"
        launched = subprocess.run([str(wrapper), uri], env=env, check=False, timeout=5)
        assert launched.returncode == 0
        assert (temp / "wine-trace").read_text(encoding="utf-8").splitlines() == [
            str(prefix), str(installed / "eggnoggplus.exe"), f"--yule-uri={uri}"
        ]
        refused = subprocess.run([str(wrapper), "https://example.org"], env=env,
                                 capture_output=True, text=True, check=False, timeout=5)
        assert refused.returncode == 2
        assert "x-scheme-handler/yule" in desktop.read_text(encoding="utf-8")
        assert xdg_state.read_text(encoding="utf-8").strip() == "yule-eggnoggplus.desktop"
        assert kde_cache_state.is_file(), "Plasma application cache was not refreshed"
        assert "no interactive input; using the default" in result.stdout
        steam_vdf = steam_config / "shortcuts.vdf"
        assert steam_vdf.is_file()
        steam_bytes = steam_vdf.read_bytes()
        assert b"appname\0Eggnogg+\0" in steam_bytes
        assert b"exe\0\"" + str(wrapper).encode("utf-8") + b"\"\0" in steam_bytes
        assert receipt["steam"]["applied"] is True
        assert receipt["steam"]["accounts"] == ["123456"]
        steam_app_id = receipt["steam"]["appid"]
        steam_icon = steam_config / "grid" / f"{steam_app_id}_icon.png"
        assert steam_icon.is_file()

        changed = b"user-modified runtime"
        (installed / "libgcc_s_dw2-1.dll").write_bytes(changed)
        removed = subprocess.run(
            ["bash", str(installer), "--uninstall"],
            cwd=ROOT,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=30,
            check=False,
        )
        assert removed.returncode == 0, removed.stdout + removed.stderr
        assert (user_map / "data.map").read_bytes() == b"user map content"
        assert (installed / "SDL2.dll").read_bytes() == vanilla
        assert not (installed / "mods/update_channel.json").exists()
        assert not (installed / "SDL2_real.dll").exists()
        assert (installed / "libgcc_s_dw2-1.dll").read_bytes() == changed
        assert (installed / "keep-user-file.txt").is_file()
        assert not receipt_path.exists()
        assert not wrapper.exists() and not desktop.exists()
        assert xdg_state.read_text(encoding="utf-8").strip() == "previous.desktop"
        assert not steam_icon.exists()
        assert b"appname\0Eggnogg+\0" not in steam_vdf.read_bytes()

    print("Linux Wine installer integration test: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
