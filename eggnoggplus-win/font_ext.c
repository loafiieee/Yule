#include "font_ext.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>

#include "log.h"

// These addresses are for the bundled eggnoggplus.exe build.
// They are absolute VAs (base + 0x401000 offset already applied).
#define ADDR_STBI_LOAD       0x4140A0u
#define ADDR_STBI_IMAGE_FREE 0x411C80u
#define HOT_RELOAD_POLL_MS   1000

typedef unsigned char* (__cdecl *fn_stbi_load_t)(const char* filename, int* x, int* y, int* comp, int req_comp);
typedef void (__cdecl *fn_stbi_image_free_t)(void* p);

static fn_stbi_load_t       p_stbi_load       = (fn_stbi_load_t)(uintptr_t)ADDR_STBI_LOAD;
static fn_stbi_image_free_t p_stbi_image_free = (fn_stbi_image_free_t)(uintptr_t)ADDR_STBI_IMAGE_FREE;

typedef struct GlyphSlot {
    int used;
    char owner[64];
    char relpath[128];
    char fullpath[MAX_PATH];
    char fullpath_norm[MAX_PATH];
    uint64_t src_write_time;
    uint64_t src_size;
    uint8_t rgba[8 * 8 * 4];
} GlyphSlot;

typedef struct AllocKey {
    char owner[64];
    char relpath[128];
    uint8_t byte_value;
} AllocKey;

static GlyphSlot g_slots[256];
static AllocKey* g_allocs = NULL;
static int g_alloc_count = 0;
static int g_alloc_cap = 0;
static int g_font_loaded = 0;
static ULONGLONG g_next_poll_ms = 0;

static int str_ends_with_icase(const char* s, const char* suffix) {
    if (!s || !suffix) return 0;
    size_t ls = strlen(s);
    size_t lf = strlen(suffix);
    if (lf > ls) return 0;
    const char* p = s + (ls - lf);
    return _stricmp(p, suffix) == 0;
}

static int is_font8x8_path(const char* path) {
    // The game passes relative paths like "data/font8x8.png".
    // Also accept backslashes.
    if (!path) return 0;
    if (str_ends_with_icase(path, "data/font8x8.png")) return 1;
    if (str_ends_with_icase(path, "data\\font8x8.png")) return 1;
    return 0;
}

