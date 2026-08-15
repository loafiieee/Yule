#ifndef EGGNOGGPLUS_MOD_HTTP_H
#define EGGNOGGPLUS_MOD_HTTP_H

#include <luajit-2.1/lua.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * owner is an opaque, stable identity used to prevent one mod from polling or
 * canceling another mod's request. owner_enabled must remain valid while the
 * Lua closures can be called.
 */
void mod_http_lua_push_api(lua_State* L, void* owner,
                           const int* owner_enabled);

/* Mark all work belonging to owner as canceled. Workers publish completion
 * before their slots are released or reused. */
void mod_http_cancel_owner(void* owner);

/* Release canceled slots whose workers have finished. Safe to call per frame. */
void mod_http_pump(void);

#ifdef __cplusplus
}
#endif

#endif
