#ifndef EGGNOGGPLUS_MOD_FS_H
#define EGGNOGGPLUS_MOD_FS_H

#include <luajit-2.1/lua.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pushes the public mod.fs table.
 *
 * owner_enabled must remain valid for as long as the returned Lua closures can
 * be called. lua_manager stores mods in fixed slots, so the field address is
 * stable through enable, disable, and unload transitions.
 */
void mod_fs_lua_push_api(lua_State* L, const int* owner_enabled);

#ifdef __cplusplus
}
#endif

#endif
