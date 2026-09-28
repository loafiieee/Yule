#!/usr/bin/env python3
"""Verified Wine install, desktop registration, Steam shortcut, and uninstall."""
from __future__ import annotations

import argparse
import base64
from collections import OrderedDict
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from urllib.parse import urljoin, urlparse
from urllib.request import urlopen
import zlib

HERE = Path(__file__).resolve().parent
DESKTOP_ID = "yule-eggnoggplus.desktop"
EXE = "eggnoggplus.exe"
UPDATER = "YuleUpdater.exe"
CHANNEL = "https://loafiieee.com/yule/releases/latest.json"

# ==ARTWORK-BEGIN== (embedded by build_release.ps1)
ARTWORK = {"grid": "", "gridp": "", "hero": "", "logo": "", "logopos": ""}
# ==ARTWORK-END==


class InstallError(Exception):
    pass


def fail(message: str) -> None:
    raise InstallError(message)


def regular(path: Path) -> bool:
    return path.is_file() and not path.is_symlink()


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def atomic(path: Path, data: bytes, mode: int = 0o644) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temp = tempfile.mkstemp(prefix=".yule-", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        os.chmod(temp, mode)
        os.replace(temp, path)
    finally:
        if os.path.exists(temp):
            os.unlink(temp)


def command(argv: list[str], timeout: int = 20) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(argv, text=True, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=timeout, check=False)
    except (OSError, subprocess.TimeoutExpired) as exc:
        fail(f"{argv[0]} could not run: {exc}")


def ask(question: str, yes: bool) -> bool:
    if yes:
        return True
    if not sys.stdin.isatty():
        print(f"{question}: yes (no interactive input; using the default)")
        return True
    return input(f"{question} [Y/n] ").strip().lower() in ("", "y", "yes")


def safe_url(url: str) -> bool:
    p = urlparse(url)
    return (p.scheme == "https" or (p.scheme == "http" and p.hostname in
            ("127.0.0.1", "localhost", "::1"))) and bool(p.netloc) and not (
                p.username or p.password or p.fragment)


def download(url: str, target: Path, limit: int) -> None:
    if not safe_url(url):
        fail(f"non-local release URLs must use HTTPS: {url}")
    try:
        with urlopen(url, timeout=30) as source, target.open("wb") as output:
            if not safe_url(source.geturl()):
                fail(f"release redirect used an unsafe URL: {source.geturl()}")
            size = 0
            while part := source.read(1024 * 1024):
                size += len(part)
                if size > limit:
                    fail(f"release download exceeds {limit} bytes: {url}")
                output.write(part)
    except OSError as exc:
        fail(f"download failed for {url}: {exc}")


def fetch_release(channel: str, work: Path) -> tuple[str, list[dict]]:
    manifest_path = work / "latest.json"
    download(channel, manifest_path, 1024 * 1024)
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (ValueError, UnicodeError) as exc:
        fail(f"release manifest is invalid: {exc}")
    version, base, files = manifest.get("version"), manifest.get("base"), manifest.get("files")
    if manifest.get("channel_version") != 1 or not isinstance(version, str) or not re.fullmatch(
            r"[0-9]+(?:\.[0-9]+)*", version):
        fail("release manifest has an unsupported channel/version")
    if not isinstance(base, str) or not safe_url(base) or not base.endswith("/"):
        fail("release manifest has an unsafe base URL")
    if not isinstance(files, list) or not 1 <= len(files) <= 32:
        fail("release manifest has an invalid file list")
    total, seen = 0, set()
    (work / "payload").mkdir()
    for item in files:
        if not isinstance(item, dict):
            fail("release manifest has a non-object file entry")
        name, size, sha = item.get("path"), item.get("size"), item.get("sha256")
        if (not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9._+-]+", name)
                or name in seen or not isinstance(size, int) or isinstance(size, bool)
                or not 1 <= size <= 64 * 1024 * 1024 or not isinstance(sha, str)
                or not re.fullmatch(r"[0-9a-fA-F]{64}", sha)
                or item.get("overwrite") is not True):
            fail(f"unsafe release file entry: {name!r}")
        total += size
        if total > 256 * 1024 * 1024:
            fail("release payload exceeds 256 MiB")
        seen.add(name)
        target = work / "payload" / name
        download(urljoin(base, name), target, size)
        if target.stat().st_size != size or digest(target) != sha.lower():
            fail(f"size or SHA-256 mismatch for {name}")
    if "SDL2.dll" not in seen:
        fail("release manifest is missing SDL2.dll")
    return version, files


