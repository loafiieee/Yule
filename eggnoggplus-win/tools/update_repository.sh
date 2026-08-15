#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
  cat <<'EOF'
Usage: tools/update_repository.sh [--dry-run]

Safely fast-forwards the Yule Git checkout and replaces tracked repository
files, while leaving the live online_server tree and local runtime state alone.

Environment:
  YULE_REPOSITORY_URL       Source Git URL (default: this checkout's origin)
  YULE_REPOSITORY_REF       Source branch or tag (default: main)
  YULE_TARGET_ROOT          Target Git repository root (normally auto-detected)
  YULE_PROJECT_PATH         Framework path inside that repository
  YULE_PRESERVE_PATHS       Extra colon-separated repository-relative paths
  YULE_SKIP_VALIDATION=1    Skip optional portable source checks
EOF
}

DRY_RUN=0
while (( $# > 0 )); do
  case "$1" in
    --dry-run)
      DRY_RUN=1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      printf 'repository update failed: unknown argument: %s\n' "$1" >&2
      usage >&2
      exit 2
      ;;
  esac
  shift
done

die() {
  printf 'repository update failed: %s\n' "$*" >&2
  exit 1
}

warn() {
  printf 'repository update warning: %s\n' "$*" >&2
}

safe_relative_path() {
  local value="$1"
  [[ -n "$value" && "$value" != /* && "$value" != . &&
     "$value" != .. && "$value" != ../* && "$value" != */../* &&
     "$value" != */.. && "$value" != *\\* &&
     "$value" != *$'\n'* && "$value" != *$'\r'* ]]
}

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
DEFAULT_PROJECT="$(cd -- "$SCRIPT_DIR/.." && pwd -P)"

if [[ -n "${YULE_TARGET_ROOT:-}" ]]; then
  [[ -d "$YULE_TARGET_ROOT" ]] ||
    die "YULE_TARGET_ROOT does not exist: $YULE_TARGET_ROOT"
  TARGET_REPO="$(cd -- "$YULE_TARGET_ROOT" && pwd -P)"
  if [[ -n "${YULE_PROJECT_PATH:-}" ]]; then
    safe_relative_path "$YULE_PROJECT_PATH" ||
      die "YULE_PROJECT_PATH must be a safe repository-relative path"
    TARGET_PROJECT="$TARGET_REPO/${YULE_PROJECT_PATH%/}"
  elif [[ -f "$TARGET_REPO/hooks.c" && -f "$TARGET_REPO/compile.sh" ]]; then
    TARGET_PROJECT="$TARGET_REPO"
  elif [[ -f "$TARGET_REPO/eggnoggplus-win/hooks.c" &&
          -f "$TARGET_REPO/eggnoggplus-win/compile.sh" ]]; then
    TARGET_PROJECT="$TARGET_REPO/eggnoggplus-win"
  else
    die "could not locate the framework project under YULE_TARGET_ROOT"
  fi
else
  TARGET_PROJECT="$DEFAULT_PROJECT"
  TARGET_REPO="$(git -C "$TARGET_PROJECT" rev-parse --show-toplevel 2>/dev/null)" ||
    die "the updater is not inside a Git checkout"
  TARGET_REPO="$(cd -- "$TARGET_REPO" && pwd -P)"
fi

[[ -d "$TARGET_PROJECT" ]] ||
  die "target framework directory does not exist: $TARGET_PROJECT"
ACTUAL_TARGET_REPO="$(git -C "$TARGET_PROJECT" rev-parse --show-toplevel 2>/dev/null)" ||
  die "target framework directory is not in a Git checkout"
ACTUAL_TARGET_REPO="$(cd -- "$ACTUAL_TARGET_REPO" && pwd -P)"
[[ "$ACTUAL_TARGET_REPO" == "$TARGET_REPO" ]] ||
  die "YULE_TARGET_ROOT is not the target framework's Git root"

PROJECT_PREFIX="$(git -C "$TARGET_PROJECT" rev-parse --show-prefix)"
PROJECT_PATH="${PROJECT_PREFIX%/}"
if [[ -n "${YULE_PROJECT_PATH:-}" &&
      "${YULE_PROJECT_PATH%/}" != "$PROJECT_PATH" ]]; then
  die "YULE_PROJECT_PATH does not match Git's project path: $PROJECT_PATH"
fi

CURRENT_BRANCH_REF="$(git -C "$TARGET_REPO" symbolic-ref -q HEAD)" ||
  die "target checkout has a detached HEAD"
CURRENT_BRANCH="${CURRENT_BRANCH_REF#refs/heads/}"
OLD_COMMIT="$(git -C "$TARGET_REPO" rev-parse HEAD)"

DEFAULT_REMOTE_URL="$(git -C "$TARGET_REPO" remote get-url origin 2>/dev/null || true)"
REPOSITORY_URL="${YULE_REPOSITORY_URL:-$DEFAULT_REMOTE_URL}"
REPOSITORY_REF="${YULE_REPOSITORY_REF:-main}"
[[ -n "$REPOSITORY_URL" ]] ||
  die "no repository URL was provided and the target has no origin remote"
[[ -n "$REPOSITORY_REF" && "$REPOSITORY_REF" != -* &&
   "$REPOSITORY_REF" != *$'\n'* && "$REPOSITORY_REF" != *$'\r'* ]] ||
  die "YULE_REPOSITORY_REF is invalid"

project_path() {
  local suffix="$1"
  if [[ -n "$PROJECT_PATH" ]]; then
    printf '%s/%s' "$PROJECT_PATH" "$suffix"
  else
    printf '%s' "$suffix"
  fi
}

PROJECT_ONLINE_SERVER="$(project_path online_server)"
PROJECT_BUILD="$(project_path build)"
PROJECT_DIST="$(project_path dist)"
PROJECT_MODS="$(project_path mods)"

declare -a EXTRA_PRESERVE=()
if [[ -n "${YULE_PRESERVE_PATHS:-}" ]]; then
  IFS=':' read -r -a requested_preserve <<< "$YULE_PRESERVE_PATHS"
  for preserve in "${requested_preserve[@]}"; do
    preserve="${preserve#./}"
    preserve="${preserve%/}"
    safe_relative_path "$preserve" ||
      die "unsafe YULE_PRESERVE_PATHS entry: $preserve"
    EXTRA_PRESERVE+=("$preserve")
  done
fi

protected_path() {
  local value="$1"
  local preserve
  case "$value" in
    .git|.git/*|.yule-update-backups|.yule-update-backups/*|\
    *.env|*.key|*.pem|*.pid|*.sock|*.log|*.dmp|\
    .vscode|.vscode/*|.idea|.idea/*)
      return 0
      ;;
    "$PROJECT_ONLINE_SERVER"|"$PROJECT_ONLINE_SERVER"/*|\
    "$PROJECT_BUILD"|"$PROJECT_BUILD"/*|\
    "$PROJECT_DIST"|"$PROJECT_DIST"/*)
      return 0
      ;;
    "$PROJECT_MODS/modframework.cfg"|\
    "$PROJECT_MODS/online_hub.cfg"|\
    "$PROJECT_MODS/console_history.txt"|\
    "$PROJECT_MODS/crash.log"|\
    "$PROJECT_MODS/desync_dump.log"|\
    "$PROJECT_MODS/updater_launcher.log"|\
    "$(project_path online.cfg)"|\
    "$(project_path online_server_data.json)"|\
    "$(project_path online_server_data.json.tmp)")
      return 0
      ;;
    "$PROJECT_MODS"/*.log|"$PROJECT_MODS"/*.dmp|\
    "$PROJECT_MODS"/*/*.log|"$PROJECT_MODS"/*/*.dmp|\
    "$PROJECT_MODS"/*/config.cfg|"$PROJECT_MODS"/*/config.json|\
    "$PROJECT_MODS"/*/storage.cfg|"$PROJECT_MODS"/*/storage.json|\
    "$PROJECT_MODS"/*/cache|"$PROJECT_MODS"/*/cache/*)
      return 0
      ;;
  esac
  for preserve in "${EXTRA_PRESERVE[@]}"; do
    if [[ "$value" == "$preserve" || "$value" == "$preserve/"* ]]; then
      return 0
    fi
  done
  return 1
}

assert_target_parent_safe() {
  local relative="$1"
  local parent_relative
  local cursor="$TARGET_REPO"
  local component
  parent_relative="$(dirname -- "$relative")"
  [[ "$parent_relative" != . ]] || return 0
  IFS='/' read -r -a parent_components <<< "$parent_relative"
  for component in "${parent_components[@]}"; do
    cursor="$cursor/$component"
    [[ ! -L "$cursor" ]] ||
      die "target parent is a symbolic link: $relative"
    if [[ -e "$cursor" && ! -d "$cursor" ]]; then
      die "target parent is not a directory: $relative"
    fi
  done
}

for command in git find cp mv cmp sort comm grep wc tr sed; do
  command -v "$command" >/dev/null 2>&1 ||
    die "required command is missing: $command"
done

if ! git -C "$TARGET_REPO" diff --cached --quiet --; then
  die "target checkout has staged changes; commit or unstage them first"
fi

declare -a DIRTY_UNPROTECTED=()
while IFS= read -r -d '' dirty_path; do
  safe_relative_path "$dirty_path" ||
    die "Git reported an unsafe changed path"
  protected_path "$dirty_path" || DIRTY_UNPROTECTED+=("$dirty_path")
done < <(git -C "$TARGET_REPO" diff --name-only -z --)
if (( ${#DIRTY_UNPROTECTED[@]} > 0 )); then
  printf '%s\n' 'non-runtime local edits that would be overwritten:' >&2
  printf '  %s\n' "${DIRTY_UNPROTECTED[@]}" >&2
  die "commit, restore, or explicitly preserve those paths before updating"
fi

TEMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/yule-repository-update.XXXXXXXX")"
SOURCE_ROOT="$TEMP_ROOT/source"
SOURCE_PROJECT=""
SOURCE_PROJECT_PATH=""
STAGE_ROOT="$TEMP_ROOT/stage"
BACKUP_ROOT="$TEMP_ROOT/backup"
SOURCE_LIST="$TEMP_ROOT/source.list"
TARGET_LIST="$TEMP_ROOT/target.list"
STALE_LIST="$TEMP_ROOT/stale.list"
BACKUP_LIST="$TEMP_ROOT/backup.list"
NEW_LIST="$TEMP_ROOT/new.list"
PROTECTED_CHANGED_LIST="$TEMP_ROOT/protected-changed.list"
APPLY_STARTED=0
INDEX_UPDATED=0
BRANCH_UPDATED=0
UPSTREAM_UPDATED=0
UPSTREAM_REF=""
UPSTREAM_OLD=""

restore_previous_tree() {
  local relative destination saved
  set +e
  printf '%s\n' 'repository update failed; restoring the previous checkout' >&2

  if [[ "$UPSTREAM_UPDATED" -eq 1 ]]; then
    git -C "$TARGET_REPO" update-ref "$UPSTREAM_REF" \
      "$UPSTREAM_OLD" "$SOURCE_COMMIT" >/dev/null 2>&1 || true
    UPSTREAM_UPDATED=0
  fi
  if [[ "$BRANCH_UPDATED" -eq 1 ]]; then
    git -C "$TARGET_REPO" update-ref "$CURRENT_BRANCH_REF" \
      "$OLD_COMMIT" "$SOURCE_COMMIT" >/dev/null 2>&1 || true
    BRANCH_UPDATED=0
  fi
  if [[ "$INDEX_UPDATED" -eq 1 ]]; then
    git -C "$TARGET_REPO" read-tree "$OLD_COMMIT" >/dev/null 2>&1 || true
    INDEX_UPDATED=0
  fi

  if [[ -f "$NEW_LIST" ]]; then
    while IFS= read -r relative; do
      safe_relative_path "$relative" || continue
      protected_path "$relative" && continue
      rm -f -- "$TARGET_REPO/$relative" >/dev/null 2>&1 || true
    done < "$NEW_LIST"
  fi
  if [[ -f "$BACKUP_LIST" ]]; then
    while IFS= read -r relative; do
      safe_relative_path "$relative" || continue
      protected_path "$relative" && continue
      destination="$TARGET_REPO/$relative"
      saved="$BACKUP_ROOT/$relative"
      mkdir -p -- "$(dirname -- "$destination")"
      cp -a -- "$saved" "$destination" >/dev/null 2>&1 || true
    done < "$BACKUP_LIST"
  fi
  APPLY_STARTED=0
}

cleanup() {
  local status=$?
  trap - EXIT
  if [[ "$APPLY_STARTED" -eq 1 ]]; then
    restore_previous_tree
  fi
  rm -rf -- "$TEMP_ROOT"
  exit "$status"
}
trap cleanup EXIT

locate_source_project() {
  local candidate project relative
  local -a projects=()

  while IFS= read -r -d '' candidate; do
    project="${candidate%/hooks.c}"
    [[ -f "$project/compile.sh" &&
       -f "$project/online_server/server.js" &&
       -d "$project/tests" &&
       -d "$project/tools" ]] || continue
    projects+=("$project")
  done < <(
    find "$SOURCE_ROOT" -mindepth 1 -maxdepth 5 -type f \
      -name hooks.c -print0
  )

  if (( ${#projects[@]} == 0 )); then
    die "clone did not contain a recognizable Yule framework project"
  fi
  if (( ${#projects[@]} > 1 )); then
    printf '%s\n' 'matching cloned project roots:' >&2
    printf '  %s\n' "${projects[@]}" >&2
    die "clone contained multiple Yule framework projects"
  fi

  SOURCE_PROJECT="${projects[0]}"
  if [[ "$SOURCE_PROJECT" == "$SOURCE_ROOT" ]]; then
    SOURCE_PROJECT_PATH=""
  else
    relative="${SOURCE_PROJECT#"$SOURCE_ROOT"/}"
    safe_relative_path "$relative" ||
      die "cloned project resolved outside the source repository"
    SOURCE_PROJECT_PATH="${relative%/}"
  fi
  [[ "$SOURCE_PROJECT_PATH" == "$PROJECT_PATH" ]] ||
    die "clone project path '$SOURCE_PROJECT_PATH' does not match target '$PROJECT_PATH'"
}

printf 'cloning %s (%s)...\n' "$REPOSITORY_URL" "$REPOSITORY_REF"
git clone --quiet --no-tags --branch "$REPOSITORY_REF" \
  "$REPOSITORY_URL" "$SOURCE_ROOT"
locate_source_project

SOURCE_COMMIT="$(git -C "$SOURCE_ROOT" rev-parse HEAD)"
git -C "$SOURCE_ROOT" fsck --no-dangling --no-progress >/dev/null
if [[ "$SOURCE_COMMIT" != "$OLD_COMMIT" ]] &&
   ! git -C "$SOURCE_ROOT" merge-base --is-ancestor \
     "$OLD_COMMIT" "$SOURCE_COMMIT"; then
  die "source ref is not a fast-forward of the target checkout"
fi

if [[ "${YULE_SKIP_VALIDATION:-0}" != 1 ]]; then
  printf '%s\n' 'validating the cloned repository before applying files...'
  if command -v node >/dev/null 2>&1; then
    command -v npm >/dev/null 2>&1 ||
      die "node is available but required command is missing: npm"
    node "$SOURCE_PROJECT/docs-site/check-docs.js"
    (
      cd -- "$SOURCE_PROJECT/online_server"
      npm run check
      npm test
    )
  else
    warn "node is unavailable; skipping documentation and Node service checks"
  fi
  if command -v python3 >/dev/null 2>&1; then
    python3 "$SOURCE_PROJECT/tests/compile_sources_static_test.py"
    python3 "$SOURCE_PROJECT/tests/online_flow_integration_static_test.py"
    python3 "$SOURCE_PROJECT/tests/online_server_auth_static_test.py"
  else
    warn "python3 is unavailable; skipping portable source contract checks"
  fi
fi

mkdir -p -- "$STAGE_ROOT" "$BACKUP_ROOT"
: > "$SOURCE_LIST"
: > "$TARGET_LIST"
: > "$BACKUP_LIST"
: > "$NEW_LIST"
: > "$PROTECTED_CHANGED_LIST"

while IFS= read -r -d '' relative; do
  safe_relative_path "$relative" ||
    die "clone contained an unsafe tracked path"
  protected_path "$relative" && continue
  source_file="$SOURCE_ROOT/$relative"
  [[ ! -L "$source_file" ]] ||
    die "refusing to deploy a tracked symbolic link: $relative"
  [[ -f "$source_file" ]] ||
    die "tracked source is not a regular file: $relative"
  printf '%s\n' "$relative" >> "$SOURCE_LIST"
  staged_file="$STAGE_ROOT/$relative"
  mkdir -p -- "$(dirname -- "$staged_file")"
  cp -a -- "$source_file" "$staged_file"
done < <(git -C "$SOURCE_ROOT" ls-files -z --)

while IFS= read -r -d '' relative; do
  safe_relative_path "$relative" ||
    die "target contained an unsafe tracked path"
  protected_path "$relative" && continue
  printf '%s\n' "$relative" >> "$TARGET_LIST"
done < <(git -C "$TARGET_REPO" ls-files -z --)

sort -u -o "$SOURCE_LIST" "$SOURCE_LIST"
sort -u -o "$TARGET_LIST" "$TARGET_LIST"
comm -23 "$TARGET_LIST" "$SOURCE_LIST" > "$STALE_LIST"

while IFS= read -r -d '' relative; do
  safe_relative_path "$relative" ||
    die "Git reported an unsafe changed path"
  protected_path "$relative" &&
    printf '%s\n' "$relative" >> "$PROTECTED_CHANGED_LIST"
done < <(
  git -C "$SOURCE_ROOT" diff --name-only -z "$OLD_COMMIT" "$SOURCE_COMMIT" --
)
sort -u -o "$PROTECTED_CHANGED_LIST" "$PROTECTED_CHANGED_LIST"

while IFS= read -r relative; do
  assert_target_parent_safe "$relative"
  destination="$TARGET_REPO/$relative"
  if [[ -e "$destination" || -L "$destination" ]]; then
    if ! grep -Fqx -- "$relative" "$TARGET_LIST"; then
      if [[ -f "$destination" && ! -L "$destination" ]] &&
         cmp -s -- "$STAGE_ROOT/$relative" "$destination"; then
        printf 'adopting byte-identical untracked upstream file: %s\n' \
          "$relative"
      else
        die "upstream file conflicts with untracked local path: $relative"
      fi
    fi
    [[ ! -d "$destination" && ! -L "$destination" ]] ||
      die "tracked destination is not a regular file: $relative"
  fi
done < "$SOURCE_LIST"
while IFS= read -r relative; do
  assert_target_parent_safe "$relative"
done < "$STALE_LIST"

SOURCE_COUNT="$(wc -l < "$SOURCE_LIST" | tr -d ' ')"
STALE_COUNT="$(wc -l < "$STALE_LIST" | tr -d ' ')"
PROTECTED_CHANGED_COUNT="$(
  wc -l < "$PROTECTED_CHANGED_LIST" | tr -d ' '
)"
printf 'source %s -> %s; %s tracked files selected, %s stale files, %s protected upstream changes\n' \
  "$OLD_COMMIT" "$SOURCE_COMMIT" "$SOURCE_COUNT" "$STALE_COUNT" \
  "$PROTECTED_CHANGED_COUNT"

if [[ "$DRY_RUN" -eq 1 ]]; then
  printf '%s\n' 'dry run complete; no target files or Git refs were changed'
  if [[ -s "$STALE_LIST" ]]; then
    printf '%s\n' 'tracked files that would be removed:'
    sed -n '1,80p' "$STALE_LIST"
  fi
  if [[ -s "$PROTECTED_CHANGED_LIST" ]]; then
    printf '%s\n' 'upstream changes intentionally left live/untouched:'
    sed -n '1,80p' "$PROTECTED_CHANGED_LIST"
  fi
  exit 0
fi

if [[ "$SOURCE_COMMIT" == "$OLD_COMMIT" ]]; then
  printf '%s\n' 'repository is already at the requested source commit'
  exit 0
fi

git -C "$TARGET_REPO" fetch --quiet --no-tags \
  "$SOURCE_ROOT" "$REPOSITORY_REF"
FETCHED_COMMIT="$(git -C "$TARGET_REPO" rev-parse FETCH_HEAD)"
[[ "$FETCHED_COMMIT" == "$SOURCE_COMMIT" ]] ||
  die "local source fetch produced an unexpected commit"

if UPSTREAM_REF="$(
    git -C "$TARGET_REPO" rev-parse --symbolic-full-name '@{upstream}' \
      2>/dev/null
  )"; then
  UPSTREAM_OLD="$(git -C "$TARGET_REPO" rev-parse "$UPSTREAM_REF")"
  if [[ -n "${YULE_REPOSITORY_URL:-}" ||
        "$UPSTREAM_REF" != "refs/remotes/origin/$REPOSITORY_REF" ]]; then
    UPSTREAM_REF=""
    UPSTREAM_OLD=""
  fi
else
  UPSTREAM_REF=""
  UPSTREAM_OLD=""
fi

APPLY_STARTED=1

while IFS= read -r relative; do
  safe_relative_path "$relative" ||
    die "stale-file manifest contained an unsafe path"
  protected_path "$relative" &&
    die "stale-file manifest selected protected state: $relative"
  destination="$TARGET_REPO/$relative"
  if [[ -e "$destination" || -L "$destination" ]]; then
    [[ ! -d "$destination" && ! -L "$destination" ]] ||
      die "stale tracked path is not a regular file: $relative"
    saved="$BACKUP_ROOT/$relative"
    mkdir -p -- "$(dirname -- "$saved")"
    cp -a -- "$destination" "$saved"
    printf '%s\n' "$relative" >> "$BACKUP_LIST"
    rm -f -- "$destination"
  fi
done < "$STALE_LIST"

while IFS= read -r relative; do
  safe_relative_path "$relative" ||
    die "source manifest contained an unsafe path"
  protected_path "$relative" &&
    die "source manifest selected protected state: $relative"
  staged_file="$STAGE_ROOT/$relative"
  destination="$TARGET_REPO/$relative"
  if [[ -e "$destination" || -L "$destination" ]]; then
    [[ ! -d "$destination" && ! -L "$destination" ]] ||
      die "target path is not a regular file: $relative"
    saved="$BACKUP_ROOT/$relative"
    mkdir -p -- "$(dirname -- "$saved")"
    cp -a -- "$destination" "$saved"
    printf '%s\n' "$relative" >> "$BACKUP_LIST"
  else
    printf '%s\n' "$relative" >> "$NEW_LIST"
  fi
  mkdir -p -- "$(dirname -- "$destination")"
  temporary="$destination.yule-update.$$"
  [[ ! -e "$temporary" && ! -L "$temporary" ]] ||
    die "temporary apply path already exists: $temporary"
  cp -a -- "$staged_file" "$temporary"
  mv -f -- "$temporary" "$destination"
done < "$SOURCE_LIST"

while IFS= read -r relative; do
  cmp -s -- "$STAGE_ROOT/$relative" "$TARGET_REPO/$relative" ||
    die "post-apply verification failed: $relative"
done < "$SOURCE_LIST"
while IFS= read -r relative; do
  [[ ! -e "$TARGET_REPO/$relative" && ! -L "$TARGET_REPO/$relative" ]] ||
    die "stale tracked file survived the update: $relative"
done < "$STALE_LIST"

git -C "$TARGET_REPO" read-tree "$SOURCE_COMMIT"
INDEX_UPDATED=1
git -C "$TARGET_REPO" update-ref "$CURRENT_BRANCH_REF" \
  "$SOURCE_COMMIT" "$OLD_COMMIT"
BRANCH_UPDATED=1
if [[ -n "$UPSTREAM_REF" ]]; then
  git -C "$TARGET_REPO" update-ref "$UPSTREAM_REF" \
    "$SOURCE_COMMIT" "$UPSTREAM_OLD"
  UPSTREAM_UPDATED=1
fi

APPLY_STARTED=0
printf 'repository update complete: %s now points to %s\n' \
  "$CURRENT_BRANCH" "$SOURCE_COMMIT"
printf '%s\n' 'The entire online_server directory, runtime configuration, logs, caches, build outputs, editor settings, and untracked files were untouched.'
if [[ "$PROTECTED_CHANGED_COUNT" -gt 0 ]]; then
  printf '%s\n' 'Protected upstream changes remain visible in git status by design; run online_server/update_server.sh separately to deploy server code.'
fi
