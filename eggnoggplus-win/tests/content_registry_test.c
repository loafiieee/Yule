#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../content_registry.h"

static int g_failures = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        g_failures++; \
    } \
} while (0)

static ContentTileInput tile_input(const char* id, int sprite_index) {
    ContentTileInput input;
    memset(&input, 0, sizeof(input));
    input.id = id;
    input.name = id;
    input.native_glyph = 'x';
    input.sprite_sheet = "builtin:tiles";
    input.sprite_index = sprite_index;
    input.frame_count = 1;
    input.frame_ticks = 1;
    input.animation_mode = CONTENT_ANIMATION_LOOP;
    input.scale_x = 1.0f;
    input.scale_y = 1.0f;
    input.tint[0] = 1.0f;
    input.tint[1] = 1.0f;
    input.tint[2] = 1.0f;
    input.tint[3] = 1.0f;
    return input;
}

static void test_key_validation(void) {
    char key[CONTENT_KEY_MAX];
    char err[256];
    CHECK(content_registry_make_key("Cool.Mod", "Stone-01", key, err, sizeof(err)));
    CHECK(strcmp(key, "cool.mod:stone-01") == 0);
    CHECK(!content_registry_make_key("../escape", "stone", key, err, sizeof(err)));
    CHECK(!content_registry_make_key("owner", "bad:id", key, err, sizeof(err)));
    CHECK(!content_registry_make_key("-owner", "tile", key, err, sizeof(err)));
    CHECK(content_registry_tile_native_glyph_allowed('x'));
    CHECK(content_registry_tile_native_glyph_allowed('X'));
    CHECK(!content_registry_tile_native_glyph_allowed('G'));
    CHECK(!content_registry_tile_native_glyph_allowed(' '));
    CHECK(!content_registry_tile_native_glyph_allowed('?'));
    CHECK(!content_registry_tile_native_glyph_allowed('K'));
}

static void test_transaction_atomicity(void) {
    ContentRegistryTx* tx;
    ContentTileInput stone = tile_input("stone", 5);
    ContentTileInput invalid = tile_input("bad", 6);
    ContentTileDef found;
    char err[256];

    content_registry_shutdown();
    tx = content_registry_begin("demo", err, sizeof(err));
    CHECK(tx != NULL);
    CHECK(content_registry_tx_register_tile(tx, &stone, err, sizeof(err)));
    CHECK(!content_registry_tx_register_tile(tx, &stone, err, sizeof(err)));
    CHECK(content_registry_commit(tx, err, sizeof(err)));
    CHECK(content_registry_tile_count() == 1);
    CHECK(content_registry_tile_find("DEMO:STONE", &found));
    CHECK(strcmp(found.key, "demo:stone") == 0);
    CHECK(strlen(found.definition_sha256_hex) == 64);

    invalid.native_glyph = 'G';
    tx = content_registry_begin("demo", err, sizeof(err));
    CHECK(tx != NULL);
    CHECK(!content_registry_tx_register_tile(tx, &invalid, err, sizeof(err)));
    content_registry_abort(tx);
    CHECK(content_registry_tile_count() == 1);
    CHECK(content_registry_tile_find("demo:stone", &found));

    invalid = tile_input("bad", 6);
    invalid.sprite_sheet = "demo_sheet";
    invalid.asset_sha256_hex = NULL;
    tx = content_registry_begin("demo", err, sizeof(err));
    CHECK(tx != NULL);
    CHECK(!content_registry_tx_register_tile(tx, &invalid, err, sizeof(err)));
    content_registry_abort(tx);
    CHECK(content_registry_tile_count() == 1);
}

