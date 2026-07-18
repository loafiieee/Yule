#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "content_tiles.h"

#define CONTENT_TILE_CELL_STRIDE 4u

typedef struct ContentMapCell {
    uint16_t definition_plus_one;
    uint8_t room_mirrored;
    uint8_t phase_seed;
} ContentMapCell;

typedef struct ContentMapDefinition {
    ContentTileDef definition;
    char native_glyph;
    int valid;
} ContentMapDefinition;

typedef struct ContentTileMapState {
    uint8_t* tilemap_base;
    int width;
    int height;
    size_t cell_count;
    ContentMapCell* cells;
    ContentMapDefinition* definitions;
    size_t definition_count;
    size_t definition_cap;
    uint64_t registry_generation;
} ContentTileMapState;

static SRWLOCK g_content_tiles_lock = SRWLOCK_INIT;
static ContentTileMapState g_content_tile_map;

static void set_err(char* err, size_t err_cap, const char* text) {
    if (!err || err_cap == 0) return;
    snprintf(err, err_cap, "%s", text ? text : "content tile error");
    err[err_cap - 1] = '\0';
}

static void clear_map_unlocked(void) {
    free(g_content_tile_map.cells);
    free(g_content_tile_map.definitions);
    memset(&g_content_tile_map, 0, sizeof(g_content_tile_map));
}

void content_tiles_init(void) {
    content_registry_init();
}

void content_tiles_shutdown(void) {
    AcquireSRWLockExclusive(&g_content_tiles_lock);
    clear_map_unlocked();
    ReleaseSRWLockExclusive(&g_content_tiles_lock);
}

int content_tiles_map_begin(void* tilemap_base,
                            int width,
                            int height,
                            char* err,
                            size_t err_cap) {
    size_t count;
    ContentMapCell* cells;
    if (!tilemap_base || width <= 0 || height <= 0) {
        set_err(err, err_cap, "tilemap base and positive dimensions are required");
        return 0;
    }
    if ((size_t)width > SIZE_MAX / (size_t)height) {
        set_err(err, err_cap, "tilemap dimensions overflow");
        return 0;
    }
    count = (size_t)width * (size_t)height;
    if (count > SIZE_MAX / sizeof(*cells) || count > UINT32_MAX) {
        set_err(err, err_cap, "tilemap is too large");
        return 0;
    }
    cells = (ContentMapCell*)calloc(count, sizeof(*cells));
    if (!cells) {
        set_err(err, err_cap, "out of memory allocating content tile metadata");
        return 0;
    }
    AcquireSRWLockExclusive(&g_content_tiles_lock);
    clear_map_unlocked();
    g_content_tile_map.tilemap_base = (uint8_t*)tilemap_base;
    g_content_tile_map.width = width;
    g_content_tile_map.height = height;
    g_content_tile_map.cell_count = count;
    g_content_tile_map.cells = cells;
    g_content_tile_map.registry_generation = content_registry_generation();
    ReleaseSRWLockExclusive(&g_content_tiles_lock);
    return 1;
}

void content_tiles_map_end(void) {
    AcquireSRWLockExclusive(&g_content_tiles_lock);
    clear_map_unlocked();
    ReleaseSRWLockExclusive(&g_content_tiles_lock);
}