def xdg_paths(home: Path) -> dict[str, Path]:
    data = Path(os.environ.get("XDG_DATA_HOME", home / ".local/share")).expanduser().resolve()
    state = Path(os.environ.get("XDG_STATE_HOME", home / ".local/state")).expanduser().resolve()
    binary = Path(os.environ.get("XDG_BIN_HOME", home / ".local/bin")).expanduser().resolve()
    return {"receipt": state / "yule/install-linux.json",
            "wrapper": binary / "yule-eggnoggplus",
            "desktop": data / "applications" / DESKTOP_ID,
            "icon": data / "icons/hicolor/256x256/apps/yule-eggnoggplus.png"}


def desktop_quote(value: str) -> str:
    value = value.replace("\\", "\\\\").replace('"', '\\"')
    value = value.replace("`", "\\`").replace("$", "\\$").replace("%", "%%")
    return f'"{value}"'


def wrapper_text(wine: str, prefix: Path, game: Path) -> str:
    return ("#!/usr/bin/env bash\nset -Eeuo pipefail\n"
            f"export WINEPREFIX={shlex.quote(str(prefix))}\n"
            "if (($# == 0)); then\n"
            f"  exec {shlex.quote(wine)} {shlex.quote(str(game))}\n"
            "fi\nuri=$1\ncase $uri in\n  yule://*) ;;\n"
            "  *) printf 'Invalid yule:// link\\n' >&2; exit 2 ;;\nesac\n"
            f"exec {shlex.quote(wine)} {shlex.quote(str(game))} \"--yule-uri=$uri\"\n")


def vdf_string(data: bytes, index: int) -> tuple[str, int]:
    end = data.find(b"\0", index)
    if end < 0:
        fail("Steam shortcuts.vdf has an unterminated string")
    return data[index:end].decode("utf-8", errors="replace"), end + 1


def vdf_map(data: bytes, index: int) -> tuple[OrderedDict, int]:
    result: OrderedDict = OrderedDict()
    while index < len(data):
        kind = data[index]
        index += 1
        if kind == 8:
            return result, index
        name, index = vdf_string(data, index)
        if kind == 0:
            value, index = vdf_map(data, index)
        elif kind == 1:
            value, index = vdf_string(data, index)
        elif kind == 2:
            if index + 4 > len(data):
                fail("Steam shortcuts.vdf has a truncated integer")
            value = struct.unpack_from("<I", data, index)[0]
            index += 4
        else:
            fail(f"Steam shortcuts.vdf uses unsupported field type {kind}; no shortcut was written")
        result[name] = value
    fail("Steam shortcuts.vdf is truncated")


def vdf_parse(data: bytes) -> OrderedDict:
    if not data:
        return OrderedDict([("shortcuts", OrderedDict())])
    root, end = vdf_map(data, 0)
    if end != len(data) or not isinstance(root.get("shortcuts"), dict):
        fail("Steam shortcuts.vdf has an invalid root")
    return root


def vdf_encode(values: dict) -> bytes:
    out = bytearray()
    for key, value in values.items():
        out.append(0 if isinstance(value, dict) else (2 if isinstance(value, int) else 1))
        out.extend(str(key).encode() + b"\0")
        if isinstance(value, dict):
            out.extend(vdf_encode(value))
        elif isinstance(value, int):
            out.extend(struct.pack("<I", value & 0xffffffff))
        else:
            out.extend(str(value).encode() + b"\0")
    out.append(8)
    return bytes(out)


def steam_locations(home: Path) -> list[tuple[Path, str]]:
    choices = [
        (os.environ.get("STEAM_ROOT") or os.environ.get("STEAM_PATH") or "", "native"),
        (str(home / ".local/share/Steam"), "native"),
        (str(home / ".steam/steam"), "native"),
        (str(home / ".steam/root"), "native"),
        (str(home / ".var/app/com.valvesoftware.Steam/data/Steam"), "flatpak"),
        (str(home / "snap/steam/common/.local/share/Steam"), "snap"),
    ]
    found, seen = [], set()
    for raw, kind in choices:
        if raw:
            root = Path(raw).expanduser().resolve()
            if "/.var/app/com.valvesoftware.Steam/" in str(root):
                kind = "flatpak"
            elif "/snap/steam/" in str(root):
                kind = "snap"
            if root not in seen and (root / "userdata").is_dir():
                seen.add(root)
                found.append((root, kind))
    return found


