from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = (ROOT / "tools" / "export_public_source.ps1").read_text(encoding="utf-8")
PRIVATE_OVERLAY = ROOT / "tools" / "public_source_overlay"
OVERLAY = PRIVATE_OVERLAY if PRIVATE_OVERLAY.is_dir() else ROOT

for required in (
    "git -C $sourceRoot ls-files -- .",
    "git -C $sourceRoot archive",
    "source_commit",
    "inherited_history",
    "Potential secret matched in public output",
    "Destination must be outside the private project source tree",
    "This is a working-tree review export",
):
    assert required in SCRIPT, f"public exporter lost required guard: {required}"

for forbidden in (
    "mods/_official_cosmetics/",
    "mods/modframework.cfg",
    "mods/online_hub.cfg",
    "online_server/users.json",
    "online_server/ratings.json",
    "online_server/server_secret.key",
):
    assert forbidden in SCRIPT, f"public exporter does not deny {forbidden}"

for required_file in (
    "README.md",
    ".gitignore",
    ".gitattributes",
    ".github/workflows/source-ci.yml",
    "CONTRIBUTING.md",
    "SECURITY.md",
    "config/modframework.example.cfg",
    "config/online_hub.example.cfg",
    "config/server.env.example",
):
    assert (OVERLAY / required_file).is_file(), f"missing public overlay {required_file}"

example_env = (OVERLAY / "config" / "server.env.example").read_text(
    encoding="utf-8"
)
assert "replace-me" in example_env
assert "DISCORD_TOKEN=" in example_env

example_framework = (
    OVERLAY / "config" / "modframework.example.cfg"
).read_text(encoding="utf-8")
assert "discord_presence=1" in example_framework
assert "discord_application_id=1531027934004117664" in example_framework

print("public source exporter/static release guards: OK")
