#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../content_registry.h"
#include "../custom_maps.h"

static int g_failures = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        g_failures++; \
    } \
} while (0)

static char* read_fixture_text(const char* path) {
    FILE* file;
    long length;
    char* text;
    file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    text = (char*)malloc((size_t)length + 1u);
    if (!text) {
        fclose(file);
        return NULL;
    }
    if (fread(text, 1, (size_t)length, file) != (size_t)length) {
        free(text);
        fclose(file);
        return NULL;
    }
    text[length] = '\0';
    fclose(file);
    return text;
}

static int write_fixture_bytes(const char* path, const void* data, size_t size) {
    FILE* file = fopen(path, "wb");
    int ok;
    if (!file) return 0;
    ok = fwrite(data, 1, size, file) == size;
    if (fclose(file) != 0) ok = 0;
    return ok;
}

static int manifest_key_for_prefix(const char* manifest, const char* prefix,
                                   char* out, size_t out_size) {
    const char* begin;
    const char* end;
    size_t length;
    if (!manifest || !prefix || !out || out_size == 0) return 0;
    begin = strstr(manifest, prefix);
    if (!begin || !(end = strchr(begin, '"'))) return 0;
    length = (size_t)(end - begin);
    if (length == 0 || length >= out_size) return 0;
    memcpy(out, begin, length);
    out[length] = '\0';
    return 1;
}

static void build_one_room_map(char symbol, char* out, size_t out_size) {
    size_t position = 0;
    int row;
    int written = snprintf(out, out_size, "; parser test\n\n[center]\n");
    if (written < 0) return;
    position = (size_t)written;
    for (row = 0; row < 12 && position < out_size; row++) {
        char cells[34];
        memset(cells, ' ', 33);
        cells[33] = '\0';
        if (row == 0 && symbol) cells[0] = symbol;
        written = snprintf(out + position, out_size - position, "\"%s\"\n", cells);
        if (written < 0) return;
        position += (size_t)written;
    }
}

static const char* v1_json(void) {
    return
        "{"
        "\"format\":\"eggnogg-map/v1\","
        "\"id\":\"legacy\",\"name\":\"Legacy\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]}"
        "}";
}

static const char* v2_builtin_json(void) {
    return
        "{"
        "\"format\":\"eggnogg-map/v2\","
        "\"id\":\"symbol_test\",\"name\":\"Symbols\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
        "\"tileset\":{\"tiles\":[{"
        "\"id\":\"moss\",\"symbol\":\"$\",\"name\":\"Moss\","
        "\"native_glyph\":\"x\",\"sprite_sheet\":\"builtin:tiles\","
        "\"sprite_index\":4,\"frame_count\":3,\"frame_ticks\":2,"
        "\"animation\":\"ping_pong\",\"mirror_with_room\":true,"
        "\"tint\":[0.2,0.4,0.6,1.0]"
        "}]}}";
}

static void test_v1_compatibility(void) {
    char map_text[1024];
    CustomMapValidationSummary summary;
    build_one_room_map(0, map_text, sizeof(map_text));
    CHECK(custom_maps_validate_package_text("legacy", ".", v1_json(), map_text, &summary));
    CHECK(summary.format_version == 1);
    CHECK(summary.source_room_count == 1);
    CHECK(summary.content_tile_count == 0);
    CHECK(summary.content_cell_count == 0);
}

static void test_v2_symbolic_builtin(void) {
    char map_text[1024];
    CustomMapValidationSummary summary;
    build_one_room_map('$', map_text, sizeof(map_text));
    CHECK(custom_maps_validate_package_text("symbol_test", ".", v2_builtin_json(), map_text, &summary));
    CHECK(summary.format_version == 2);
    CHECK(summary.content_tile_count == 1);
    CHECK(summary.content_cell_count == 1);
}