static int find_definition_unlocked(const char* key) {
    size_t i;
    for (i = 0; i < g_content_tile_map.definition_count; i++) {
        if (strcmp(g_content_tile_map.definitions[i].definition.key, key) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int add_definition_unlocked(const ContentTileDef* definition,
                                   char* err,
                                   size_t err_cap) {
    ContentMapDefinition* bigger;
    size_t new_cap;
    if (!definition) return -1;
    if (g_content_tile_map.definition_count >= UINT16_MAX) {
        set_err(err, err_cap, "active map references too many unique tile definitions");
        return -1;
    }
    if (g_content_tile_map.definition_count == g_content_tile_map.definition_cap) {
        new_cap = g_content_tile_map.definition_cap
            ? g_content_tile_map.definition_cap * 2u : 8u;
        if (new_cap > (size_t)UINT16_MAX) new_cap = (size_t)UINT16_MAX;
        bigger = (ContentMapDefinition*)realloc(
            g_content_tile_map.definitions, new_cap * sizeof(*bigger));
        if (!bigger) {
            set_err(err, err_cap, "out of memory growing active tile definitions");
            return -1;
        }
        g_content_tile_map.definitions = bigger;
        g_content_tile_map.definition_cap = new_cap;
    }
    g_content_tile_map.definitions[g_content_tile_map.definition_count].definition = *definition;
    g_content_tile_map.definitions[g_content_tile_map.definition_count].native_glyph =
        definition->native_glyph;
    g_content_tile_map.definitions[g_content_tile_map.definition_count].valid = 1;
    return (int)g_content_tile_map.definition_count++;
}

static uint8_t cell_phase_seed(const char* key, int x, int y) {
    uint32_t hash = 2166136261u;
    const unsigned char* p = (const unsigned char*)key;
    while (p && *p) {
        hash ^= *p++;
        hash *= 16777619u;
    }
    hash ^= (uint32_t)x;
    hash *= 16777619u;
    hash ^= (uint32_t)y;
    hash *= 16777619u;
    return (uint8_t)(hash ^ (hash >> 8) ^ (hash >> 16) ^ (hash >> 24));
}

int content_tiles_map_set(int x,
                          int y,
                          const char* qualified_key,
                          int room_mirrored,
                          char* err,
                          size_t err_cap) {
    ContentTileDef definition;
    size_t cell_index;
    int definition_index;
    if (!qualified_key || !qualified_key[0]) {
        set_err(err, err_cap, "qualified tile key is required");
        return 0;
    }
    if (!content_registry_tile_find(qualified_key, &definition)) {
        set_err(err, err_cap, "referenced content tile is not registered");
        return 0;
    }
    AcquireSRWLockExclusive(&g_content_tiles_lock);
    if (!g_content_tile_map.cells || x < 0 || y < 0 ||
        x >= g_content_tile_map.width || y >= g_content_tile_map.height) {
        ReleaseSRWLockExclusive(&g_content_tiles_lock);
        set_err(err, err_cap, "content tile coordinates are outside the active map");
        return 0;
    }
    definition_index = find_definition_unlocked(definition.key);
    if (definition_index < 0) {
        definition_index = add_definition_unlocked(&definition, err, err_cap);
        if (definition_index < 0) {
            ReleaseSRWLockExclusive(&g_content_tiles_lock);
            return 0;
        }
    }
    cell_index = (size_t)y * (size_t)g_content_tile_map.width + (size_t)x;
    g_content_tile_map.cells[cell_index].definition_plus_one =
        (uint16_t)(definition_index + 1);
    g_content_tile_map.cells[cell_index].room_mirrored = room_mirrored ? 1u : 0u;
    g_content_tile_map.cells[cell_index].phase_seed =
        cell_phase_seed(definition.key, x, y);
    ReleaseSRWLockExclusive(&g_content_tiles_lock);
    return 1;
}

int content_tiles_map_clear(int x, int y, char* err, size_t err_cap) {
    size_t cell_index;
    AcquireSRWLockExclusive(&g_content_tiles_lock);
    if (!g_content_tile_map.cells || x < 0 || y < 0 ||
        x >= g_content_tile_map.width || y >= g_content_tile_map.height) {
        ReleaseSRWLockExclusive(&g_content_tiles_lock);
        set_err(err, err_cap, "content tile coordinates are outside the active map");
        return 0;
    }
    cell_index = (size_t)y * (size_t)g_content_tile_map.width + (size_t)x;
    memset(&g_content_tile_map.cells[cell_index], 0,
           sizeof(g_content_tile_map.cells[cell_index]));
    ReleaseSRWLockExclusive(&g_content_tiles_lock);
    return 1;
}

static size_t refresh_registry_unlocked(void) {
    size_t valid_count = 0;
    size_t i;
    int attempt;
    for (attempt = 0; attempt < 8; attempt++) {
        uint64_t before = content_registry_generation();
        valid_count = 0;
        for (i = 0; i < g_content_tile_map.definition_count; i++) {
            ContentTileDef refreshed;
            ContentMapDefinition* active = &g_content_tile_map.definitions[i];
            if (content_registry_tile_find(active->definition.key, &refreshed) &&
                refreshed.native_glyph == active->native_glyph) {
                active->definition = refreshed;
                active->valid = 1;
                valid_count++;
            } else {
                /* The native engine cell was generated with native_glyph and
                 * cannot safely change behavior in place. Missing/redefined
                 * content therefore falls back to that existing native cell. */
                active->valid = 0;
            }
        }
        if (before == content_registry_generation()) {
            g_content_tile_map.registry_generation = before;
            return valid_count;
        }
    }
    /* Persistent concurrent mutation is unlikely (reload runs on the main
     * thread), but never label a mixed snapshot current. */
    g_content_tile_map.registry_generation = 0;
    return valid_count;
}

size_t content_tiles_refresh_registry(void) {
    size_t count;
    AcquireSRWLockExclusive(&g_content_tiles_lock);
    count = refresh_registry_unlocked();
    ReleaseSRWLockExclusive(&g_content_tiles_lock);
    return count;
}

static int animation_frame(const ContentTileDef* def,
                           const ContentMapCell* cell,
                           uint64_t tick) {
    uint64_t step;
    uint64_t period;
    int count;
    if (!def || !cell || def->frame_count <= 1) return 0;
    count = def->frame_count;
    step = tick / (uint64_t)def->frame_ticks;
    if ((def->flags & CONTENT_TILE_RANDOM_PHASE) != 0) {
        step += (uint64_t)cell->phase_seed;
    }
    if (def->animation_mode == CONTENT_ANIMATION_ONCE) {
        return step >= (uint64_t)count ? count - 1 : (int)step;
    }
    if (def->animation_mode == CONTENT_ANIMATION_PING_PONG) {
        period = (uint64_t)(count * 2 - 2);
        step %= period;
        if (step >= (uint64_t)count) step = period - step;
        return (int)step;
    }
    return (int)(step % (uint64_t)count);
}

int content_tiles_render_for_action(const void* tile,
                                    int mode,
                                    int x,
                                    int y,
                                    uint64_t deterministic_tick,
                                    ContentTileRender* out) {
    uintptr_t base;
    uintptr_t address;
    uintptr_t byte_offset;
    size_t cell_index;
    ContentMapCell cell;
    ContentMapDefinition active;
    uint64_t observed_generation;
    int frame;
    int pointer_x;
    int pointer_y;
    if (!tile || mode != 2 || !out) return 0;

retry:
    AcquireSRWLockShared(&g_content_tiles_lock);
    if (!g_content_tile_map.cells || !g_content_tile_map.tilemap_base) {
        ReleaseSRWLockShared(&g_content_tiles_lock);
        return 0;
    }
    if (g_content_tile_map.registry_generation != content_registry_generation()) {
        ReleaseSRWLockShared(&g_content_tiles_lock);
        content_tiles_refresh_registry();
        goto retry;
    }
    base = (uintptr_t)g_content_tile_map.tilemap_base;
    address = (uintptr_t)tile;
    if (address < base) {
        ReleaseSRWLockShared(&g_content_tiles_lock);
        return 0;
    }
    byte_offset = address - base;
    if ((byte_offset % CONTENT_TILE_CELL_STRIDE) != 0) {
        ReleaseSRWLockShared(&g_content_tiles_lock);
        return 0;
    }
    cell_index = (size_t)(byte_offset / CONTENT_TILE_CELL_STRIDE);
    if (cell_index >= g_content_tile_map.cell_count) {
        ReleaseSRWLockShared(&g_content_tiles_lock);
        return 0;
    }
    pointer_x = (int)(cell_index % (size_t)g_content_tile_map.width);
    pointer_y = (int)(cell_index / (size_t)g_content_tile_map.width);
    if ((x >= 0 && x != pointer_x) || (y >= 0 && y != pointer_y)) {
        ReleaseSRWLockShared(&g_content_tiles_lock);
        return 0;
    }
    cell = g_content_tile_map.cells[cell_index];
    if (cell.definition_plus_one == 0 ||
        (size_t)(cell.definition_plus_one - 1u) >= g_content_tile_map.definition_count) {
        ReleaseSRWLockShared(&g_content_tiles_lock);
        return 0;
    }
    active = g_content_tile_map.definitions[cell.definition_plus_one - 1u];
    observed_generation = g_content_tile_map.registry_generation;
    ReleaseSRWLockShared(&g_content_tiles_lock);
    if (observed_generation != content_registry_generation()) goto retry;
    if (!active.valid) return 0;

    frame = animation_frame(&active.definition, &cell, deterministic_tick);
    memset(out, 0, sizeof(*out));
    snprintf(out->owner, sizeof(out->owner), "%s", active.definition.owner);
    snprintf(out->key, sizeof(out->key), "%s", active.definition.key);
    snprintf(out->sprite_sheet, sizeof(out->sprite_sheet), "%s",
             active.definition.sprite_sheet);
    out->sprite_index = active.definition.sprite_index + frame;
    out->layer = active.definition.layer;
    out->flip_x = cell.room_mirrored &&
        (active.definition.flags & CONTENT_TILE_MIRROR_WITH_ROOM) ? 1 : 0;
    out->offset_x = active.definition.offset_x;
    out->offset_y = active.definition.offset_y;
    out->scale_x = active.definition.scale_x;
    out->scale_y = active.definition.scale_y;
    out->angle_degrees = active.definition.angle_degrees;
    memcpy(out->tint, active.definition.tint, sizeof(out->tint));
    return 1;
}

int content_tiles_map_active(void) {
    int active;
    AcquireSRWLockShared(&g_content_tiles_lock);
    active = g_content_tile_map.cells != NULL;
    ReleaseSRWLockShared(&g_content_tiles_lock);
    return active;
}

size_t content_tiles_map_cell_count(void) {
    size_t count;
    AcquireSRWLockShared(&g_content_tiles_lock);
    count = g_content_tile_map.cell_count;
    ReleaseSRWLockShared(&g_content_tiles_lock);
    return count;
}

size_t content_tiles_map_definition_count(void) {
    size_t count;
    AcquireSRWLockShared(&g_content_tiles_lock);
    count = g_content_tile_map.definition_count;
    ReleaseSRWLockShared(&g_content_tiles_lock);
    return count;
}
