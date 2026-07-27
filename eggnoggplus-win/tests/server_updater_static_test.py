from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = (ROOT / "online_server" / "update_server.sh").read_text(encoding="utf-8")

assert "set -Eeuo pipefail" in SCRIPT
assert "git clone --quiet --depth 1 --branch" in SCRIPT
apply_stop = SCRIPT.index(
    'systemctl_yule stop "$SERVICE_NAME"',
    SCRIPT.index("stopping the service for the application-file swap"),
)
assert SCRIPT.index("npm run check") < apply_stop
assert "npm test" in SCRIPT
assert "online_server_match_protocol_test.py" in SCRIPT
assert 'python3 "$TARGET_SERVER/check_deployment.py"' in SCRIPT
assert "restore_previous_application" in SCRIPT
assert "APPLY_STARTED=1" in SCRIPT and "APPLY_STARTED=0" in SCRIPT

for protected in (
    "users.json",
    "ratings.json",
    "server_secret.key",
    "*.log",
    "*.key",
    "*.env",
    "*.pid",
    "*.sock",
    "node_modules/*",
    "__pycache__/*",
):
    assert protected in SCRIPT

assert "protected_runtime_path" in SCRIPT
assert 'die "refusing to apply protected runtime state: $relative"' in SCRIPT
assert "rsync" not in SCRIPT
assert "--delete" not in SCRIPT
assert "git pull" not in SCRIPT
assert "git reset" not in SCRIPT

print("state-preserving online-server updater checks: OK")