static void safe_snprintf(char* out, int out_sz, const char* fmt, ...) {
    if (!out || out_sz <= 0) return;
    va_list va;
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

static int load_icon_8x8_rgba(const char* full_path, uint8_t out_rgba[8 * 8 * 4], char* err, int err_sz) {
    if (!p_stbi_load || !p_stbi_image_free) {
        safe_snprintf(err, err_sz, "stbi not available (addr mismatch?)");
        return 0;
    }
    if (!full_path || !full_path[0]) {
        safe_snprintf(err, err_sz, "no path");
        return 0;
    }

    int w = 0, h = 0, comp = 0;
    unsigned char* pix = p_stbi_load(full_path, &w, &h, &comp, 4);
    if (!pix) {
        safe_snprintf(err, err_sz, "failed to load %s", full_path);
        return 0;
    }
    if (w != 8 || h != 8) {
        safe_snprintf(err, err_sz, "icon must be 8x8 (got %dx%d)", w, h);
        p_stbi_image_free(pix);
        return 0;
    }

    memcpy(out_rgba, pix, 8 * 8 * 4);
    p_stbi_image_free(pix);
    return 1;
}

static int find_alloc_cached(const char* owner, const char* relpath, uint8_t* out_byte) {
    if (!owner || !relpath) return 0;
    for (int i = 0; i < g_alloc_count; i++) {
        if (_stricmp(g_allocs[i].owner, owner) == 0 && _stricmp(g_allocs[i].relpath, relpath) == 0) {
            if (out_byte) *out_byte = g_allocs[i].byte_value;
            return 1;
        }
    }
    return 0;
}

static void add_alloc_cache(const char* owner, const char* relpath, uint8_t byte_value) {
    if (!owner || !relpath) return;
    if (g_alloc_count + 1 > g_alloc_cap) {
        int nc = (g_alloc_cap == 0) ? 16 : (g_alloc_cap * 2);
        AllocKey* na = (AllocKey*)realloc(g_allocs, sizeof(AllocKey) * nc);
        if (!na) return;
        g_allocs = na;
        g_alloc_cap = nc;
    }
    AllocKey* a = &g_allocs[g_alloc_count++];
    memset(a, 0, sizeof(*a));
    strncpy(a->owner, owner, sizeof(a->owner) - 1);
    strncpy(a->relpath, relpath, sizeof(a->relpath) - 1);
    a->byte_value = byte_value;
}

void font_ext_init(void) {
    g_next_poll_ms = GetTickCount64() + HOT_RELOAD_POLL_MS;
}

void font_ext_shutdown(void) {
    if (g_allocs) free(g_allocs);
    g_allocs = NULL;
    g_alloc_count = 0;
    g_alloc_cap = 0;
    memset(g_slots, 0, sizeof(g_slots));
    g_font_loaded = 0;
    g_next_poll_ms = 0;
}

int font_ext_font_loaded(void) {
    return g_font_loaded;
}

static int set_slot(
    const char* owner_mod_id,
    const char* owner_mod_folder,
    uint8_t byte_value,
    const char* rel_path,
    int override_other,
    char* err,
    int err_sz
) {
    if (!owner_mod_id || !owner_mod_id[0]) {
        safe_snprintf(err, err_sz, "missing mod id");
        return 0;
    }
    if (!owner_mod_folder || !owner_mod_folder[0]) {
        safe_snprintf(err, err_sz, "missing mod folder");
        return 0;
    }
    if (!rel_path || !rel_path[0]) {
        safe_snprintf(err, err_sz, "missing icon path");
        return 0;
    }

    GlyphSlot* s = &g_slots[byte_value];
    if (s->used && _stricmp(s->owner, owner_mod_id) != 0 && !override_other) {
        safe_snprintf(err, err_sz, "glyph 0x%02X is owned by %s", (unsigned)byte_value, s->owner);
        return 0;
    }

    // Load icon RGBA.
    char full[MAX_PATH];
    safe_snprintf(full, (int)sizeof(full), "%s\\%s", owner_mod_folder, rel_path);
    uint8_t tmp[8 * 8 * 4];
    if (!load_icon_8x8_rgba(full, tmp, err, err_sz)) return 0;
    uint64_t write_time = 0;
    uint64_t size = 0;
    if (!query_file_signature(full, &write_time, &size)) {
        safe_snprintf(err, err_sz, "failed to stat %s", full);
        return 0;
    }

    memset(s, 0, sizeof(*s));
    s->used = 1;
    strncpy(s->owner, owner_mod_id, sizeof(s->owner) - 1);
    strncpy(s->relpath, rel_path, sizeof(s->relpath) - 1);
    strncpy(s->fullpath, full, sizeof(s->fullpath) - 1);
    normalize_slashes_lower(full, s->fullpath_norm, (int)sizeof(s->fullpath_norm));
    s->src_write_time = write_time;
    s->src_size = size;
    memcpy(s->rgba, tmp, sizeof(s->rgba));
    return 1;
}

int font_ext_register_glyph(
    const char* owner_mod_id,
    const char* owner_mod_folder,
    uint8_t byte_value,
    const char* rel_path,
    int override_other,
    char* err,
    int err_sz
) {
    return set_slot(owner_mod_id, owner_mod_folder, byte_value, rel_path, override_other, err, err_sz);
}

int font_ext_alloc_glyph(
    const char* owner_mod_id,
    const char* owner_mod_folder,
    const char* rel_path,
    uint8_t* out_byte,
    char* err,
    int err_sz
) {
    if (!out_byte) {
        safe_snprintf(err, err_sz, "out_byte is null");
        return 0;
    }

    // Cache: same mod + same relpath returns same byte.
    uint8_t cached = 0;
    if (find_alloc_cached(owner_mod_id, rel_path, &cached)) {
        *out_byte = cached;
        return 1;
    }

    // Allocate from 0x80..0xFF.
    for (int b = 0x80; b <= 0xFF; b++) {
        GlyphSlot* s = &g_slots[b];
        if (!s->used) {
            if (!set_slot(owner_mod_id, owner_mod_folder, (uint8_t)b, rel_path, 0, err, err_sz)) {
                return 0;
            }
            add_alloc_cache(owner_mod_id, rel_path, (uint8_t)b);
            *out_byte = (uint8_t)b;
            return 1;
        }
    }

    safe_snprintf(err, err_sz, "no free glyph slots left (0x80..0xFF)");
    return 0;
}

static int glyph_slot_needs_reload(const GlyphSlot* s, int force_reload, uint64_t* out_write_time, uint64_t* out_size) {
    uint64_t write_time = 0;
    uint64_t size = 0;
    if (!s || !s->used || !s->fullpath[0]) return 0;
    if (!query_file_signature(s->fullpath, &write_time, &size)) {
        if (!force_reload) return 0;
        if (out_write_time) *out_write_time = 0;
        if (out_size) *out_size = 0;
        return 1;
    }
    if (out_write_time) *out_write_time = write_time;
    if (out_size) *out_size = size;
    if (force_reload) return 1;
    if (write_time != s->src_write_time) return 1;
    if (size != s->src_size) return 1;
    return 0;
}

static int glyph_slot_reload(GlyphSlot* s, uint64_t write_time, uint64_t size, char* err, int err_sz) {
    uint8_t rgba[8 * 8 * 4];
    if (!s || !s->used) {
        safe_snprintf(err, err_sz, "invalid glyph slot");
        return 0;
    }
    if (!load_icon_8x8_rgba(s->fullpath, rgba, err, err_sz)) return 0;
    memcpy(s->rgba, rgba, sizeof(s->rgba));
    s->src_write_time = write_time;
    s->src_size = size;
    return 1;
}

static void font_ext_reload_impl(int force_reload, int emit_info_log,
                                 int* out_reloaded, int* out_failed, int* out_restart_required) {
    int reloaded = 0;
    int failed = 0;
    int restart_required = 0;
    for (int i = 0; i < 256; i++) {
        GlyphSlot* s = &g_slots[i];
        uint64_t write_time = 0;
        uint64_t size = 0;
        char err[256] = {0};
        if (!s->used) continue;

        if (!glyph_slot_needs_reload(s, force_reload, &write_time, &size)) continue;
        if (!glyph_slot_reload(s, write_time, size, err, (int)sizeof(err))) {
            LOG_WARN("font_ext: failed to reload glyph 0x%02X from %s: %s",
                     (unsigned)i, s->fullpath, err[0] ? err : "unknown error");
            failed++;
            continue;
        }

        reloaded++;
        if (emit_info_log) {
            LOG_INFO("font_ext: reloaded glyph 0x%02X from %s", (unsigned)i, s->fullpath);
        }
    }

    if (out_reloaded) *out_reloaded = reloaded;
    if (out_failed) *out_failed = failed;
    if (out_restart_required) *out_restart_required = restart_required;
}

int font_ext_is_tracked_path(const char* full_path) {
    char norm[MAX_PATH];
    if (!full_path || !full_path[0]) return 0;
    normalize_slashes_lower(full_path, norm, (int)sizeof(norm));
    for (int i = 0; i < 256; i++) {
        GlyphSlot* s = &g_slots[i];
        if (!s->used || !s->fullpath_norm[0]) continue;
        if (_stricmp(s->fullpath_norm, norm) == 0) return 1;
    }
    return 0;
}

int font_ext_poll_hot_reload(void) {
    int reloaded = 0;
    ULONGLONG now = GetTickCount64();
    if (now < g_next_poll_ms) return 0;
    g_next_poll_ms = now + HOT_RELOAD_POLL_MS;
    font_ext_reload_impl(0, 1, &reloaded, NULL, NULL);
    return reloaded;
}

void font_ext_reload_all(int* out_reloaded, int* out_failed, int* out_restart_required) {
    font_ext_reload_impl(1, 1, out_reloaded, out_failed, out_restart_required);
}

static void blit_icon_into_font(RgbaImage* img, uint8_t code, const uint8_t src_rgba[8 * 8 * 4]) {
    if (!img || !img->pixels) return;
    if (img->w <= 0 || img->h <= 0) return;

    // font8x8.png layout: 1px border, then 16x16 cells.
    // Each cell is 9px (8 glyph + 1 gutter), so glyph top-left is:
    //   x0 = 1 + col*9
    //   y0 = 1 + row*9
    int col = (int)(code & 0x0F);
    int row = (int)(code >> 4);
    int x0 = 1 + col * 9;
    int y0 = 1 + row * 9;

    uint8_t* dst = (uint8_t*)img->pixels;
    int stride = img->w * 4;

    // Transparent magenta used throughout the sheet background.
    const uint8_t bg_r = 255, bg_g = 0, bg_b = 255, bg_a = 0;

    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            int di = (y0 + y) * stride + (x0 + x) * 4;
            int si = (y * 8 + x) * 4;
            uint8_t r = src_rgba[si + 0];
            uint8_t g = src_rgba[si + 1];
            uint8_t b = src_rgba[si + 2];
            uint8_t a = src_rgba[si + 3];

            if (a == 0) {
                dst[di + 0] = bg_r;
                dst[di + 1] = bg_g;
                dst[di + 2] = bg_b;
                dst[di + 3] = bg_a;
            } else {
                // Avoid exact keycolor (solid blue) so atlas_add_glyphs continues
                // to see a sprite at the expected top-left corner.
                if (r == 0 && g == 0 && b == 255 && a == 255) {
                    b = 254;
                }
                dst[di + 0] = r;
                dst[di + 1] = g;
                dst[di + 2] = b;
                dst[di + 3] = a;
            }
        }
    }

    // Ensure the tile's top-left pixel is never keycolor.
    {
        int di = (y0 + 0) * stride + (x0 + 0) * 4;
        if (dst[di + 0] == 0 && dst[di + 1] == 0 && dst[di + 2] == 255 && dst[di + 3] == 255) {
            dst[di + 2] = 254;
        }
    }
}

void font_ext_on_rgba_load(const char* path, RgbaImage* img) {
    if (!path || !img) return;
    if (!is_font8x8_path(path)) return;

    g_font_loaded = 1;

    // Sanity check sheet dimensions.
    if (img->w < 145 || img->h < 145 || !img->pixels) {
        LOG_WARN("font_ext: unexpected font8x8 size %dx%d", img->w, img->h);
        return;
    }

    // Apply all registered overlays.
    for (int i = 0; i < 256; i++) {
        GlyphSlot* s = &g_slots[i];
        if (!s->used) continue;
        blit_icon_into_font(img, (uint8_t)i, s->rgba);
    }
}