static void test_repository_runtime_fixture(void) {
    const char* folder = "maps\\v2_symbolic_demo";
    char* json_text = read_fixture_text("maps\\v2_symbolic_demo\\data.json");
    char* map_text = read_fixture_text("maps\\v2_symbolic_demo\\data.map");
    CustomMapValidationSummary summary;
    CHECK(json_text != NULL);
    CHECK(map_text != NULL);
    if (json_text && map_text) {
        CHECK(custom_maps_validate_package_text("v2_symbolic_demo", folder,
                                                json_text, map_text, &summary));
        CHECK(summary.format_version == 2);
        CHECK(summary.source_room_count == 2);
        CHECK(summary.content_tile_count == 3);
        CHECK(summary.content_cell_count == 37);
    }
    free(json_text);
    free(map_text);
}

static void test_v2_hard_failures(void) {
    static const char duplicate_symbol_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"bad\","
        "\"name\":\"Bad\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
        "\"tileset\":{\"tiles\":["
        "{\"id\":\"a\",\"symbol\":\"$\",\"native_glyph\":\"x\","
        "\"sprite_sheet\":\"builtin:tiles\"},"
        "{\"id\":\"b\",\"symbol\":\"$\",\"native_glyph\":\"x\","
        "\"sprite_sheet\":\"builtin:tiles\"}]}}";
    static const char duplicate_key_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"format\":\"eggnogg-map/v1\"}";
    static const char typo_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"bad\","
        "\"name\":\"Bad\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
        "\"tileset\":{\"tiles\":[{\"id\":\"a\",\"symbol\":\"$\","
        "\"native_glyph\":\"x\",\"sprite_sheet\":\"builtin:tiles\","
        "\"sprite_indx\":3}]}}";
    static const char nul_escape_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"bad\\u0000hidden\","
        "\"name\":\"Bad\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]}}";
    static const char huge_integer_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"bad\","
        "\"name\":\"Bad\",\"author\":\"Test\","
        "\"rules\":{\"mode\":\"swords\",\"score_target\":1e100},"
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]}}";
    static const char missing_stable_id_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"name\":\"Bad\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]}}";
    static const char overlong_stable_id_json[] =
        "{\"format\":\"eggnogg-map/v2\","
        "\"id\":\"this_identifier_is_far_too_long_for_the_map_content_namespace\","
        "\"name\":\"Bad\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]}}";
    static const char invalid_layer_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"bad\"," 
        "\"name\":\"Bad\",\"author\":\"Test\"," 
        "\"layout\":{\"kind\":\"mirrored_source_rooms\"," 
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
        "\"tileset\":{\"tiles\":[{\"id\":\"a\",\"symbol\":\"$\"," 
        "\"native_glyph\":\"x\",\"sprite_sheet\":\"builtin:tiles\"," 
        "\"layer\":2}]}}";
    char map_text[1024];
    char blank_map_text[1024];
    CustomMapValidationSummary summary;
    build_one_room_map('$', map_text, sizeof(map_text));
    build_one_room_map(0, blank_map_text, sizeof(blank_map_text));
    CHECK(!custom_maps_validate_package_text("bad", ".", duplicate_symbol_json, map_text, &summary));
    CHECK(summary.error_count > 0);
    CHECK(!custom_maps_validate_package_text("bad", ".", duplicate_key_json, map_text, &summary));
    CHECK(summary.error_count > 0);
    CHECK(!custom_maps_validate_package_text("bad", ".", typo_json, map_text, &summary));
    CHECK(summary.error_count > 0);
    CHECK(!custom_maps_validate_package_text("bad", ".", nul_escape_json, map_text, &summary));
    CHECK(summary.error_count > 0);
    CHECK(!custom_maps_validate_package_text("bad", ".", huge_integer_json,
                                             blank_map_text, &summary));
    CHECK(summary.error_count > 0);
    CHECK(!custom_maps_validate_package_text("bad", ".", missing_stable_id_json,
                                             blank_map_text, &summary));
    CHECK(summary.error_count > 0);
    CHECK(!custom_maps_validate_package_text("bad", ".", overlong_stable_id_json,
                                             blank_map_text, &summary));
    CHECK(summary.error_count > 0);
    CHECK(!custom_maps_validate_package_text("bad", ".", invalid_layer_json,
                                             map_text, &summary));
    CHECK(summary.error_count > 0);
}

