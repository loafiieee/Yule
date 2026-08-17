#ifndef EGGNOGGPLUS_MOD_CALLBACKS_H
#define EGGNOGGPLUS_MOD_CALLBACKS_H

#include <stddef.h>

#include <luajit-2.1/lua.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ModCallbackEntry {
    unsigned int id;
    int ref;
} ModCallbackEntry;

typedef struct ModCallbackList {
    ModCallbackEntry* entries;
    int count;
    int cap;
} ModCallbackList;

int mod_callback_list_add(ModCallbackList* list, unsigned int id, int ref);
int mod_callback_list_remove(lua_State* L, ModCallbackList* list,
                             unsigned int id);
int mod_callback_list_ref(const ModCallbackList* list, unsigned int id);
int mod_callback_list_snapshot(const ModCallbackList* list,
                               unsigned int** out_ids, int* out_count);
void mod_callback_list_clear(lua_State* L, ModCallbackList* list);
size_t mod_callback_list_reserved_bytes(const ModCallbackList* list);

#ifdef __cplusplus
}
#endif

#endif