static void test_replace_remove_and_stable_fingerprint(void) {
    ContentRegistryTx* tx;
    ContentTileInput a = tile_input("a", 1);
    ContentTileInput b = tile_input("b", 2);
    ContentTileInput c = tile_input("c", 3);
    uint8_t first[CONTENT_SHA256_SIZE];
    uint8_t second[CONTENT_SHA256_SIZE];
    char first_hex[CONTENT_SHA256_HEX_SIZE];
    char second_hex[CONTENT_SHA256_HEX_SIZE];
    char err[256];

    content_registry_shutdown();
    tx = content_registry_begin("alpha", err, sizeof(err));
    CHECK(tx != NULL);
    CHECK(content_registry_tx_register_tile(tx, &b, err, sizeof(err)));
    CHECK(content_registry_tx_register_tile(tx, &a, err, sizeof(err)));
    CHECK(content_registry_commit(tx, err, sizeof(err)));
    CHECK(content_registry_tile_count() == 2);
    CHECK(content_registry_fingerprint(first, first_hex));
    CHECK(strlen(first_hex) == 64);

    /* Re-registering the same definitions in another order is byte-stable. */
    tx = content_registry_begin("ALPHA", err, sizeof(err));
    CHECK(tx != NULL);
    CHECK(content_registry_tx_register_tile(tx, &a, err, sizeof(err)));
    CHECK(content_registry_tx_register_tile(tx, &b, err, sizeof(err)));
    CHECK(content_registry_commit(tx, err, sizeof(err)));
    CHECK(content_registry_fingerprint(second, second_hex));
    CHECK(memcmp(first, second, sizeof(first)) == 0);
    CHECK(strcmp(first_hex, second_hex) == 0);

    tx = content_registry_begin("beta", err, sizeof(err));
    CHECK(tx != NULL);
    CHECK(content_registry_tx_register_tile(tx, &c, err, sizeof(err)));
    CHECK(content_registry_commit(tx, err, sizeof(err)));
    CHECK(content_registry_tile_count() == 3);

    CHECK(content_registry_remove_owner("alpha", err, sizeof(err)));
    CHECK(content_registry_tile_count() == 1);
    CHECK(!content_registry_tile_find("alpha:a", &(ContentTileDef){0}));
    CHECK(content_registry_tile_find("beta:c", &(ContentTileDef){0}));
}

static void test_numeric_validation(void) {
    ContentRegistryTx* tx;
    ContentTileInput input = tile_input("bad_numeric", 1);
    char err[256];

    tx = content_registry_begin("numeric", err, sizeof(err));
    CHECK(tx != NULL);
    input.angle_degrees = NAN;
    CHECK(!content_registry_tx_register_tile(tx, &input, err, sizeof(err)));
    input = tile_input("bad_flags", 1);
    input.flags = 0x80000000u;
    CHECK(!content_registry_tx_register_tile(tx, &input, err, sizeof(err)));
    input = tile_input("bad_frames", 1);
    input.frame_count = 257;
    CHECK(!content_registry_tx_register_tile(tx, &input, err, sizeof(err)));
    input = tile_input("negative_frames", 1);
    input.frame_count = -1;
    CHECK(!content_registry_tx_register_tile(tx, &input, err, sizeof(err)));
    input = tile_input("zero_scale", 1);
    input.scale_x = 0.0f;
    CHECK(!content_registry_tx_register_tile(tx, &input, err, sizeof(err)));
    input = tile_input("negative_layer", 1);
    input.layer = -1;
    CHECK(!content_registry_tx_register_tile(tx, &input, err, sizeof(err)));
    input = tile_input("large_layer", 1);
    input.layer = 2;
    CHECK(!content_registry_tx_register_tile(tx, &input, err, sizeof(err)));
    input = tile_input("upper_layer", 1);
    input.layer = 1;
    CHECK(content_registry_tx_register_tile(tx, &input, err, sizeof(err)));
    content_registry_abort(tx);
}

