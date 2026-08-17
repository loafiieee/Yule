#!/usr/bin/env bash
# EGGNOGG+ Framework (Yule) installer for Linux hosts running the Windows game
# through Wine. Run with: bash install-linux.sh

set -Eeuo pipefail
IFS=$'\n\t'

APP_NAME='EGGNOGG+'
EXE_NAME='eggnoggplus.exe'
UPDATER_NAME='YuleUpdater.exe'
CHANNEL_URL='https://loafiieee.com/yule/releases/latest.json'
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
XDG_DATA_ROOT=${XDG_DATA_HOME:-"$HOME/.local/share"}
XDG_STATE_ROOT=${XDG_STATE_HOME:-"$HOME/.local/state"}
XDG_BIN_ROOT=${XDG_BIN_HOME:-"$HOME/.local/bin"}
INSTALL_DIR="$XDG_DATA_ROOT/yule/EGGNOGG+"
STATE_DIR="$XDG_STATE_ROOT/yule"
RECEIPT="$STATE_DIR/install-linux.json"
APPLICATIONS_DIR="$XDG_DATA_ROOT/applications"
ICONS_DIR="$XDG_DATA_ROOT/icons/hicolor/256x256/apps"
WRAPPER_PATH="$XDG_BIN_ROOT/yule-eggnoggplus"
DESKTOP_PATH="$APPLICATIONS_DIR/yule-eggnoggplus.desktop"
ICON_PATH="$ICONS_DIR/yule-eggnoggplus.png"
GAME_PATH=''
WINE_PREFIX=${WINEPREFIX:-"$HOME/.wine"}
WINE_COMMAND=${WINE_BIN:-wine}
ASSUME_YES=0
UNINSTALL=0
SKIP_PROTOCOL=0

