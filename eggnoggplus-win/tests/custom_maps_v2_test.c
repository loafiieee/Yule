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

static int write_png_header_fixture(const char* path,
                                    unsigned int width,
                                    unsigned int height,
                                    unsigned char identity_byte) {
    unsigned char bytes[25] = {
        0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a,
        0x00, 0x00, 0x00, 0x0d, 'I', 'H', 'D', 'R'
    };
    bytes[16] = (unsigned char)((width >> 24) & 0xffu);
    bytes[17] = (unsigned char)((width >> 16) & 0xffu);
    bytes[18] = (unsigned char)((width >> 8) & 0xffu);
    bytes[19] = (unsigned char)(width & 0xffu);
    bytes[20] = (unsigned char)((height >> 24) & 0xffu);
    bytes[21] = (unsigned char)((height >> 16) & 0xffu);
    bytes[22] = (unsigned char)((height >> 8) & 0xffu);
    bytes[23] = (unsigned char)(height & 0xffu);
    bytes[24] = identity_byte;
    return write_fixture_bytes(path, bytes, sizeof(bytes));
}

static int read_png_header_dimensions(const char* path,
                                      unsigned int* out_width,
                                      unsigned int* out_height) {
    unsigned char bytes[24];
    FILE* file;
    if (!path || !out_width || !out_height) return 0;
    file = fopen(path, "rb");
    if (!file) return 0;
    if (fread(bytes, 1, sizeof(bytes), file) != sizeof(bytes)) {
        fclose(file);
        return 0;
    }
    fclose(file);
    if (memcmp(bytes, "\x89PNG\r\n\x1a\n", 8) != 0 ||
        memcmp(bytes + 12, "IHDR", 4) != 0) {
        return 0;
    }
    *out_width = ((unsigned int)bytes[16] << 24) |
                 ((unsigned int)bytes[17] << 16) |
                 ((unsigned int)bytes[18] << 8) |
                 (unsigned int)bytes[19];
    *out_height = ((unsigned int)bytes[20] << 24) |
                  ((unsigned int)bytes[21] << 16) |
                  ((unsigned int)bytes[22] << 8) |
                  (unsigned int)bytes[23];
    return 1;
}