static void test_collision_and_force_behavior(void) {
    ContentRegistryTx* tx;
    ContentTileInput input = tile_input("behavior", 7);
    ContentTileDef found;
    uint8_t before[CONTENT_SHA256_SIZE];
    uint8_t visual[CONTENT_SHA256_SIZE];
    uint8_t after[CONTENT_SHA256_SIZE];
    char err[256];

    content_registry_shutdown();
    input.native_glyph = '\0';
    input.collision_mode = CONTENT_COLLISION_SOLID;
    tx = content_registry_begin("behavior", err, sizeof(err));
    CHECK(tx != NULL);
    CHECK(content_registry_tx_register_tile(tx, &input, err, sizeof(err)));
    CHECK(content_registry_commit(tx, err, sizeof(err)));
    CHECK(content_registry_tile_find("behavior:behavior", &found));
    CHECK(found.collision_mode == CONTENT_COLLISION_SOLID);
    CHECK(found.native_glyph == '@');
    CHECK(content_registry_fingerprint(before, NULL));

    input.flags |= CONTENT_TILE_NATIVE_VISUAL_UNDERLAY;
    tx = content_registry_begin("behavior", err, sizeof(err));
    CHECK(tx != NULL);
    CHECK(content_registry_tx_register_tile(tx, &input, err, sizeof(err)));
    CHECK(content_registry_commit(tx, err, sizeof(err)));
    CHECK(content_registry_tile_find("behavior:behavior", &found));
    CHECK((found.flags & CONTENT_TILE_NATIVE_VISUAL_UNDERLAY) != 0);
    CHECK(content_registry_fingerprint(visual, NULL));
    CHECK(memcmp(before, visual, sizeof(before)) != 0);

    input.force_mode = CONTENT_FORCE_ADD;
    input.force_axes = CONTENT_FORCE_AXIS_X | CONTENT_FORCE_AXIS_Y;
    input.force_x = 0.25f;
    input.force_y = -0.5f;
    input.max_speed_x = 3.0f;
    input.max_speed_y = 8.0f;
    tx = content_registry_begin("behavior", err, sizeof(err));
    CHECK(tx != NULL);
    CHECK(content_registry_tx_register_tile(tx, &input, err, sizeof(err)));
    CHECK(content_registry_commit(tx, err, sizeof(err)));
    CHECK(content_registry_tile_find("behavior:behavior", &found));
    CHECK(found.force_axes == (CONTENT_FORCE_AXIS_X | CONTENT_FORCE_AXIS_Y));
    CHECK(found.force_x == 0.25f && found.force_y == -0.5f);
    CHECK(content_registry_fingerprint(after, NULL));
    CHECK(memcmp(visual, after, sizeof(visual)) != 0);

    input.native_glyph = 'x';
    tx = content_registry_begin("invalid_behavior", err, sizeof(err));
    CHECK(tx != NULL);
    CHECK(!content_registry_tx_register_tile(tx, &input, err, sizeof(err)));
    input.native_glyph = '\0';
    input.force_x = 65.0f;
    CHECK(!content_registry_tx_register_tile(tx, &input, err, sizeof(err)));
    input.force_x = 0.25f;
    input.force_axes = CONTENT_FORCE_AXIS_Y;
    CHECK(!content_registry_tx_register_tile(tx, &input, err, sizeof(err)));
    content_registry_abort(tx);
    CHECK(content_registry_tile_find("behavior:behavior", &found));
}

static void test_sheet_key_validation(void) {
    ContentRegistryTx* tx;
    ContentTileInput input = tile_input("sheet", 1);
    char err[256];

    tx = content_registry_begin("sheets", err, sizeof(err));
    CHECK(tx != NULL);
    input.sprite_sheet = "builtin:";
    CHECK(!content_registry_tx_register_tile(tx, &input, err, sizeof(err)));
    input.sprite_sheet = "not_namespaced";
    input.asset_sha256_hex =
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    CHECK(!content_registry_tx_register_tile(tx, &input, err, sizeof(err)));
    input.sprite_sheet = "builtin:tiles";
    CHECK(!content_registry_tx_register_tile(tx, &input, err, sizeof(err)));
    content_registry_abort(tx);
}

