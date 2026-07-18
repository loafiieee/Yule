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
             "\"native_glyph\":\"x\",\"sprite_sheet\":\"tiles.png\","
             "\"asset_sha256\":\"%s\"}]}}", digest);
    build_one_room_map('$', map_text, sizeof(map_text));
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
    {
        char* digest_pos = strstr(json, digest);
        CHECK(digest_pos != NULL);
        if (digest_pos) digest_pos[0] = digest_pos[0] == '0' ? '1' : '0';
    }
    CHECK(!custom_maps_validate_package_text("asset_test", folder, json, map_text, &summary));
    DeleteFileA(asset);
    RemoveDirectoryA(folder);
}

int main(void) {
    content_registry_shutdown();
    test_v1_compatibility();
    test_v2_symbolic_builtin();
    test_repository_runtime_fixture();
    test_v2_hard_failures();
    test_external_asset_hash();
    content_registry_shutdown();
    if (g_failures != 0) {
        fprintf(stderr, "%d custom map v2 test(s) failed\n", g_failures);
        return 1;
    }
    puts("custom_maps_v2_test: all checks passed");
    return 0;
}
