#include <windows.h>

#include <math.h>
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

static int has_lower_hex_signature(const char* key) {
    const char* signature = key ? strrchr(key, ':') : NULL;
    size_t i;
    if (!signature || strlen(++signature) != 32u) return 0;
    for (i = 0; i < 32u; i++) {
        char c = signature[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return 0;
    }
    return 1;
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

static void build_one_room_map_at(char symbol, int symbol_row, int symbol_col,
                                  char* out, size_t out_size) {
    size_t position = 0;
    int row;
    int written = snprintf(out, out_size, "; parser test\n\n[center]\n");
    if (written < 0) return;
    position = (size_t)written;
    for (row = 0; row < 12 && position < out_size; row++) {
        char cells[34];
        memset(cells, ' ', 33);
        cells[33] = '\0';
        if (row == symbol_row && symbol && symbol_col >= 0 && symbol_col < 33) {
            cells[symbol_col] = symbol;
        }
        written = snprintf(out + position, out_size - position, "\"%s\"\n", cells);
        if (written < 0) return;
        position += (size_t)written;
    }
}

static void build_one_room_map(char symbol, char* out, size_t out_size) {
    build_one_room_map_at(symbol, 0, 0, out, out_size);
}

static void build_one_room_floor_map(char* out, size_t out_size) {
    size_t position = 0;
    int row;
    int written = snprintf(out, out_size, "; spawn floor test\n\n[center]\n");
    if (written < 0) return;
    position = (size_t)written;
    for (row = 0; row < 12 && position < out_size; ++row) {
        char cells[34];
        memset(cells, row == 11 ? '@' : ' ', 33);
        cells[33] = '\0';
        written = snprintf(out + position, out_size - position,
                           "\"%s\"\n", cells);
        if (written < 0) return;
        position += (size_t)written;
    }
}

static void build_two_room_query_map(char* out, size_t out_size) {
    size_t position = 0;
    const char* names[] = { "left", "center" };
    int room;
    int row;
    for (room = 0; room < 2 && position < out_size; ++room) {
        int written = snprintf(out + position, out_size - position,
                               "%s[%s]\n", room ? "\n" : "", names[room]);
        if (written < 0 || (size_t)written >= out_size - position) return;
        position += (size_t)written;
        for (row = 0; row < 12 && position < out_size; ++row) {
            char cells[34];
            memset(cells, ' ', 33);
            cells[33] = '\0';
            if (row == 2) cells[room ? 3 : 2] = room ? '$' : '@';
            written = snprintf(out + position, out_size - position,
                               "\"%s\"\n", cells);
            if (written < 0 || (size_t)written >= out_size - position) return;
            position += (size_t)written;
        }
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

static void append_variable_room(char* out, size_t out_size, size_t* position,
                                 const char* id, int width, int height,
                                 int mark_x, int mark_y, char mark) {
    int row;
    int written;
    if (!out || !position || !id || width < 1 || width > 128 || height < 1) return;
    written = snprintf(out + *position, out_size - *position, "[%s]\n", id);
    if (written < 0 || (size_t)written >= out_size - *position) return;
    *position += (size_t)written;
    for (row = 0; row < height && *position < out_size; ++row) {
        char cells[129];
        memset(cells, ' ', (size_t)width);
        cells[width] = '\0';
        if (row == mark_y && mark_x >= 0 && mark_x < width) cells[mark_x] = mark;
        written = snprintf(out + *position, out_size - *position, "\"%s\"\n", cells);
        if (written < 0 || (size_t)written >= out_size - *position) return;
        *position += (size_t)written;
    }
    if (*position + 1u < out_size) out[(*position)++] = '\n';
    if (*position < out_size) out[*position] = '\0';
}

static void append_variable_floor_room(char* out, size_t out_size,
                                       size_t* position, const char* id,
                                       int width, int height) {
    int row;
    int written;
    if (!out || !position || !id || width < 1 || width > 128 || height < 2)
        return;
    written = snprintf(out + *position, out_size - *position, "[%s]\n", id);
    if (written < 0 || (size_t)written >= out_size - *position) return;
    *position += (size_t)written;
    for (row = 0; row < height && *position < out_size; ++row) {
        char cells[129];
        memset(cells, row == height - 1 ? '@' : ' ', (size_t)width);
        cells[width] = '\0';
        written = snprintf(out + *position, out_size - *position,
                           "\"%s\"\n", cells);
        if (written < 0 || (size_t)written >= out_size - *position) return;
        *position += (size_t)written;
    }
    if (*position + 1u < out_size) out[(*position)++] = '\n';
    if (*position < out_size) out[*position] = '\0';
}

static void build_variable_two_room_map(char* out, size_t out_size) {
    size_t position = 0;
    if (!out || out_size == 0) return;
    out[0] = '\0';
    append_variable_floor_room(out, out_size, &position, "center", 37, 16);
    append_variable_room(out, out_size, &position, "outer", 20, 9,
                         0, 8, 'x');
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

static void test_native_tentacle_headroom(void) {
    char map_text[1024];
    CustomMapValidationSummary summary;

    build_one_room_map_at('T', 2, 16, map_text, sizeof(map_text));
    CHECK(!custom_maps_validate_package_text("legacy", ".", v1_json(),
                                             map_text, &summary));
    build_one_room_map_at('T', 3, 16, map_text, sizeof(map_text));
    CHECK(custom_maps_validate_package_text("legacy", ".", v1_json(),
                                            map_text, &summary));

    build_one_room_map_at('t', 1, 16, map_text, sizeof(map_text));
    CHECK(!custom_maps_validate_package_text("legacy", ".", v1_json(),
                                             map_text, &summary));
    build_one_room_map_at('t', 2, 16, map_text, sizeof(map_text));
    CHECK(custom_maps_validate_package_text("legacy", ".", v1_json(),
                                            map_text, &summary));

    build_one_room_map_at('s', 0, 16, map_text, sizeof(map_text));
    CHECK(!custom_maps_validate_package_text("legacy", ".", v1_json(),
                                             map_text, &summary));
    build_one_room_map_at('s', 1, 16, map_text, sizeof(map_text));
    CHECK(custom_maps_validate_package_text("legacy", ".", v1_json(),
                                            map_text, &summary));
}

static void test_in_memory_v1_preview(void) {
    static const char json[] =
        "{\"format\":\"eggnogg-map/v1\",\"id\":\"preview_test\","
        "\"name\":\"Preview Test\",\"author\":\"Greggnogg\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\",\"outer\"]},"
        "\"rooms\":{\"center\":{\"opponent_spawn\":\"always\"},"
        "\"outer\":{\"opponent_spawn\":\"default\"}}}";
    static const char v2_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"preview_test\","
        "\"name\":\"Preview Test\",\"author\":\"Greggnogg\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]}}";
    char one_room[1024], map_text[2048];
    char error[256];
    char* manifest = NULL;
    CustomMapContentView view;
    int selector = -1;
    int total_before;
    int needed;
    CustomMapValidationSummary exported;

    build_one_room_map(0, one_room, sizeof(one_room));
    snprintf(map_text, sizeof(map_text), "%s[outer]\n%s", one_room,
             strstr(one_room, "[center]\n") + strlen("[center]\n"));
    CHECK(custom_maps_validate_package_text("preview_test", ".", json,
                                            map_text, &exported));
    CHECK(exported.final_opponent_spawn[0] == 2 &&
          exported.final_opponent_spawn[1] == 1 &&
          exported.final_opponent_spawn[2] == 2);
    custom_maps_init();
    total_before = custom_maps_total_selectors();
    CHECK(custom_maps_install_preview_text(json, map_text, &selector,
                                           error, sizeof(error)));
    CHECK(error[0] == '\0');
    CHECK(selector == total_before);
    CHECK(custom_maps_total_selectors() == total_before + 1);
    CHECK(custom_maps_content_view_open(selector, &view) == 1);
    CHECK(view.format_version == 1);
    CHECK(view.source_room_count == 2);
    selector = custom_maps_test_pin_folder("_greggnogg_preview");
    CHECK(selector >= 0);
    CHECK(custom_maps_opponent_spawn_policy(selector, 0) ==
          exported.final_opponent_spawn[0]);
    CHECK(custom_maps_opponent_spawn_policy(selector, 1) ==
          exported.final_opponent_spawn[1]);
    CHECK(custom_maps_opponent_spawn_policy(selector, 2) ==
          exported.final_opponent_spawn[2]);
    custom_maps_test_pin_folder(NULL);

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

static void test_v2_preview_folder(void) {
    char token[33],relative[96],folder[MAX_PATH],path[MAX_PATH+32],map[1024],error[256];int selector=-1;
    CustomMapValidationSummary export_summary;
    const char* json=
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"spawn_budget\","
        "\"name\":\"Spawn budget\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
        "\"particles\":[{\"id\":\"preview_mote\",\"name\":\"Preview mote\","
        "\"visual\":{\"sprite_sheet\":\"builtin:misc\",\"sprite_index\":0,"
        "\"frame_count\":1,\"frame_ticks\":1,\"tint\":[1,0.5,0.25,0.8],"
        "\"scale_x\":1,\"scale_y\":1,\"end_tint\":[0.1,0.2,0.3,0],"
        "\"end_scale_x\":0,\"end_scale_y\":2,\"start_rotation\":-45,"
        "\"end_rotation\":90,\"interpolation\":\"ease_in_out\"},"
        "\"lifetime_ticks\":90,"
        "\"fade_in_ticks\":5,\"fade_out_ticks\":15}],"
        "\"ambiances\":[{\"id\":\"preview_glow\",\"name\":\"Preview glow\","
        "\"native_ambient\":\"none\",\"emitters\":[{"
        "\"particle\":\"preview_mote\",\"count\":12,"
        "\"area\":{\"x\":0,\"y\":0,\"width\":528,\"height\":192},"
        "\"velocity_x\":{\"min\":-0.1,\"max\":0.1},"
        "\"velocity_y\":{\"min\":0.1,\"max\":0.3},"
        "\"acceleration_x\":0,\"acceleration_y\":0,"
        "\"rotation_speed\":{\"min\":-0.5,\"max\":0.5},"
        "\"particle_layer\":1,\"blend\":\"additive\","
        "\"mirror_with_room\":true}]}],"
        "\"defaults\":{\"room\":{\"ambient\":\"preview_glow\",\"opponent_spawn\":\"never\"}}}";
    const char* entities="{\"schema\":2,\"capacity\":8,\"types\":[{\"key\":\"demo:preview\",\"regions\":[],\"visual\":{\"sheet\":\"builtin:tiles\",\"sprite\":4}}],\"placements\":[{\"name\":\"first\",\"type\":\"demo:preview\",\"room\":\"center\",\"x\":48,\"y\":64,\"vx\":1}]}";
    const char* source="entity.on_update('demo:preview',function(h) map.state.seen=true end)";
    snprintf(token,sizeof(token),"%08lx%08lx%08x%08x",(unsigned long)GetCurrentProcessId(),(unsigned long)GetTickCount(),1u,2u);
    snprintf(relative,sizeof(relative),"_greggnogg_previews/%s",token);snprintf(folder,sizeof(folder),"maps/%s",relative);
    CreateDirectoryA("maps/_greggnogg_previews",NULL);CHECK(CreateDirectoryA(folder,NULL)!=0);
    build_one_room_map(0,map,sizeof(map));
    snprintf(path,sizeof(path),"%s/data.json",folder);CHECK(write_fixture_bytes(path,json,strlen(json)));
    snprintf(path,sizeof(path),"%s/data.map",folder);CHECK(write_fixture_bytes(path,map,strlen(map)));
    snprintf(path,sizeof(path),"%s/entities.json",folder);CHECK(write_fixture_bytes(path,entities,strlen(entities)));
    snprintf(path,sizeof(path),"%s/map.lua",folder);CHECK(write_fixture_bytes(path,source,strlen(source)));
    CHECK(custom_maps_validate_package_text("spawn_budget",folder,json,map,&export_summary));
    CHECK(export_summary.final_opponent_spawn[0]==2);
    custom_maps_init();int before=custom_maps_total_selectors();
    CHECK(custom_maps_install_preview_folder(token,&selector,error,sizeof(error)));CHECK(selector==before);
    CHECK(custom_maps_total_selectors()==before+1);
    selector=custom_maps_test_pin_folder(relative);CHECK(selector>=0);
    CHECK(custom_maps_opponent_spawn_policy(selector,0)==export_summary.final_opponent_spawn[0]);
    {
        const MapAmbianceCatalog* catalog=NULL;
        uint16_t ambiance_index=UINT16_MAX;
        int source_room=-1,mirror_room=-1;
        CHECK(custom_maps_pinned_ambiance(selector,0,&catalog,&ambiance_index,
                                          &source_room,&mirror_room)==1);
        CHECK(catalog!=NULL&&catalog->particle_count==1&&
              catalog->ambiance_count==1&&ambiance_index==0&&
              catalog->ambiances[0].emitters[0].count==12&&
              catalog->particles[0].end_scale_x_q==0&&
              catalog->particles[0].end_scale_y_q==512&&
              catalog->particles[0].end_rgba==UINT32_C(0x1a334d00)&&
              catalog->particles[0].start_rotation_q==-45*256&&
              catalog->particles[0].end_rotation_q==90*256&&
              catalog->particles[0].transition_flags==7&&
              catalog->particles[0].interpolation==
                  MAP_PARTICLE_INTERPOLATION_EASE_IN_OUT&&
              source_room==0&&mirror_room==0);
    }
    CHECK(custom_maps_activate_script_for_selector(selector,NULL,error,sizeof(error)));CHECK(map_script_has_entities());
    CHECK(map_script_dispatch_tick(error,sizeof(error)));uint32_t cursor=0;EntityRenderView render;
    CHECK(map_script_entity_render_next(&cursor,&render)&&render.x==49*256);
    int needed=custom_maps_build_manifest_json(NULL,0);char* manifest=malloc((size_t)needed+1u);CHECK(manifest!=NULL);
    if(manifest){custom_maps_build_manifest_json(manifest,(size_t)needed+1u);CHECK(strstr(manifest,"spawn_budget")==NULL);free(manifest);}
    uint64_t generation=custom_maps_generation();
    CHECK(write_fixture_bytes(path,"error('bad preview')",20));
    CHECK(!custom_maps_install_preview_folder(token,&selector,error,sizeof(error)));CHECK(custom_maps_generation()==generation);
    CHECK(!custom_maps_install_preview_folder("../bad",&selector,error,sizeof(error)));
    CHECK(custom_maps_preview_session_in_use(token));
    custom_maps_deactivate_script();custom_maps_clear_preview();
    CHECK(custom_maps_preview_session_in_use(token)); /* Engine still holds the retired map. */
    custom_maps_test_pin_folder(NULL);
    CHECK(!custom_maps_preview_session_in_use(token));
    CHECK(!custom_maps_preview_session_in_use("../bad"));
    CHECK(custom_maps_total_selectors()==before);
    {
        char original[MAX_PATH],original_path[MAX_PATH+32];snprintf(original,sizeof(original),"maps/preview_original_%s",token);
        CHECK(CreateDirectoryA(original,NULL)!=0);
        snprintf(original_path,sizeof(original_path),"%s/data.json",original);CHECK(write_fixture_bytes(original_path,json,strlen(json)));
        snprintf(original_path,sizeof(original_path),"%s/data.map",original);CHECK(write_fixture_bytes(original_path,map,strlen(map)));
        (void)custom_maps_generation();CHECK(custom_maps_total_selectors()==before+1);
        snprintf(path,sizeof(path),"%s/map.lua",folder);CHECK(write_fixture_bytes(path,source,strlen(source)));
        CHECK(custom_maps_install_preview_folder(token,&selector,error,sizeof(error)));
        CHECK(custom_maps_total_selectors()==before+1); /* Same authored ID is overridden, not duplicated. */
        custom_maps_clear_preview();CHECK(custom_maps_total_selectors()==before+1);
        needed=custom_maps_build_manifest_json(NULL,0);manifest=malloc((size_t)needed+1u);CHECK(manifest!=NULL);
        if(manifest){custom_maps_build_manifest_json(manifest,(size_t)needed+1u);CHECK(strstr(manifest,"spawn_budget")!=NULL);free(manifest);}
        CHECK(DeleteFileA(original_path)!=0);snprintf(original_path,sizeof(original_path),"%s/data.json",original);CHECK(DeleteFileA(original_path)!=0);CHECK(RemoveDirectoryA(original)!=0);
    }
    const char* names[]={"data.json","data.map","entities.json","map.lua"};
    for(size_t i=0;i<4;i++){snprintf(path,sizeof(path),"%s/%s",folder,names[i]);CHECK(DeleteFileA(path)!=0);}CHECK(RemoveDirectoryA(folder)!=0);
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

static void test_pinned_world_tile_query(void) {
    static const char json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"world_tile_query\","
        "\"name\":\"World tile query\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"left\",\"center\"]},"
        "\"tileset\":{\"tiles\":[{\"id\":\"moss\",\"symbol\":\"$\","
        "\"native_glyph\":\"x\",\"sprite_sheet\":\"builtin:tiles\"}]}}";
    char folder_id[96];
    char folder[MAX_PATH];
    char json_path[MAX_PATH + 32];
    char map_path[MAX_PATH + 32];
    char map_text[2048];
    char reference[8];
    int selector;

    snprintf(folder_id, sizeof(folder_id), "world_tile_query_%lu",
             (unsigned long)GetCurrentProcessId());
    snprintf(folder, sizeof(folder), "maps/%s", folder_id);
    snprintf(json_path, sizeof(json_path), "%s/data.json", folder);
    snprintf(map_path, sizeof(map_path), "%s/data.map", folder);
    DeleteFileA(json_path);
    DeleteFileA(map_path);
    RemoveDirectoryA(folder);
    CHECK(CreateDirectoryA(folder, NULL) != 0);
    build_two_room_query_map(map_text, sizeof(map_text));
    CHECK(write_fixture_bytes(json_path, json, strlen(json)));
    CHECK(write_fixture_bytes(map_path, map_text, strlen(map_text)));

    custom_maps_init();
    selector = custom_maps_test_pin_folder(folder_id);
    CHECK(selector >= 0);
    /* Final room order is center, left, mirrored center. */
    CHECK(custom_maps_pinned_tile_at_world(selector, 3 * 16 + 8,
                                           2 * 16 + 8,
                                           reference, sizeof(reference)) == 1);
    CHECK(strcmp(reference, "$") == 0);
    CHECK(custom_maps_pinned_tile_at_world(selector, 33 * 16 + 2 * 16 + 8,
                                           2 * 16 + 8,
                                           reference, sizeof(reference)) == 1);
    CHECK(strcmp(reference, "@") == 0);
    CHECK(custom_maps_pinned_tile_at_world(selector,
                                           2 * 33 * 16 + 29 * 16 + 8,
                                           2 * 16 + 8,
                                           reference, sizeof(reference)) == 1);
    CHECK(strcmp(reference, "$") == 0);
    CHECK(custom_maps_pinned_tile_at_world(selector, 4 * 16 + 8,
                                           2 * 16 + 8,
                                           reference, sizeof(reference)) == 1);
    CHECK(strcmp(reference, " ") == 0);
    CHECK(custom_maps_pinned_tile_at_world(selector, -1, 0,
                                           reference, sizeof(reference)) == 0);
    CHECK(custom_maps_pinned_tile_at_world(selector, 0, 12 * 16,
                                           reference, sizeof(reference)) == 0);
    CHECK(custom_maps_pinned_tile_at_world(selector + 1, 0, 0,
                                           reference, sizeof(reference)) == -1);
    CHECK(custom_maps_pinned_tile_at_world(selector, 0, 0,
                                           reference, 1) == -1);
    custom_maps_test_pin_folder(NULL);
    custom_maps_shutdown();
    CHECK(DeleteFileA(json_path) != 0);
    CHECK(DeleteFileA(map_path) != 0);
    CHECK(RemoveDirectoryA(folder) != 0);
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

static void test_repository_ambiance_fixture(void) {
    const char* folder = "maps\\ambiance_demo";
    char* json_text = read_fixture_text("maps\\ambiance_demo\\data.json");
    char* map_text = read_fixture_text("maps\\ambiance_demo\\data.map");
    CustomMapValidationSummary summary;
    CHECK(json_text != NULL);
    CHECK(map_text != NULL);
    if (json_text && map_text) {
        CHECK(custom_maps_validate_package_text("ambiance_demo", folder,
                                                json_text, map_text, &summary));
        CHECK(summary.particle_definition_count == 2);
        CHECK(summary.ambiance_definition_count == 1);
        CHECK(summary.ambiance_lane_count == 45);
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

static void test_v2_custom_ambiances(void) {
    static const char valid_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"weather_test\","
        "\"name\":\"Weather test\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
        "\"particles\":[{\"id\":\"snowflake\",\"name\":\"Snowflake\","
        "\"visual\":{\"sprite_sheet\":\"builtin:misc\",\"sprite_index\":0,"
        "\"frame_count\":2,\"frame_ticks\":4,\"tint\":[0.8,0.9,1,0.75],"
        "\"scale_x\":1.5,\"scale_y\":1.5},\"lifetime_ticks\":180,"
        "\"fade_in_ticks\":12,\"fade_out_ticks\":24}],"
        "\"ambiances\":[{\"id\":\"snow\",\"name\":\"Snow\","
        "\"native_ambient\":\"dust\",\"emitters\":["
        "{\"particle\":\"snowflake\",\"count\":40,\"shape\":\"ellipse\","
        "\"motion_interpolation\":\"ease_in\","
        "\"area\":{\"x\":0,\"y\":-16,\"width\":528,\"height\":16},"
        "\"velocity_x\":{\"min\":-0.2,\"max\":0.2},"
        "\"velocity_y\":{\"min\":0.4,\"max\":0.8},"
        "\"rotation_speed\":{\"min\":-1,\"max\":1},"
        "\"particle_layer\":1,\"blend\":\"alpha\","
        "\"mirror_with_room\":true},"
        "{\"particle\":\"snowflake\",\"count\":8,\"shape\":\"line\","
        "\"area\":{\"x\":32,\"y\":0,\"width\":464,\"height\":192},"
        "\"acceleration_y\":0.01,\"particle_layer\":3,"
        "\"blend\":\"additive\"}]}],"
        "\"defaults\":{\"room\":{\"ambient\":\"snow\"}}}";
    static const char unknown_particle_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"bad_weather\","
        "\"name\":\"Bad\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
        "\"particles\":[],\"ambiances\":[{\"id\":\"rain\","
        "\"name\":\"Rain\",\"emitters\":[{\"particle\":\"missing\","
        "\"area\":{\"x\":0,\"y\":0,\"width\":528,\"height\":192}}]}]}";
    static const char invalid_blend_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"bad_blend\","
        "\"name\":\"Bad\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
        "\"particles\":[{\"id\":\"dot\",\"name\":\"Dot\","
        "\"visual\":{\"sprite_sheet\":\"builtin:misc\"}}],"
        "\"ambiances\":[{\"id\":\"rain\",\"name\":\"Rain\","
        "\"emitters\":[{\"particle\":\"dot\",\"blend\":\"multiply\","
        "\"area\":{\"x\":0,\"y\":0,\"width\":528,\"height\":192}}]}]}";
    static const char invalid_builtin_range_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"bad_range\","
        "\"name\":\"Bad\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
        "\"particles\":[{\"id\":\"dot\",\"name\":\"Dot\","
        "\"visual\":{\"sprite_sheet\":\"builtin:misc\","
        "\"sprite_index\":63,\"frame_count\":2}}],"
        "\"ambiances\":[]}";
    static const char invalid_transition_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"bad_transition\","
        "\"name\":\"Bad\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
        "\"particles\":[{\"id\":\"dot\",\"name\":\"Dot\","
        "\"visual\":{\"sprite_sheet\":\"builtin:misc\","
        "\"end_tint\":[1,0,0,2],\"interpolation\":\"bounce\"}}],"
        "\"ambiances\":[]}";
    static const char lane_overflow_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"too_busy\","
        "\"name\":\"Too busy\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
        "\"particles\":[{\"id\":\"dot\",\"name\":\"Dot\","
        "\"visual\":{\"sprite_sheet\":\"builtin:misc\"}}],"
        "\"ambiances\":[{\"id\":\"storm\",\"name\":\"Storm\","
        "\"emitters\":[{\"particle\":\"dot\",\"count\":300,"
        "\"area\":{\"x\":0,\"y\":0,\"width\":528,\"height\":192}},"
        "{\"particle\":\"dot\",\"count\":300,"
        "\"area\":{\"x\":0,\"y\":0,\"width\":528,\"height\":192}}]}]}";
    static const char v1_catalog_json[] =
        "{\"format\":\"eggnogg-map/v1\",\"id\":\"legacy_weather\","
        "\"name\":\"Legacy\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]},"
        "\"particles\":[],\"ambiances\":[]}";
    char map_text[1024];
    char invalid_shape_json[sizeof(valid_json)];
    char invalid_motion_json[sizeof(valid_json)];
    CustomMapValidationSummary summary;

    build_one_room_map(0, map_text, sizeof(map_text));
    CHECK(custom_maps_validate_package_text("weather_test", ".", valid_json,
                                            map_text, &summary));
    CHECK(summary.particle_definition_count == 1);
    CHECK(summary.ambiance_definition_count == 1);
    CHECK(summary.ambiance_lane_count == 48);
    memcpy(invalid_shape_json, valid_json, sizeof(valid_json));
    {
        char* shape = strstr(invalid_shape_json, "ellipse");
        CHECK(shape != NULL);
        if (shape) memcpy(shape, "unknown", 7u);
    }
    CHECK(!custom_maps_validate_package_text("weather_test", ".",
                                             invalid_shape_json,
                                             map_text, &summary));
    memcpy(invalid_motion_json, valid_json, sizeof(valid_json));
    {
        char* motion = strstr(invalid_motion_json, "ease_in");
        CHECK(motion != NULL);
        if (motion) memcpy(motion, "unknown", 7u);
    }
    CHECK(!custom_maps_validate_package_text("weather_test", ".",
                                             invalid_motion_json,
                                             map_text, &summary));
    CHECK(!custom_maps_validate_package_text("bad_weather", ".",
                                             unknown_particle_json,
                                             map_text, &summary));
    CHECK(summary.error_count > 0);
    CHECK(!custom_maps_validate_package_text("bad_blend", ".",
                                             invalid_blend_json,
                                             map_text, &summary));
    CHECK(summary.error_count > 0);
    CHECK(!custom_maps_validate_package_text("bad_range", ".",
                                             invalid_builtin_range_json,
                                             map_text, &summary));
    CHECK(summary.error_count > 0);
    CHECK(!custom_maps_validate_package_text("bad_transition", ".",
                                             invalid_transition_json,
                                             map_text, &summary));
    CHECK(summary.error_count >= 2);
    CHECK(!custom_maps_validate_package_text("too_busy", ".",
                                             lane_overflow_json,
                                             map_text, &summary));
    CHECK(summary.error_count > 0);
    CHECK(!custom_maps_validate_package_text("legacy_weather", ".",
                                             v1_catalog_json,
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
    static const char json_room_native[] =
        "{\"format\":\"eggnogg-map/v2\","
        "\"id\":\"tileset_identity_test\","
        "\"name\":\"Tileset identity test\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"vanilla_33x12\",\"order\":[\"center\",\"left\"]},"
        "\"tileset\":{\"sprite_sheet\":\"tiles.png\",\"native_layout\":false,"
        "\"sheets\":[{\"sprite_sheet\":\"alt.png\"}]},"
        "\"defaults\":{\"room\":{\"native_tileset\":\"tiles.png\"}},"
        "\"rooms\":{\"center\":{\"native_tileset\":\"alt.png\"}}}";
    static const char json_graph_native[] =
        "{\"format\":\"eggnogg-map/v2\","
        "\"id\":\"tileset_identity_test\","
        "\"name\":\"Tileset identity test\",\"author\":\"Test\","
        "\"rules\":{\"mode\":\"swords\",\"round_end_rooms\":\"any\"},"
        "\"layout\":{\"kind\":\"room_graph\",\"room_format\":\"variable_cells\","
        "\"start\":\"left_node\",\"nodes\":["
        "{\"id\":\"left_node\",\"room\":\"left\",\"x\":0,\"y\":0},"
        "{\"id\":\"center_node\",\"room\":\"center\",\"x\":33,\"y\":0,"
        "\"overrides\":{\"native_tileset\":\"alt.png\"}}],"
        "\"connections\":[{\"from\":\"left_node\",\"from_side\":\"right\","
        "\"from_offset\":0,\"to\":\"center_node\",\"to_side\":\"left\","
        "\"to_offset\":0,\"span\":12}]},"
        "\"tileset\":{\"sprite_sheet\":\"tiles.png\",\"native_layout\":false,"
        "\"sheets\":[{\"sprite_sheet\":\"alt.png\"}]},"
        "\"defaults\":{\"room\":{\"native_tileset\":\"tiles.png\"}}}";
    char folder[MAX_PATH];
    char json_path[MAX_PATH * 2];
    char map_path[MAX_PATH * 2];
    char asset_path[MAX_PATH * 2];
    char alt_asset_path[MAX_PATH * 2];
    char map_text[1024];
    char key_plain[160];
    char key_native[160];
    char key_asset[160];
    char key_room[160];
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
    snprintf(alt_asset_path, sizeof(alt_asset_path), "%s\\alt.png", folder);
    DeleteFileA(json_path);
    DeleteFileA(map_path);
    DeleteFileA(asset_path);
    DeleteFileA(alt_asset_path);
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

    CHECK(write_png_header_fixture(alt_asset_path, 128, 256, 0x61));
    build_two_room_query_map(map_text, sizeof(map_text));
    {
        char* custom_symbol = strchr(map_text, '$');
        if (custom_symbol) *custom_symbol = '@';
    }
    CHECK(write_fixture_bytes(map_path, map_text, strlen(map_text)));
    CHECK(write_fixture_bytes(json_path, json_room_native,
                              strlen(json_room_native)));
    needed = custom_maps_build_manifest_json(NULL, 0);
    CHECK(needed > 0);
    if (needed > 0) manifest = (char*)malloc((size_t)needed + 1u);
    CHECK(manifest != NULL);
    if (manifest) {
        CHECK(custom_maps_build_manifest_json(manifest,
                                              (size_t)needed + 1u) == needed);
        CHECK(manifest_key_for_prefix(manifest,
                                      "custom:tileset_identity_test:",
                                      key_room, sizeof(key_room)));
        CHECK(strcmp(key_asset, key_room) != 0);
    }
    free(manifest);
    selector = custom_maps_test_pin_folder(strrchr(folder, '\\') + 1);
    CHECK(selector >= 0);
    {
        char resolved_sheet[128];
        int resolved_count = 0;
        char mirrored_sheet[128];
        int mirrored_count = 0;
        CHECK(custom_maps_pinned_native_tileset(
                  selector, 1, resolved_sheet, sizeof(resolved_sheet),
                  &resolved_count) == 1);
        CHECK(resolved_count == 128);
        CHECK(strcmp(resolved_sheet, view.default_sheet_key) != 0);
        CHECK(custom_maps_pinned_native_tileset(
                  selector, 0, mirrored_sheet, sizeof(mirrored_sheet),
                  &mirrored_count) == 1);
        CHECK(mirrored_count == 128);
        CHECK(strcmp(mirrored_sheet, view.default_sheet_key) == 0);
        mirrored_sheet[0] = '\0';
        CHECK(custom_maps_pinned_native_tileset(
                  selector, 2, mirrored_sheet, sizeof(mirrored_sheet),
                  &mirrored_count) == 1);
        CHECK(strcmp(mirrored_sheet, view.default_sheet_key) == 0);
    }
    custom_maps_test_pin_folder(NULL);

    CHECK(write_fixture_bytes(json_path, json_graph_native,
                              strlen(json_graph_native)));
    needed = custom_maps_build_manifest_json(NULL, 0);
    CHECK(needed > 0);
    selector = custom_maps_test_pin_folder(strrchr(folder, '\\') + 1);
    CHECK(selector >= 0);
    {
        char inherited_sheet[128];
        char instance_sheet[128];
        int inherited_count = 0;
        int instance_count = 0;
        CHECK(custom_maps_pinned_native_tileset(
                  selector, 0, inherited_sheet, sizeof(inherited_sheet),
                  &inherited_count) == 1);
        CHECK(custom_maps_pinned_native_tileset(
                  selector, 1, instance_sheet, sizeof(instance_sheet),
                  &instance_count) == 1);
        CHECK(inherited_count == 128 && instance_count == 128);
        CHECK(strcmp(inherited_sheet, instance_sheet) != 0);
    }
    custom_maps_test_pin_folder(NULL);

    custom_maps_shutdown();
    DeleteFileA(json_path);
    DeleteFileA(map_path);
    DeleteFileA(asset_path);
    DeleteFileA(alt_asset_path);
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

static void test_eggnogg_color(void) {
    const char* invalid[] = {"null", "[]", "[0,1]", "[0,1,0,1]",
        "[0,1,\"0\"]", "[0,-0.01,0]", "[1.000000001,0,0]", "[1e999,0,0]"};
    char json[2048], map_text[1024];
    CustomMapValidationSummary summary;
    const char* base = v1_json();
    size_t i;
    float untouched[3] = {0.25f, 0.5f, 0.75f};
    build_one_room_map('^', map_text, sizeof(map_text));
    CHECK(custom_maps_validate_package_text("legacy", ".", base, map_text, &summary));
    CHECK(!summary.has_eggnogg_color);
    snprintf(json, sizeof(json), "%.*s,\"rules\":{\"mode\":\"swords\",\"eggnogg_color\":[0,0.5,1]}}",
             (int)strlen(base) - 1, base);
    CHECK(custom_maps_validate_package_text("legacy", ".", json, map_text, &summary));
    CHECK(summary.has_eggnogg_color && summary.eggnogg_color[0] == 0.0f &&
          summary.eggnogg_color[1] == 0.5f && summary.eggnogg_color[2] == 1.0f);
    base = spawn_budget_v2_json();
    snprintf(json, sizeof(json), "%.*s,\"rules\":{\"mode\":\"swords\",\"eggnogg_color\":[0,0.5,1]}}",
             (int)strlen(base) - 1, base);
    CHECK(custom_maps_validate_package_text("legacy", ".", json, map_text, &summary));
    CHECK(summary.format_version == 2 && summary.has_eggnogg_color);
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        snprintf(json, sizeof(json), "%.*s,\"rules\":{\"mode\":\"swords\",\"eggnogg_color\":%s}}",
                 (int)strlen(base) - 1, base, invalid[i]);
        CHECK(!custom_maps_validate_package_text("legacy", ".", json, map_text, &summary));
    }
    CHECK(!custom_maps_pinned_eggnogg_color(-1, untouched));
    CHECK(untouched[0] == 0.25f && untouched[1] == 0.5f && untouched[2] == 0.75f);
}

static void test_opponent_spawn(void) {
    const char* invalid[] = {"null", "true", "0", "[]", "{}", "\"ALWAYS\"", "\"inherit\""};
    const char* policies[] = {"default", "always", "never"};
    char json[2048], map_text[1024];
    CustomMapValidationSummary summary;
    const char* bases[] = {v1_json(), spawn_budget_v2_json()};
    build_one_room_map('^', map_text, sizeof(map_text));
    for (size_t b = 0; b < 2; ++b) {
        const char* base = bases[b];
        CHECK(custom_maps_validate_package_text("legacy", ".", base, map_text, &summary));
        CHECK(summary.opponent_spawn[0] == 0);
        for (int policy = 0; policy < 3; ++policy) {
            snprintf(json, sizeof(json), "%.*s,\"defaults\":{\"room\":{\"opponent_spawn\":\"%s\"}}}",
                     (int)strlen(base) - 1, base, policies[policy]);
            CHECK(custom_maps_validate_package_text("legacy", ".", json, map_text, &summary));
            CHECK(summary.opponent_spawn[0] == policy);
            snprintf(json, sizeof(json), "%.*s,\"defaults\":{\"room\":{\"opponent_spawn\":\"never\"}},"
                "\"rooms\":{\"center\":{\"opponent_spawn\":\"%s\"}}}",
                (int)strlen(base) - 1, base, policies[policy]);
            CHECK(custom_maps_validate_package_text("legacy", ".", json, map_text, &summary));
            CHECK(summary.opponent_spawn[0] == policy);
        }
        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
            snprintf(json, sizeof(json), "%.*s,\"defaults\":{\"room\":{\"opponent_spawn\":%s}}}",
                (int)strlen(base) - 1, base, invalid[i]);
            CHECK(!custom_maps_validate_package_text("legacy", ".", json, map_text, &summary));
            snprintf(json, sizeof(json), "%.*s,\"rooms\":{\"center\":{\"opponent_spawn\":%s}}}",
                (int)strlen(base) - 1, base, invalid[i]);
            CHECK(!custom_maps_validate_package_text("legacy", ".", json, map_text, &summary));
        }
    }
    {
        char multi_map[4096];
        const char* rows = strchr(strstr(map_text, "[center]"), '\n') + 1;
        snprintf(multi_map, sizeof(multi_map), "[center]\n%s[inner]\n%s[outer]\n%s", rows, rows, rows);
        const char* three = "{\"format\":\"eggnogg-map/v1\",\"id\":\"legacy\",\"name\":\"Policy\",\"author\":\"Test\","
            "\"layout\":{\"kind\":\"mirrored_source_rooms\",\"room_format\":\"vanilla_33x12\",\"order\":[\"center\",\"inner\",\"outer\"]},"
            "\"defaults\":{\"room\":{\"opponent_spawn\":\"never\"}},"
            "\"rooms\":{\"center\":{\"opponent_spawn\":\"default\"},\"outer\":{\"opponent_spawn\":\"always\"}}}";
        CHECK(custom_maps_validate_package_text("legacy", ".", three, multi_map, &summary));
        CHECK(summary.source_room_count == 3);
        CHECK(summary.final_opponent_spawn[0] == 1 && summary.final_opponent_spawn[1] == 2 &&
              summary.final_opponent_spawn[2] == 0 && summary.final_opponent_spawn[3] == 2 &&
              summary.final_opponent_spawn[4] == 1);
        /* Inherited and explicit native defaults both retain the traditional
         * no-opponent outer rooms. Explicit Always above still wins. */
        snprintf(json, sizeof(json),
                 "{\"format\":\"eggnogg-map/v1\",\"id\":\"legacy\",\"name\":\"Policy\",\"author\":\"Test\","
                 "\"layout\":{\"kind\":\"mirrored_source_rooms\",\"room_format\":\"vanilla_33x12\","
                 "\"order\":[\"center\",\"inner\",\"outer\"]}}" );
        CHECK(custom_maps_validate_package_text("legacy", ".", json, multi_map, &summary));
        CHECK(summary.final_opponent_spawn[0] == 2 && summary.final_opponent_spawn[1] == 0 &&
              summary.final_opponent_spawn[2] == 0 && summary.final_opponent_spawn[3] == 0 &&
              summary.final_opponent_spawn[4] == 2);
        snprintf(json, sizeof(json),
                 "{\"format\":\"eggnogg-map/v1\",\"id\":\"legacy\",\"name\":\"Policy\",\"author\":\"Test\","
                 "\"layout\":{\"kind\":\"mirrored_source_rooms\",\"room_format\":\"vanilla_33x12\","
                 "\"order\":[\"center\",\"inner\",\"outer\"]},"
                 "\"defaults\":{\"room\":{\"opponent_spawn\":\"default\"}},"
                 "\"rooms\":{\"outer\":{\"opponent_spawn\":\"default\"}}}");
        CHECK(custom_maps_validate_package_text("legacy", ".", json, multi_map, &summary));
        CHECK(summary.final_opponent_spawn[0] == 2 && summary.final_opponent_spawn[4] == 2);
    }
    CHECK(custom_maps_opponent_spawn_policy(0, 0) == 0);
    CHECK(custom_maps_opponent_spawn_policy(-1, 0) == 0);
    CHECK(custom_maps_opponent_spawn_policy(999, 0) == 0);
}

static void test_spawn_markers(void) {
    char json[4096], map_text[1024];
    CustomMapValidationSummary summary;
    const char* base = spawn_budget_v2_json();
    build_one_room_floor_map(map_text, sizeof(map_text));
    snprintf(json, sizeof(json), "%.*s,\"rooms\":{\"center\":{\"spawn\":{"
        "\"players\":{\"1\":{\"x\":4,\"y\":11,\"facing\":\"right\"}},"
        "\"markers\":[{\"x\":4,\"y\":11,\"kind\":\"allow_p1\"},"
        "{\"x\":28,\"y\":11,\"kind\":\"allow_p2\"},"
        "{\"x\":16,\"y\":11,\"kind\":\"deny\"}]}}}}",
        (int)strlen(base) - 1, base);
    CHECK(custom_maps_validate_package_text("spawn_budget", ".", json, map_text, &summary));
    snprintf(json, sizeof(json), "%.*s,\"rooms\":{\"center\":{\"spawn\":{"
        "\"markers\":[{\"x\":4,\"y\":11,\"kind\":\"sometimes\"}]}}}}",
        (int)strlen(base) - 1, base);
    CHECK(!custom_maps_validate_package_text("spawn_budget", ".", json, map_text, &summary));
    snprintf(json, sizeof(json), "%.*s,\"rooms\":{\"center\":{\"spawn\":{"
        "\"markers\":[{\"x\":40,\"y\":11,\"kind\":\"allow\"}]}}}}",
        (int)strlen(base) - 1, base);
    CHECK(!custom_maps_validate_package_text("spawn_budget", ".", json, map_text, &summary));
    snprintf(json, sizeof(json), "%.*s,\"rooms\":{\"center\":{\"spawn\":{"
        "\"markers\":[{\"x\":4,\"y\":10,\"kind\":\"allow\"}]}}}}",
        (int)strlen(base) - 1, base);
    CHECK(!custom_maps_validate_package_text("spawn_budget", ".", json, map_text, &summary));
    snprintf(json, sizeof(json), "%.*s,\"rooms\":{\"center\":{\"spawn\":{"
        "\"players\":{\"2\":{\"x\":4,\"y\":10,\"facing\":\"left\"}}}}}}",
        (int)strlen(base) - 1, base);
    CHECK(!custom_maps_validate_package_text("spawn_budget", ".", json, map_text, &summary));
}

static void test_variable_room_metadata_and_bounds(void) {
    static const char json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"variable_room_fixture\","
        "\"name\":\"Variable rooms\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"mirrored_source_rooms\","
        "\"room_format\":\"variable_cells\",\"order\":[\"center\",\"outer\"]}}";
    char token[33], relative[96], folder[MAX_PATH], path[MAX_PATH + 32];
    char map_text[8192], error[256], glyph[8];
    CustomMapContentView view;
    CustomMapValidationSummary summary;
    int selector = -1, room = -1, start = -1, top = -1, width = -1, height = -1;
    int exit_room = -1, exit_side = -1, exit_offset = -1, connection = -1;
    float spawn_x, spawn_y;

    build_variable_two_room_map(map_text, sizeof(map_text));
    CHECK(custom_maps_validate_package_text("variable_room_fixture", ".", json,
                                            map_text, &summary));
    snprintf(token, sizeof(token), "%08lx%08lx%08x%08x",
             (unsigned long)GetCurrentProcessId(), (unsigned long)GetTickCount(),
             3u, 4u);
    snprintf(relative, sizeof(relative), "_greggnogg_previews/%s", token);
    snprintf(folder, sizeof(folder), "maps/%s", relative);
    CreateDirectoryA("maps/_greggnogg_previews", NULL);
    CHECK(CreateDirectoryA(folder, NULL) != 0);
    snprintf(path, sizeof(path), "%s/data.json", folder);
    CHECK(write_fixture_bytes(path, json, strlen(json)));
    snprintf(path, sizeof(path), "%s/data.map", folder);
    CHECK(write_fixture_bytes(path, map_text, strlen(map_text)));

    custom_maps_init();
    CHECK(custom_maps_install_preview_folder(token, &selector, error, sizeof(error)));
    selector = custom_maps_test_pin_folder(relative);
    CHECK(selector >= 0);
    CHECK(custom_maps_pinned_content_view(selector, &view) == 1);
    CHECK(view.variable_rooms == 1 && view.source_room_count == 2);
    CHECK(custom_maps_variable_room_count(selector) == 3);
    CHECK(custom_maps_start_room(selector) == 1);
    CHECK(view.room_width[0] == 37 && view.room_height[0] == 16);
    CHECK(view.room_width[1] == 20 && view.room_height[1] == 9);
    CHECK(view.final_room_count == 3 && view.graph_start_room == 1 &&
          view.connection_count == 2 && view.layout_width == 77 &&
          view.layout_height == 16);
    CHECK(view.final_source_room[0] == 1 && view.final_x[0] == 0 &&
          view.final_y[0] == 0 && view.final_mirror_x[0] == 0);
    CHECK(view.final_source_room[1] == 0 && view.final_x[1] == 20 &&
          view.final_y[1] == 0 && view.final_mirror_x[1] == 0);
    CHECK(view.final_source_room[2] == 1 && view.final_x[2] == 57 &&
          view.final_y[2] == 0 && view.final_mirror_x[2] == 1);
    CHECK(custom_maps_room_exit(selector, 1, 0, 5, &exit_room, &exit_side,
                                &exit_offset, &connection) == 1);
    CHECK(exit_room == 0 && exit_side == 1 && exit_offset == 5 &&
          connection == 0);
    CHECK(custom_maps_room_exit(selector, 1, 1, 8, &exit_room, &exit_side,
                                &exit_offset, &connection) == 1);
    CHECK(exit_room == 2 && exit_side == 0 && exit_offset == 8 &&
          connection == 1);
    CHECK(custom_maps_room_exit(selector, 1, 1, 9, &exit_room, &exit_side,
                                &exit_offset, &connection) == 0);
    CHECK(exit_room == -1 && exit_side == -1 && exit_offset == -1 &&
          connection == -1);

    CHECK(custom_maps_variable_room_bounds_for_index(selector, 0, &start, &width,
                                                     &height) == 1);
    CHECK(start == 0 && width == 20 * 16 && height == 9 * 16);
    CHECK(custom_maps_variable_room_bounds_for_index(selector, 1, &start, &width,
                                                     &height) == 1);
    CHECK(start == 20 * 16 && width == 37 * 16 && height == 16 * 16);
    CHECK(custom_maps_variable_room_bounds_for_index(selector, 2, &start, &width,
                                                     &height) == 1);
    CHECK(start == (20 + 37) * 16 && width == 20 * 16 && height == 9 * 16);
    CHECK(custom_maps_variable_room_bounds(selector, (20 + 5) * 16 + 1,
                                           &room, &start, &width, &height) == 1);
    CHECK(room == 1 && start == 20 * 16 && width == 37 * 16 && height == 16 * 16);
    CHECK(custom_maps_variable_room_at(selector, (20 + 5) * 16 + 1,
                                       15 * 16 + 1, &room, &start, &top,
                                       &width, &height) == 1);
    CHECK(room == 1 && start == 20 * 16 && top == 0 &&
          width == 37 * 16 && height == 16 * 16);
    CHECK(custom_maps_variable_room_at(selector, 5 * 16 + 1,
                                       10 * 16 + 1, &room, NULL, NULL,
                                       NULL, NULL) == -1);
    CHECK(custom_maps_variable_room_bounds_2d_for_index(
              selector, 2, &start, &top, &width, &height) == 1);
    CHECK(start == (20 + 37) * 16 && top == 0 &&
          width == 20 * 16 && height == 9 * 16);

    CHECK(custom_maps_pinned_tile_at_world(selector, 1.0, 8 * 16 + 1.0,
                                           glyph, sizeof(glyph)) == 1);
    CHECK(strcmp(glyph, "x") == 0);
    CHECK(custom_maps_pinned_tile_at_world(selector, (20 + 37 + 19) * 16 + 1.0,
                                           8 * 16 + 1.0, glyph, sizeof(glyph)) == 1);
    CHECK(strcmp(glyph, "x") == 0);
    CHECK(custom_maps_pinned_tile_at_world(selector, (20 + 36) * 16 + 1.0,
                                           15 * 16 + 1.0, glyph, sizeof(glyph)) == 1);
    CHECK(strcmp(glyph, "@") == 0);

    /* The safe-floor pass must cover authored columns beyond native
     * find_good_spot's hardcoded 1..31 search window. */
    spawn_x = (float)((20 + 35) * 16 + 8);
    spawn_y = 8.0f;
    CHECK(custom_maps_adjust_spawn_position(selector, 1, 0,
                                            &spawn_x, &spawn_y) == 1);
    CHECK(spawn_x == (float)((20 + 35) * 16 + 8));
    CHECK(spawn_y == 14.0f * 16.0f);

    custom_maps_clear_preview();
    custom_maps_test_pin_folder(NULL);
    CHECK(custom_maps_variable_room_count(selector) == 0);
    custom_maps_shutdown();
    snprintf(path, sizeof(path), "%s/data.json", folder); DeleteFileA(path);
    snprintf(path, sizeof(path), "%s/data.map", folder); DeleteFileA(path);
    RemoveDirectoryA(folder);
}

static void test_room_graph_manifest_and_transitions(void) {
    static const char symmetric_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"room_graph_fixture\","
        "\"name\":\"Symmetric graph\",\"author\":\"Test\","
        "\"rules\":{\"mode\":\"swords\",\"round_end_rooms\":\"any\"},"
        "\"layout\":{\"kind\":\"room_graph\",\"room_format\":\"variable_cells\","
        "\"start\":\"center\",\"nodes\":["
        "{\"id\":\"left_1\",\"room\":\"outer\",\"x\":0,\"y\":0},"
        "{\"id\":\"center\",\"room\":\"center\",\"x\":20,\"y\":0},"
        "{\"id\":\"right_1\",\"room\":\"outer\",\"x\":57,\"y\":0,\"mirror_x\":true}],"
        "\"connections\":["
        "{\"from\":\"left_1\",\"from_side\":\"right\",\"from_offset\":0,\"to\":\"center\",\"to_side\":\"left\",\"to_offset\":0,\"span\":9,\"players\":\"both\",\"focus\":\"go\"},"
        "{\"from\":\"center\",\"from_side\":\"right\",\"from_offset\":0,\"to\":\"right_1\",\"to_side\":\"left\",\"to_offset\":0,\"span\":9,\"players\":\"both\",\"focus\":\"go\"}]}}";
    static const char reordered_symmetric_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"room_graph_fixture\","
        "\"name\":\"Reordered symmetric graph\",\"author\":\"Test\","
        "\"rules\":{\"mode\":\"swords\",\"round_end_rooms\":\"any\"},"
        "\"layout\":{\"kind\":\"room_graph\",\"room_format\":\"variable_cells\","
        "\"start\":\"right_1\",\"nodes\":["
        "{\"id\":\"center\",\"room\":\"center\",\"x\":20,\"y\":0},"
        "{\"id\":\"right_1\",\"room\":\"outer\",\"x\":57,\"y\":0,\"mirror_x\":true},"
        "{\"id\":\"left_1\",\"room\":\"outer\",\"x\":0,\"y\":0}],"
        "\"connections\":["
        "{\"from\":\"left_1\",\"from_side\":\"right\",\"from_offset\":0,\"to\":\"center\",\"to_side\":\"left\",\"to_offset\":0,\"span\":9},"
        "{\"from\":\"center\",\"from_side\":\"right\",\"from_offset\":0,\"to\":\"right_1\",\"to_side\":\"left\",\"to_offset\":0,\"span\":9}]}}";
    static const char valid_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"room_graph_fixture\","
        "\"name\":\"Room graph\",\"author\":\"Test\","
        "\"rules\":{\"mode\":\"swords\",\"round_end_rooms\":\"any\"},"
        "\"rooms\":{\"center\":{\"spawn\":{\"players\":{\"1\":{\"x\":3,\"y\":15},\"2\":{\"x\":4,\"y\":15}},\"markers\":[{\"x\":8,\"y\":15,\"kind\":\"allow\"}]}}},"
        "\"layout\":{\"kind\":\"room_graph\",\"room_format\":\"variable_cells\","
        "\"start\":\"center_node\",\"nodes\":["
        "{\"id\":\"center_node\",\"room\":\"center\",\"x\":0,\"y\":0,\"overrides\":{\"spawn\":{\"players\":{\"1\":{\"x\":5,\"y\":15,\"facing\":\"left\"}},\"markers\":[{\"x\":7,\"y\":15,\"kind\":\"allow\"}]}}},"
        "{\"id\":\"lower_node\",\"room\":\"outer\",\"x\":10,\"y\":16,"
        "\"mirror_x\":true,\"appearance\":\"mirror\","
        "\"overrides\":{\"ambient\":\"bats\",\"opponent_spawn\":\"never\"}}],"
        "\"connections\":[{\"from\":\"center_node\",\"from_side\":\"bottom\","
        "\"from_offset\":10,\"to\":\"lower_node\",\"to_side\":\"top\","
        "\"to_offset\":0,\"span\":4,\"one_way\":false,"
        "\"players\":\"player2\",\"focus\":\"crossing\"}]}}";
    static const char invalid_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"room_graph_fixture\","
        "\"name\":\"Room graph\",\"author\":\"Test\","
        "\"rules\":{\"mode\":\"swords\",\"round_end_rooms\":\"any\"},"
        "\"layout\":{\"kind\":\"room_graph\",\"room_format\":\"variable_cells\","
        "\"start\":\"center_node\",\"nodes\":["
        "{\"id\":\"center_node\",\"room\":\"center\",\"x\":0,\"y\":0},"
        "{\"id\":\"lower_node\",\"room\":\"outer\",\"x\":10,\"y\":16}],"
        "\"connections\":[{\"from\":\"center_node\",\"from_side\":\"bottom\","
        "\"from_offset\":10,\"to\":\"lower_node\",\"to_side\":\"top\","
        "\"to_offset\":1,\"span\":4}]}}";
    static const char classic_graph_json[] =
        "{\"format\":\"eggnogg-map/v2\",\"id\":\"room_graph_fixture\","
        "\"name\":\"Room graph\",\"author\":\"Test\","
        "\"layout\":{\"kind\":\"room_graph\",\"room_format\":\"variable_cells\","
        "\"start\":\"center_node\",\"nodes\":["
        "{\"id\":\"center_node\",\"room\":\"center\",\"x\":0,\"y\":0},"
        "{\"id\":\"lower_node\",\"room\":\"outer\",\"x\":10,\"y\":16}],"
        "\"connections\":[{\"from\":\"center_node\",\"from_side\":\"bottom\","
        "\"from_offset\":10,\"to\":\"lower_node\",\"to_side\":\"top\","
        "\"to_offset\":0,\"span\":4}]}}";
    static const char graph_entities[] =
        "{\"schema\":2,\"capacity\":8,\"types\":[{\"key\":\"demo:orb\",\"regions\":[]}],"
        "\"placements\":[{\"name\":\"lower_orb\",\"type\":\"demo:orb\","
        "\"room\":\"outer\",\"instance\":\"lower_node\",\"x\":8,\"y\":12}]}";
    static const char ambiguous_graph_entities[] =
        "{\"schema\":2,\"capacity\":8,\"types\":[{\"key\":\"demo:orb\",\"regions\":[]}],"
        "\"placements\":[{\"name\":\"lower_orb\",\"type\":\"demo:orb\","
        "\"room\":\"outer\",\"x\":8,\"y\":12}]}";
    char token[33], relative[96], folder[MAX_PATH], path[MAX_PATH + 32];
    char map_text[8192], map_with_unused[8192], error[256];
    CustomMapValidationSummary summary;
    CustomMapContentView view;
    int selector = -1;
    int destination = -1;
    int connection = -1;
    int ambient = -1;
    int player_policy = -1;
    int focus_policy = -1;
    char invalid_policy_json[sizeof(valid_json)];
    char invalid_spawn_json[sizeof(valid_json)];
    char symmetric_always_json[sizeof(symmetric_json) + 96];
    char renamed_symmetric_json[sizeof(symmetric_json)];
    char* policy_value;
    build_variable_two_room_map(map_text, sizeof(map_text));

    CHECK(custom_maps_validate_package_text("room_graph_fixture", ".",
                                            symmetric_json, map_text, &summary));
    CHECK(summary.final_opponent_spawn[0] == 2 &&
          summary.final_opponent_spawn[1] == 0 &&
          summary.final_opponent_spawn[2] == 2);
    CHECK(custom_maps_validate_package_text("room_graph_fixture", ".",
                                            reordered_symmetric_json, map_text,
                                            &summary));
    CHECK(summary.final_opponent_spawn[0] == 0 &&
          summary.final_opponent_spawn[1] == 2 &&
          summary.final_opponent_spawn[2] == 2);
    snprintf(map_with_unused, sizeof(map_with_unused), "%s", map_text);
    {
        size_t position = strlen(map_with_unused);
        append_variable_room(map_with_unused, sizeof(map_with_unused),
                             &position, "unused", 8, 8, 0, 0, 'x');
    }
    CHECK(custom_maps_validate_package_text("room_graph_fixture", ".",
                                            reordered_symmetric_json,
                                            map_with_unused, &summary));
    CHECK(summary.source_room_count == 3 &&
          summary.final_opponent_spawn[1] == 2 &&
          summary.final_opponent_spawn[2] == 2);
    memcpy(renamed_symmetric_json, symmetric_json, sizeof(symmetric_json));
    for (char* name = renamed_symmetric_json;
         (name = strstr(name, "left_1")) != NULL; name += 6)
        memcpy(name, "west__", 6);
    for (char* name = renamed_symmetric_json;
         (name = strstr(name, "right_1")) != NULL; name += 7)
        memcpy(name, "east___", 7);
    CHECK(custom_maps_validate_package_text("room_graph_fixture", ".",
                                            renamed_symmetric_json, map_text,
                                            &summary));
    CHECK(summary.final_opponent_spawn[0] == 2 &&
          summary.final_opponent_spawn[2] == 2);
    snprintf(symmetric_always_json, sizeof(symmetric_always_json),
             "%.*s\"rooms\":{\"outer\":{\"opponent_spawn\":\"always\"}},%s",
             (int)(strstr(symmetric_json, "\"layout\"") - symmetric_json),
             symmetric_json, strstr(symmetric_json, "\"layout\""));
    CHECK(custom_maps_validate_package_text("room_graph_fixture", ".",
                                            symmetric_always_json, map_text,
                                            &summary));
    CHECK(summary.final_opponent_spawn[0] == 1 &&
          summary.final_opponent_spawn[2] == 1);

    CHECK(custom_maps_validate_package_text("room_graph_fixture", ".",
                                            valid_json, map_text, &summary));
    CHECK(summary.error_count == 0 && summary.source_room_count == 2 &&
          summary.room_graph == 1);
    CHECK(summary.final_room_count == 2 && summary.graph_start_room == 0 &&
          summary.connection_count == 1 && summary.layout_bounds_x == 0 &&
          summary.layout_bounds_y == 0 && summary.layout_width == 37 &&
          summary.layout_height == 25);
    CHECK(summary.final_opponent_spawn[0] == 0 &&
          summary.final_opponent_spawn[1] == 2);

    memcpy(invalid_spawn_json, valid_json, sizeof(valid_json));
    {
        char* point = strstr(invalid_spawn_json, "\"x\":5,\"y\":15");
        CHECK(point != NULL);
        if (point) {
            char* y = strstr(point, "15");
            if (y) memcpy(y, "99", 2u);
        }
    }
    CHECK(!custom_maps_validate_package_text("room_graph_fixture", ".",
                                             invalid_spawn_json, map_text,
                                             &summary));
    CHECK(summary.error_count > 0);
    memcpy(invalid_spawn_json, valid_json, sizeof(valid_json));
    {
        char* marker = strstr(invalid_spawn_json, "\"x\":7,\"y\":15");
        CHECK(marker != NULL);
        if (marker) {
            char* y = strstr(marker, "15");
            if (y) memcpy(y, "99", 2u);
        }
    }
    CHECK(!custom_maps_validate_package_text("room_graph_fixture", ".",
                                             invalid_spawn_json, map_text,
                                             &summary));
    CHECK(summary.error_count > 0);

    memcpy(invalid_policy_json, valid_json, sizeof(valid_json));
    policy_value = strstr(invalid_policy_json, "player2");
    CHECK(policy_value != NULL);
    if (policy_value) memcpy(policy_value, "winner!", 7u);
    CHECK(!custom_maps_validate_package_text("room_graph_fixture", ".",
                                             invalid_policy_json, map_text,
                                             &summary));
    CHECK(summary.error_count > 0);
    memcpy(invalid_policy_json, valid_json, sizeof(valid_json));
    policy_value = strstr(invalid_policy_json, "crossing");
    CHECK(policy_value != NULL);
    if (policy_value) memcpy(policy_value, "camera!!", 8u);
    CHECK(!custom_maps_validate_package_text("room_graph_fixture", ".",
                                             invalid_policy_json, map_text,
                                             &summary));
    CHECK(summary.error_count > 0);

    CHECK(!custom_maps_validate_package_text("room_graph_fixture", ".",
                                             invalid_json, map_text, &summary));
    CHECK(summary.error_count == 1 && summary.source_room_count == 2);
    CHECK(summary.final_room_count == 0 && summary.layout_width == 0 &&
          summary.layout_height == 0);

    CHECK(!custom_maps_validate_package_text("room_graph_fixture", ".",
                                             classic_graph_json, map_text,
                                             &summary));
    CHECK(summary.error_count == 1 && summary.room_graph == 1 &&
          summary.final_room_count == 0);

    snprintf(token, sizeof(token), "%08lx%08lx%08x%08x",
             (unsigned long)GetCurrentProcessId(), (unsigned long)GetTickCount(),
             7u, 8u);
    snprintf(relative, sizeof(relative), "_greggnogg_previews/%s", token);
    snprintf(folder, sizeof(folder), "maps/%s", relative);
    CreateDirectoryA("maps/_greggnogg_previews", NULL);
    CHECK(CreateDirectoryA(folder, NULL) != 0);
    snprintf(path, sizeof(path), "%s/data.json", folder);
    CHECK(write_fixture_bytes(path, valid_json, strlen(valid_json)));
    snprintf(path, sizeof(path), "%s/data.map", folder);
    CHECK(write_fixture_bytes(path, map_text, strlen(map_text)));
    snprintf(path, sizeof(path), "%s/entities.json", folder);
    CHECK(write_fixture_bytes(path, graph_entities, strlen(graph_entities)));
    CHECK(custom_maps_validate_package_text("room_graph_fixture", folder,
                                            valid_json, map_text, &summary));
    CHECK(write_fixture_bytes(path, ambiguous_graph_entities,
                              strlen(ambiguous_graph_entities)));
    CHECK(!custom_maps_validate_package_text("room_graph_fixture", folder,
                                             valid_json, map_text, &summary));
    CHECK(write_fixture_bytes(path, graph_entities, strlen(graph_entities)));
    custom_maps_init();
    CHECK(custom_maps_install_preview_folder(token, &selector, error,
                                             sizeof(error)));
    selector = custom_maps_test_pin_folder(relative);
    CHECK(selector >= 0);
    CHECK(custom_maps_pinned_content_view(selector, &view) == 1);
    CHECK(view.room_graph == 1 && custom_maps_uses_room_graph(selector) == 1);
    CHECK(view.final_source_room[0] == 0 && view.final_source_room[1] == 1);
    CHECK(custom_maps_pinned_room_definition(selector, 1, &destination,
                                              &connection) == 1);
    CHECK(destination == 1 && connection == 1);
    CHECK(custom_maps_opponent_spawn_policy(selector, 0) == 0);
    CHECK(custom_maps_opponent_spawn_policy(selector, 1) == 2);
    {
        float x = 0.0f, y = 0.0f;
        int facing = 0;
        CHECK(custom_maps_player_start_position(selector, 0, 0.0f,
                                                &x, &y, &facing) == 1);
        CHECK(fabsf(x - 88.0f) < 0.001f && fabsf(y - 224.0f) < 0.001f && facing == -1);
        CHECK(custom_maps_player_start_position(selector, 1, 0.0f,
                                                &x, &y, &facing) == 1);
        CHECK(fabsf(x - 72.0f) < 0.001f && fabsf(y - 224.0f) < 0.001f);
        x = 0.0f; y = 0.0f;
        CHECK(custom_maps_adjust_spawn_position(selector, 0, 0,
                                                &x, &y) == 1);
        CHECK(fabsf(x - 120.0f) < 0.001f && fabsf(y - 224.0f) < 0.001f);
    }
    CHECK(custom_maps_pinned_room_ambient_override(selector, 0, &ambient) == 0);
    CHECK(custom_maps_pinned_room_ambient_override(selector, 1, &ambient) == 1 &&
          ambient == 7);
    CHECK(custom_maps_pinned_room_definition(selector, 2, &destination,
                                              &connection) == -1);
    CHECK(custom_maps_room_connection_policy(selector, 0, &player_policy,
                                              &focus_policy) == 1);
    CHECK(player_policy == 2 && focus_policy == 1);
    CHECK(custom_maps_room_connection_policy(selector, 1, &player_policy,
                                              &focus_policy) == -1);
    destination = -1;
    connection = -1;
    CHECK(custom_maps_resolve_room_transition(
              selector, 0, 10 * 16 + 8, 16 * 16 - 2,
              10 * 16 + 8, 16 * 16, &destination, &connection) == 1);
    CHECK(destination == 1 && connection == 0);
    CHECK(custom_maps_resolve_room_transition(
              selector, 1, 10 * 16 + 8, 16 * 16 + 2,
              10 * 16 + 8, 16 * 16 - 1, &destination, &connection) == 1);
    CHECK(destination == 0 && connection == 0);
    CHECK(custom_maps_resolve_room_transition(
              selector, 0, 2 * 16 + 8, 16 * 16 - 2,
              2 * 16 + 8, 16 * 16, &destination, &connection) == 0);
    CHECK(destination == -1 && connection == -1);
    custom_maps_clear_preview();
    custom_maps_test_pin_folder(NULL);
    custom_maps_shutdown();
    snprintf(path, sizeof(path), "%s/entities.json", folder); DeleteFileA(path);
    snprintf(path, sizeof(path), "%s/data.json", folder); DeleteFileA(path);
    snprintf(path, sizeof(path), "%s/data.map", folder); DeleteFileA(path);
    RemoveDirectoryA(folder);
}

static void check_entity_map_tick_value(int expected) {
    char err[384];MapScriptSnapshot map;
    size_t size=map_script_content_snapshot_size();unsigned char* bytes=malloc(size);
    CHECK(bytes!=NULL);if(!bytes) return;
    CHECK(map_script_dispatch_tick(err,sizeof(err)));
    CHECK(map_script_dispatch_tick(err,sizeof(err)));
    CHECK(map_script_content_snapshot_save(bytes,size,err,sizeof(err)));
    memcpy(&map,bytes+MAP_SCRIPT_CONTENT_HEADER_BYTES,sizeof(map));
    CHECK(map.state_count==1 && map.state[0].number_value==expected);
    free(bytes);
}
static void test_entity_package_discovery(void) {
    static const char json[]="{\"format\":\"eggnogg-map/v2\",\"id\":\"entity_discovery\",\"name\":\"Entity discovery\",\"author\":\"Test\",\"layout\":{\"kind\":\"mirrored_source_rooms\",\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]}}";
    static const char entities[]="{\"schema\":1,\"capacity\":8,\"types\":[{\"key\":\"demo:orb\",\"regions\":[]}],\"placements\":[{\"name\":\"orb\",\"type\":\"demo:orb\",\"vx\":1}]}";
    static const char source[]="entity.on_update('demo:orb',function(h) map.state.seen=entity.get(h).x end)";
    char folder_id[96],folder[MAX_PATH],entity_path[MAX_PATH+32],script_path[MAX_PATH+32];
    char json_path[MAX_PATH+32],map_path[MAX_PATH+32],map_text[1024],err[384],changed[sizeof(entities)];
    char online_key_old[160]={0},online_key_new[160]={0},pinned_key[160]={0};
    CustomMapValidationSummary summary;uint64_t generation,old_script_id,new_script_id;
    WIN32_FILE_ATTRIBUTE_DATA info;int selector,needed;char* manifest;
    snprintf(folder_id,sizeof(folder_id),"entity_discovery_%lu",(unsigned long)GetCurrentProcessId());
    snprintf(folder,sizeof(folder),"maps\\%s",folder_id);
    snprintf(entity_path,sizeof(entity_path),"%s\\entities.json",folder);
    snprintf(script_path,sizeof(script_path),"%s\\map.lua",folder);
    snprintf(json_path,sizeof(json_path),"%s\\data.json",folder);
    snprintf(map_path,sizeof(map_path),"%s\\data.map",folder);
    CHECK(CreateDirectoryA(folder,NULL)!=0);
    build_one_room_map(0,map_text,sizeof(map_text));
    CHECK(write_fixture_bytes(entity_path,entities,strlen(entities)));
    CHECK(custom_maps_validate_package_text("entities",folder,json,map_text,&summary));
    CHECK(summary.has_entities && summary.has_script && summary.script_size==0 && summary.entity_size==strlen(entities));
    CHECK(!custom_maps_validate_package_text("entities",folder,v1_json(),map_text,&summary));
    {
        const char* room_json="{\"schema\":2,\"capacity\":8,\"types\":[{\"key\":\"demo:orb\",\"regions\":[]}],\"placements\":[{\"name\":\"pad\",\"type\":\"demo:orb\",\"room\":\"center\",\"x\":8,\"vx\":2}]}";
        CHECK(write_fixture_bytes(entity_path,room_json,strlen(room_json)));
        CHECK(custom_maps_validate_package_text("entities",folder,json,map_text,&summary));
        CHECK(write_fixture_bytes(entity_path,entities,strlen(entities)));
    }
    {
        const char* sheets[]={"builtin:tiles","builtin:typo","missing.png"};
        char visual_json[512];
        for(int i=0;i<3;i++) {
            snprintf(visual_json,sizeof(visual_json),"{\"schema\":1,\"capacity\":8,\"types\":[{\"key\":\"demo:orb\",\"regions\":[],\"visual\":{\"sheet\":\"%s\",\"sprite\":0}}],\"placements\":[]}",sheets[i]);
            CHECK(write_fixture_bytes(entity_path,visual_json,strlen(visual_json)));
            CHECK(custom_maps_validate_package_text("entities",folder,json,map_text,&summary)==(i==0));
        }
        {
            char png[MAX_PATH+32],map_json[1024];
            snprintf(png,sizeof(png),"%s\\visual.png",folder);
            CHECK(write_png_header_fixture(png,32,16,0x71));
            snprintf(map_json,sizeof(map_json),"%.*s,\"tileset\":{\"sprite_sheet\":\"visual.png\",\"cell_w\":16,\"cell_h\":16}}",(int)strlen(json)-1,json);
            for(int frames=2;frames<=3;frames++) {
                snprintf(visual_json,sizeof(visual_json),"{\"schema\":1,\"capacity\":8,\"types\":[{\"key\":\"demo:orb\",\"regions\":[],\"visual\":{\"sheet\":\"visual.png\",\"sprite\":0,\"frames\":%d}}],\"placements\":[]}",frames);
                CHECK(write_fixture_bytes(entity_path,visual_json,strlen(visual_json)));
                CHECK(custom_maps_validate_package_text("entities",folder,map_json,map_text,&summary)==(frames==2));
            }
            for(int first=1;first<=2;first++) {
                snprintf(visual_json,sizeof(visual_json),"{\"schema\":1,\"capacity\":8,\"types\":[{\"key\":\"demo:orb\",\"regions\":[],\"visual\":{\"sheet\":\"visual.png\",\"sprite\":0},\"animations\":[{\"name\":\"flash\",\"sprite\":%d}]}],\"placements\":[]}",first);
                CHECK(write_fixture_bytes(entity_path,visual_json,strlen(visual_json)));
                CHECK(custom_maps_validate_package_text("entities",folder,map_json,map_text,&summary)==(first==1));
            }
            snprintf(visual_json,sizeof(visual_json),"{\"schema\":1,\"capacity\":8,\"types\":[{\"key\":\"demo:orb\",\"regions\":[],\"visual\":{\"sheet\":\"visual.png\",\"sprite\":0}}],\"placements\":[]}");
            CHECK(write_fixture_bytes(entity_path,visual_json,strlen(visual_json)));
            for(int width=16;width>=8;width-=8) {
                snprintf(map_json,sizeof(map_json),"%.*s,\"tileset\":{\"sprite_sheet\":\"visual.png\",\"cell_w\":16,\"cell_h\":16,\"tiles\":[{\"id\":\"grid\",\"symbol\":\"$\",\"native_glyph\":\"x\",\"cell_w\":%d}]}}",(int)strlen(json)-1,json,width);
                CHECK(custom_maps_validate_package_text("entities",folder,map_json,map_text,&summary)==(width==16));
            }
            snprintf(map_json,sizeof(map_json),"%.*s,\"tileset\":{\"sheets\":[{\"sprite_sheet\":\"visual.png\",\"cell_w\":32,\"cell_h\":16}]}}",(int)strlen(json)-1,json);
            CHECK(custom_maps_validate_package_text("entities",folder,map_json,map_text,&summary));
            snprintf(map_json,sizeof(map_json),"%.*s,\"tileset\":{\"sheets\":[{\"sprite_sheet\":\"visual.png\",\"cell_w\":0}]}}",(int)strlen(json)-1,json);
            CHECK(!custom_maps_validate_package_text("entities",folder,map_json,map_text,&summary));
            {
                char odd_png[MAX_PATH+32];
                CHECK(write_fixture_bytes(entity_path,entities,strlen(entities)));
                snprintf(odd_png,sizeof(odd_png),"%s\\odd.png",folder);
                CHECK(write_png_header_fixture(odd_png,35,19,0x72));
                snprintf(map_json,sizeof(map_json),"%.*s,\"tileset\":{\"tiles\":[{\"id\":\"crop\",\"symbol\":\"$\",\"native_glyph\":\"x\",\"sprite_sheet\":\"odd.png\",\"cell_w\":16,\"cell_h\":16,\"source_x\":3,\"source_y\":3,\"source_w\":32,\"source_h\":16}]}}",(int)strlen(json)-1,json);
                CHECK(custom_maps_validate_package_text("entities",folder,map_json,map_text,&summary));
                snprintf(map_json,sizeof(map_json),"%.*s,\"tileset\":{\"tiles\":[{\"id\":\"crop\",\"symbol\":\"$\",\"native_glyph\":\"x\",\"sprite_sheet\":\"odd.png\",\"cell_w\":16,\"cell_h\":16,\"source_x\":3,\"source_y\":3,\"source_w\":31,\"source_h\":16}]}}",(int)strlen(json)-1,json);
                CHECK(!custom_maps_validate_package_text("entities",folder,map_json,map_text,&summary));
                CHECK(DeleteFileA(odd_png)!=0);
            }
            CHECK(DeleteFileA(png)!=0);
        }
        CHECK(write_fixture_bytes(entity_path,entities,strlen(entities)));
    }

    CHECK(write_fixture_bytes(entity_path,"{}",2));
    CHECK(!custom_maps_validate_package_text("entities",folder,json,map_text,&summary));
    CHECK(DeleteFileA(entity_path)!=0);
    CHECK(CreateDirectoryA(entity_path,NULL)!=0);
    CHECK(!custom_maps_validate_package_text("entities",folder,json,map_text,&summary));
    CHECK(RemoveDirectoryA(entity_path)!=0);
    CHECK(write_fixture_bytes(entity_path,entities,strlen(entities)));
    CHECK(write_fixture_bytes(script_path,source,strlen(source)));
    CHECK(write_fixture_bytes(json_path,json,strlen(json)));
    CHECK(write_fixture_bytes(map_path,map_text,strlen(map_text)));
    CHECK(custom_maps_validate_package_text("entities",folder,json,map_text,&summary));
    old_script_id=summary.script_id;
    CHECK(GetFileAttributesExA(entity_path,GetFileExInfoStandard,&info)!=0);
    generation=custom_maps_generation();
    needed=custom_maps_build_manifest_json(NULL,0);manifest=malloc((size_t)needed+1u);
    CHECK(manifest!=NULL);
    if(manifest) {
        custom_maps_build_manifest_json(manifest,(size_t)needed+1u);
        CHECK(manifest_key_for_prefix(manifest,"custom:entity_discovery:",online_key_old,sizeof(online_key_old)));
        CHECK(has_lower_hex_signature(online_key_old));
        CHECK(custom_maps_selector_for_key(online_key_old,&selector));
        free(manifest);
    }
    selector=custom_maps_test_pin_folder(folder_id);CHECK(selector>=0);
    CHECK(custom_maps_pinned_online_key(selector,pinned_key,sizeof(pinned_key))==1);
    CHECK(strcmp(pinned_key,online_key_old)==0);
    CHECK(custom_maps_activate_script_for_selector(selector,NULL,err,sizeof(err)));
    CHECK(map_script_has_entities() && map_script_network_admissible(err,sizeof(err)) && !err[0]);
    check_entity_map_tick_value(1);
    memcpy(changed,entities,sizeof(changed));strstr(changed,"\"vx\":1")[5]='2';
    CHECK(write_fixture_bytes(entity_path,changed,strlen(changed)));
    CHECK(restore_file_write_time(entity_path,&info.ftLastWriteTime));
    CHECK(custom_maps_generation()>generation);
    needed=custom_maps_build_manifest_json(NULL,0);manifest=malloc((size_t)needed+1u);
    CHECK(manifest!=NULL);
    if(manifest) {
        custom_maps_build_manifest_json(manifest,(size_t)needed+1u);
        CHECK(manifest_key_for_prefix(manifest,"custom:entity_discovery:",online_key_new,sizeof(online_key_new)));
        CHECK(has_lower_hex_signature(online_key_new));
        CHECK(strcmp(online_key_old,online_key_new)!=0);
        free(manifest);
    }
    CHECK(custom_maps_pinned_script_id(selector,&new_script_id)==1 && new_script_id==old_script_id);
    CHECK(custom_maps_pinned_online_key(selector,pinned_key,sizeof(pinned_key))==1);
    CHECK(strcmp(pinned_key,online_key_old)==0); /* Reload cannot mutate the pin. */
    CHECK(custom_maps_activate_script_for_selector(selector,NULL,err,sizeof(err)));
    check_entity_map_tick_value(1); /* Pinned old source survives registry replacement. */
    selector=custom_maps_test_pin_folder(folder_id);CHECK(selector>=0);
    CHECK(custom_maps_pinned_online_key(selector,pinned_key,sizeof(pinned_key))==1);
    CHECK(strcmp(pinned_key,online_key_new)==0);
    CHECK(custom_maps_pinned_script_id(selector,&new_script_id)==1 && new_script_id!=old_script_id);
    CHECK(custom_maps_activate_script_for_selector(selector,NULL,err,sizeof(err)));
    check_entity_map_tick_value(2);
    custom_maps_deactivate_script();custom_maps_test_pin_folder(NULL);
    CHECK(DeleteFileA(entity_path)!=0);CHECK(DeleteFileA(script_path)!=0);
    CHECK(DeleteFileA(json_path)!=0);CHECK(DeleteFileA(map_path)!=0);CHECK(RemoveDirectoryA(folder)!=0);
    (void)custom_maps_generation();
}

static void test_nested_zip_discovery(void) {
    static const char json[]="{\"format\":\"eggnogg-map/v2\",\"id\":\"nested_zip_fixture\",\"name\":\"Nested ZIP Fixture\",\"author\":\"Test\",\"layout\":{\"kind\":\"mirrored_source_rooms\",\"room_format\":\"vanilla_33x12\",\"order\":[\"center\"]}}";
    char outer[MAX_PATH],middle[MAX_PATH],folder[MAX_PATH],assets[MAX_PATH];
    char json_path[MAX_PATH],map_path[MAX_PATH],asset_json[MAX_PATH],asset_map[MAX_PATH],map_text[1024],key[160];
    char* manifest;int needed,selector=-1,total_before;uint64_t generation;
    (void)custom_maps_generation();total_before=custom_maps_total_selectors();
    snprintf(outer,sizeof(outer),"maps\\zip_wrapper_%lu",(unsigned long)GetCurrentProcessId());
    CHECK(snprintf(middle,sizeof(middle),"%s\\Extracted archive",outer)<(int)sizeof(middle));
    CHECK(snprintf(folder,sizeof(folder),"%s\\Map package",middle)<(int)sizeof(folder));
    CHECK(snprintf(assets,sizeof(assets),"%s\\assets",folder)<(int)sizeof(assets));
    CHECK(snprintf(json_path,sizeof(json_path),"%s\\data.json",folder)<(int)sizeof(json_path));
    CHECK(snprintf(map_path,sizeof(map_path),"%s\\data.map",folder)<(int)sizeof(map_path));
    CHECK(snprintf(asset_json,sizeof(asset_json),"%s\\data.json",assets)<(int)sizeof(asset_json));
    CHECK(snprintf(asset_map,sizeof(asset_map),"%s\\data.map",assets)<(int)sizeof(asset_map));
    CHECK(CreateDirectoryA(outer,NULL)!=0);CHECK(CreateDirectoryA(middle,NULL)!=0);
    CHECK(CreateDirectoryA(folder,NULL)!=0);CHECK(CreateDirectoryA(assets,NULL)!=0);
    build_one_room_map(0,map_text,sizeof(map_text));
    CHECK(write_fixture_bytes(json_path,json,strlen(json)));CHECK(write_fixture_bytes(map_path,map_text,strlen(map_text)));
    /* A valid package owns its descendants: this duplicate must not be scanned. */
    CHECK(write_fixture_bytes(asset_json,json,strlen(json)));CHECK(write_fixture_bytes(asset_map,map_text,strlen(map_text)));
    generation=custom_maps_generation();CHECK(custom_maps_total_selectors()==total_before+1);
    needed=custom_maps_build_manifest_json(NULL,0);manifest=malloc((size_t)needed+1u);CHECK(manifest!=NULL);
    if(manifest) {
        custom_maps_build_manifest_json(manifest,(size_t)needed+1u);
        CHECK(manifest_key_for_prefix(manifest,"custom:nested_zip_fixture:",key,sizeof(key)));
        CHECK(custom_maps_selector_for_key(key,&selector));free(manifest);
    }
    CHECK(DeleteFileA(asset_json)!=0);CHECK(DeleteFileA(asset_map)!=0);CHECK(RemoveDirectoryA(assets)!=0);
    CHECK(DeleteFileA(json_path)!=0);CHECK(DeleteFileA(map_path)!=0);CHECK(RemoveDirectoryA(folder)!=0);
    CHECK(RemoveDirectoryA(middle)!=0);CHECK(RemoveDirectoryA(outer)!=0);
    CHECK(custom_maps_generation()>generation);CHECK(custom_maps_total_selectors()==total_before);
}

int main(void) {
    content_registry_shutdown();
    test_v1_compatibility();
    test_eggnogg_color();
    test_opponent_spawn();
    test_spawn_markers();
    test_variable_room_metadata_and_bounds();
    test_room_graph_manifest_and_transitions();
    test_native_tentacle_headroom();
    test_in_memory_v1_preview();
    test_v2_preview_folder();
    test_native_room_spawn_budget();
    test_v2_symbolic_builtin();
    test_pinned_world_tile_query();
    test_v2_whole_symbol_override();
    test_repository_runtime_fixture();
    test_repository_ambiance_fixture();
    test_v2_hard_failures();
    test_v2_custom_ambiances();
    test_external_asset_hash();
    test_tileset_defaults_and_native_layout();
    test_external_asset_online_identity();
    test_tileset_default_online_identity_and_view();
    test_map_script_validation_and_safety();
    test_map_script_online_identity();
    test_entity_package_discovery();
    test_nested_zip_discovery();
    content_registry_shutdown();
    if (g_failures != 0) {
        fprintf(stderr, "%d custom map v2 test(s) failed\n", g_failures);
        return 1;
    }
    puts("custom_maps_v2_test: all checks passed");
    return 0;
}
