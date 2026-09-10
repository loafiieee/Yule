#!/usr/bin/env bash
set -Eeuo pipefail

REPOSITORY_URL="${YULE_REPOSITORY_URL:-https://github.com/loafiieee/Yule.git}"
REPOSITORY_REF="${YULE_REPOSITORY_REF:-main}"
SERVICE_NAME="${YULE_SERVICE_NAME:-eggnogg}"
SERVER_PORT="${YULE_SERVER_PORT:-47778}"
READINESS_TIMEOUT_SECONDS="${YULE_READINESS_TIMEOUT_SECONDS:-30}"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
TARGET_ROOT="${YULE_TARGET_ROOT:-$(cd -- "$SCRIPT_DIR/.." && pwd -P)}"
TARGET_SERVER="$TARGET_ROOT/online_server"
TEMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/yule-server-update.XXXXXXXX")"
SOURCE_ROOT="$TEMP_ROOT/source"
SOURCE_PROJECT=""
SOURCE_SERVER=""
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

wait_for_protocol_ready() {
  local deadline=$((SECONDS + READINESS_TIMEOUT_SECONDS))
  local probe_log="$TEMP_ROOT/readiness-probe.log"
  while (( SECONDS < deadline )); do
    if python3 "$TARGET_SERVER/check_deployment.py" \
        127.0.0.1 "$SERVER_PORT" --timeout 1 >"$probe_log" 2>&1; then
      cat -- "$probe_log"
      return 0
    fi
    systemctl_yule is-active --quiet "$SERVICE_NAME" || break
    sleep 1
  done
  if [[ -s "$probe_log" ]]; then
    printf '%s\n' 'last protocol-probe failure:' >&2
    cat -- "$probe_log" >&2
  fi
  return 1
}

print_failure_diagnostics() {
  local log_file
  printf '%s\n' 'new service diagnostics before rollback:' >&2
  systemctl_yule status "$SERVICE_NAME" --no-pager --full >&2 || true
  for log_file in "$TARGET_SERVER/server.err.log" "$TARGET_SERVER/server.log"; do
    if [[ -s "$log_file" ]]; then
      printf '%s\n' "--- tail of $log_file ---" >&2
      tail -n 80 -- "$log_file" >&2 || true
    fi
  done
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
    *.log|*.jsonl|*.key|*.env|*.pid|*.sock)
      return 0
      ;;
  esac
  return 1
}

locate_source_project() {
  local candidate project
  local -a projects=()

  while IFS= read -r -d '' candidate; do
    project="${candidate%/online_server/server.js}"
    [[ -f "$project/online_server/package.json" && -d "$project/tests" ]] ||
      continue
    projects+=("$project")
  done < <(
    find "$SOURCE_ROOT" -mindepth 2 -maxdepth 5 -type f \
      -path '*/online_server/server.js' -print0
  )

  if (( ${#projects[@]} == 0 )); then
    printf '%s\n' 'clone layout (top three directory levels):' >&2
    find "$SOURCE_ROOT" -mindepth 1 -maxdepth 3 -type d -print >&2 || true
    die "clone did not contain a project with online_server/server.js, online_server/package.json, and tests/"
  fi
  if (( ${#projects[@]} > 1 )); then
    printf '%s\n' 'matching project roots:' >&2
    printf '  %s\n' "${projects[@]}" >&2
    die "clone contained multiple online-server project roots"
  fi

  SOURCE_PROJECT="${projects[0]}"
  SOURCE_SERVER="$SOURCE_PROJECT/online_server"
  printf 'using cloned project root: %s\n' \
    "${SOURCE_PROJECT#"$SOURCE_ROOT"/}"
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

for command in git node npm python3 find cp mv cat sleep tail; do
  command -v "$command" >/dev/null 2>&1 ||
    die "required command is missing: $command"
done
[[ "$READINESS_TIMEOUT_SECONDS" =~ ^[0-9]+$ ]] ||
  die "YULE_READINESS_TIMEOUT_SECONDS must be an integer"
(( READINESS_TIMEOUT_SECONDS >= 5 && READINESS_TIMEOUT_SECONDS <= 120 )) ||
  die "YULE_READINESS_TIMEOUT_SECONDS must be in 5..120"
[[ -d "$TARGET_SERVER" ]] || die "target server directory does not exist: $TARGET_SERVER"

printf 'cloning %s (%s)...\n' "$REPOSITORY_URL" "$REPOSITORY_REF"
git clone --quiet --depth 1 --branch "$REPOSITORY_REF" \
  "$REPOSITORY_URL" "$SOURCE_ROOT"
locate_source_project

printf '%s\n' 'validating the cloned server before stopping the live service...'
(
  cd -- "$SOURCE_SERVER"
  npm run check
  npm test
)
python3 "$SOURCE_PROJECT/tests/discord_lfg_server_static_test.py"
python3 "$SOURCE_PROJECT/tests/online_server_auth_static_test.py"
python3 "$SOURCE_PROJECT/tests/online_server_match_protocol_test.py"

mkdir -p -- "$STAGE_ROOT" "$BACKUP_ROOT"
while IFS= read -r -d '' source_file; do
  relative="${source_file#"$SOURCE_SERVER/"}"
  safe_relative_path "$relative" ||
    die "clone produced an unsafe server path: $relative"
  protected_runtime_path "$relative" && continue
  destination="$STAGE_ROOT/$relative"
  mkdir -p -- "$(dirname -- "$destination")"
  cp -a -- "$source_file" "$destination"
done < <(find "$SOURCE_SERVER" -type f -print0)

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

printf 'waiting up to %s seconds for TCP and UDP readiness...\n' \
  "$READINESS_TIMEOUT_SECONDS"
if ! wait_for_protocol_ready; then
  print_failure_diagnostics
  restore_previous_application
  die "the restarted service failed its TCP/UDP protocol probe"
fi

APPLY_STARTED=0
printf '%s\n' 'server update complete; runtime accounts, ratings, secrets, logs, and environment files were untouched'
