from __future__ import annotations

import hashlib
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import tempfile
from threading import Thread


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
        home = temp / "home"
        source = temp / "source game"
        installed = temp / "installed game"
        channel = temp / "channel"
        fake_bin = temp / "bin"
        prefix = temp / "wine prefix"
        for directory in (home, source, channel, fake_bin, prefix):
            directory.mkdir(parents=True)

        vanilla = b"vanilla SDL"
        proxy = b"verified modframework proxy"
        runtime = b"verified runtime"
        (source / "eggnoggplus.exe").write_bytes(b"fake Windows game")
        (source / "SDL2.dll").write_bytes(vanilla)
        (source / "keep-user-file.txt").write_text("keep", encoding="utf-8")
        (channel / "SDL2.dll").write_bytes(proxy)
        (channel / "libgcc_s_dw2-1.dll").write_bytes(runtime)

        xdg_state = temp / "xdg-handler"
        xdg_state.write_text("previous.desktop\n", encoding="utf-8")
        (fake_bin / "wine-test").write_text(
            "#!/usr/bin/env bash\nexit 90\n", encoding="utf-8"
        )
        (fake_bin / "xdg-mime").write_text(
            "#!/usr/bin/env bash\n"
            "set -eu\n"
            "if [ \"$1\" = query ]; then cat \"$YULE_XDG_STATE\"; exit 0; fi\n"
            "if [ \"$1\" = default ]; then printf '%s\\n' \"$2\" > \"$YULE_XDG_STATE\"; exit 0; fi\n"
            "exit 2\n",
            encoding="utf-8",
        )
        (fake_bin / "update-desktop-database").write_text(
            "#!/usr/bin/env bash\nexit 0\n", encoding="utf-8"
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
                "PATH": str(fake_bin) + os.pathsep + env.get("PATH", ""),
            }
        )
        command = [
            "bash",
            str(INSTALLER),
            "--yes",
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
            result = subprocess.run(
                command,
                cwd=ROOT,
                env=env,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=30,
                check=False,
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
        assert (installed / "SDL2_real.dll").read_bytes() == vanilla
        assert (installed / "libgcc_s_dw2-1.dll").read_bytes() == runtime
        assert (installed / "YuleUpdater.exe").is_file()
        assert (installed / "keep-user-file.txt").is_file()
        assert receipt["platform"] == "linux-wine"
        assert receipt["wine_prefix"] == str(prefix)
        assert receipt["adopted_vanilla"] is True
        assert wrapper.is_file() and os.access(wrapper, os.X_OK)
        assert '"--yule-uri=$uri"' in wrapper.read_text(encoding="utf-8")
        assert "x-scheme-handler/yule" in desktop.read_text(encoding="utf-8")
        assert xdg_state.read_text(encoding="utf-8").strip() == "yule-eggnoggplus.desktop"

        changed = b"user-modified runtime"
        (installed / "libgcc_s_dw2-1.dll").write_bytes(changed)
        removed = subprocess.run(
            ["bash", str(INSTALLER), "--uninstall"],
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
        assert not (installed / "SDL2_real.dll").exists()
        assert (installed / "libgcc_s_dw2-1.dll").read_bytes() == changed
        assert (installed / "keep-user-file.txt").is_file()
        assert not receipt_path.exists()
        assert not wrapper.exists() and not desktop.exists()
        assert xdg_state.read_text(encoding="utf-8").strip() == "previous.desktop"

    print("Linux Wine installer integration test: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
