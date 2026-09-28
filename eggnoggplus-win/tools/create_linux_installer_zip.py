"""Create the Linux installer ZIP with portable Unix file modes."""

from __future__ import annotations

from pathlib import Path
import sys
import zipfile


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: create_linux_installer_zip.py STAGING_DIR OUTPUT.zip")
    root = Path(sys.argv[1]).resolve()
    output = Path(sys.argv[2]).resolve()
    if not root.is_dir():
        raise SystemExit(f"staging directory does not exist: {root}")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.unlink(missing_ok=True)
    try:
        with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for path in sorted(root.rglob("*")):
                if not path.is_file():
                    continue
                name = path.relative_to(root).as_posix()
                info = zipfile.ZipInfo.from_file(path, name)
                info.create_system = 3
                mode = 0o755 if path.suffix == ".sh" else 0o644
                info.external_attr = (0o100000 | mode) << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(info, path.read_bytes(), compresslevel=9)
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