static int restore_file_write_time(const char* path, const FILETIME* write_time) {
    HANDLE file;
    int ok;
    if (!path || !write_time) return 0;
    file = CreateFileA(path, FILE_WRITE_ATTRIBUTES,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    ok = SetFileTime(file, NULL, NULL, write_time) != 0;
    CloseHandle(file);
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

static int fixture_tile_sprite_index(const char* json, const char* tile_id,
                                     int* out_sprite_index) {
    char id_value[CONTENT_LOCAL_ID_MAX + 8];
    const char* id;
    const char* next_id;
    const char* sprite_index;
    char* end;
    long value;
    int written;
    if (!json || !tile_id || !out_sprite_index) return 0;
    written = snprintf(id_value, sizeof(id_value), "\"%s\"", tile_id);
    if (written < 0 || (size_t)written >= sizeof(id_value)) return 0;
    id = strstr(json, id_value);
    if (!id) return 0;
    next_id = strstr(id + (size_t)written, "\"id\"");
    sprite_index = strstr(id + (size_t)written, "\"sprite_index\"");
    if (!sprite_index || (next_id && sprite_index >= next_id)) return 0;
    sprite_index = strchr(sprite_index, ':');
    if (!sprite_index) return 0;
    value = strtol(sprite_index + 1, &end, 10);
    if (end == sprite_index + 1 || value < 0 || value > 1000000) return 0;
    *out_sprite_index = (int)value;
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

static void build_one_room_spawn_map(int k_count,
                                     int sword_count,
                                     int mine_count,
                                     char* out,
                                     size_t out_size) {
    size_t position = 0;
    int cell_index = 0;
    int row;
    int written = snprintf(out, out_size, "; native spawn budget test\n\n[center]\n");
    if (written < 0) return;
    position = (size_t)written;
    for (row = 0; row < 12 && position < out_size; row++) {
        char cells[34];
        int col;
        for (col = 0; col < 33; col++, cell_index++) {
            if (cell_index < k_count) cells[col] = 'K';
            else if (cell_index < k_count + sword_count) cells[col] = '*';
            else if (cell_index < k_count + sword_count + mine_count) cells[col] = 'm';
            else cells[col] = ' ';
        }
        cells[33] = '\0';
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

static const char* spawn_budget_v2_json(void) {
    return
        "{"
        "\"format\":\"eggnogg-map/v2\","
        "\"id\":\"spawn_budget\",\"name\":\"Spawn budget\",\"author\":\"Test\","
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
        "\"native_visual\":\"underlay\","
        "\"tint\":[0.2,0.4,0.6,1.0]"
        "}]}}";
}

static const char* v2_builtin_override_json(void) {
    return
        "{"
        "\"format\":\"eggnogg-map/v2\","
        "\"id\":\"builtin_skin\",\"name\":\"Builtin skin\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
        "\"tileset\":{\"tiles\":[{"
        "\"id\":\"terrain\",\"symbol\":\"@\",\"collision\":\"native\","
        "\"sprite_sheet\":\"builtin:tiles\",\"sprite_index\":2"
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

static void test_in_memory_v1_preview(void) {
    static const char json[] =
        "{\"format\":\"eggnogg-map/v1\",\"id\":\"preview_test\","
        "\"name\":\"Preview Test\",\"author\":\"Greggnogg\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]}}";
    static const char v2_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"preview_test\","
        "\"name\":\"Preview Test\",\"author\":\"Greggnogg\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]}}";
    char map_text[1024];
    char error[256];
    char* manifest = NULL;
    CustomMapContentView view;
    int selector = -1;
    int total_before;
    int needed;

    build_one_room_map(0, map_text, sizeof(map_text));
    custom_maps_init();
    total_before = custom_maps_total_selectors();
    CHECK(custom_maps_install_preview_text(json, map_text, &selector,
                                           error, sizeof(error)));
    CHECK(error[0] == '\0');
    CHECK(selector == total_before);
    CHECK(custom_maps_total_selectors() == total_before + 1);
    CHECK(custom_maps_content_view_open(selector, &view) == 1);
    CHECK(view.format_version == 1);
    CHECK(view.source_room_count == 1);

    needed = custom_maps_build_manifest_json(NULL, 0);
    CHECK(needed > 0);
    if (needed > 0) manifest = (char*)malloc((size_t)needed + 1u);
    CHECK(manifest != NULL);
    if (manifest) {
        CHECK(custom_maps_build_manifest_json(manifest,
                                              (size_t)needed + 1u) == needed);
        CHECK(strstr(manifest, "Preview Test") == NULL);
        CHECK(strstr(manifest, "_greggnogg_preview") == NULL);
    }
    free(manifest);

    CHECK(!custom_maps_install_preview_text(v2_json, map_text, &selector,
                                            error, sizeof(error)));
    CHECK(error[0] != '\0');
    CHECK(custom_maps_total_selectors() == total_before + 1);
    custom_maps_shutdown();
}

static void test_native_room_spawn_budget(void) {
    char map_text[1024];
    CustomMapValidationSummary summary;

    build_one_room_spawn_map(13, 0, 0, map_text, sizeof(map_text));
    CHECK(custom_maps_validate_package_text("spawn_budget", ".",
                                            spawn_budget_v2_json(), map_text,
                                            &summary));
    CHECK(summary.max_native_room_spawns == 13);
    CHECK(summary.native_room_spawn_limit == 13);
    CHECK(summary.native_k_marker_count == 13);

    build_one_room_spawn_map(14, 0, 0, map_text, sizeof(map_text));
    CHECK(!custom_maps_validate_package_text("spawn_budget", ".",
                                             spawn_budget_v2_json(), map_text,
                                             &summary));
    CHECK(summary.max_native_room_spawns == 14);
    CHECK(summary.native_room_spawn_limit == 13);
    CHECK(summary.native_k_marker_count == 14);
    CHECK(summary.error_count > 0);

    build_one_room_spawn_map(5, 4, 4, map_text, sizeof(map_text));
    CHECK(custom_maps_validate_package_text("spawn_budget", ".",
                                            spawn_budget_v2_json(), map_text,
                                            &summary));
    CHECK(summary.max_native_room_spawns == 13);
    CHECK(summary.native_k_marker_count == 5);

    build_one_room_spawn_map(5, 4, 5, map_text, sizeof(map_text));
    CHECK(!custom_maps_validate_package_text("spawn_budget", ".",
                                             spawn_budget_v2_json(), map_text,
                                             &summary));
    CHECK(summary.max_native_room_spawns == 14);
    CHECK(summary.error_count > 0);

    /* Existing no-K rooms retain their native fail-safe behavior: mine_action
     * checks allocation failure and sword_new can recycle prior swords. */
    build_one_room_spawn_map(0, 14, 0, map_text, sizeof(map_text));
    CHECK(custom_maps_validate_package_text("spawn_budget", ".",
                                            spawn_budget_v2_json(), map_text,
                                            &summary));
    build_one_room_spawn_map(0, 0, 14, map_text, sizeof(map_text));
    CHECK(custom_maps_validate_package_text("spawn_budget", ".",
                                            spawn_budget_v2_json(), map_text,
                                            &summary));
}

static void test_v2_symbolic_builtin(void) {
    char map_text[1024];
    CustomMapValidationSummary summary;
    build_one_room_map('$', map_text, sizeof(map_text));
    CHECK(custom_maps_validate_package_text("symbol_test", ".", v2_builtin_json(), map_text, &summary));
    CHECK(summary.format_version == 2);
    CHECK(summary.content_tile_count == 1);
    CHECK(summary.content_cell_count == 1);

    build_one_room_map('@', map_text, sizeof(map_text));
    CHECK(custom_maps_validate_package_text("builtin_skin", ".",
                                            v2_builtin_override_json(), map_text,
                                            &summary));
    CHECK(summary.content_tile_count == 1);
    CHECK(summary.content_cell_count == 1);
}

static void test_v2_whole_symbol_override(void) {
    static const char expanded_symbol_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"expanded_skin\","
        "\"name\":\"Expanded skin\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
        "\"tileset\":{\"tiles\":[{\"id\":\"mural\",\"symbol\":\"G\","
        "\"native_glyph\":\"x\",\"sprite_sheet\":\"builtin:tiles\"}]}}";
    static const char space_symbol_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"space_skin\","
        "\"name\":\"Space skin\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
        "\"tileset\":{\"tiles\":[{\"id\":\"air\",\"symbol\":\" \","
        "\"collision\":\"pass_through\",\"sprite_sheet\":\"builtin:tiles\"}]}}";
    char map_text[1024];
    CustomMapValidationSummary summary;

    build_one_room_map('G', map_text, sizeof(map_text));
    CHECK(custom_maps_validate_package_text("expanded_skin", ".",
                                            expanded_symbol_json, map_text, &summary));
    CHECK(summary.content_cell_count == 1);

    build_one_room_map(0, map_text, sizeof(map_text));
    CHECK(custom_maps_validate_package_text("space_skin", ".",
                                            space_symbol_json, map_text, &summary));
    CHECK(summary.content_cell_count == 33 * 12);
}

static void test_repository_runtime_fixture(void) {
    const char* folder = "maps\\v2_symbolic_demo";
    char* json_text = read_fixture_text("maps\\v2_symbolic_demo\\data.json");
    char* map_text = read_fixture_text("maps\\v2_symbolic_demo\\data.map");
    CustomMapValidationSummary summary;
    int sprite_index = -1;
    CHECK(json_text != NULL);
    CHECK(map_text != NULL);
    if (json_text && map_text) {
        CHECK(custom_maps_validate_package_text("v2_symbolic_demo", folder,
                                                json_text, map_text, &summary));
        CHECK(summary.format_version == 2);
        CHECK(summary.source_room_count == 2);
        CHECK(summary.content_tile_count == 5);
        /* The checked-in acceptance room is intentionally editable while
         * authoring. Package validity must not depend on one exact placement
         * count; every declared behavior is covered separately below. */
        CHECK(summary.content_cell_count >= summary.content_tile_count);
        CHECK(summary.native_layout == 1);
        CHECK(summary.default_sheet_sprite_count >= 128);
        CHECK(summary.default_sheet_key[0] != '\0');
        CHECK(summary.has_script == 1);
        CHECK(summary.script_size > 0);
        CHECK(summary.script_id != 0);
        CHECK(strlen(summary.script_sha256) == CONTENT_SHA256_HEX_SIZE - 1u);
        CHECK(strstr(json_text, "\"force_") == NULL);
        CHECK(strstr(json_text, "\"native_visual\": \"underlay\"") != NULL);
        /* Ordinary @ cells are deliberately absent from the per-tile list:
         * native_layout must reskin them through the map-level sheet. The
         * explicit spring uses the first appended cell after the 128-cell
         * native prefix. */
        CHECK(strstr(json_text, "builtin_terrain_skin") == NULL);
        CHECK(fixture_tile_sprite_index(json_text, "spring_pad",
                                        &sprite_index));
        CHECK(sprite_index == 128);
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
    static const char invalid_behavior_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"bad\","
        "\"name\":\"Bad\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
        "\"tileset\":{\"tiles\":[{\"id\":\"a\",\"symbol\":\"$\","
        "\"collision\":\"hover\",\"force_mode\":\"pulse\","
        "\"sprite_sheet\":\"builtin:tiles\"}]}}";
    static const char invalid_native_visual_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"bad\","
        "\"name\":\"Bad\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
        "\"tileset\":{\"tiles\":[{\"id\":\"a\",\"symbol\":\"$\","
        "\"collision\":\"solid\",\"native_visual\":\"merge\","
        "\"sprite_sheet\":\"builtin:tiles\"}]}}";
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
    build_one_room_map('$', map_text, sizeof(map_text));
    CHECK(!custom_maps_validate_package_text("bad", ".", invalid_behavior_json,
                                              map_text, &summary));
    CHECK(summary.error_count > 0);
    CHECK(!custom_maps_validate_package_text("bad", ".",
                                              invalid_native_visual_json,
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

static void test_tileset_defaults_and_native_layout(void) {
    const char* folder = "build\\map_tileset_defaults_test";
    const char* default_path =
        "build\\map_tileset_defaults_test\\default.png";
    const char* override_path =
        "build\\map_tileset_defaults_test\\override.png";
    const char* native_path =
        "build\\map_tileset_defaults_test\\native.png";
    static const char missing_default_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"missing_default\","
        "\"name\":\"Missing\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
        "\"tileset\":{\"tiles\":[{\"id\":\"tile\",\"symbol\":\"$\","
        "\"collision\":\"solid\",\"sprite_index\":0}]}}";
    static const char native_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"native_default\","
        "\"name\":\"Native\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
        "\"tileset\":{\"sprite_sheet\":\"native.png\","
        "\"native_layout\":true}}";
    static const char native_small_cells_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"native_default\","
        "\"name\":\"Native\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
        "\"tileset\":{\"sprite_sheet\":\"native.png\","
        "\"cell_w\":8,\"cell_h\":8,\"native_layout\":true}}";
    static const char native_missing_sheet_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"native_default\","
        "\"name\":\"Native\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
        "\"tileset\":{\"native_layout\":true}}";
    static const char native_builtin_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"native_default\","
        "\"name\":\"Native\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
        "\"tileset\":{\"sprite_sheet\":\"builtin:tiles\","
        "\"native_layout\":true}}";
    static const char native_non_bool_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"native_default\","
        "\"name\":\"Native\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
        "\"tileset\":{\"sprite_sheet\":\"native.png\","
        "\"native_layout\":1}}";
    char inherited_json[4096];
    char default_sha[CONTENT_SHA256_HEX_SIZE];
    char map_text[1024];
    char err[256];
    unsigned int builtin_w = 0;
    unsigned int builtin_h = 0;
    CustomMapValidationSummary summary;

    CreateDirectoryA("build", NULL);
    DeleteFileA(default_path);
    DeleteFileA(override_path);
    DeleteFileA(native_path);
    RemoveDirectoryA(folder);
    CHECK(CreateDirectoryA(folder, NULL) != 0);
    CHECK(write_png_header_fixture(default_path, 16, 16, 0x11));
    CHECK(write_png_header_fixture(override_path, 30, 18, 0x22));
    CHECK(write_png_header_fixture(native_path, 128, 384, 0x33));
    err[0] = '\0';
    CHECK(content_registry_sha256_file(default_path, NULL, default_sha,
                                       err, sizeof(err)));
    snprintf(inherited_json, sizeof(inherited_json),
             "{\"format\":\"eggnogg-map/v2\","
             "\"id\":\"tileset_defaults\",\"name\":\"Defaults\","
             "\"author\":\"Test\","
             "\"layout\":{\"kind\":\"mirrored_source_rooms\","
             "\"room_format\":\"vanilla_33x12\","
             "\"order\":[\"center\"]},"
             "\"tileset\":{\"sprite_sheet\":\"default.png\","
             "\"asset_sha256\":\"%s\",\"cell_w\":8,\"cell_h\":8,"
             "\"padding\":0,\"tiles\":["
             "{\"id\":\"inherited\",\"symbol\":\"$\","
             "\"collision\":\"solid\",\"sprite_index\":3},"
             "{\"id\":\"overridden\",\"symbol\":\"%%\","
             "\"collision\":\"pass_through\","
             "\"sprite_sheet\":\"override.png\","
             "\"cell_w\":10,\"cell_h\":9,\"sprite_index\":5}]}}",
             default_sha);
    build_one_room_map('$', map_text, sizeof(map_text));
    CHECK(custom_maps_validate_package_text("tileset_defaults", folder,
                                            inherited_json, map_text,
                                            &summary));
    CHECK(summary.content_tile_count == 2);
    CHECK(summary.content_cell_count == 1);
    CHECK(summary.native_layout == 0);
    CHECK(summary.default_sheet_sprite_count == 4);
    CHECK(strncmp(summary.default_sheet_key, "map.tileset_defaults:",
                  strlen("map.tileset_defaults:")) == 0);

    CHECK(!custom_maps_validate_package_text("missing_default", folder,
                                             missing_default_json, map_text,
                                             &summary));
    CHECK(summary.error_count > 0);

    build_one_room_map(0, map_text, sizeof(map_text));
    CHECK(read_png_header_dimensions("data\\tiles.png", &builtin_w,
                                     &builtin_h));
    CHECK(builtin_w == 128u && builtin_h == 256u);
    CHECK(custom_maps_validate_package_text("native_default", folder,
                                            native_json, map_text, &summary));
    CHECK(summary.content_tile_count == 0);
    CHECK(summary.native_layout == 1);
    CHECK(summary.default_sheet_sprite_count == 192);
    CHECK(summary.default_sheet_key[0] != '\0');

    /* Atlas shape is authoring policy, not a runtime safety requirement. The
     * first 128 row-major records are the native prefix; extra records are
     * available to explicit custom tiles. */
    CHECK(write_png_header_fixture(native_path, 256, 128, 0x44));
    CHECK(custom_maps_validate_package_text("native_default", folder,
                                            native_json, map_text, &summary));
    CHECK(summary.default_sheet_sprite_count == 128);
    CHECK(write_png_header_fixture(native_path, 128, 384, 0x33));
    CHECK(custom_maps_validate_package_text("native_default", folder,
                                            native_small_cells_json, map_text,
                                            &summary));
    CHECK(summary.default_sheet_sprite_count == 768);

    /* Fewer than 128 records would let native actions address beyond the
     * packed sheet, so this is the one native-layout size constraint. */
    CHECK(write_png_header_fixture(native_path, 64, 256, 0x55));
    CHECK(!custom_maps_validate_package_text("native_default", folder,
                                             native_json, map_text, &summary));
    CHECK(summary.error_count > 0);
    CHECK(!custom_maps_validate_package_text("native_default", folder,
                                             native_missing_sheet_json,
                                             map_text, &summary));
    CHECK(summary.error_count > 0);
    CHECK(!custom_maps_validate_package_text("native_default", folder,
                                             native_builtin_json, map_text,
                                             &summary));
    CHECK(summary.error_count > 0);
    CHECK(!custom_maps_validate_package_text("native_default", folder,
                                             native_non_bool_json, map_text,
                                             &summary));
    CHECK(summary.error_count > 0);

    DeleteFileA(default_path);
    DeleteFileA(override_path);
    DeleteFileA(native_path);
    CHECK(RemoveDirectoryA(folder) != 0);
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

static void test_tileset_default_online_identity_and_view(void) {
    static const char json_plain[] =
        "{\"format\":\"eggnogg-map/v2\","
        "\"id\":\"tileset_identity_test\","
        "\"name\":\"Tileset identity test\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
        "\"tileset\":{\"sprite_sheet\":\"tiles.png\","
        "\"native_layout\":false}}";
    static const char json_native[] =
        "{\"format\":\"eggnogg-map/v2\","
        "\"id\":\"tileset_identity_test\","
        "\"name\":\"Tileset identity test\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
        "\"tileset\":{\"sprite_sheet\":\"tiles.png\","
        "\"native_layout\":true}}";
    char folder[MAX_PATH];
    char json_path[MAX_PATH * 2];
    char map_path[MAX_PATH * 2];
    char asset_path[MAX_PATH * 2];
    char map_text[1024];
    char key_plain[160];
    char key_native[160];
    char key_asset[160];
    char sheet_key_native[128];
    char sheet_path[260];
    char sheet_sha[CONTENT_SHA256_HEX_SIZE];
    char* manifest = NULL;
    WIN32_FILE_ATTRIBUTE_DATA asset_info;
    FILETIME original_write_time;
    CustomMapContentView view;
    int selector = -1;
    int needed;

    snprintf(folder, sizeof(folder), "maps\\v2_tileset_identity_test_%lu",
             (unsigned long)GetCurrentProcessId());
    snprintf(json_path, sizeof(json_path), "%s\\data.json", folder);
    snprintf(map_path, sizeof(map_path), "%s\\data.map", folder);
    snprintf(asset_path, sizeof(asset_path), "%s\\tiles.png", folder);
    DeleteFileA(json_path);
    DeleteFileA(map_path);
    DeleteFileA(asset_path);
    RemoveDirectoryA(folder);
    CHECK(CreateDirectoryA(folder, NULL) != 0);
    build_one_room_map(0, map_text, sizeof(map_text));
    CHECK(write_fixture_bytes(json_path, json_plain, strlen(json_plain)));
    CHECK(write_fixture_bytes(map_path, map_text, strlen(map_text)));
    CHECK(write_png_header_fixture(asset_path, 128, 256, 0x51));
    memset(&asset_info, 0, sizeof(asset_info));
    CHECK(GetFileAttributesExA(asset_path, GetFileExInfoStandard,
                               &asset_info) != 0);
    original_write_time = asset_info.ftLastWriteTime;
    key_plain[0] = '\0';
    key_native[0] = '\0';
    key_asset[0] = '\0';

    custom_maps_init();
    needed = custom_maps_build_manifest_json(NULL, 0);
    CHECK(needed > 0);
    if (needed > 0) manifest = (char*)malloc((size_t)needed + 1u);
    CHECK(manifest != NULL);
    if (manifest) {
        CHECK(custom_maps_build_manifest_json(manifest,
                                              (size_t)needed + 1u) == needed);
        CHECK(manifest_key_for_prefix(manifest,
                                      "custom:tileset_identity_test:",
                                      key_plain, sizeof(key_plain)));
    }
    free(manifest);
    manifest = NULL;
    CHECK(custom_maps_selector_for_key(key_plain, &selector));
    CHECK(custom_maps_content_view_open(selector, &view) == 1);
    CHECK(view.content_tile_count == 0);
    CHECK(view.native_layout == 0);
    CHECK(view.default_sheet_sprite_count == 128);
    CHECK(view.default_sheet_key[0] != '\0');

    CHECK(write_fixture_bytes(json_path, json_native, strlen(json_native)));
    needed = custom_maps_build_manifest_json(NULL, 0);
    CHECK(needed > 0);
    if (needed > 0) manifest = (char*)malloc((size_t)needed + 1u);
    CHECK(manifest != NULL);
    if (manifest) {
        CHECK(custom_maps_build_manifest_json(manifest,
                                              (size_t)needed + 1u) == needed);
        CHECK(manifest_key_for_prefix(manifest,
                                      "custom:tileset_identity_test:",
                                      key_native, sizeof(key_native)));
        CHECK(strcmp(key_plain, key_native) != 0);
    }
    free(manifest);
    manifest = NULL;
    CHECK(custom_maps_selector_for_key(key_native, &selector));
    CHECK(custom_maps_content_view_open(selector, &view) == 1);
    CHECK(view.native_layout == 1);
    CHECK(view.default_sheet_sprite_count == 128);
    snprintf(sheet_key_native, sizeof(sheet_key_native), "%s",
             view.default_sheet_key);
    CHECK(custom_maps_content_sheet_for_key(sheet_key_native,
                                            sheet_path, sizeof(sheet_path),
                                            sheet_sha, sizeof(sheet_sha)));
    CHECK(sheet_path[0] != '\0');
    CHECK(strlen(sheet_sha) == CONTENT_SHA256_HEX_SIZE - 1u);

    CHECK(write_png_header_fixture(asset_path, 128, 256, 0x52));
    /* Default sheets participate in byte-sensitive reload even with no tiles. */
    CHECK(restore_file_write_time(asset_path, &original_write_time));
    needed = custom_maps_build_manifest_json(NULL, 0);
    CHECK(needed > 0);
    if (needed > 0) manifest = (char*)malloc((size_t)needed + 1u);
    CHECK(manifest != NULL);
    if (manifest) {
        CHECK(custom_maps_build_manifest_json(manifest,
                                              (size_t)needed + 1u) == needed);
        CHECK(manifest_key_for_prefix(manifest,
                                      "custom:tileset_identity_test:",
                                      key_asset, sizeof(key_asset)));
        CHECK(strcmp(key_native, key_asset) != 0);
    }
    free(manifest);
    CHECK(custom_maps_selector_for_key(key_asset, &selector));
    CHECK(custom_maps_content_view_open(selector, &view) == 1);
    CHECK(view.native_layout == 1);
    CHECK(strcmp(sheet_key_native, view.default_sheet_key) != 0);

    custom_maps_shutdown();
    DeleteFileA(json_path);
    DeleteFileA(map_path);
    DeleteFileA(asset_path);
    CHECK(RemoveDirectoryA(folder) != 0);
}

static void test_map_script_validation_and_safety(void) {
    typedef BOOLEAN (WINAPI *CreateSymbolicLinkAFn)(LPCSTR, LPCSTR, DWORD);
    const size_t oversize = 256u * 1024u + 1u;
    const char* folder = "build\\map_script_package_test";
    const char* script_path = "build\\map_script_package_test\\map.lua";
    const char* target_path = "build\\map_script_package_test\\target.lua";
    static const char valid_script[] = "return {}\n";
    static const char invalid_script[] = "function broken(\n";
    static const char invalid_binding_script[] =
        "map.on_enter(\"?\", function(object, tile) end)\n";
    static const char binding_variant_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"symbol_test\","
        "\"name\":\"Symbols\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
        "\"tileset\":{\"tiles\":[{\"id\":\"moss_variant\","
        "\"symbol\":\"$\",\"native_glyph\":\"x\","
        "\"sprite_sheet\":\"builtin:tiles\"}]}}";
    static const char binding_order_a_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"symbol_test\","
        "\"name\":\"Symbols\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
        "\"tileset\":{\"tiles\":["
        "{\"id\":\"moss\",\"symbol\":\"$\",\"native_glyph\":\"x\","
        "\"sprite_sheet\":\"builtin:tiles\"},"
        "{\"id\":\"ember\",\"symbol\":\"%\",\"native_glyph\":\"x\","
        "\"sprite_sheet\":\"builtin:tiles\"}]}}";
    static const char binding_order_b_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"symbol_test\","
        "\"name\":\"Symbols\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
        "\"tileset\":{\"tiles\":["
        "{\"id\":\"ember\",\"symbol\":\"%\",\"native_glyph\":\"x\","
        "\"sprite_sheet\":\"builtin:tiles\"},"
        "{\"id\":\"moss\",\"symbol\":\"$\",\"native_glyph\":\"x\","
        "\"sprite_sheet\":\"builtin:tiles\"}]}}";
    char map_text[1024];
    char target_full[MAX_PATH];
    char expected_script_full[MAX_PATH];
    char expected_script_sha256[CONTENT_SHA256_HEX_SIZE];
    unsigned char* huge = NULL;
    CustomMapValidationSummary summary;
    uint64_t original_script_id = 0;
    uint64_t ordered_script_id = 0;
    HMODULE kernel32;
    CreateSymbolicLinkAFn create_symbolic_link = NULL;

    build_one_room_map(0, map_text, sizeof(map_text));
    DeleteFileA(script_path);
    DeleteFileA(target_path);
    RemoveDirectoryA(script_path);
    RemoveDirectoryA(folder);
    CreateDirectoryA("build", NULL);
    CHECK(CreateDirectoryA(folder, NULL) != 0);

    CHECK(write_fixture_bytes(script_path, valid_script,
                              sizeof(valid_script) - 1u));
    CHECK(GetFullPathNameA(script_path, (DWORD)sizeof(expected_script_full),
                           expected_script_full, NULL) > 0);
    CHECK(content_registry_sha256_bytes(valid_script,
                                        sizeof(valid_script) - 1u,
                                        NULL, expected_script_sha256,
                                        NULL, 0));
    CHECK(custom_maps_validate_package_text("script_test", folder,
                                            v2_builtin_json(), map_text,
                                            &summary));
    CHECK(summary.has_script == 1);
    CHECK(summary.script_size == sizeof(valid_script) - 1u);
    CHECK(summary.script_id != 0);
    original_script_id = summary.script_id;
    CHECK(_stricmp(summary.script_full_path, expected_script_full) == 0);
    CHECK(strcmp(summary.script_sha256, expected_script_sha256) == 0);
    CHECK(custom_maps_validate_package_text("script_test", folder,
                                            binding_variant_json, map_text,
                                            &summary));
    CHECK(summary.script_id != 0);
    CHECK(summary.script_id != original_script_id);
    CHECK(custom_maps_validate_package_text("script_test", folder,
                                            binding_order_a_json, map_text,
                                            &summary));
    ordered_script_id = summary.script_id;
    CHECK(ordered_script_id != 0);
    CHECK(custom_maps_validate_package_text("script_test", folder,
                                            binding_order_b_json, map_text,
                                            &summary));
    CHECK(summary.script_id != 0);
    CHECK(summary.script_id != ordered_script_id);

    CHECK(write_fixture_bytes(script_path, invalid_script,
                              sizeof(invalid_script) - 1u));
    CHECK(!custom_maps_validate_package_text("script_test", folder,
                                             v2_builtin_json(), map_text,
                                             &summary));
    CHECK(summary.error_count > 0);

    CHECK(write_fixture_bytes(script_path, invalid_binding_script,
                              sizeof(invalid_binding_script) - 1u));
    CHECK(!custom_maps_validate_package_text("script_test", folder,
                                             v2_builtin_json(), map_text,
                                             &summary));
    CHECK(summary.error_count > 0);

    CHECK(write_fixture_bytes(script_path, valid_script,
                              sizeof(valid_script) - 1u));
    CHECK(!custom_maps_validate_package_text("script_test", folder,
                                             v1_json(), map_text, &summary));
    CHECK(summary.error_count > 0);

    huge = (unsigned char*)malloc(oversize);
    CHECK(huge != NULL);
    if (huge) {
        memset(huge, '-', oversize);
        CHECK(write_fixture_bytes(script_path, huge, oversize));
        CHECK(!custom_maps_validate_package_text("script_test", folder,
                                                 v2_builtin_json(), map_text,
                                                 &summary));
        CHECK(summary.error_count > 0);
    }
    free(huge);

    DeleteFileA(script_path);
    CHECK(CreateDirectoryA(script_path, NULL) != 0);
    CHECK(!custom_maps_validate_package_text("script_test", folder,
                                             v2_builtin_json(), map_text,
                                             &summary));
    CHECK(summary.error_count > 0);
    CHECK(RemoveDirectoryA(script_path) != 0);

    CHECK(write_fixture_bytes(target_path, valid_script,
                              sizeof(valid_script) - 1u));
    kernel32 = GetModuleHandleA("kernel32.dll");
    if (kernel32) {
        create_symbolic_link = (CreateSymbolicLinkAFn)(uintptr_t)
            GetProcAddress(kernel32, "CreateSymbolicLinkA");
    }
    if (create_symbolic_link &&
        GetFullPathNameA(target_path, (DWORD)sizeof(target_full),
                         target_full, NULL) > 0 &&
        create_symbolic_link(script_path, target_full, 0x2u)) {
        CHECK(!custom_maps_validate_package_text("script_test", folder,
                                                 v2_builtin_json(), map_text,
                                                 &summary));
        CHECK(summary.error_count > 0);
        CHECK(DeleteFileA(script_path) != 0);
    }

    DeleteFileA(script_path);
    DeleteFileA(target_path);
    CHECK(RemoveDirectoryA(folder) != 0);
}

static void test_map_script_online_identity(void) {
    static const char json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"script_identity_test\","
        "\"name\":\"Script identity test\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]}}";
    static const char script_a[] = "-- package identity A\nreturn {}\n";
    static const char script_b[] = "-- package identity B\nreturn {}\n";
    char folder[MAX_PATH];
    char json_path[MAX_PATH * 2];
    char map_path[MAX_PATH * 2];
    char script_path[MAX_PATH * 2];
    char map_text[1024];
    char key_a[160];
    char key_after_invalid[160];
    char key_b[160];
    char invalid_script[sizeof(script_a)];
    char* manifest = NULL;
    WIN32_FILE_ATTRIBUTE_DATA script_info;
    FILETIME original_write_time;
    int needed;

    CHECK(sizeof(script_a) == sizeof(script_b));
    memset(invalid_script, ' ', sizeof(invalid_script) - 1u);
    memcpy(invalid_script, "function broken(", strlen("function broken("));
    invalid_script[sizeof(invalid_script) - 1u] = '\0';
    snprintf(folder, sizeof(folder), "maps\\v2_script_identity_test_%lu",
             (unsigned long)GetCurrentProcessId());
    snprintf(json_path, sizeof(json_path), "%s\\data.json", folder);
    snprintf(map_path, sizeof(map_path), "%s\\data.map", folder);
    snprintf(script_path, sizeof(script_path), "%s\\map.lua", folder);
    DeleteFileA(json_path);
    DeleteFileA(map_path);
    DeleteFileA(script_path);
    RemoveDirectoryA(folder);
    CHECK(CreateDirectoryA(folder, NULL) != 0);
    build_one_room_map(0, map_text, sizeof(map_text));
    CHECK(write_fixture_bytes(json_path, json, strlen(json)));
    CHECK(write_fixture_bytes(map_path, map_text, strlen(map_text)));
    CHECK(write_fixture_bytes(script_path, script_a, sizeof(script_a) - 1u));
    CHECK(GetFileAttributesExA(script_path, GetFileExInfoStandard,
                               &script_info) != 0);
    original_write_time = script_info.ftLastWriteTime;

    key_a[0] = '\0';
    key_after_invalid[0] = '\0';
    key_b[0] = '\0';
    custom_maps_init();
    needed = custom_maps_build_manifest_json(NULL, 0);
    CHECK(needed > 0);
    if (needed > 0) manifest = (char*)malloc((size_t)needed + 1u);
    CHECK(manifest != NULL);
    if (manifest) {
        CHECK(custom_maps_build_manifest_json(manifest,
                                              (size_t)needed + 1u) == needed);
        CHECK(manifest_key_for_prefix(manifest,
                                      "custom:script_identity_test:",
                                      key_a, sizeof(key_a)));
    }
    free(manifest);
    manifest = NULL;

    CHECK(write_fixture_bytes(script_path, invalid_script,
                              sizeof(invalid_script) - 1u));
    CHECK(restore_file_write_time(script_path, &original_write_time));
    needed = custom_maps_build_manifest_json(NULL, 0);
    CHECK(needed > 0);
    if (needed > 0) manifest = (char*)malloc((size_t)needed + 1u);
    CHECK(manifest != NULL);
    if (manifest) {
        CHECK(custom_maps_build_manifest_json(manifest,
                                              (size_t)needed + 1u) == needed);
        CHECK(manifest_key_for_prefix(manifest,
                                      "custom:script_identity_test:",
                                      key_after_invalid,
                                      sizeof(key_after_invalid)));
        CHECK(strcmp(key_a, key_after_invalid) == 0);
    }
    free(manifest);
    manifest = NULL;

    CHECK(write_fixture_bytes(script_path, script_b, sizeof(script_b) - 1u));
    /* Prove the reload signature follows bytes, not just size/timestamp. */
    CHECK(restore_file_write_time(script_path, &original_write_time));
    needed = custom_maps_build_manifest_json(NULL, 0);
    CHECK(needed > 0);
    if (needed > 0) manifest = (char*)malloc((size_t)needed + 1u);
    CHECK(manifest != NULL);
    if (manifest) {
        CHECK(custom_maps_build_manifest_json(manifest,
                                              (size_t)needed + 1u) == needed);
        CHECK(manifest_key_for_prefix(manifest,
                                      "custom:script_identity_test:",
                                      key_b, sizeof(key_b)));
        CHECK(strcmp(key_a, key_b) != 0);
    }
    free(manifest);
    custom_maps_shutdown();
    DeleteFileA(json_path);
    DeleteFileA(map_path);
    DeleteFileA(script_path);
    CHECK(RemoveDirectoryA(folder) != 0);
}

int main(void) {
    content_registry_shutdown();
    test_v1_compatibility();
    test_in_memory_v1_preview();
    test_native_room_spawn_budget();
    test_v2_symbolic_builtin();
    test_v2_whole_symbol_override();
    test_repository_runtime_fixture();
    test_v2_hard_failures();
    test_external_asset_hash();
    test_tileset_defaults_and_native_layout();
    test_external_asset_online_identity();
    test_tileset_default_online_identity_and_view();
    test_map_script_validation_and_safety();
    test_map_script_online_identity();
    content_registry_shutdown();
    if (g_failures != 0) {
        fprintf(stderr, "%d custom map v2 test(s) failed\n", g_failures);
        return 1;
    }
    puts("custom_maps_v2_test: all checks passed");
    return 0;
}
