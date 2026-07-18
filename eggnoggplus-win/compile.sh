#!/usr/bin/env bash
set -euo pipefail

mkdir -p build

sources=(
  dllmain.c stubs.c hooks.c custom_maps.c
  content_registry.c content_tiles.c content_bridge.c
  cursor_ext.c
  credential_ext.c online_control.c
  lua_manager.c ggpo_ext.c ggpo_loopback.c ggpo_local.c ggpo_net.c
  font_ext.c texture_ext.c log.c net_ext.c update_ext.c
)

libraries=(
  -lkernel32 -luser32 -ladvapi32 -lopengl32 -lluajit-5.1
  -lws2_32 -lwinhttp -lbcrypt -lcomdlg32 -lshell32 -lole32
  -I/mingw32/include
)

# Prove the complete source/library set links before touching the installed
# proxy, then install that exact verified artifact. Copying (instead of linking
# a second time) keeps the installed DLL byte-identical to the build output and
# still fails cleanly if a running game has SDL2.dll mapped.
gcc -m32 -shared -o build/SDL2_test.dll "${sources[@]}" "${libraries[@]}"
cp -f build/SDL2_test.dll SDL2.dll