def steam_running() -> bool:
    pgrep = shutil.which("pgrep")
    return bool(pgrep and subprocess.run([pgrep, "-x", "steam"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                check=False).returncode == 0)


def stop_steam(yes: bool) -> bool:
    if not steam_running():
        return False
    if not ask("Steam must close before shortcuts are changed. Close it now?", yes):
        fail("Steam is running; close it and rerun the installer")
    steam = shutil.which("steam")
    if steam:
        command([steam, "-shutdown"], 10)
    for _ in range(30):
        if not steam_running():
            return True
        time.sleep(1)
    fail("Steam did not close within 30 seconds; shortcut was not changed")


def steam_add(root: Path, wrapper: Path, game: Path, icon: Path | None) -> dict:
    accounts = sorted(p for p in (root / "userdata").iterdir()
                      if p.is_dir() and not p.is_symlink() and p.name.isdecimal())
    if not accounts:
        fail(f"Steam at {root} has no signed-in userdata accounts")
    # Validate every account before touching any shortcuts or artwork. One
    # malformed existing VDF must not leave earlier accounts half-installed.
    for account in accounts:
        config = account / "config"
        if not config.is_dir() or config.is_symlink():
            fail(f"Steam account {account.name} has no config directory")
        vdf = config / "shortcuts.vdf"
        if vdf.exists() and not regular(vdf):
            fail(f"Steam shortcut path is not a regular file: {vdf}")
        if vdf.exists():
            vdf_parse(vdf.read_bytes())
    exe = f'"{wrapper}"'
    appid = (zlib.crc32((exe + "Eggnogg+").encode()) | 0x80000000) & 0xffffffff
    done, artwork_files = [], []
    for account in accounts:
        config = account / "config"
        if not config.is_dir():
            fail(f"Steam account {account.name} has no config directory")
        path = config / "shortcuts.vdf"
        if path.exists() and not regular(path):
            fail(f"Steam shortcut path is not a regular file: {path}")
        original = path.read_bytes() if path.exists() else b""
        root_data = vdf_parse(original)
        shortcuts = root_data["shortcuts"]
        key = next((k for k, e in shortcuts.items() if isinstance(e, dict) and
                    (str(e.get("appname", e.get("AppName", ""))).lower() == "eggnogg+" or
                     str(e.get("exe", e.get("Exe", ""))).strip('"') == str(wrapper))), None)
        if key is None:
            key = str(max((int(k) for k in shortcuts if str(k).isdecimal()), default=-1) + 1)
        grid = config / "grid"
        if grid.is_symlink():
            fail(f"Steam account {account.name} has a linked artwork directory")
        grid.mkdir(exist_ok=True)
        icon_path = grid / f"{appid}_icon.png"
        if icon:
            atomic(icon_path, icon.read_bytes())
            artwork_files.append({"path": str(icon_path), "sha256": digest(icon_path)})
        shortcuts[key] = OrderedDict([
            ("appid", appid), ("appname", "Eggnogg+"), ("exe", exe),
            ("StartDir", f'"{game}"'), ("icon", str(icon_path) if icon else ""),
            ("ShortcutPath", ""), ("LaunchOptions", ""), ("IsHidden", 0),
            ("AllowDesktopConfig", 1), ("AllowOverlay", 1), ("OpenVR", 0),
            ("Devkit", 0), ("DevkitGameID", ""), ("DevkitOverrideAppID", 0),
            ("LastPlayTime", 0), ("FlatpakAppID", ""), ("tags", OrderedDict()),
        ])
        if original:
            backup = path.with_name(path.name + ".yule-backup-" + datetime.now().strftime("%Y%m%d%H%M%S"))
            shutil.copy2(path, backup)
        atomic(path, vdf_encode(root_data))
        recorded = vdf_parse(path.read_bytes())["shortcuts"].get(key, {})
        if recorded.get("appid") != appid or recorded.get("exe") != exe:
            atomic(path, original)
            fail(f"Steam shortcut verification failed for account {account.name}; original restored")
        for slot, suffix in (("grid", ".png"), ("gridp", "p.png"),
                             ("hero", "_hero.png"), ("logo", "_logo.png"),
                             ("logopos", ".json")):
            if ARTWORK.get(slot):
                artwork_path = grid / f"{appid}{suffix}"
                atomic(artwork_path, base64.b64decode(ARTWORK[slot], validate=True))
                artwork_files.append({"path": str(artwork_path), "sha256": digest(artwork_path)})
        done.append(account.name)
        print(f"Steam account {account.name}: shortcut verified")
    return {"applied": True, "root": str(root), "appid": appid,
            "accounts": done, "artwork": artwork_files}


def steam_remove(receipt: dict) -> None:
    steam = receipt.get("steam", {})
    if not steam.get("applied"):
        return
    root = Path(steam["root"])
    expected = f'"{receipt["desktop"]["wrapper"]}"'
    for account in steam.get("accounts", []):
        if not str(account).isdecimal():
            continue
        path = root / "userdata" / str(account) / "config/shortcuts.vdf"
        if not regular(path):
            continue
        tree = vdf_parse(path.read_bytes())
        for key in list(tree["shortcuts"]):
            entry = tree["shortcuts"][key]
            if (isinstance(entry, dict) and entry.get("appid") == steam.get("appid")
                    and entry.get("exe") == expected):
                del tree["shortcuts"][key]
        atomic(path, vdf_encode(tree))
    for entry in steam.get("artwork", []):
        path = Path(entry["path"])
        if regular(path) and digest(path) == entry["sha256"]:
            path.unlink()


def refresh_desktop(folder: Path) -> list[str]:
    warnings = []
    database = shutil.which("update-desktop-database")
    if database:
        result = command([database, str(folder)])
        if result.returncode:
            warnings.append(f"desktop database refresh failed: {result.stderr.strip()}")
    for name in ("kbuildsycoca6", "kbuildsycoca5", "kbuildsycoca"):
        program = shutil.which(name)
        if program:
            result = command([program, "--noincremental"])
            if result.returncode:
                warnings.append(f"KDE application cache refresh failed: {result.stderr.strip()}")
            break
    return warnings


def desktop_add(game: Path, wine: str, prefix: Path, p: dict[str, Path],
                launcher: bool, protocol: bool, icon_source: Path | None,
                previous: str) -> tuple[dict, list[str]]:
    wrapper, desktop, icon = p["wrapper"], p["desktop"], p["icon"]
    atomic(wrapper, wrapper_text(wine, prefix, game / EXE).encode(), 0o755)
    if not os.access(wrapper, os.X_OK):
        fail(f"desktop launcher wrapper is not executable: {wrapper}")
    if icon_source:
        atomic(icon, icon_source.read_bytes())
    warnings = []
    if launcher or protocol:
        lines = ["[Desktop Entry]", "Type=Application", "Version=1.0", "Name=EGGNOGG+",
                 "Comment=Launch EGGNOGG+ through Wine",
                 f"Exec={desktop_quote(str(wrapper))}" + (" %u" if protocol else ""),
                 f"Path={game}", "Terminal=false", "Categories=Game;",
                 "StartupWMClass=eggnoggplus.exe"]
        if protocol:
            lines.append("MimeType=x-scheme-handler/yule;")
        if icon_source:
            lines.append(f"Icon={icon}")
        atomic(desktop, ("\n".join(lines) + "\n").encode())
        if not regular(desktop):
            fail(f"desktop launcher was not installed: {desktop}")
        validator = shutil.which("desktop-file-validate")
        if validator:
            checked = command([validator, str(desktop)])
            if checked.returncode:
                fail(f"desktop entry validation failed: {checked.stderr.strip() or checked.stdout.strip()}")
        warnings = refresh_desktop(desktop.parent)
    if protocol:
        xdg = shutil.which("xdg-mime")
        if not xdg:
            fail("xdg-mime is missing; yule:// registration cannot be verified")
        result = command([xdg, "default", DESKTOP_ID, "x-scheme-handler/yule"])
        if result.returncode:
            fail(f"xdg-mime rejected yule:// registration: {result.stderr.strip()}")
        result = command([xdg, "query", "default", "x-scheme-handler/yule"])
        if result.returncode or result.stdout.strip() != DESKTOP_ID:
            fail(f"yule:// handler verification failed: {result.stdout.strip() or result.stderr.strip()}")
    return ({"file": str(desktop) if launcher or protocol else "",
             "file_sha256": digest(desktop) if launcher or protocol else "",
             "wrapper": str(wrapper), "wrapper_sha256": digest(wrapper),
             "icon": str(icon) if icon_source else "",
             "icon_sha256": digest(icon) if icon_source else "",
             "previous_handler": previous,
             "launcher_applied": launcher, "protocol_applied": protocol}, warnings)


def desktop_remove(receipt: dict) -> None:
    desktop = receipt.get("desktop", {})
    xdg = shutil.which("xdg-mime")
    if xdg and desktop.get("protocol_applied"):
        current = command([xdg, "query", "default", "x-scheme-handler/yule"])
        previous = desktop.get("previous_handler", "")
        if current.stdout.strip() == DESKTOP_ID and previous and previous != DESKTOP_ID:
            command([xdg, "default", previous, "x-scheme-handler/yule"])
    for name, sha in (("file", "file_sha256"), ("wrapper", "wrapper_sha256"),
                      ("icon", "icon_sha256")):
        if desktop.get(name):
            path = Path(desktop[name])
            if regular(path) and digest(path) == desktop.get(sha):
                path.unlink()
            elif path.exists():
                print(f"PRESERVED modified desktop file: {path}")
    if desktop.get("file"):
        refresh_desktop(Path(desktop["file"]).parent)


def receipt_write(path: Path, data: dict) -> None:
    atomic(path, (json.dumps(data, indent=2, sort_keys=True) + "\n").encode(), 0o600)


def uninstall(receipt_path: Path) -> int:
    if not regular(receipt_path):
        fail(f"no Linux installer receipt found: {receipt_path}")
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    game = Path(receipt["install_dir"]).resolve()
    if game in (Path("/"), Path.home().resolve()) or not regular(game / EXE):
        fail("receipt points to an unsafe or missing game directory")
    if steam_running():
        fail("close Steam before uninstalling its shortcut")
    steam_remove(receipt)
    desktop_remove(receipt)
    for item in receipt.get("managed_files", []):
        name = item.get("path", "")
        if not re.fullmatch(r"[A-Za-z0-9._+-]+", name):
            fail(f"unsafe managed filename in receipt: {name!r}")
        path = game / name
        if regular(path) and digest(path) == item.get("sha256"):
            path.unlink()
        elif path.exists():
            print(f"PRESERVED modified managed file: {path}")
    original, target = game / "SDL2_real.dll", game / "SDL2.dll"
    if receipt.get("adopted_vanilla") and regular(original):
        if not target.exists():
            os.replace(original, target)
        else:
            print(f"PRESERVED {original}: modified SDL2.dll still exists")
    for path in (game / "install-linux.json", receipt_path):
        if regular(path):
            path.unlink()
    print("Uninstalled installer-owned files; maps, mods, saves, and modified files were preserved.")
    return 0


def install(args: argparse.Namespace, p: dict[str, Path]) -> int:
    home = Path.home().resolve()
    wine = shutil.which(args.wine_bin)
    if not wine:
        fail(f"Wine executable not found: {args.wine_bin}; install Wine or pass --wine-bin")
    prefix = Path(args.wine_prefix or os.environ.get("WINEPREFIX", home / ".wine")).expanduser().resolve()
    existing = json.loads(p["receipt"].read_text()) if regular(p["receipt"]) else {}
    raw_game = args.game_path or existing.get("game_executable")
    if not raw_game:
        for candidate in (HERE / EXE, HERE.parent / EXE):
            if regular(candidate):
                raw_game = str(candidate)
                break
    if not raw_game:
        if args.yes or not sys.stdin.isatty():
            fail("game path was not found; pass --game-path /path/to/eggnoggplus.exe")
        raw_game = input("Path to eggnoggplus.exe (or its folder): ").strip()
    game_path = Path(raw_game).expanduser().resolve()
    if game_path.is_dir():
        game_path /= EXE
    if game_path.name != EXE or not regular(game_path):
        fail(f"could not find a regular {EXE} at {game_path}")
    source_dir = game_path.parent
    game = Path(args.install_dir or existing.get("install_dir") or
                home / ".local/share/yule/EGGNOGG+").expanduser().resolve()

    with tempfile.TemporaryDirectory(prefix="yule-install-") as temp:
        work = Path(temp)
        print("Fetching and verifying the release before changing the game...")
        version, files = fetch_release(args.channel_url, work)
        updater = HERE / UPDATER
        if not regular(updater):
            updater = HERE.parent.parent.parent / UPDATER
        if not regular(updater):
            fail(f"installer package is missing {UPDATER}")
        moved = False
        if source_dir != game:
            if regular(game / EXE):
                print(f"Existing installation found at {game}; source copy left untouched")
            elif game.exists() and any(game.iterdir()):
                fail(f"destination exists and is not an EGGNOGG+ install: {game}")
            elif ask(f"Move the game to {game}?", args.yes):
                game.parent.mkdir(parents=True, exist_ok=True)
                if game.is_dir():
                    game.rmdir()
                shutil.move(str(source_dir), str(game))
                moved = True
            else:
                game = source_dir
        if not regular(game / EXE):
            fail(f"installed game executable is missing: {game / EXE}")
        for child in ("mods", "maps", "SDL2_real.dll"):
            if (game / child).is_symlink():
                fail(f"installed game has a linked {child}; refusing to write through it")

        adopted = bool(existing.get("adopted_vanilla"))
        made_original = False
        if not (game / "SDL2_real.dll").exists():
            current = game / "SDL2.dll"
            if not regular(current):
                fail("vanilla SDL2.dll is missing; cannot adopt this game")
            if b"YULE_FRAMEWORK_VERSION=" in current.read_bytes():
                fail("SDL2.dll is already Yule but SDL2_real.dll is missing")
            shutil.copy2(current, game / "SDL2_real.dll")
            adopted, made_original = True, True

        managed = []
        prior: list[tuple[Path, bytes | None]] = []
        config = game / "mods/modframework.cfg"
        config_old: bytes | None = None
        try:
            for item in files + [{"path": UPDATER}]:
                name = item["path"]
                source = updater if name == UPDATER else work / "payload" / name
                target = game / name
                if target.exists() and not regular(target):
                    fail(f"managed target is not a regular file: {target}")
                prior.append((target, target.read_bytes() if target.exists() else None))
                atomic(target, source.read_bytes())
                if digest(target) != digest(source):
                    fail(f"installed file verification failed: {name}")
                managed.append({"path": name, "sha256": digest(target),
                                "size": target.stat().st_size})
            (game / "mods").mkdir(exist_ok=True)
            (game / "maps").mkdir(exist_ok=True)
            config_old = config.read_bytes() if regular(config) else None
            lines = config_old.decode("utf-8", errors="replace").splitlines() if config_old else []
            lines = [line for line in lines if not re.match(r"\s*show_log_console\s*=", line)]
            atomic(config, ("\n".join(lines + ["show_log_console=0"]) + "\n").encode())
        except Exception:
            for target, original in reversed(prior):
                if original is None:
                    target.unlink(missing_ok=True)
                else:
                    atomic(target, original)
            if config_old is not None:
                atomic(config, config_old)
            elif config.exists():
                config.unlink()
            if made_original:
                (game / "SDL2_real.dll").unlink(missing_ok=True)
            raise

    icon = HERE / "assets/steam_icon.png"
    if not regular(icon):
        icon = HERE.parent / "windows/assets/steam_icon.png"
    if not regular(icon):
        icon = None
    steps = {"move": "ok" if moved else "already-there", "framework": "ok",
             "shortcut": "skipped-switch" if args.skip_launcher else "declined",
             "deep_links": "skipped-switch" if args.skip_protocol else "declined",
             "steam": "skipped-switch" if args.skip_steam else "no-steam",
             "log_console": "ok"}
    issues: list[str] = []
    desktop = existing.get("desktop", {})
    steam = existing.get("steam", {"applied": False, "accounts": [], "artwork": []})
    launcher = not args.skip_launcher and ask("Add EGGNOGG+ to the application menu?", args.yes)
    protocol = not args.skip_protocol and ask("Register yule:// links?", args.yes)
    choices = steam_locations(home)
    add_steam = not args.skip_steam and bool(choices) and ask("Add EGGNOGG+ to Steam?", args.yes)
    if not args.skip_steam and choices and not add_steam:
        steps["steam"] = "declined"
    previous = desktop.get("previous_handler", "")
    if protocol and shutil.which("xdg-mime"):
        current = command(["xdg-mime", "query", "default", "x-scheme-handler/yule"]).stdout.strip()
        if current != DESKTOP_ID:
            previous = current
    if launcher or protocol or add_steam:
        try:
            desktop, warnings = desktop_add(game, wine, prefix, p, launcher,
                                            protocol, icon, previous)
            issues.extend(warnings)
            if launcher:
                steps["shortcut"] = "ok"
            if protocol:
                steps["deep_links"] = "ok"
        except (InstallError, OSError) as exc:
            if launcher:
                steps["shortcut"] = "failed"
            if protocol:
                steps["deep_links"] = "failed"
            issues.append(f"desktop/yule://: {exc}")
            # Registration may fail after the launcher was written. Keep exact
            # ownership hashes so retry/uninstall can clean up that partial work.
            desktop = {"file": str(p["desktop"]),
                       "file_sha256": digest(p["desktop"]) if regular(p["desktop"]) else "",
                       "wrapper": str(p["wrapper"]),
                       "wrapper_sha256": digest(p["wrapper"]) if regular(p["wrapper"]) else "",
                       "icon": str(p["icon"]) if regular(p["icon"]) else "",
                       "icon_sha256": digest(p["icon"]) if regular(p["icon"]) else "",
                       "previous_handler": previous, "launcher_applied": False,
                       "protocol_applied": False}
    if add_steam:
        root, kind = choices[0]
        if kind != "native":
            steps["steam"] = "unsupported"
            issues.append(f"Steam at {root} uses {kind} sandboxing; host Wine shortcuts cannot be verified inside it")
        elif not regular(p["wrapper"]):
            steps["steam"] = "failed"
            issues.append("Steam requires the Wine launcher wrapper, which was not installed")
        else:
            try:
                was_running = stop_steam(args.yes)
                steam = steam_add(root, p["wrapper"], game, icon)
                steps["steam"] = "ok"
                if was_running:
                    steam_bin = shutil.which("steam")
                    if steam_bin:
                        subprocess.Popen([steam_bin], stdin=subprocess.DEVNULL,
                                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                         start_new_session=True)
                        print("Steam was restarted to load the new shortcut")
                    else:
                        issues.append("Steam shortcut installed; restart Steam manually to load it")
            except (InstallError, OSError, ValueError) as exc:
                steps["steam"] = "failed"
                issues.append(f"Steam: {exc}")

    receipt = {"manifest_version": 3, "installer_version": 3, "platform": "linux-wine",
               "install_dir": str(game), "game_executable": str(game / EXE),
               "updater_executable": str(game / UPDATER), "framework_version": version,
               "channel_url": args.channel_url, "wine_prefix": str(prefix),
               "wine_command": wine, "installed_at": datetime.now(timezone.utc).isoformat(),
               "moved_by_installer": moved, "adopted_vanilla": adopted,
               "managed_files": managed, "steps": steps, "desktop": desktop,
               "steam": steam, "issues": issues}
    receipt_write(p["receipt"], receipt)
    receipt_write(game / "install-linux.json", receipt)
    print(f"Installed Yule {version} at {game}")
    for name, status in steps.items():
        print(f"  {name:12} {status}")
    for issue in issues:
        print(f"FAILED: {issue}")
    if issues:
        print(f"Framework installed, but integration failed. Details: {p['receipt']}")
        return 2
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Install EGGNOGG+ for Wine")
    parser.add_argument("--game-path")
    parser.add_argument("--install-dir")
    parser.add_argument("--wine-prefix")
    parser.add_argument("--wine-bin", default=os.environ.get("WINE_BIN", "wine"))
    parser.add_argument("--channel-url", default=CHANNEL)
    parser.add_argument("--skip-launcher", action="store_true")
    parser.add_argument("--skip-protocol", action="store_true")
    parser.add_argument("--skip-steam", action="store_true")
    parser.add_argument("--yes", "-y", action="store_true")
    parser.add_argument("--uninstall", action="store_true")
    args = parser.parse_args()
    if sys.platform != "linux":
        fail("this installer requires Linux; use the Windows installer ZIP on Windows")
    p = xdg_paths(Path.home().resolve())
    return uninstall(p["receipt"]) if args.uninstall else install(args, p)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (InstallError, OSError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
