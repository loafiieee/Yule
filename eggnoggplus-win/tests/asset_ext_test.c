#include <windows.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../font_ext.h"
#include "../texture_ext.h"

static int width = 8, height = 8, loads, frees;
unsigned char* __cdecl asset_test_load(const char* path, int* w, int* h, int* comp, int requested) {
    unsigned char* pixels = malloc(256);
    (void)path; (void)requested;
    assert(pixels);
    memset(pixels, 37, 256);
    *w = width; *h = height; *comp = 4;
    loads++;
    return pixels;
}
void __cdecl asset_test_free(void* pixels) { frees++; free(pixels); }
void log_write(const char* level, const char* fmt, ...) { (void)level; (void)fmt; }

int main(void) {
    char folder[MAX_PATH], full[MAX_PATH], original[MAX_PATH];
    char error[256], canonical[128], long_name[160];
    uint8_t glyph, second;
    int count, failed, restart;
    assert(GetTempPathA(sizeof(folder), folder));
    assert(GetTempFileNameA(folder, "aet", 0, full));
    strcpy(original, full);
    char* leaf = strrchr(full, '\\');
    assert(leaf);
    *leaf++ = '\0';
    strcpy(folder, full);

    assert(texture_ext_canonicalize_target("SPRITES.PNG", canonical, sizeof(canonical)));
    assert(strcmp(canonical, "data/sprites.png") == 0);
    assert(texture_ext_canonicalize_target("C:\\game\\data\\sprites.png", canonical, sizeof(canonical)));
    assert(!texture_ext_canonicalize_target("metadata/sprites.png", canonical, sizeof(canonical)));
    assert(!texture_ext_canonicalize_target("data/../sprites.png", canonical, sizeof(canonical)));
    assert(!texture_ext_canonicalize_target("data//sprites.png", canonical, sizeof(canonical)));
    memset(long_name, 'x', sizeof(long_name));
    memcpy(long_name + sizeof(long_name) - 5, ".png", 5);
    assert(!texture_ext_canonicalize_target(long_name, canonical, sizeof(canonical)));

    font_ext_init();
    assert(font_ext_alloc_glyph("alpha", folder, leaf, &glyph, error, sizeof(error)));
    assert(glyph == 0x80);
    int before = loads;
    assert(font_ext_alloc_glyph("alpha", folder, leaf, &second, error, sizeof(error)));
    assert(second == glyph && loads == before);
    font_ext_reset_runtime_state();
    assert(font_ext_alloc_glyph("new_owner", folder, leaf, &second, error, sizeof(error)));
    assert(second != glyph);
    before++;
    assert(font_ext_alloc_glyph("alpha", folder, leaf, &second, error, sizeof(error)));
    assert(second == glyph && loads == before + 1);
    assert(font_ext_is_tracked_path(original));
    assert(font_ext_register_glyph("beta", folder, glyph, leaf, 1, error, sizeof(error)));
    assert(!font_ext_alloc_glyph("alpha", folder, leaf, &second, error, sizeof(error)));
    assert(strstr(error, "replaced"));
    font_ext_forget_glyph_cache("alpha", glyph);
    assert(font_ext_alloc_glyph("alpha", folder, leaf, &second, error, sizeof(error)));
    assert(second != glyph);
    before = loads;
    assert(!font_ext_register_glyph(long_name, folder, 130, leaf, 0, error, sizeof(error)));
    assert(loads == before);
    font_ext_shutdown();

    for (int i = 0; i < 100; i++) {
        unsigned char destination[256] = {0};
        RgbaImage image = {8, 8, destination};
        texture_ext_init();
        assert(texture_ext_register_png("alpha", folder, "sprites.png", leaf, 0, error, sizeof(error)));
        assert(!texture_ext_register_png("beta", folder, "data/sprites.png", leaf, 0, error, sizeof(error)));
        texture_ext_on_rgba_load("data/sprites.png", &image);
        assert(destination[0] == 37 && destination[255] == 37);
        assert(texture_ext_path_loaded("sprites.png"));
        assert(texture_ext_is_tracked_path(original));
        width = height = 65536;
        assert(!texture_ext_register_png("alpha", folder, "sprites.png", leaf, 0, error, sizeof(error)));
        width = height = 8;
        memset(destination, 0, sizeof(destination));
        texture_ext_on_rgba_load("data/sprites.png", &image);
        assert(destination[0] == 37 && destination[255] == 37);
        texture_ext_reset_runtime_state();
        assert(!texture_ext_is_tracked_path(original));
        texture_ext_reload_all(&count, &failed, &restart);
        assert(count == 0 && failed == 0 && restart == 0);
        texture_ext_shutdown();
    }
    assert(loads == frees);
    assert(DeleteFileA(original));
    puts("asset ownership, canonical paths, overflow and reload tests: OK");
    return 0;
}