static void test_atomic_multi_owner_batch(void) {
    ContentRegistryTx* alpha;
    ContentRegistryTx* beta;
    ContentRegistryTx* duplicate;
    ContentRegistryBatch* batch;
    ContentTileInput a = tile_input("new", 40);
    ContentTileInput b = tile_input("new", 50);
    char err[256];

    content_registry_shutdown();
    alpha = content_registry_begin("alpha", err, sizeof(err));
    beta = content_registry_begin("beta", err, sizeof(err));
    CHECK(alpha != NULL && beta != NULL);
    CHECK(content_registry_tx_register_tile(alpha, &a, err, sizeof(err)));
    CHECK(content_registry_tx_register_tile(beta, &b, err, sizeof(err)));
    batch = content_registry_batch_begin(err, sizeof(err));
    CHECK(batch != NULL);
    CHECK(content_registry_batch_add(batch, alpha, err, sizeof(err)));
    CHECK(content_registry_batch_add(batch, beta, err, sizeof(err)));
    CHECK(content_registry_batch_commit(batch, err, sizeof(err)));
    CHECK(content_registry_tile_count() == 2);
    CHECK(content_registry_tile_find("alpha:new", &(ContentTileDef){0}));
    CHECK(content_registry_tile_find("beta:new", &(ContentTileDef){0}));

    /* A duplicate owner is rejected without stealing the rejected tx. */
    alpha = content_registry_begin("alpha", err, sizeof(err));
    duplicate = content_registry_begin("ALPHA", err, sizeof(err));
    batch = content_registry_batch_begin(err, sizeof(err));
    CHECK(alpha != NULL && duplicate != NULL && batch != NULL);
    CHECK(content_registry_batch_add(batch, alpha, err, sizeof(err)));
    CHECK(!content_registry_batch_add(batch, duplicate, err, sizeof(err)));
    content_registry_abort(duplicate);
    content_registry_batch_abort(batch);
    CHECK(content_registry_tile_count() == 2);
}

static void test_file_sha256(void) {
    static const char expected[] =
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    const char* path = "content_registry_hash_test.tmp";
    uint8_t digest[CONTENT_SHA256_SIZE];
    char hex[CONTENT_SHA256_HEX_SIZE];
    char err[256];
    FILE* file = fopen(path, "wb");
    CHECK(content_registry_sha256_bytes("abc", 3, digest, hex,
                                        err, sizeof(err)));
    CHECK(strcmp(hex, expected) == 0);
    CHECK(content_registry_sha256_bytes(NULL, 0, digest, hex,
                                        err, sizeof(err)));
    CHECK(!content_registry_sha256_bytes(NULL, 1, digest, hex,
                                         err, sizeof(err)));
    CHECK(file != NULL);
    if (file) {
        CHECK(fwrite("abc", 1, 3, file) == 3);
        fclose(file);
        CHECK(content_registry_sha256_file(path, digest, hex, err, sizeof(err)));
        CHECK(strcmp(hex, expected) == 0);
        remove(path);
    }
    CHECK(!content_registry_sha256_file("missing-content-asset.bin",
                                        digest, hex, err, sizeof(err)));
}

static void test_explicit_transparent_tint(void) {
    ContentRegistryTx* tx;
    ContentTileInput input = tile_input("transparent", 1);
    ContentTileDef found;
    char err[256];

    /* A legacy zero-initialized caller still receives the opaque-white default. */
    memset(input.tint, 0, sizeof(input.tint));
    input.tint_provided = 0;
    tx = content_registry_begin("tint_default", err, sizeof(err));
    CHECK(tx != NULL);
    CHECK(content_registry_tx_register_tile(tx, &input, err, sizeof(err)));
    CHECK(content_registry_commit(tx, err, sizeof(err)));
    CHECK(content_registry_tile_find("tint_default:transparent", &found));
    CHECK(found.tint[0] == 1.0f && found.tint[3] == 1.0f);

    memset(input.tint, 0, sizeof(input.tint));
    input.tint_provided = 1;
    tx = content_registry_begin("tint", err, sizeof(err));
    CHECK(tx != NULL);
    CHECK(content_registry_tx_register_tile(tx, &input, err, sizeof(err)));
    CHECK(content_registry_commit(tx, err, sizeof(err)));
    CHECK(content_registry_tile_find("tint:transparent", &found));
    CHECK(found.tint[0] == 0.0f && found.tint[3] == 0.0f);
}

int main(void) {
    test_key_validation();
    test_transaction_atomicity();
    test_replace_remove_and_stable_fingerprint();
    test_numeric_validation();
    test_collision_and_force_behavior();
    test_sheet_key_validation();
    test_atomic_multi_owner_batch();
    test_file_sha256();
    test_explicit_transparent_tint();
    content_registry_shutdown();
    if (g_failures != 0) {
        fprintf(stderr, "%d content registry test(s) failed\n", g_failures);
        return 1;
    }
    puts("content_registry_test: all checks passed");
    return 0;
}
