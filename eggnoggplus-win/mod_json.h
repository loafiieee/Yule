#ifndef EGGNOGGPLUS_MOD_JSON_H
#define EGGNOGGPLUS_MOD_JSON_H

#include <luajit-2.1/lua.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOD_JSON_MAX_INPUT_BYTES (1024u * 1024u)
#define MOD_JSON_MAX_OUTPUT_BYTES (1024u * 1024u)
#define MOD_JSON_MAX_STRING_BYTES (256u * 1024u)
#define MOD_JSON_MAX_DEPTH 32
#define MOD_JSON_MAX_NODES 65536

int mod_json_lua_encode(lua_State* L);
int mod_json_lua_decode(lua_State* L);
int mod_json_lua_array(lua_State* L);
int mod_json_lua_object(lua_State* L);
int mod_json_lua_is_null(lua_State* L);
void mod_json_lua_push_null(lua_State* L);

#ifdef __cplusplus
}
#endif

#endif
