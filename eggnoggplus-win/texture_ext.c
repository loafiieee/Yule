#include "texture_ext.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>

#include "font_ext.h"
#include "log.h"

// These addresses are for the bundled eggnoggplus.exe build.
// They are absolute VAs (base + 0x401000 offset already applied).
#define ADDR_STBI_LOAD       0x4140A0u
#define ADDR_STBI_IMAGE_FREE 0x411C80u

#define MAX_TEXTURE_REPLACEMENTS 16
#define MAX_LOADED_PATHS        32
#define TEX_PATH_MAX            128
#define HOT_RELOAD_POLL_MS      1000

typedef unsigned char* (__cdecl *fn_stbi_load_t)(const char* filename, int* x, int* y, int* comp, int req_comp);
typedef void (__cdecl *fn_stbi_image_free_t)(void* p);

static fn_stbi_load_t       p_stbi_load       = (fn_stbi_load_t)(uintptr_t)ADDR_STBI_LOAD;
static fn_stbi_image_free_t p_stbi_image_free = (fn_stbi_image_free_t)(uintptr_t)ADDR_STBI_IMAGE_FREE;

typedef struct TextureReplacement {
    int used;
    char owner[64];
    char target[TEX_PATH_MAX];
    char relpath[128];
    char fullpath[MAX_PATH];
    char fullpath_norm[MAX_PATH];
    uint64_t src_write_time;
    uint64_t src_size;
    int w;
    int h;
    uint8_t* rgba;
} TextureReplacement;

static TextureReplacement g_replacements[MAX_TEXTURE_REPLACEMENTS];
static char g_loaded_paths[MAX_LOADED_PATHS][TEX_PATH_MAX];
static int g_loaded_count = 0;
static ULONGLONG g_next_poll_ms = 0;

static void safe_snprintf(char* out, int out_sz, const char* fmt, ...) {
    va_list va;
    if (!out || out_sz <= 0) return;
    va_start(va, fmt);
    _vsnprintf(out, out_sz - 1, fmt, va);
    va_end(va);
    out[out_sz - 1] = '\0';
}

static void normalize_slashes_lower(const char* in, char* out, int out_sz) {
    int oi = 0;
    if (!out || out_sz <= 0) return;
    out[0] = '\0';
    if (!in) return;

    while (*in && oi < out_sz - 1) {
        char c = *in++;
        if (c == '\\') c = '/';
        out[oi++] = (char)tolower((unsigned char)c);
    }
    out[oi] = '\0';
}

