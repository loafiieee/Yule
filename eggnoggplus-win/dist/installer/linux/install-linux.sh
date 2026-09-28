#!/usr/bin/env bash
# The Linux installer uses a checked Python implementation on Linux hosts.
set -Eeuo pipefail
script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
if ! command -v python3 >/dev/null 2>&1; then
    printf 'ERROR: Python 3 is required to install Yule on Linux.\n' >&2
    exit 1
fi
exec python3 "$script_dir/install-linux.py" "$@"