static void test_external_asset_hash(void) {
    static const unsigned char png_header[24] = {
        0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a,
        0x00, 0x00, 0x00, 0x0d, 'I', 'H', 'D', 'R',
        0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10
    };
    const char* folder = "build\\map_v2_package_test";
    const char* asset = "build\\map_v2_package_test\\tiles.png";
    char digest[CONTENT_SHA256_HEX_SIZE];
    char err[256];
    char json[2048];
    char geometry_json[2048];
    char map_text[1024];
    CustomMapValidationSummary summary;
    FILE* file;
    CreateDirectoryA("build", NULL);
    CreateDirectoryA(folder, NULL);
    file = fopen(asset, "wb");
    CHECK(file != NULL);
    if (!file) return;
    CHECK(fwrite(png_header, 1, sizeof(png_header), file) == sizeof(png_header));
    fclose(file);
    CHECK(content_registry_sha256_file(asset, NULL, digest, err, sizeof(err)));
    snprintf(json, sizeof(json),
             "{\"format\":\"eggnogg-map/v2\",\"id\":\"asset_test\","
             "\"name\":\"Asset\",\"author\":\"Test\","
             "\"layout\":{\"kind\":\"mirrored_source_rooms\","
             "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
             "\"tileset\":{\"tiles\":[{\"id\":\"tile\",\"symbol\":\"$\","
             "\"native_glyph\":\"x\",\"sprite_sheet\":\"tiles.png\"}]}}");
    build_one_room_map('$', map_text, sizeof(map_text));
    /* The runtime-computed digest is the normal authoring path. */
    CHECK(custom_maps_validate_package_text("asset_test", folder, json, map_text, &summary));
    CHECK(summary.content_tile_count == 1 && summary.content_cell_count == 1);
    snprintf(geometry_json, sizeof(geometry_json),
             "{\"format\":\"eggnogg-map/v2\",\"id\":\"geometry_test\","
             "\"name\":\"Geometry\",\"author\":\"Test\","
             "\"layout\":{\"kind\":\"mirrored_source_rooms\","
             "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
             "\"tileset\":{\"tiles\":[{\"id\":\"tile\",\"symbol\":\"$\","
             "\"native_glyph\":\"x\",\"sprite_sheet\":\"tiles.png\","
             "\"asset_sha256\":\"%s\",\"cell_w\":8,\"cell_h\":8,"
             "\"sprite_index\":3}]}}", digest);
    CHECK(custom_maps_validate_package_text("geometry_test", folder, geometry_json,
                                            map_text, &summary));
    {
        char* index_pos = strstr(geometry_json, "\"sprite_index\":3");
        CHECK(index_pos != NULL);
        if (index_pos) index_pos[strlen("\"sprite_index\":")] = '4';
    }
    CHECK(!custom_maps_validate_package_text("geometry_test", folder, geometry_json,
                                             map_text, &summary));
    /* Authors may still pin a digest when package-integrity policy needs it. */
    snprintf(json, sizeof(json),
             "{\"format\":\"eggnogg-map/v2\",\"id\":\"asset_test\","
             "\"name\":\"Asset\",\"author\":\"Test\","
             "\"layout\":{\"kind\":\"mirrored_source_rooms\","
             "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
             "\"tileset\":{\"tiles\":[{\"id\":\"tile\",\"symbol\":\"$\","
             "\"native_glyph\":\"x\",\"sprite_sheet\":\"tiles.png\","
             "\"asset_sha256\":\"%s\"}]}}", digest);
    CHECK(custom_maps_validate_package_text("asset_test", folder, json, map_text, &summary));
    {
        char* digest_pos = strstr(json, digest);
        CHECK(digest_pos != NULL);
        if (digest_pos) digest_pos[0] = digest_pos[0] == '0' ? '1' : '0';
    }
    CHECK(!custom_maps_validate_package_text("asset_test", folder, json, map_text, &summary));
    DeleteFileA(asset);
    RemoveDirectoryA(folder);
}