static int query_file_signature(const char* full_path, uint64_t* out_write_time, uint64_t* out_size) {
    WIN32_FILE_ATTRIBUTE_DATA fad;
    uint64_t write_time;
    uint64_t size;
    if (!full_path || !full_path[0]) return 0;
    if (!GetFileAttributesExA(full_path, GetFileExInfoStandard, &fad)) return 0;
    if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return 0;
    write_time = ((uint64_t)fad.ftLastWriteTime.dwHighDateTime << 32) | fad.ftLastWriteTime.dwLowDateTime;
    size = ((uint64_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
    if (out_write_time) *out_write_time = write_time;
    if (out_size) *out_size = size;
    return 1;
}

static int str_ends_with(const char* s, const char* suffix) {
    size_t ls;
    size_t lf;
    if (!s || !suffix) return 0;
    ls = strlen(s);
    lf = strlen(suffix);
    if (lf > ls) return 0;
    return memcmp(s + (ls - lf), suffix, lf) == 0;
}

static int canonicalize_target_path(const char* input, char* out, int out_sz) {
    char norm[512];
    const char* p;

    if (!input || !input[0]) return 0;
    if (!out || out_sz <= 0) return 0;

    normalize_slashes_lower(input, norm, (int)sizeof(norm));

    // Allow shorthand like "sprites.png" and map it to data/sprites.png.
    if (!strchr(norm, '/')) {
        if (!str_ends_with(norm, ".png")) return 0;
        safe_snprintf(out, out_sz, "data/%s", norm);
        return 1;
    }

    // Prefer matching from the last "data/" segment if input is absolute.
    p = NULL;
    {
        const char* it = norm;
        while ((it = strstr(it, "data/")) != NULL) {
            p = it;
            it += 5;
        }
    }
    if (!p) {
        p = norm;
    }

    if (strncmp(p, "data/", 5) != 0) return 0;
    if (!str_ends_with(p, ".png")) return 0;
    if ((int)strlen(p) >= out_sz) return 0;

    strcpy(out, p);
    return 1;
}

static int load_png_rgba(const char* full_path, uint8_t** out_pixels, int* out_w, int* out_h, char* err, int err_sz) {
    int w = 0;
    int h = 0;
    int comp = 0;
    unsigned char* pix;
    size_t bytes;
    uint8_t* copy;

    if (!out_pixels || !out_w || !out_h) {
        safe_snprintf(err, err_sz, "internal: null output pointer");
        return 0;
    }
    *out_pixels = NULL;
    *out_w = 0;
    *out_h = 0;

    if (!p_stbi_load || !p_stbi_image_free) {
        safe_snprintf(err, err_sz, "stbi not available (addr mismatch?)");
        return 0;
    }
    if (!full_path || !full_path[0]) {
        safe_snprintf(err, err_sz, "missing png path");
        return 0;
    }

    pix = p_stbi_load(full_path, &w, &h, &comp, 4);
    if (!pix) {
        safe_snprintf(err, err_sz, "failed to load %s", full_path);
        return 0;
    }
    if (w <= 0 || h <= 0) {
        p_stbi_image_free(pix);
        safe_snprintf(err, err_sz, "invalid image size %dx%d", w, h);
        return 0;
    }

    bytes = (size_t)w * (size_t)h * 4u;
    copy = (uint8_t*)malloc(bytes);
    if (!copy) {
        p_stbi_image_free(pix);
        safe_snprintf(err, err_sz, "out of memory for %dx%d image", w, h);
        return 0;
    }

    memcpy(copy, pix, bytes);
    p_stbi_image_free(pix);

    *out_pixels = copy;
    *out_w = w;
    *out_h = h;
    return 1;
}

static int find_replacement_index(const char* target) {
    int i;
    if (!target || !target[0]) return -1;
    for (i = 0; i < MAX_TEXTURE_REPLACEMENTS; i++) {
        TextureReplacement* r = &g_replacements[i];
        if (!r->used) continue;
        if (_stricmp(r->target, target) == 0) return i;
    }
    return -1;
}

static int find_free_replacement_index(void) {
    int i;
    for (i = 0; i < MAX_TEXTURE_REPLACEMENTS; i++) {
        if (!g_replacements[i].used) return i;
    }
    return -1;
}

static int loaded_path_index(const char* canonical_target) {
    int i;
    if (!canonical_target || !canonical_target[0]) return -1;
    for (i = 0; i < g_loaded_count; i++) {
        if (_stricmp(g_loaded_paths[i], canonical_target) == 0) return i;
    }
    return -1;
}

static void mark_loaded_path(const char* canonical_target) {
    if (!canonical_target || !canonical_target[0]) return;
    if (loaded_path_index(canonical_target) >= 0) return;
    if (g_loaded_count >= MAX_LOADED_PATHS) return;

    strncpy(g_loaded_paths[g_loaded_count], canonical_target, TEX_PATH_MAX - 1);
    g_loaded_paths[g_loaded_count][TEX_PATH_MAX - 1] = '\0';
    g_loaded_count++;
}

static int replacement_needs_reload(const TextureReplacement* r, int force_reload, uint64_t* out_write_time, uint64_t* out_size) {
    uint64_t write_time = 0;
    uint64_t size = 0;
    if (!r || !r->used || !r->fullpath[0]) return 0;
    if (!query_file_signature(r->fullpath, &write_time, &size)) {
        if (!force_reload) return 0;
        if (out_write_time) *out_write_time = 0;
        if (out_size) *out_size = 0;
        return 1;
    }
    if (out_write_time) *out_write_time = write_time;
    if (out_size) *out_size = size;
    if (force_reload) return 1;
    if (write_time != r->src_write_time) return 1;
    if (size != r->src_size) return 1;
    return 0;
}

static int replacement_reload_from_disk(TextureReplacement* r, uint64_t write_time, uint64_t size, char* err, int err_sz) {
    uint8_t* rgba = NULL;
    int w = 0;
    int h = 0;
    if (!r || !r->used) {
        safe_snprintf(err, err_sz, "invalid replacement slot");
        return 0;
    }
    if (!load_png_rgba(r->fullpath, &rgba, &w, &h, err, err_sz)) return 0;
    if (w != r->w || h != r->h) {
        free(rgba);
        safe_snprintf(err, err_sz,
                      "replacement dimensions changed for %s (%dx%d -> %dx%d)",
                      r->target, r->w, r->h, w, h);
        return 0;
    }

    if (r->rgba) free(r->rgba);
    r->rgba = rgba;
    r->src_write_time = write_time;
    r->src_size = size;
    return 1;
}

static void texture_ext_reload_impl(int force_reload, int emit_info_log,
                                    int* out_reloaded, int* out_failed, int* out_restart_required) {
    int reloaded = 0;
    int failed = 0;
    int restart_required = 0;
    for (int i = 0; i < MAX_TEXTURE_REPLACEMENTS; i++) {
        TextureReplacement* r = &g_replacements[i];
        uint64_t write_time = 0;
        uint64_t size = 0;
        char err[256] = {0};
        if (!r->used) continue;

        if (!replacement_needs_reload(r, force_reload, &write_time, &size)) continue;
        if (!replacement_reload_from_disk(r, write_time, size, err, (int)sizeof(err))) {
            LOG_WARN("texture_ext: failed to reload %s (%s): %s",
                     r->target, r->fullpath, err[0] ? err : "unknown error");
            failed++;
            continue;
        }

        reloaded++;
        if (emit_info_log) {
            LOG_INFO("texture_ext: reloaded %s from %s", r->target, r->fullpath);
        }
    }

    if (out_reloaded) *out_reloaded = reloaded;
    if (out_failed) *out_failed = failed;
    if (out_restart_required) *out_restart_required = restart_required;
}

void texture_ext_init(void) {
    g_next_poll_ms = GetTickCount64() + HOT_RELOAD_POLL_MS;
}

void texture_ext_shutdown(void) {
    int i;
    for (i = 0; i < MAX_TEXTURE_REPLACEMENTS; i++) {
        if (g_replacements[i].rgba) free(g_replacements[i].rgba);
    }
    memset(g_replacements, 0, sizeof(g_replacements));
    memset(g_loaded_paths, 0, sizeof(g_loaded_paths));
    g_loaded_count = 0;
    g_next_poll_ms = 0;
}

int texture_ext_path_loaded(const char* target_path) {
    char canonical[TEX_PATH_MAX];
    if (!canonicalize_target_path(target_path, canonical, (int)sizeof(canonical))) return 0;
    return loaded_path_index(canonical) >= 0;
}

int texture_ext_is_tracked_path(const char* full_path) {
    char norm[MAX_PATH];
    if (!full_path || !full_path[0]) return 0;
    normalize_slashes_lower(full_path, norm, (int)sizeof(norm));
    for (int i = 0; i < MAX_TEXTURE_REPLACEMENTS; i++) {
        TextureReplacement* r = &g_replacements[i];
        if (!r->used || !r->fullpath_norm[0]) continue;
        if (_stricmp(r->fullpath_norm, norm) == 0) return 1;
    }
    return 0;
}

int texture_ext_poll_hot_reload(void) {
    int reloaded = 0;
    ULONGLONG now = GetTickCount64();
    if (now < g_next_poll_ms) return 0;
    g_next_poll_ms = now + HOT_RELOAD_POLL_MS;
    texture_ext_reload_impl(0, 1, &reloaded, NULL, NULL);
    return reloaded;
}

void texture_ext_reload_all(int* out_reloaded, int* out_failed, int* out_restart_required) {
    texture_ext_reload_impl(1, 1, out_reloaded, out_failed, out_restart_required);
}

int texture_ext_register_png(
    const char* owner_mod_id,
    const char* owner_mod_folder,
    const char* target_path,
    const char* rel_path,
    int override_other,
    char* err,
    int err_sz
) {
    char canonical[TEX_PATH_MAX];
    char full[MAX_PATH];
    uint8_t* rgba = NULL;
    int w = 0;
    int h = 0;
    int slot = -1;

    if (!owner_mod_id || !owner_mod_id[0]) {
        safe_snprintf(err, err_sz, "missing mod id");
        return 0;
    }
    if (!owner_mod_folder || !owner_mod_folder[0]) {
        safe_snprintf(err, err_sz, "missing mod folder");
        return 0;
    }
    if (!target_path || !target_path[0]) {
        safe_snprintf(err, err_sz, "missing target path");
        return 0;
    }
    if (!rel_path || !rel_path[0]) {
        safe_snprintf(err, err_sz, "missing texture path");
        return 0;
    }

    if (!canonicalize_target_path(target_path, canonical, (int)sizeof(canonical))) {
        safe_snprintf(err, err_sz, "invalid target path '%s'", target_path);
        return 0;
    }

    slot = find_replacement_index(canonical);
    if (slot >= 0) {
        TextureReplacement* existing = &g_replacements[slot];
        if (_stricmp(existing->owner, owner_mod_id) != 0 && !override_other) {
            safe_snprintf(err, err_sz, "%s is owned by %s", existing->target, existing->owner);
            return 0;
        }
    } else {
        slot = find_free_replacement_index();
        if (slot < 0) {
            safe_snprintf(err, err_sz, "too many texture replacements (max=%d)", MAX_TEXTURE_REPLACEMENTS);
            return 0;
        }
    }

    safe_snprintf(full, (int)sizeof(full), "%s\\%s", owner_mod_folder, rel_path);
    if (!load_png_rgba(full, &rgba, &w, &h, err, err_sz)) return 0;

    uint64_t write_time = 0;
    uint64_t size = 0;
    if (!query_file_signature(full, &write_time, &size)) {
        free(rgba);
        safe_snprintf(err, err_sz, "failed to stat %s", full);
        return 0;
    }

    {
        TextureReplacement* r = &g_replacements[slot];
        if (r->rgba) free(r->rgba);
        memset(r, 0, sizeof(*r));
        r->used = 1;
        strncpy(r->owner, owner_mod_id, sizeof(r->owner) - 1);
        strncpy(r->target, canonical, sizeof(r->target) - 1);
        strncpy(r->relpath, rel_path, sizeof(r->relpath) - 1);
        strncpy(r->fullpath, full, sizeof(r->fullpath) - 1);
        normalize_slashes_lower(full, r->fullpath_norm, (int)sizeof(r->fullpath_norm));
        r->src_write_time = write_time;
        r->src_size = size;
        r->w = w;
        r->h = h;
        r->rgba = rgba;
    }

    return 1;
}

int texture_ext_sprites_loaded(void) {
    return texture_ext_path_loaded("data/sprites.png");
}

int texture_ext_register_spritesheet(
    const char* owner_mod_id,
    const char* owner_mod_folder,
    const char* rel_path,
    int override_other,
    char* err,
    int err_sz
) {
    return texture_ext_register_png(
        owner_mod_id,
        owner_mod_folder,
        "data/sprites.png",
        rel_path,
        override_other,
        err,
        err_sz
    );
}

void texture_ext_on_rgba_load(const char* path, RgbaImage* img) {
    char canonical[TEX_PATH_MAX];
    int idx;
    size_t bytes;

    if (!path || !img) return;
    if (!img->pixels || img->w <= 0 || img->h <= 0) return;

    if (!canonicalize_target_path(path, canonical, (int)sizeof(canonical))) {
        return;
    }

    mark_loaded_path(canonical);

    idx = find_replacement_index(canonical);
    if (idx < 0) return;

    {
        TextureReplacement* r = &g_replacements[idx];
        if (img->w != r->w || img->h != r->h) {
            LOG_WARN("texture_ext: %s replacement for %s is %dx%d, game loaded %dx%d; skipping",
                     r->owner, r->target, r->w, r->h, img->w, img->h);
            return;
        }

        bytes = (size_t)img->w * (size_t)img->h * 4u;
        memcpy(img->pixels, r->rgba, bytes);
    }
}
