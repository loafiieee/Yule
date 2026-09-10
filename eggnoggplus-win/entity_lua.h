#pragma once
#include "entity_world.h"
#include <luajit-2.1/lua.h>
/* Host owns this context and world until the Lua state is closed. The host must
 * sandbox callbacks and wrap them in its script+world snapshot transaction.
 * This adapter exposes no native addresses or raw world advancement/restore. */
typedef uint32_t (*EntityResolveTypeFn)(void* user,const char* key,size_t length);
typedef struct EntityLuaBinding {
    EntityWorld* world;
    EntityResolveTypeFn resolve_type;
    void* user;
    void (*lifecycle)(lua_State*,int,EntityHandle,const EntityValue*); /* 1 spawn, 2 remove; may raise. */
    EntityHandle (*find_placement)(void*,const char*);
    const char* (*type_key)(void*,uint32_t);
    uint32_t* work_budget; /* Optional shared host instruction budget. */
} EntityLuaBinding;
/* Pushes an API table; host makes it read-only in its locked environment. */
void entity_lua_push_api(lua_State* L,EntityLuaBinding* binding);

/* Exact opaque handle representation shared by managed lifecycle callbacks. */
void entity_lua_push_handle(lua_State* L,EntityHandle handle);

void entity_lua_push_value(lua_State* L,const EntityValue* value);