static void test_external_asset_online_identity(void) {
    static const unsigned char png_a[24] = {
        0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a,
        0x00, 0x00, 0x00, 0x0d, 'I', 'H', 'D', 'R',
        0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10
    };
    unsigned char png_b[25];
    static const char json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"online_identity_test\","
        "\"name\":\"Online identity test\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
        "\"tileset\":{\"tiles\":[{\"id\":\"tile\",\"symbol\":\"$\","
        "\"native_glyph\":\"x\",\"sprite_sheet\":\"tiles.png\"}]}}";
    char folder[MAX_PATH];
    char json_path[MAX_PATH * 2];
    char map_path[MAX_PATH * 2];
    char asset_path[MAX_PATH * 2];
    char map_text[1024];
    char key_a[160];
    char key_b[160];
    char* manifest = NULL;
    int needed;
    int built;

    key_a[0] = '\0';
    key_b[0] = '\0';

    snprintf(folder, sizeof(folder), "maps\\v2_online_identity_test_%lu",
             (unsigned long)GetCurrentProcessId());
    snprintf(json_path, sizeof(json_path), "%s\\data.json", folder);
    snprintf(map_path, sizeof(map_path), "%s\\data.map", folder);
    snprintf(asset_path, sizeof(asset_path), "%s\\tiles.png", folder);
    DeleteFileA(json_path);
    DeleteFileA(map_path);
    DeleteFileA(asset_path);
    RemoveDirectoryA(folder);
    CHECK(CreateDirectoryA(folder, NULL) != 0);
    build_one_room_map('$', map_text, sizeof(map_text));
    CHECK(write_fixture_bytes(json_path, json, strlen(json)));
    CHECK(write_fixture_bytes(map_path, map_text, strlen(map_text)));
    CHECK(write_fixture_bytes(asset_path, png_a, sizeof(png_a)));

    custom_maps_init();
    needed = custom_maps_build_manifest_json(NULL, 0);
    CHECK(needed > 0);
    if (needed > 0) manifest = (char*)malloc((size_t)needed + 1u);
    CHECK(manifest != NULL);
    if (manifest) {
        built = custom_maps_build_manifest_json(manifest, (size_t)needed + 1u);
        CHECK(built == needed);
        CHECK(manifest_key_for_prefix(manifest,
                                      "custom:online_identity_test:",
                                      key_a, sizeof(key_a)));
        free(manifest);
        manifest = NULL;
    }

    memcpy(png_b, png_a, sizeof(png_a));
    png_b[sizeof(png_b) - 1u] = 0x5a;
    CHECK(write_fixture_bytes(asset_path, png_b, sizeof(png_b)));
    needed = custom_maps_build_manifest_json(NULL, 0);
    CHECK(needed > 0);
    if (needed > 0) manifest = (char*)malloc((size_t)needed + 1u);
    CHECK(manifest != NULL);
    if (manifest) {
        built = custom_maps_build_manifest_json(manifest, (size_t)needed + 1u);
        CHECK(built == needed);
        CHECK(manifest_key_for_prefix(manifest,
                                      "custom:online_identity_test:",
                                      key_b, sizeof(key_b)));
        CHECK(strcmp(key_a, key_b) != 0);
    }
    free(manifest);
    custom_maps_shutdown();
    DeleteFileA(json_path);
    DeleteFileA(map_path);
    DeleteFileA(asset_path);
    CHECK(RemoveDirectoryA(folder) != 0);
}

int main(void) {
    content_registry_shutdown();
    test_v1_compatibility();
    test_v2_symbolic_builtin();
    test_repository_runtime_fixture();
    test_v2_hard_failures();
    test_external_asset_hash();
    test_external_asset_online_identity();
    content_registry_shutdown();
    if (g_failures != 0) {
        fprintf(stderr, "%d custom map v2 test(s) failed\n", g_failures);
        return 1;
    }
    puts("custom_maps_v2_test: all checks passed");
    return 0;
}
