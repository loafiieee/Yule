#include "../mod_callbacks.h"

#include <luajit-2.1/lauxlib.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static int make_callback_ref(lua_State* L, int value) {
    char source[64];
    snprintf(source, sizeof(source), "return function() return %d end", value);
    assert(luaL_loadstring(L, source) == 0);
    assert(lua_pcall(L, 0, 1, 0) == 0);
    return luaL_ref(L, LUA_REGISTRYINDEX);
}

static int call_ref(lua_State* L, int ref) {
    int result;
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    assert(lua_isfunction(L, -1));
    assert(lua_pcall(L, 0, 1, 0) == 0);
    result = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    return result;
}

int main(void) {
    lua_State* L = luaL_newstate();
    ModCallbackList list = {0};
    unsigned int* snapshot = NULL;
    int snapshot_count = 0;
    int ref10;
    int ref20;
    int ref30;

    assert(L != NULL);
    ref10 = make_callback_ref(L, 10);
    ref20 = make_callback_ref(L, 20);
    ref30 = make_callback_ref(L, 30);

    assert(mod_callback_list_add(&list, 10, ref10));
    assert(mod_callback_list_add(&list, 20, ref20));
    assert(mod_callback_list_add(&list, 30, ref30));
    assert(list.count == 3);
    assert(call_ref(L, mod_callback_list_ref(&list, 20)) == 20);

    assert(mod_callback_list_snapshot(&list, &snapshot, &snapshot_count));
    assert(snapshot_count == 3);
    assert(snapshot[0] == 10 && snapshot[1] == 20 && snapshot[2] == 30);

    /* A callback may remove itself or a later callback while dispatch is
     * iterating this stable ID snapshot. Resolution observes the removal. */
    assert(mod_callback_list_remove(L, &list, 20));
    assert(!mod_callback_list_remove(L, &list, 20));
    assert(mod_callback_list_ref(&list, snapshot[1]) == LUA_NOREF);
    assert(call_ref(L, mod_callback_list_ref(&list, snapshot[2])) == 30);
    assert(list.count == 2);
    free(snapshot);

    for (unsigned int id = 100; id < 200; id++) {
        int ref = make_callback_ref(L, (int)id);
        assert(mod_callback_list_add(&list, id, ref));
    }
    assert(list.count == 102);
    assert(mod_callback_list_reserved_bytes(&list) >=
           sizeof(ModCallbackEntry) * 102u);
    for (unsigned int id = 100; id < 200; id += 2) {
        assert(mod_callback_list_remove(L, &list, id));
    }
    assert(list.count == 52);

    mod_callback_list_clear(L, &list);
    assert(list.entries == NULL);
    assert(list.count == 0);
    assert(list.cap == 0);
    assert(mod_callback_list_reserved_bytes(&list) == 0);
    lua_close(L);
    puts("removable mod callback list tests: OK");
    return 0;
}
