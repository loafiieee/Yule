#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Simple moddable glyph overlay for the built-in 8x8 font.
//
// Mods can reserve/override 0..255 glyph bytes and provide an 8x8 RGBA icon.
// The framework patches the pixels of data/font8x8.png *as it is loaded* by
// the game (via a detour on rgba_load), so the base engine text renderer will
// draw the new glyphs with no further hooks.

typedef struct RgbaImage {
    int w;
    int h;
    void* pixels; // stbi_load buffer: w*h*4 bytes (RGBA)
} RgbaImage;

void font_ext_init(void);
void font_ext_shutdown(void);

// Returns 1 after we've seen data/font8x8.png load once.
int font_ext_font_loaded(void);

// Allocate a glyph byte from the "extended" range 0x80..0xFF for this mod.
// rel_path is relative to the mod folder.
// On success, returns 1 and writes out_byte.
int font_ext_alloc_glyph(
    const char* owner_mod_id,
    const char* owner_mod_folder,
    const char* rel_path,
    uint8_t* out_byte,
    char* err,
    int err_sz
);

// Register a specific glyph byte (0..255). If override_other is 0 and the
// glyph is owned by another mod, this will fail.
int font_ext_register_glyph(
    const char* owner_mod_id,
    const char* owner_mod_folder,
    uint8_t byte_value,
    const char* rel_path,
    int override_other,
    char* err,
    int err_sz
);

// Called from the rgba_load detour. If path is data/font8x8.png, applies all
// registered glyph overrides into the pixel buffer.
void font_ext_on_rgba_load(const char* path, RgbaImage* img);

#ifdef __cplusplus
}
#endif
