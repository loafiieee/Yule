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
  dllmain.c stubs.c hooks.c custom_maps.c room_graph.c
  content_registry.c content_tiles.c content_bridge.c map_script.c map_ambiance.c
  entity_world.c entity_lua.c entity_package.c entity_package_json.c
  cursor_ext.c
  credential_ext.c online_control.c launch_request.c launch_ipc.c
  preview_bridge.c preview_http.c preview_package.c preview_stage.c
  lua_manager.c mod_api.c mod_callbacks.c mod_fs.c mod_http.c mod_json.c
  ggpo_ext.c ggpo_loopback.c ggpo_local.c ggpo_net.c ggpo_transport_native.c
  eos_runtime.c ggpo_transport_eos.c
  fp_control.c rollback_schema.c
  image_util.c text_util.c console_catalog.c console_parse.c command_history.c
  font_ext.c ui_text.c texture_ext.c log.c net_ext.c net_tls.c update_ext.c
  discord_rpc_ext.c bytebeat_ext.c bytebeat_chakra.c bytebeat_js.c
  bytebeat_stream.c
)

vendor_sources=(
  third_party/quickjs-ng/quickjs-amalgam.c
)

libraries=(
  -lkernel32 -luser32 -lgdi32 -ladvapi32 -lopengl32 -lluajit-5.1
  -lws2_32 -liphlpapi -lwinhttp -lbcrypt -lcomdlg32 -lshell32 -lole32 -lsecur32
  -I/mingw32/include
)

# EOS is optional during development. The runtime is loaded beside the game
# executable, so an absent SDK DLL leaves native UDP available in auto mode.
eos_flags=()
if [[ -n "${EOS_SDK_DIR:-}" ]]; then
  if [[ ! -f "$EOS_SDK_DIR/Include/eos_sdk.h" ]]; then
    printf 'error: EOS_SDK_DIR does not contain Include/eos_sdk.h\n' >&2
    exit 1
  fi
  if [[ ! -f "$EOS_SDK_DIR/Bin/EOSSDK-Win32-Shipping.dll" ]]; then
    printf 'error: EOS_SDK_DIR does not contain Bin/EOSSDK-Win32-Shipping.dll\n' >&2
    exit 1
  fi
  if [[ -n "${EOS_CLIENT_SECRET_FILE:-}" && ! -s "$EOS_CLIENT_SECRET_FILE" ]]; then
    printf 'error: EOS_CLIENT_SECRET_FILE is missing or empty\n' >&2
    exit 1
  fi
  eos_flags=(-DYULE_ENABLE_EOS "-I$EOS_SDK_DIR/Include")
fi

# Build the one-shot updater separately. The framework starts it only after a
# verified transaction is staged; it waits for Eggnogg to close before swapping
# mapped DLLs, relaunches the game, and exits.
gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic \
  -DUPDATE_EXT_HELPER -municode -mwindows -static -static-libgcc \
  -o build/YuleUpdater.exe updater_helper.c update_ext.c \
  -lwinhttp -lbcrypt -lws2_32 -lshell32 -luser32
for replaceable_dll in \
  SDL2.dll lua51.dll libgcc_s_dw2-1.dll libwinpthread-1.dll SDL2_mixer.dll \
  EOSSDK-Win32-Shipping.dll
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
  "${eos_flags[@]}" "${sources[@]}" "${vendor_sources[@]}" "${libraries[@]}"
if [[ -n "${EOS_SDK_DIR:-}" ]]; then
  cp -f "$EOS_SDK_DIR/Bin/EOSSDK-Win32-Shipping.dll" EOSSDK-Win32-Shipping.dll
  if [[ -n "${EOS_CLIENT_SECRET_FILE:-}" ]]; then
    cp -f "$EOS_CLIENT_SECRET_FILE" eos_client_secret.txt
  fi
fi
cp -f build/SDL2_test.dll SDL2.dll
cp -f third_party/tandy2k/LICENSE.txt Tandy2K-LICENSE.txt
cp -f third_party/tandy2k/README.md Tandy2K-ATTRIBUTION.txt
