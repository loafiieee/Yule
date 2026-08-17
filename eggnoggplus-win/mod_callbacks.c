#include "mod_callbacks.h"

#include <luajit-2.1/lauxlib.h>

#include <stdlib.h>
#include <string.h>

int mod_callback_list_add(ModCallbackList* list, unsigned int id, int ref) {
    ModCallbackEntry* resized;
    int capacity;
    if (!list || id == 0 || ref == LUA_NOREF || ref == LUA_REFNIL) return 0;
    if (list->count >= list->cap) {
        capacity = list->cap == 0 ? 8 : list->cap * 2;
        resized = (ModCallbackEntry*)realloc(
            list->entries, sizeof(*resized) * (size_t)capacity);
        if (!resized) return 0;
        list->entries = resized;
        list->cap = capacity;
    }
    list->entries[list->count].id = id;
    list->entries[list->count].ref = ref;
    list->count++;
    return 1;
}

int mod_callback_list_remove(lua_State* L, ModCallbackList* list,
                             unsigned int id) {
    int index;
    if (!list || id == 0) return 0;
    for (index = 0; index < list->count; index++) {
        if (list->entries[index].id != id) continue;
        if (L && list->entries[index].ref != LUA_NOREF &&
            list->entries[index].ref != LUA_REFNIL) {
            luaL_unref(L, LUA_REGISTRYINDEX, list->entries[index].ref);
        }
        if (index + 1 < list->count) {
            memmove(&list->entries[index], &list->entries[index + 1],
                    sizeof(*list->entries) *
                    (size_t)(list->count - index - 1));
        }
        list->count--;
        return 1;
    }
    return 0;
}

int mod_callback_list_ref(const ModCallbackList* list, unsigned int id) {
    int index;
    if (!list || id == 0) return LUA_NOREF;
    for (index = 0; index < list->count; index++) {
        if (list->entries[index].id == id) return list->entries[index].ref;
    }
    return LUA_NOREF;
}

int mod_callback_list_snapshot(const ModCallbackList* list,
                               unsigned int** out_ids, int* out_count) {
    unsigned int* ids;
    int index;
    if (out_ids) *out_ids = NULL;
    if (out_count) *out_count = 0;
    if (!list || list->count <= 0) return 1;
    if (!out_ids || !out_count) return 0;
    ids = (unsigned int*)malloc(sizeof(*ids) * (size_t)list->count);
    if (!ids) return 0;
    for (index = 0; index < list->count; index++) {
        ids[index] = list->entries[index].id;
    }
    *out_ids = ids;
    *out_count = list->count;
    return 1;
}

void mod_callback_list_clear(lua_State* L, ModCallbackList* list) {
    int index;
    if (!list) return;
    if (L) {
        for (index = 0; index < list->count; index++) {
            int ref = list->entries[index].ref;
            if (ref != LUA_NOREF && ref != LUA_REFNIL) {
                luaL_unref(L, LUA_REGISTRYINDEX, ref);
            }
        }
    }
    free(list->entries);
    memset(list, 0, sizeof(*list));
}

size_t mod_callback_list_reserved_bytes(const ModCallbackList* list) {
    return list && list->cap > 0
        ? sizeof(ModCallbackEntry) * (size_t)list->cap
        : 0;
}
