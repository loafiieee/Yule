#!/usr/bin/env bash
set -Eeuo pipefail

REPOSITORY_URL="${YULE_REPOSITORY_URL:-https://github.com/loafiieee/Yule.git}"
REPOSITORY_REF="${YULE_REPOSITORY_REF:-main}"
SERVICE_NAME="${YULE_SERVICE_NAME:-eggnogg}"
SERVER_PORT="${YULE_SERVER_PORT:-47778}"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
TARGET_ROOT="${YULE_TARGET_ROOT:-$(cd -- "$SCRIPT_DIR/.." && pwd -P)}"
TARGET_SERVER="$TARGET_ROOT/online_server"
TEMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/yule-server-update.XXXXXXXX")"
SOURCE_ROOT="$TEMP_ROOT/source"
STAGE_ROOT="$TEMP_ROOT/stage"
BACKUP_ROOT="$TEMP_ROOT/backup"
EXISTING_LIST="$TEMP_ROOT/existing.list"
NEW_LIST="$TEMP_ROOT/new.list"
APPLY_STARTED=0

cleanup() {
  local status=$?
  trap - EXIT
  if [[ "$APPLY_STARTED" -eq 1 ]]; then
    restore_previous_application
  fi
  rm -rf -- "$TEMP_ROOT"
  exit "$status"
}
trap cleanup EXIT

die() {
  printf 'server update failed: %s\n' "$*" >&2
  exit 1
}

systemctl_yule() {
  if [[ "$(id -u)" -eq 0 ]]; then
    systemctl "$@"
  else
    sudo systemctl "$@"
  fi
}

safe_relative_path() {
  local value="$1"
  [[ -n "$value" && "$value" != /* && "$value" != *'..'* &&
     "$value" != *$'\n'* && "$value" != *$'\r'* ]]
}

protected_runtime_path() {
  local value="$1"
  local base="${value##*/}"
  case "$value" in
    users.json|ratings.json|server_secret.key|node_modules/*|__pycache__/*)
      return 0
      ;;
  esac
  case "$base" in
    *.log|*.key|*.env|*.pid|*.sock)
      return 0
      ;;
  esac
  return 1
}

restore_previous_application() {
  local relative destination saved
  printf '%s\n' 'new server failed validation; restoring the previous application files' >&2
  systemctl_yule stop "$SERVICE_NAME" >/dev/null 2>&1 || true
  while IFS= read -r relative; do
    safe_relative_path "$relative" || continue
    destination="$TARGET_SERVER/$relative"
    saved="$BACKUP_ROOT/$relative"
    mkdir -p -- "$(dirname -- "$destination")"
    cp -a -- "$saved" "$destination"
  done < "$EXISTING_LIST"
  while IFS= read -r relative; do
    safe_relative_path "$relative" || continue
    protected_runtime_path "$relative" && continue
    rm -f -- "$TARGET_SERVER/$relative"
  done < "$NEW_LIST"
  systemctl_yule start "$SERVICE_NAME" >/dev/null 2>&1 || true
  APPLY_STARTED=0
}

for command in git node npm python3 find cp mv; do
  command -v "$command" >/dev/null 2>&1 ||
    die "required command is missing: $command"
done
[[ -d "$TARGET_SERVER" ]] || die "target server directory does not exist: $TARGET_SERVER"

printf 'cloning %s (%s)...\n' "$REPOSITORY_URL" "$REPOSITORY_REF"
git clone --quiet --depth 1 --branch "$REPOSITORY_REF" \
  "$REPOSITORY_URL" "$SOURCE_ROOT"
[[ -f "$SOURCE_ROOT/online_server/server.js" ]] ||
  die "clone did not contain online_server/server.js"

printf '%s\n' 'validating the cloned server before stopping the live service...'
(
  cd -- "$SOURCE_ROOT/online_server"
  npm run check
  npm test
)
python3 "$SOURCE_ROOT/tests/discord_lfg_server_static_test.py"
python3 "$SOURCE_ROOT/tests/online_server_auth_static_test.py"
python3 "$SOURCE_ROOT/tests/online_server_match_protocol_test.py"

mkdir -p -- "$STAGE_ROOT" "$BACKUP_ROOT"
while IFS= read -r -d '' source_file; do
  relative="${source_file#"$SOURCE_ROOT/online_server/"}"
  safe_relative_path "$relative" ||
    die "clone produced an unsafe server path: $relative"
  protected_runtime_path "$relative" && continue
  destination="$STAGE_ROOT/$relative"
  mkdir -p -- "$(dirname -- "$destination")"
  cp -a -- "$source_file" "$destination"
done < <(find "$SOURCE_ROOT/online_server" -type f -print0)

[[ -f "$STAGE_ROOT/server.js" ]] || die "staging omitted server.js"
: > "$EXISTING_LIST"
: > "$NEW_LIST"

printf '%s\n' 'stopping the service for the application-file swap...'
systemctl_yule stop "$SERVICE_NAME"
APPLY_STARTED=1

while IFS= read -r -d '' staged_file; do
  relative="${staged_file#"$STAGE_ROOT/"}"
  safe_relative_path "$relative" ||
    die "staging produced an unsafe path: $relative"
  protected_runtime_path "$relative" &&
    die "refusing to apply protected runtime state: $relative"
  destination="$TARGET_SERVER/$relative"
  if [[ -e "$destination" ]]; then
    saved="$BACKUP_ROOT/$relative"
    mkdir -p -- "$(dirname -- "$saved")"
    cp -a -- "$destination" "$saved"
    printf '%s\n' "$relative" >> "$EXISTING_LIST"
  else
    printf '%s\n' "$relative" >> "$NEW_LIST"
  fi
  mkdir -p -- "$(dirname -- "$destination")"
  temporary="$destination.yule-update.$$"
  cp -a -- "$staged_file" "$temporary"
  if [[ "$relative" == "update_server.sh" ]]; then
    chmod 0755 -- "$temporary"
  fi
  mv -f -- "$temporary" "$destination"
done < <(find "$STAGE_ROOT" -type f -print0)

if ! systemctl_yule start "$SERVICE_NAME"; then
  restore_previous_application
  die "systemd could not start $SERVICE_NAME after the update"
fi

if ! systemctl_yule is-active --quiet "$SERVICE_NAME"; then
  restore_previous_application
  die "$SERVICE_NAME did not remain active"
fi

if ! python3 "$TARGET_SERVER/check_deployment.py" \
    127.0.0.1 "$SERVER_PORT" --timeout 5; then
  restore_previous_application
  die "the restarted service failed its TCP/UDP protocol probe"
fi

APPLY_STARTED=0
printf '%s\n' 'server update complete; runtime accounts, ratings, secrets, logs, and environment files were untouched'
