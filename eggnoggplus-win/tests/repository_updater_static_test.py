from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = (ROOT / "tools" / "update_repository.sh").read_text(encoding="utf-8")
DOCS = (ROOT / "REPOSITORY_UPDATER.md").read_text(encoding="utf-8")

for required in (
    "set -Eeuo pipefail",
    "--dry-run",
    "YULE_REPOSITORY_URL",
    "YULE_REPOSITORY_REF",
    "YULE_PRESERVE_PATHS",
    "protected_path",
    'PROJECT_ONLINE_SERVER="$(project_path online_server)"',
    '"$PROJECT_ONLINE_SERVER"|"$PROJECT_ONLINE_SERVER"/*',
    "target checkout has staged changes",
    "non-runtime local edits that would be overwritten",
    "upstream file conflicts with untracked local path",
    "source ref is not a fast-forward",
    "post-apply verification failed",
    "restore_previous_tree",
    'git -C "$TARGET_REPO" read-tree "$SOURCE_COMMIT"',
    'git -C "$TARGET_REPO" update-ref "$CURRENT_BRANCH_REF"',
    "APPLY_STARTED=1",
    "APPLY_STARTED=0",
):
    assert required in SCRIPT, f"repository updater lost safety behavior: {required}"

for protected in (
    "online_server",
    "build",
    "dist",
    ".vscode",
    ".idea",
    "*.env",
    "*.key",
    "modframework.cfg",
    "online_hub.cfg",
    "config.cfg",
    "console_history.txt",
    "storage.cfg",
    "cache",
):
    assert protected in SCRIPT, f"repository updater does not preserve {protected}"

assert SCRIPT.index('if [[ "$DRY_RUN" -eq 1 ]]') < SCRIPT.index("APPLY_STARTED=1")
assert "git pull" not in SCRIPT
assert "git reset" not in SCRIPT
assert "rsync" not in SCRIPT
assert "--delete" not in SCRIPT

assert "entire `online_server/` directory" in DOCS
assert "online_server/update_server.sh" in DOCS
assert "--dry-run" in DOCS

print("repository-wide updater safety checks: OK")