say() { printf '%s\n' "$*"; }
warn() { printf 'warning: %s\n' "$*" >&2; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

usage() {
    cat <<'EOF'
Usage: bash install-linux.sh [options]

Installs the Windows EGGNOGG+ build for Wine and registers yule:// links with
the Linux desktop. The game itself remains the Windows build.

Options:
  --game-path PATH     Existing eggnoggplus.exe or its directory
  --install-dir PATH   Destination (default: ~/.local/share/yule/EGGNOGG+)
  --wine-prefix PATH   Wine prefix used for launches (default: $WINEPREFIX or ~/.wine)
  --wine-bin COMMAND   Wine executable (default: $WINE_BIN or wine)
  --channel-url URL    Release channel manifest
  --skip-protocol      Do not install the desktop launcher or yule:// handler
  --yes                Accept installation prompts
  --uninstall          Safely remove installer-owned files and restore vanilla SDL2
  --help                Show this help
EOF
}

while (($#)); do
    case "$1" in
        --game-path) (($# >= 2)) || die '--game-path needs a value'; GAME_PATH=$2; shift 2 ;;
        --install-dir) (($# >= 2)) || die '--install-dir needs a value'; INSTALL_DIR=$2; shift 2 ;;
        --wine-prefix) (($# >= 2)) || die '--wine-prefix needs a value'; WINE_PREFIX=$2; shift 2 ;;
        --wine-bin) (($# >= 2)) || die '--wine-bin needs a value'; WINE_COMMAND=$2; shift 2 ;;
        --channel-url) (($# >= 2)) || die '--channel-url needs a value'; CHANNEL_URL=$2; shift 2 ;;
        --skip-protocol) SKIP_PROTOCOL=1; shift ;;
        --yes|-y) ASSUME_YES=1; shift ;;
        --uninstall) UNINSTALL=1; shift ;;
        --help|-h) usage; exit 0 ;;
        *) die "unknown option: $1" ;;
    esac
done

need() { command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"; }
need python3
need sha256sum
if command -v curl >/dev/null 2>&1; then
    fetch() { curl --fail --location --proto '=https,http' --tlsv1.2 --silent --show-error --output "$2" "$1"; }
elif command -v wget >/dev/null 2>&1; then
    fetch() { wget --quiet --output-document="$2" "$1"; }
else
    die 'curl or wget is required'
fi

absolute_path() {
    python3 - "$1" <<'PY'
import os, sys
print(os.path.abspath(os.path.expanduser(sys.argv[1])))
PY
}

INSTALL_DIR=$(absolute_path "$INSTALL_DIR")
STATE_DIR=$(absolute_path "$STATE_DIR")
RECEIPT="$STATE_DIR/install-linux.json"
WINE_PREFIX=$(absolute_path "$WINE_PREFIX")

sha256_file() { sha256sum -- "$1" | awk '{print tolower($1)}'; }

ask_yes() {
    local prompt=$1 answer
    if ((ASSUME_YES)); then return 0; fi
    read -r -p "$prompt [Y/n] " answer
    [[ -z $answer || $answer == [yY]* ]]
}

json_field() {
    python3 - "$1" "$2" <<'PY'
import json, sys
with open(sys.argv[1], encoding='utf-8') as stream:
    value = json.load(stream)
for part in sys.argv[2].split('.'):
    value = value[part]
print(value if value is not None else '')
PY
}

remove_owned_desktop_files() {
    local receipt_path=$1 current previous desktop wrapper icon desktop_hash wrapper_hash icon_hash
    desktop=$(json_field "$receipt_path" desktop.file 2>/dev/null || true)
    wrapper=$(json_field "$receipt_path" desktop.wrapper 2>/dev/null || true)
    icon=$(json_field "$receipt_path" desktop.icon 2>/dev/null || true)
    desktop_hash=$(json_field "$receipt_path" desktop.file_sha256 2>/dev/null || true)
    wrapper_hash=$(json_field "$receipt_path" desktop.wrapper_sha256 2>/dev/null || true)
    icon_hash=$(json_field "$receipt_path" desktop.icon_sha256 2>/dev/null || true)
    previous=$(json_field "$receipt_path" desktop.previous_handler 2>/dev/null || true)

    current=''
    if command -v xdg-mime >/dev/null 2>&1; then
        current=$(xdg-mime query default x-scheme-handler/yule 2>/dev/null || true)
    fi
    if [[ $current == 'yule-eggnoggplus.desktop' && -n $previous && $previous != 'yule-eggnoggplus.desktop' ]]; then
        xdg-mime default "$previous" x-scheme-handler/yule >/dev/null 2>&1 || true
    fi
    for spec in "$desktop|$desktop_hash" "$wrapper|$wrapper_hash" "$icon|$icon_hash"; do
        local path=${spec%%|*} expected=${spec#*|}
        [[ -n $path && -f $path ]] || continue
        if [[ -n $expected && $(sha256_file "$path") == "$expected" ]]; then
            rm -f -- "$path"
        else
            warn "preserved modified desktop integration file: $path"
        fi
    done
    command -v update-desktop-database >/dev/null 2>&1 &&
        update-desktop-database "$APPLICATIONS_DIR" >/dev/null 2>&1 || true
}

uninstall_yule() {
    [[ -f $RECEIPT ]] || die "no Linux installer receipt found at $RECEIPT"
    local receipt_install adopted path expected target preserved=0
    receipt_install=$(absolute_path "$(json_field "$RECEIPT" install_dir)")
    [[ $receipt_install != / && $receipt_install != "$HOME" ]] || die 'unsafe install directory in receipt'
    [[ -d $receipt_install ]] || die "installed game directory is missing: $receipt_install"
    adopted=$(json_field "$RECEIPT" adopted_vanilla)

    while IFS=$'\t' read -r path expected; do
        [[ $path =~ ^[A-Za-z0-9._+-]+$ ]] || die "unsafe managed path in receipt: $path"
        target="$receipt_install/$path"
        [[ -f $target ]] || continue
        if [[ $(sha256_file "$target") == "$expected" ]]; then
            rm -f -- "$target"
        else
            warn "preserved modified managed file: $target"
            preserved=1
        fi
    done < <(python3 - "$RECEIPT" <<'PY'
import json, sys
with open(sys.argv[1], encoding='utf-8') as stream:
    receipt = json.load(stream)
for item in receipt.get('managed_files', []):
    print(f"{item['path']}\t{item['sha256']}")
PY
)

    if [[ $adopted == True || $adopted == true || $adopted == 1 ]]; then
        if [[ ! -e "$receipt_install/SDL2.dll" && -f "$receipt_install/SDL2_real.dll" ]]; then
            mv -- "$receipt_install/SDL2_real.dll" "$receipt_install/SDL2.dll"
            say 'Restored the original SDL2.dll.'
        elif [[ -f "$receipt_install/SDL2_real.dll" ]]; then
            warn 'preserved SDL2_real.dll because the installed SDL2.dll was modified'
            preserved=1
        fi
    fi

    remove_owned_desktop_files "$RECEIPT"
    rm -f -- "$receipt_install/install-linux.json" "$RECEIPT"
    rmdir --ignore-fail-on-non-empty "$STATE_DIR" 2>/dev/null || true
    say 'Yule framework uninstall complete; maps, mods, saves, and modified files were preserved.'
    ((preserved == 0)) || say 'Some locally modified framework files remain, as listed above.'
}

if ((UNINSTALL)); then
    uninstall_yule
    exit 0
fi

command -v "$WINE_COMMAND" >/dev/null 2>&1 || die "Wine executable not found: $WINE_COMMAND"
python3 - "$CHANNEL_URL" <<'PY'
import sys
from urllib.parse import urlparse
parsed = urlparse(sys.argv[1])
if (parsed.scheme not in ('https', 'http') or not parsed.netloc or
        parsed.username or parsed.password or
        (parsed.scheme == 'http' and parsed.hostname not in ('127.0.0.1', 'localhost', '::1'))):
    raise SystemExit('error: channel URL must use HTTPS (HTTP is allowed only for local testing)')
PY

if [[ -z $GAME_PATH && -f $RECEIPT ]]; then
    GAME_PATH=$(json_field "$RECEIPT" game_executable 2>/dev/null || true)
fi
if [[ -z $GAME_PATH && -f "$SCRIPT_DIR/$EXE_NAME" ]]; then GAME_PATH="$SCRIPT_DIR/$EXE_NAME"; fi
if [[ -z $GAME_PATH && -f "$SCRIPT_DIR/../$EXE_NAME" ]]; then GAME_PATH="$SCRIPT_DIR/../$EXE_NAME"; fi
if [[ -z $GAME_PATH ]]; then
    if ((ASSUME_YES)); then die '--game-path is required for a first unattended install'; fi
    read -r -p "Path to the Windows $EXE_NAME (or its folder): " GAME_PATH
fi
GAME_PATH=$(absolute_path "$GAME_PATH")
if [[ -d $GAME_PATH ]]; then GAME_PATH="$GAME_PATH/$EXE_NAME"; fi
[[ -f $GAME_PATH && $(basename -- "$GAME_PATH") == "$EXE_NAME" ]] ||
    die "could not find $EXE_NAME at $GAME_PATH"
SOURCE_DIR=$(dirname -- "$GAME_PATH")

if [[ ! -e $INSTALL_DIR ]]; then
    mkdir -p -- "$(dirname -- "$INSTALL_DIR")"
    if [[ $SOURCE_DIR == "$INSTALL_DIR" ]]; then
        mkdir -p -- "$INSTALL_DIR"
    else
        cp -a -- "$SOURCE_DIR" "$INSTALL_DIR"
    fi
elif [[ $(absolute_path "$SOURCE_DIR") != "$INSTALL_DIR" && ! -f "$INSTALL_DIR/$EXE_NAME" ]]; then
    die "destination exists but is not an EGGNOGG+ install: $INSTALL_DIR"
fi
[[ -f "$INSTALL_DIR/$EXE_NAME" ]] || die 'installed game executable is missing after copy'

TMP_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/yule-linux-install.XXXXXXXX")
ROLLBACK_DIR="$TMP_ROOT/rollback"
mkdir -p -- "$ROLLBACK_DIR"
COMMITTED=0
SDL_REAL_CREATED=0
CFG_TOUCHED=0
CFG_EXISTED=0
DESKTOP_TOUCHED=0
WRAPPER_TOUCHED=0
ICON_TOUCHED=0
PROTOCOL_APPLIED=0
PREVIOUS_HANDLER=''
DESKTOP_HASH=''
WRAPPER_HASH=''
ICON_HASH=''
if ((SKIP_PROTOCOL)) && [[ -f $RECEIPT ]]; then
    # A maintenance install that declines desktop changes must retain ownership
    # metadata for integration installed by an earlier successful run.
    old_value=$(json_field "$RECEIPT" desktop.file 2>/dev/null || true)
    [[ -z $old_value ]] || DESKTOP_PATH=$old_value
    old_value=$(json_field "$RECEIPT" desktop.wrapper 2>/dev/null || true)
    [[ -z $old_value ]] || WRAPPER_PATH=$old_value
    old_value=$(json_field "$RECEIPT" desktop.icon 2>/dev/null || true)
    [[ -z $old_value ]] || ICON_PATH=$old_value
    DESKTOP_HASH=$(json_field "$RECEIPT" desktop.file_sha256 2>/dev/null || true)
    WRAPPER_HASH=$(json_field "$RECEIPT" desktop.wrapper_sha256 2>/dev/null || true)
    ICON_HASH=$(json_field "$RECEIPT" desktop.icon_sha256 2>/dev/null || true)
    PREVIOUS_HANDLER=$(json_field "$RECEIPT" desktop.previous_handler 2>/dev/null || true)
fi
rollback() {
    local status=$?
    if ((COMMITTED == 0)); then
        warn 'installation did not complete; restoring changed framework files'
        if [[ -f "$TMP_ROOT/changed.list" ]]; then
            while IFS=$'\t' read -r name existed; do
                [[ -n $name ]] || continue
                if [[ $existed == 1 ]]; then
                    cp -a -- "$ROLLBACK_DIR/$name" "$INSTALL_DIR/$name"
                else
                    rm -f -- "$INSTALL_DIR/$name"
                fi
            done < "$TMP_ROOT/changed.list"
        fi
        if ((CFG_TOUCHED)); then
            if ((CFG_EXISTED)); then
                cp -a -- "$ROLLBACK_DIR/modframework.cfg" "$INSTALL_DIR/mods/modframework.cfg"
            else
                rm -f -- "$INSTALL_DIR/mods/modframework.cfg"
            fi
        fi
        if ((DESKTOP_TOUCHED)); then
            if [[ -f "$ROLLBACK_DIR/desktop" ]]; then cp -a -- "$ROLLBACK_DIR/desktop" "$DESKTOP_PATH"; else rm -f -- "$DESKTOP_PATH"; fi
        fi
        if ((WRAPPER_TOUCHED)); then
            if [[ -f "$ROLLBACK_DIR/wrapper" ]]; then cp -a -- "$ROLLBACK_DIR/wrapper" "$WRAPPER_PATH"; else rm -f -- "$WRAPPER_PATH"; fi
        fi
        if ((ICON_TOUCHED)); then
            if [[ -f "$ROLLBACK_DIR/icon" ]]; then cp -a -- "$ROLLBACK_DIR/icon" "$ICON_PATH"; else rm -f -- "$ICON_PATH"; fi
        fi
        if ((PROTOCOL_APPLIED)) && command -v xdg-mime >/dev/null 2>&1 &&
           [[ -n $PREVIOUS_HANDLER && $PREVIOUS_HANDLER != yule-eggnoggplus.desktop ]]; then
            xdg-mime default "$PREVIOUS_HANDLER" x-scheme-handler/yule >/dev/null 2>&1 || true
        fi
        ((SDL_REAL_CREATED == 0)) || rm -f -- "$INSTALL_DIR/SDL2_real.dll"
    fi
    rm -rf -- "$TMP_ROOT"
    exit "$status"
}
trap rollback EXIT HUP INT TERM

fetch "$CHANNEL_URL" "$TMP_ROOT/latest.json"
mapfile -t manifest_lines < <(python3 - "$TMP_ROOT/latest.json" <<'PY'
import json, re, sys
from urllib.parse import urljoin, urlparse
with open(sys.argv[1], encoding='utf-8') as stream:
    data = json.load(stream)
if data.get('channel_version') != 1:
    raise SystemExit('unsupported release channel version')
version = data.get('version')
base = data.get('base')
if not isinstance(version, str) or not re.fullmatch(r'[0-9]+(?:\.[0-9]+)*', version):
    raise SystemExit('invalid release version')
parsed = urlparse(base if isinstance(base, str) else '')
if parsed.scheme not in ('https', 'http') or not parsed.netloc or parsed.username or parsed.password:
    raise SystemExit('invalid release base URL')
if parsed.scheme == 'http' and parsed.hostname not in ('127.0.0.1', 'localhost', '::1'):
    raise SystemExit('non-local release URLs must use HTTPS')
files = data.get('files')
if not isinstance(files, list) or not files or len(files) > 32:
    raise SystemExit('invalid release file list')
print('META\t' + version + '\t' + base)
seen = set()
for item in files:
    path = item.get('path')
    digest = item.get('sha256')
    size = item.get('size')
    if (not isinstance(path, str) or not re.fullmatch(r'[A-Za-z0-9._+-]+', path)
            or path in seen or not isinstance(digest, str)
            or not re.fullmatch(r'[0-9a-fA-F]{64}', digest)
            or not isinstance(size, int) or isinstance(size, bool)
            or size < 1 or size > 64 * 1024 * 1024
            or item.get('overwrite') is not True):
        raise SystemExit('unsafe release file entry')
    seen.add(path)
    print('FILE\t' + path + '\t' + digest.lower() + '\t' + str(size) + '\t' + urljoin(base, path))
PY
)
[[ ${#manifest_lines[@]} -ge 2 ]] || die 'release channel contains no files'
IFS=$'\t' read -r _ CHANNEL_VERSION CHANNEL_BASE <<<"${manifest_lines[0]}"
mkdir -p -- "$TMP_ROOT/payload"
MANAGED_TSV="$TMP_ROOT/managed.tsv"
: > "$MANAGED_TSV"
for line in "${manifest_lines[@]:1}"; do
    IFS=$'\t' read -r kind name expected size url <<<"$line"
    [[ $kind == FILE ]] || die 'invalid parsed manifest record'
    fetch "$url" "$TMP_ROOT/payload/$name"
    actual_size=$(wc -c < "$TMP_ROOT/payload/$name")
    actual_hash=$(sha256_file "$TMP_ROOT/payload/$name")
    [[ $actual_size == "$size" ]] || die "size mismatch for $name"
    [[ $actual_hash == "$expected" ]] || die "SHA-256 mismatch for $name"
done

UPDATER_SOURCE=''
for candidate in "$SCRIPT_DIR/$UPDATER_NAME" "$SCRIPT_DIR/../../../$UPDATER_NAME"; do
    if [[ -f $candidate ]]; then UPDATER_SOURCE=$candidate; break; fi
done
[[ -n $UPDATER_SOURCE ]] || die "$UPDATER_NAME is missing from the installer package"

ADOPTED_VANILLA=false
if [[ -f $RECEIPT ]] && [[ $(json_field "$RECEIPT" adopted_vanilla 2>/dev/null || true) == True ]]; then
    ADOPTED_VANILLA=true
fi
if [[ ! -f "$INSTALL_DIR/SDL2_real.dll" ]]; then
    [[ -f "$INSTALL_DIR/SDL2.dll" ]] || die 'vanilla SDL2.dll is missing'
    if grep -a -q 'modframework' "$INSTALL_DIR/SDL2.dll"; then
        die 'SDL2.dll is already the framework proxy but SDL2_real.dll is missing'
    fi
    cp -a -- "$INSTALL_DIR/SDL2.dll" "$INSTALL_DIR/SDL2_real.dll"
    ADOPTED_VANILLA=true
    SDL_REAL_CREATED=1
fi

install_managed() {
    local source=$1 name=$2 target="$INSTALL_DIR/$2" existed=0 temp_target
    if [[ -e $target ]]; then
        [[ -f $target && ! -L $target ]] || die "refusing non-regular managed target: $target"
        existed=1
        cp -a -- "$target" "$ROLLBACK_DIR/$name"
    fi
    printf '%s\t%s\n' "$name" "$existed" >> "$TMP_ROOT/changed.list"
    temp_target="$INSTALL_DIR/.yule-install-$name.tmp"
    install -m 0644 -- "$source" "$temp_target"
    mv -f -- "$temp_target" "$target"
    printf '%s\t%s\t%s\n' "$name" "$(sha256_file "$target")" "$(wc -c < "$target")" >> "$MANAGED_TSV"
}

for line in "${manifest_lines[@]:1}"; do
    IFS=$'\t' read -r _ name _ _ _ <<<"$line"
    install_managed "$TMP_ROOT/payload/$name" "$name"
done
install_managed "$UPDATER_SOURCE" "$UPDATER_NAME"

mkdir -p -- "$INSTALL_DIR/mods"
CFG="$INSTALL_DIR/mods/modframework.cfg"
if [[ -f $CFG ]]; then
    cp -a -- "$CFG" "$ROLLBACK_DIR/modframework.cfg"
    CFG_EXISTED=1
    grep -Ev '^[[:space:]]*show_log_console[[:space:]]*=' "$CFG" > "$TMP_ROOT/modframework.cfg" || true
else : > "$TMP_ROOT/modframework.cfg"; fi
printf 'show_log_console=0\n' >> "$TMP_ROOT/modframework.cfg"
install -m 0644 -- "$TMP_ROOT/modframework.cfg" "$CFG"
CFG_TOUCHED=1

if ((SKIP_PROTOCOL == 0)); then
    need xdg-mime
    mkdir -p -- "$XDG_BIN_ROOT" "$APPLICATIONS_DIR" "$ICONS_DIR"
    PREVIOUS_HANDLER=$(xdg-mime query default x-scheme-handler/yule 2>/dev/null || true)
    if [[ $PREVIOUS_HANDLER == yule-eggnoggplus.desktop && -f $RECEIPT ]]; then
        PREVIOUS_HANDLER=$(json_field "$RECEIPT" desktop.previous_handler 2>/dev/null || true)
    fi
    WINE_ABS=$(command -v "$WINE_COMMAND")
    [[ ! -f $WRAPPER_PATH ]] || cp -a -- "$WRAPPER_PATH" "$ROLLBACK_DIR/wrapper"
    WRAPPER_TOUCHED=1
    python3 - "$WRAPPER_PATH" "$WINE_ABS" "$WINE_PREFIX" "$INSTALL_DIR/$EXE_NAME" <<'PY'
import shlex, sys
path, wine, prefix, game = sys.argv[1:]
text = f'''#!/usr/bin/env bash
set -euo pipefail
export WINEPREFIX={shlex.quote(prefix)}
if (($# == 0)); then
    exec {shlex.quote(wine)} {shlex.quote(game)}
fi
uri=$1
case $uri in
    yule://*) ;;
    *) printf 'Refusing non-yule URI\\n' >&2; exit 2 ;;
esac
exec {shlex.quote(wine)} {shlex.quote(game)} "--yule-uri=$uri"
'''
with open(path, 'w', encoding='utf-8', newline='\n') as stream:
    stream.write(text)
PY
    chmod 0755 -- "$WRAPPER_PATH"

    if [[ -f "$SCRIPT_DIR/assets/steam_icon.png" ]]; then
        [[ ! -f $ICON_PATH ]] || cp -a -- "$ICON_PATH" "$ROLLBACK_DIR/icon"
        ICON_TOUCHED=1
        install -m 0644 -- "$SCRIPT_DIR/assets/steam_icon.png" "$ICON_PATH"
    elif [[ -f "$SCRIPT_DIR/../windows/assets/steam_icon.png" ]]; then
        [[ ! -f $ICON_PATH ]] || cp -a -- "$ICON_PATH" "$ROLLBACK_DIR/icon"
        ICON_TOUCHED=1
        install -m 0644 -- "$SCRIPT_DIR/../windows/assets/steam_icon.png" "$ICON_PATH"
    fi
    [[ ! -f $DESKTOP_PATH ]] || cp -a -- "$DESKTOP_PATH" "$ROLLBACK_DIR/desktop"
    DESKTOP_TOUCHED=1
    python3 - "$DESKTOP_PATH" "$WRAPPER_PATH" "$INSTALL_DIR" "$ICON_PATH" <<'PY'
import sys
path, wrapper, workdir, icon = sys.argv[1:]
def quote(value):
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"').replace('`', '\\`').replace('$', '\\$') + '"'
lines = [
    '[Desktop Entry]', 'Type=Application', 'Version=1.0', 'Name=EGGNOGG+',
    'Comment=Launch the Windows EGGNOGG+ build through Wine',
    f'Exec={quote(wrapper)} %u', f'Path={workdir}', 'Terminal=false',
    'Categories=Game;', 'MimeType=x-scheme-handler/yule;',
    'StartupWMClass=eggnoggplus.exe'
]
if icon and __import__('os').path.isfile(icon): lines.append(f'Icon={icon}')
with open(path, 'w', encoding='utf-8', newline='\n') as stream:
    stream.write('\n'.join(lines) + '\n')
PY
    chmod 0644 -- "$DESKTOP_PATH"
    update-desktop-database "$APPLICATIONS_DIR" >/dev/null 2>&1 || true
    xdg-mime default yule-eggnoggplus.desktop x-scheme-handler/yule
    [[ $(xdg-mime query default x-scheme-handler/yule 2>/dev/null || true) == yule-eggnoggplus.desktop ]] ||
        die 'desktop environment did not accept the yule:// handler'
    PROTOCOL_APPLIED=1
    WRAPPER_HASH=$(sha256_file "$WRAPPER_PATH")
    DESKTOP_HASH=$(sha256_file "$DESKTOP_PATH")
    [[ -f $ICON_PATH ]] && ICON_HASH=$(sha256_file "$ICON_PATH")
fi

mkdir -p -- "$STATE_DIR"
python3 - "$RECEIPT" "$INSTALL_DIR/install-linux.json" "$INSTALL_DIR" "$WINE_PREFIX" \
    "$WINE_COMMAND" "$CHANNEL_URL" "$CHANNEL_VERSION" "$ADOPTED_VANILLA" "$MANAGED_TSV" \
    "$DESKTOP_PATH" "$DESKTOP_HASH" "$WRAPPER_PATH" "$WRAPPER_HASH" "$ICON_PATH" "$ICON_HASH" \
    "$PREVIOUS_HANDLER" <<'PY'
import datetime, json, os, sys
(receipt, local_receipt, install_dir, wine_prefix, wine_command, channel_url,
 version, adopted, managed_tsv, desktop, desktop_hash, wrapper, wrapper_hash,
 icon, icon_hash, previous) = sys.argv[1:]
managed = []
with open(managed_tsv, encoding='utf-8') as stream:
    for line in stream:
        path, digest, size = line.rstrip('\n').split('\t')
        managed.append({'path': path, 'sha256': digest, 'size': int(size)})
data = {
    'manifest_version': 1, 'platform': 'linux-wine',
    'install_dir': install_dir,
    'game_executable': os.path.join(install_dir, 'eggnoggplus.exe'),
    'updater_executable': os.path.join(install_dir, 'YuleUpdater.exe'),
    'framework_version': version, 'channel_url': channel_url,
    'wine_prefix': wine_prefix, 'wine_command': wine_command,
    'installed_at': datetime.datetime.now(datetime.timezone.utc).isoformat(),
    'adopted_vanilla': adopted.lower() == 'true', 'managed_files': managed,
    'desktop': {'file': desktop, 'file_sha256': desktop_hash,
                'wrapper': wrapper, 'wrapper_sha256': wrapper_hash,
                'icon': icon, 'icon_sha256': icon_hash,
                'previous_handler': previous}
}
encoded = json.dumps(data, indent=2, sort_keys=True) + '\n'
for path in (receipt, local_receipt):
    temp = path + '.tmp'
    with open(temp, 'w', encoding='utf-8', newline='\n') as stream: stream.write(encoded)
    os.replace(temp, path)
PY

COMMITTED=1
trap - EXIT HUP INT TERM
rm -rf -- "$TMP_ROOT"
say "Installed Yule $CHANNEL_VERSION for Wine at: $INSTALL_DIR"
if ((SKIP_PROTOCOL == 0)); then
    say 'Linux desktop launcher and yule:// link handler installed.'
fi
say "Wine prefix: $WINE_PREFIX"
say "Run: $WRAPPER_PATH"
if [[ $SOURCE_DIR != "$INSTALL_DIR" ]]; then
    say "The source game folder was preserved: $SOURCE_DIR"
fi
