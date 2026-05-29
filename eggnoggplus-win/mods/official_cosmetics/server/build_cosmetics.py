#!/usr/bin/env python3
"""Build the official hats manifest and spritesheet for the Ubuntu server."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from datetime import datetime
from pathlib import Path
from typing import Any

try:
    from PIL import Image
except ImportError as exc:
    raise SystemExit(
        "Pillow is required. On Ubuntu run: sudo apt install python3-pil"
    ) from exc


ROOT = Path(__file__).resolve().parent
CELL_W = 32
CELL_H = 32
ID_RE = re.compile(r"^[A-Za-z0-9_.-]{1,64}$")


def root_path(value: str | Path) -> Path:
    path = Path(value)
    return path if path.is_absolute() else ROOT / path


def sha256_hex(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def read_json(path: Path) -> dict[str, Any] | None:
    if not path.exists():
        return None
    with path.open("r", encoding="utf-8") as f:
        value = json.load(f)
    if not isinstance(value, dict):
        raise ValueError(f"{path} must contain a JSON object")
    return value


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as f:
        json.dump(value, f, indent=2)
        f.write("\n")


def valid_hat_id(value: str) -> bool:
    return bool(ID_RE.fullmatch(value))


def title_from_id(value: str) -> str:
    parts = [p for p in re.split(r"[-_.]+", value) if p]
    return " ".join(p[:1].upper() + p[1:] for p in parts) or "Hat"


def default_hat_meta(hat_id: str, name: str | None) -> dict[str, Any]:
    return {
        "id": hat_id,
        "name": name or title_from_id(hat_id),
        "motion": True,
        "scale": 0.65,
        "x": 0.0,
        "y": 10.5,
        "bob": 0.5,
        "tilt": 5.0,
        "drag": 0.3,
        "allowed_online": True,
    }


def require_hat_id(hat_id: str) -> None:
    if not valid_hat_id(hat_id):
        raise SystemExit(
            f"Invalid hat id {hat_id!r}. Use letters, numbers, dash, underscore, or dot."
        )


def new_hat_scaffold(hat_id: str, name: str | None, force: bool) -> None:
    require_hat_id(hat_id)
    hats_dir = ROOT / "hats"
    hats_dir.mkdir(parents=True, exist_ok=True)

    png_path = hats_dir / f"{hat_id}.png"
    json_path = hats_dir / f"{hat_id}.json"
    if not force:
        for path in (png_path, json_path):
            if path.exists():
                raise SystemExit(f"{path} already exists. Use --force to overwrite.")

    Image.new("RGBA", (CELL_W, CELL_H), (0, 0, 0, 0)).save(png_path)
    write_json(json_path, default_hat_meta(hat_id, name))
    print(f"Created {png_path}")
    print(f"Created {json_path}")


def import_hats_from_sheet(sheet_path: Path, manifest_path: Path, force: bool) -> None:
    if not sheet_path.exists():
        raise SystemExit(f"Sheet not found: {sheet_path}")
    manifest = read_json(manifest_path)
    if not manifest or not isinstance(manifest.get("hats"), list):
        raise SystemExit(f"Manifest has no hats list: {manifest_path}")

    hats_dir = ROOT / "hats"
    hats_dir.mkdir(parents=True, exist_ok=True)

    with Image.open(sheet_path) as sheet:
        sheet = sheet.convert("RGBA")
        for hat in manifest["hats"]:
            if not isinstance(hat, dict):
                continue
            hat_id = str(hat.get("id") or "")
            require_hat_id(hat_id)
            idx = int(hat.get("sprite_index", hat.get("sprite", hat.get("frame", 0))))
            src_x = idx * CELL_W
            if src_x + CELL_W > sheet.width or CELL_H > sheet.height:
                raise SystemExit(f"Sprite index {idx} for {hat_id!r} is outside {sheet_path}")

            png_path = hats_dir / f"{hat_id}.png"
            json_path = hats_dir / f"{hat_id}.json"
            if not force:
                for path in (png_path, json_path):
                    if path.exists():
                        raise SystemExit(f"{path} already exists. Use --force to overwrite.")

            sheet.crop((src_x, 0, src_x + CELL_W, CELL_H)).save(png_path)
            meta = {
                "id": hat_id,
                "name": str(hat.get("name") or title_from_id(hat_id)),
                "motion": bool(hat.get("motion", True)),
                "scale": float(hat.get("scale", 0.65)),
                "x": float(hat.get("x", hat.get("offset_x", 0.0))),
                "y": float(hat.get("y", hat.get("anchor_y", 10.5))),
                "bob": float(hat.get("bob", 0.5)),
                "tilt": float(hat.get("tilt", 5.0)),
                "drag": float(hat.get("drag", 0.3)),
                "allowed_online": bool(hat.get("allowed_online", True)),
            }
            write_json(json_path, meta)
            print(f"Imported {hat_id}")


def number_meta(meta: dict[str, Any], key: str, default: float) -> float:
    value = meta.get(key, default)
    return float(value)


def bool_meta(meta: dict[str, Any], key: str, default: bool) -> bool:
    value = meta.get(key, default)
    return bool(value)


def hat_definitions() -> list[dict[str, Any]]:
    hats_dir = ROOT / "hats"
    if not hats_dir.exists():
        raise SystemExit("No hats folder. Add hats/*.png first, or run --new-hat-id <id>.")

    defs: list[dict[str, Any]] = []
    for png_path in sorted(hats_dir.glob("*.png")):
        if png_path.stem.startswith("_"):
            continue
        meta = read_json(png_path.with_suffix(".json")) or {}
        if meta.get("enabled") is False:
            continue

        hat_id = str(meta.get("id") or png_path.stem)
        require_hat_id(hat_id)

        with Image.open(png_path) as img:
            if img.size != (CELL_W, CELL_H):
                raise SystemExit(f"{png_path} must be {CELL_W}x{CELL_H}; got {img.width}x{img.height}")

        defs.append(
            {
                "png": png_path,
                "order": int(meta.get("order", 100000)),
                "manifest": {
                    "id": hat_id,
                    "name": str(meta.get("name") or title_from_id(hat_id)),
                    "sprite_index": 0,
                    "motion": bool_meta(meta, "motion", True),
                    "scale": number_meta(meta, "scale", 0.65),
                    "x": number_meta(meta, "x", 0.0),
                    "y": number_meta(meta, "y", 10.5),
                    "bob": number_meta(meta, "bob", 0.5),
                    "tilt": number_meta(meta, "tilt", 5.0),
                    "drag": number_meta(meta, "drag", 0.3),
                    "allowed_online": bool_meta(meta, "allowed_online", True),
                },
            }
        )

    defs.sort(key=lambda d: (d["order"], d["manifest"]["id"]))
    if not defs:
        raise SystemExit("No enabled hats found in hats/*.png")
    return defs


def build(base_url: str, version: str) -> None:
    defs = hat_definitions()
    assets_dir = ROOT / "assets"
    assets_dir.mkdir(parents=True, exist_ok=True)
    sheet_path = assets_dir / "hats.png"

    sheet = Image.new("RGBA", (len(defs) * CELL_W, CELL_H), (0, 0, 0, 0))
    for idx, item in enumerate(defs):
        with Image.open(item["png"]) as img:
            sheet.paste(img.convert("RGBA"), (idx * CELL_W, 0))
        item["manifest"]["sprite_index"] = idx
    sheet.save(sheet_path)

    sha = sha256_hex(sheet_path)
    if not version:
        version = datetime.utcnow().strftime("%Y-%m-%d.%H%M%S")

    manifest = {
        "schema": 1,
        "version": version,
        "assets": {
            "hats": {
                "url": f"{base_url.rstrip('/')}/assets/hats.png",
                "sha256": sha,
                "cell_w": CELL_W,
                "cell_h": CELL_H,
            }
        },
        "hats": [item["manifest"] for item in defs],
    }
    manifest_path = ROOT / "manifest.json"
    write_json(manifest_path, manifest)

    print(f"Wrote {manifest_path}")
    print(f"Wrote {sheet_path}")
    print(f"SHA256 {sha}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Build official Eggnogg+ hat assets.")
    parser.add_argument("--base-url", default="https://loafiieee.com/eggnogg/cosmetics/v1")
    parser.add_argument("--version", default="")
    parser.add_argument("--import-sheet", action="store_true")
    parser.add_argument("--sheet-path", default="../assets/hats.png")
    parser.add_argument("--manifest-path", default="manifest.json")
    parser.add_argument("--new-hat-id", default="")
    parser.add_argument("--new-hat-name", default="")
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()

    if args.new_hat_id:
        new_hat_scaffold(args.new_hat_id, args.new_hat_name or None, args.force)
        if not args.import_sheet:
            return

    if args.import_sheet:
        import_hats_from_sheet(root_path(args.sheet_path), root_path(args.manifest_path), args.force)

    build(args.base_url, args.version)


if __name__ == "__main__":
    main()
