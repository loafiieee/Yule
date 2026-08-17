#!/usr/bin/env bash
set -euo pipefail

if ! command -v gcc >/dev/null 2>&1; then
  tool_dir=/c/msys64/mingw32/bin
  if [[ -x "$tool_dir/gcc.exe" ]]; then
    PATH="$tool_dir:$PATH"
  fi
fi
if ! command -v gcc >/dev/null 2>&1; then
  printf '%s\n' 'error: 32-bit MinGW GCC was not found (expected C:\msys64\mingw32\bin\gcc.exe)' >&2
  exit 1
fi

mkdir -p build

sources=(
  dllmain.c stubs.c hooks.c custom_maps.c
  content_registry.c content_tiles.c content_bridge.c map_script.c
  cursor_ext.c
  credential_ext.c online_control.c launch_request.c launch_ipc.c
  lua_manager.c mod_api.c mod_callbacks.c mod_fs.c mod_http.c mod_json.c
  ggpo_ext.c ggpo_loopback.c ggpo_local.c ggpo_net.c
  fp_control.c rollback_schema.c
  image_util.c text_util.c console_catalog.c console_parse.c command_history.c
  font_ext.c texture_ext.c log.c net_ext.c update_ext.c
  discord_rpc_ext.c bytebeat_ext.c bytebeat_chakra.c bytebeat_js.c
  bytebeat_stream.c
)

vendor_sources=(
  third_party/quickjs-ng/quickjs-amalgam.c
)

libraries=(
  -lkernel32 -luser32 -ladvapi32 -lopengl32 -lluajit-5.1
  -lws2_32 -liphlpapi -lwinhttp -lbcrypt -lcomdlg32 -lshell32 -lole32
  -I/mingw32/include
)

# Build the one-shot updater separately. The framework starts it only after a
# verified transaction is staged; it waits for Eggnogg to close before swapping
# mapped DLLs, relaunches the game, and exits.
gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic \
  -DUPDATE_EXT_HELPER -municode -mwindows -static -static-libgcc \
  -o build/YuleUpdater.exe updater_helper.c update_ext.c \
  -lwinhttp -lbcrypt -lws2_32 -lshell32 -luser32
for replaceable_dll in \
  SDL2.dll lua51.dll libgcc_s_dw2-1.dll libwinpthread-1.dll SDL2_mixer.dll
do
  if objdump -p build/YuleUpdater.exe |
      grep -Fqi "DLL Name: ${replaceable_dll}"; then
    printf 'error: YuleUpdater.exe imports replaceable payload %s\n' \
      "$replaceable_dll" >&2
    exit 1
  fi
done
cp -f build/YuleUpdater.exe YuleUpdater.exe

# Prove the complete source/library set links before touching the installed
# proxy, then install that exact verified artifact. Copying (instead of linking
# a second time) keeps the installed DLL byte-identical to the build output and
# still fails cleanly if a running game has SDL2.dll mapped.
gcc -m32 -shared -O2 -o build/SDL2_test.dll \
  "${sources[@]}" "${vendor_sources[@]}" "${libraries[@]}"
cp -f build/SDL2_test.dll SDL2.dll
