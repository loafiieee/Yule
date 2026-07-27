#include <windows.h>
#include <bcrypt.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "content_registry.h"

struct ContentRegistryTx {
    char owner[CONTENT_OWNER_MAX];
    ContentTileDef* tiles;
    size_t tile_count;
    size_t tile_cap;
};

struct ContentRegistryBatch {
    ContentRegistryTx** transactions;
    size_t count;
    size_t cap;
};

typedef struct ContentSha256 {
    BCRYPT_ALG_HANDLE algorithm;
    BCRYPT_HASH_HANDLE hash;
    uint8_t* object;
    DWORD object_size;
    int ok;
} ContentSha256;

static SRWLOCK g_content_lock = SRWLOCK_INIT;
static ContentTileDef* g_content_tiles = NULL;
static size_t g_content_tile_count = 0;
static uint64_t g_content_generation = 1;

static void set_err(char* err, size_t err_cap, const char* text) {
    if (!err || err_cap == 0) return;
    snprintf(err, err_cap, "%s", text ? text : "content registry error");
    err[err_cap - 1] = '\0';
}

static int ascii_is_id_char(unsigned char ch) {
    return (ch >= 'a' && ch <= 'z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '_' || ch == '-' || ch == '.';
}

static int normalize_id_part(const char* input,
                             char* out,
                             size_t out_cap,
                             const char* label,
                             char* err,
                             size_t err_cap) {
    size_t i;
    size_t len;
    if (!input || !input[0]) {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s is required", label);
        set_err(err, err_cap, msg);
        return 0;
    }
    len = strlen(input);
    if (len >= out_cap) {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s is too long (max %u characters)",
                 label, (unsigned)(out_cap - 1));
        set_err(err, err_cap, msg);
        return 0;
    }
    for (i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)input[i];
        if (ch >= 'A' && ch <= 'Z') ch = (unsigned char)(ch - 'A' + 'a');
        if (!ascii_is_id_char(ch)) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                     "%s contains invalid character 0x%02x; use ASCII letters, numbers, '.', '_' or '-'",
                     label, (unsigned)ch);
            set_err(err, err_cap, msg);
            return 0;
        }
        out[i] = (char)ch;
    }
    out[len] = '\0';
    if (out[0] == '.' || out[0] == '-') {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s must start with a letter, number, or '_'", label);
        set_err(err, err_cap, msg);
        return 0;
    }
    return 1;
}

int content_registry_make_key(const char* owner,
                              const char* local_id,
                              char out[CONTENT_KEY_MAX],
                              char* err,
                              size_t err_cap) {
    char normalized_owner[CONTENT_OWNER_MAX];
    char normalized_id[CONTENT_LOCAL_ID_MAX];
    int written;
    if (!out) {
        set_err(err, err_cap, "output key buffer is required");
        return 0;
    }
    out[0] = '\0';
    if (!normalize_id_part(owner, normalized_owner, sizeof(normalized_owner),
                           "content owner", err, err_cap) ||
        !normalize_id_part(local_id, normalized_id, sizeof(normalized_id),
                           "content id", err, err_cap)) {
        return 0;
    }
    written = snprintf(out, CONTENT_KEY_MAX, "%s:%s", normalized_owner, normalized_id);
    if (written < 0 || written >= CONTENT_KEY_MAX) {
        set_err(err, err_cap, "qualified content key is too long");
        out[0] = '\0';
        return 0;
    }
    return 1;
}

static int normalize_qualified_key(const char* key,
                                   char out[CONTENT_KEY_MAX],
                                   char* err,
                                   size_t err_cap) {
    const char* colon;
    char owner[CONTENT_OWNER_MAX];
    char local_id[CONTENT_LOCAL_ID_MAX];
    size_t owner_len;
    if (!key) {
        set_err(err, err_cap, "qualified content key is required");
        return 0;
    }
    colon = strchr(key, ':');
    if (!colon || strchr(colon + 1, ':')) {
        set_err(err, err_cap, "content key must be exactly 'owner:id'");
        return 0;
    }
    owner_len = (size_t)(colon - key);
    if (owner_len == 0 || owner_len >= sizeof(owner) || strlen(colon + 1) >= sizeof(local_id)) {
        set_err(err, err_cap, "qualified content key is too long");
        return 0;
    }
    memcpy(owner, key, owner_len);
    owner[owner_len] = '\0';
    snprintf(local_id, sizeof(local_id), "%s", colon + 1);
    return content_registry_make_key(owner, local_id, out, err, err_cap);
}

static int normalize_sprite_sheet_key(const char* key,
                                      char out[CONTENT_SHEET_KEY_MAX],
                                      char* err,
                                      size_t err_cap) {
    char normalized[CONTENT_KEY_MAX];
    if (!key || !key[0]) {
        set_err(err, err_cap, "tile sprite_sheet is required");
        return 0;
    }
    if (!normalize_qualified_key(key, normalized, err, err_cap)) {
        set_err(err, err_cap,
                "tile sprite_sheet must be a namespaced key such as 'builtin:tiles' or 'owner:sheet'");
        return 0;
    }
    snprintf(out, CONTENT_SHEET_KEY_MAX, "%s", normalized);
    return 1;
}

int content_registry_tile_native_glyph_allowed(char glyph) {
    /* Only bounded glyphs whose native plotter footprint is one cell. '?' and
     * blank create no drawable cell; G/L/N/Y/T/s/t and '~' expand/back-fill.
     * K is one cell but remains excluded from the generic registry because
     * registry owners can exist outside a fully audited map package. Direct
     * map K markers are separately admitted only after package validation
     * proves the strict per-room native reset-spawn budget. */
    static const char allowed[] =
        "!#()*+-12:=@ACEFHIOPQSWXZ^_`cefilmquvwx|";
    return glyph != '\0' && strchr(allowed, glyph) != NULL;
}

static int hex_value(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static int parse_sha256_hex(const char* text, uint8_t out[CONTENT_SHA256_SIZE]) {
    int i;
    if (!text || strlen(text) != 64) return 0;
    for (i = 0; i < CONTENT_SHA256_SIZE; i++) {
        int hi = hex_value(text[i * 2]);
        int lo = hex_value(text[i * 2 + 1]);
        if (hi < 0 || lo < 0) return 0;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 1;
}

static void sha256_to_hex(const uint8_t digest[CONTENT_SHA256_SIZE],
                          char out[CONTENT_SHA256_HEX_SIZE]) {
    static const char digits[] = "0123456789abcdef";
    int i;
    for (i = 0; i < CONTENT_SHA256_SIZE; i++) {
        out[i * 2] = digits[digest[i] >> 4];
        out[i * 2 + 1] = digits[digest[i] & 15];
    }
    out[64] = '\0';
}

static int sha256_begin(ContentSha256* ctx) {
    DWORD result_size = 0;
    if (!ctx) return 0;
    memset(ctx, 0, sizeof(*ctx));
    if (BCryptOpenAlgorithmProvider(&ctx->algorithm, BCRYPT_SHA256_ALGORITHM,
                                    NULL, 0) < 0) {
        return 0;
    }
    if (BCryptGetProperty(ctx->algorithm, BCRYPT_OBJECT_LENGTH,
                          (PUCHAR)&ctx->object_size, sizeof(ctx->object_size),
                          &result_size, 0) < 0 || ctx->object_size == 0) {
        BCryptCloseAlgorithmProvider(ctx->algorithm, 0);
        memset(ctx, 0, sizeof(*ctx));
        return 0;
    }
    ctx->object = (uint8_t*)malloc(ctx->object_size);
    if (!ctx->object || BCryptCreateHash(ctx->algorithm, &ctx->hash,
                                         ctx->object, ctx->object_size,
                                         NULL, 0, 0) < 0) {
        free(ctx->object);
        BCryptCloseAlgorithmProvider(ctx->algorithm, 0);
        memset(ctx, 0, sizeof(*ctx));
        return 0;
    }
    ctx->ok = 1;
    return 1;
}

static int sha256_update(ContentSha256* ctx, const void* data, size_t len) {
    if (!ctx || !ctx->ok || (!data && len != 0) || len > 0xffffffffu) return 0;
    if (len != 0 && BCryptHashData(ctx->hash, (PUCHAR)data, (ULONG)len, 0) < 0) {
        ctx->ok = 0;
        return 0;
    }
    return 1;
}

static int sha256_u32(ContentSha256* ctx, uint32_t value) {
    uint8_t bytes[4];
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
    return sha256_update(ctx, bytes, sizeof(bytes));
}

static int sha256_string(ContentSha256* ctx, const char* text) {
    size_t len = text ? strlen(text) : 0;
    return len <= 0xffffffffu &&
           sha256_u32(ctx, (uint32_t)len) &&
           sha256_update(ctx, text, len);
}

static int sha256_finish(ContentSha256* ctx, uint8_t out[CONTENT_SHA256_SIZE]) {
    int ok = 0;
    if (ctx && ctx->ok && out &&
        BCryptFinishHash(ctx->hash, out, CONTENT_SHA256_SIZE, 0) >= 0) {
        ok = 1;
    }
    if (ctx) {
        if (ctx->hash) BCryptDestroyHash(ctx->hash);
        if (ctx->algorithm) BCryptCloseAlgorithmProvider(ctx->algorithm, 0);
        free(ctx->object);
        memset(ctx, 0, sizeof(*ctx));
    }
    return ok;
}

int content_registry_sha256_file(const char* path,
                                 uint8_t out[CONTENT_SHA256_SIZE],
                                 char out_hex[CONTENT_SHA256_HEX_SIZE],
                                 char* err,
                                 size_t err_cap) {
    ContentSha256 sha;
    FILE* file;
    uint8_t buffer[64 * 1024];
    size_t count;
    int ok = 1;
    uint8_t local_digest[CONTENT_SHA256_SIZE];
    uint8_t* digest = out ? out : local_digest;

    if (!path || !path[0]) {
        set_err(err, err_cap, "asset file path is required");
        return 0;
    }
    file = fopen(path, "rb");
    if (!file) {
        char message[256];
        snprintf(message, sizeof(message), "cannot open asset file: %s", path);
        set_err(err, err_cap, message);
        return 0;
    }
    if (!sha256_begin(&sha)) {
        fclose(file);
        set_err(err, err_cap, "failed to initialize SHA-256");
        return 0;
    }
    while ((count = fread(buffer, 1, sizeof(buffer), file)) != 0) {
        if (!sha256_update(&sha, buffer, count)) {
            ok = 0;
            break;
        }
    }
    if (ferror(file)) ok = 0;
    fclose(file);
    if (!ok || !sha256_finish(&sha, digest)) {
        if (!ok) (void)sha256_finish(&sha, digest);
        set_err(err, err_cap, "failed while hashing asset file");
        return 0;
    }
    if (out_hex) sha256_to_hex(digest, out_hex);
    return 1;
}

int content_registry_sha256_bytes(const void* data,
                                  size_t len,
                                  uint8_t out[CONTENT_SHA256_SIZE],
                                  char out_hex[CONTENT_SHA256_HEX_SIZE],
                                  char* err,
                                  size_t err_cap) {
    ContentSha256 sha;
    uint8_t local_digest[CONTENT_SHA256_SIZE];
    uint8_t* digest = out ? out : local_digest;
    int ok = 1;
    if (!data && len != 0u) {
        set_err(err, err_cap, "SHA-256 input buffer is required");
        return 0;
    }
    if (!sha256_begin(&sha)) {
        set_err(err, err_cap, "failed to initialize SHA-256");
        return 0;
    }
    if (len != 0u && !sha256_update(&sha, data, len)) ok = 0;
    if (!sha256_finish(&sha, digest)) ok = 0;
    if (!ok) {
        set_err(err, err_cap, "failed while hashing input bytes");
        return 0;
    }
    if (out_hex) sha256_to_hex(digest, out_hex);
    return 1;
}

static uint32_t normalized_float_bits(float value) {
    uint32_t bits;
    if (value == 0.0f) value = 0.0f; /* canonicalize negative zero */
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static int compute_builtin_asset_digest(const char* sheet,
                                        uint8_t out[CONTENT_SHA256_SIZE]) {
    static const char domain[] = "eggnoggplus/builtin-sheet/v1";
    ContentSha256 sha;
    int ok;
    if (!sha256_begin(&sha)) return 0;
    ok = sha256_update(&sha, domain, sizeof(domain) - 1) &&
         sha256_string(&sha, sheet);
    if (!ok) {
        sha256_finish(&sha, out);
        return 0;
    }
    return sha256_finish(&sha, out);
}

static int compute_tile_digest(ContentTileDef* def) {
    static const char domain[] = "eggnoggplus/content-tile/v2";
    ContentSha256 sha;
    int i;
    if (!def || !sha256_begin(&sha)) return 0;
    if (!sha256_update(&sha, domain, sizeof(domain) - 1) ||
        !sha256_string(&sha, def->key) ||
        !sha256_string(&sha, def->name) ||
        !sha256_u32(&sha, (uint8_t)def->native_glyph) ||
        !sha256_string(&sha, def->sprite_sheet) ||
        !sha256_u32(&sha, (uint32_t)def->sprite_index) ||
        !sha256_u32(&sha, (uint32_t)def->frame_count) ||
        !sha256_u32(&sha, (uint32_t)def->frame_ticks) ||
        !sha256_u32(&sha, (uint32_t)def->animation_mode) ||
        !sha256_u32(&sha, (uint32_t)def->layer) ||
        !sha256_u32(&sha, def->flags) ||
        !sha256_u32(&sha, (uint32_t)def->collision_mode) ||
        !sha256_u32(&sha, (uint32_t)def->force_mode) ||
        !sha256_u32(&sha, def->force_axes) ||
        !sha256_u32(&sha, normalized_float_bits(def->force_x)) ||
        !sha256_u32(&sha, normalized_float_bits(def->force_y)) ||
        !sha256_u32(&sha, normalized_float_bits(def->max_speed_x)) ||
        !sha256_u32(&sha, normalized_float_bits(def->max_speed_y)) ||
        !sha256_u32(&sha, normalized_float_bits(def->offset_x)) ||
        !sha256_u32(&sha, normalized_float_bits(def->offset_y)) ||
        !sha256_u32(&sha, normalized_float_bits(def->scale_x)) ||
        !sha256_u32(&sha, normalized_float_bits(def->scale_y)) ||
        !sha256_u32(&sha, normalized_float_bits(def->angle_degrees))) {
        sha256_finish(&sha, def->definition_sha256);
        return 0;
    }
    for (i = 0; i < 4; i++) {
        if (!sha256_u32(&sha, normalized_float_bits(def->tint[i]))) {
            sha256_finish(&sha, def->definition_sha256);
            return 0;
        }
    }
    if (!sha256_update(&sha, def->asset_sha256, sizeof(def->asset_sha256)) ||
        !sha256_finish(&sha, def->definition_sha256)) {
        return 0;
    }
    sha256_to_hex(def->definition_sha256, def->definition_sha256_hex);
    return 1;
}

static int finite_between(float value, float lo, float hi) {
    return isfinite(value) && value >= lo && value <= hi;
}

static int build_tile_def(const char* owner,
                          const ContentTileInput* input,
                          ContentTileDef* out,
                          char* err,
                          size_t err_cap) {
    char key[CONTENT_KEY_MAX];
    int i;
    if (!input || !out) {
        set_err(err, err_cap, "tile definition is required");
        return 0;
    }
    memset(out, 0, sizeof(*out));
    if (!content_registry_make_key(owner, input->id, key, err, err_cap)) return 0;
    if (!normalize_id_part(owner, out->owner, sizeof(out->owner),
                           "content owner", err, err_cap) ||
        !normalize_id_part(input->id, out->local_id, sizeof(out->local_id),
                           "content id", err, err_cap)) {
        return 0;
    }
    snprintf(out->key, sizeof(out->key), "%s", key);
    if (input->name && input->name[0]) {
        if (strlen(input->name) >= sizeof(out->name)) {
            set_err(err, err_cap, "tile display name is too long");
            return 0;
        }
        snprintf(out->name, sizeof(out->name), "%s", input->name);
    } else {
        snprintf(out->name, sizeof(out->name), "%s", out->local_id);
    }
    if (input->collision_mode < CONTENT_COLLISION_NATIVE ||
        input->collision_mode > CONTENT_COLLISION_HAZARD) {
        set_err(err, err_cap, "tile collision mode is invalid");
        return 0;
    }
    out->collision_mode = input->collision_mode;
    switch (out->collision_mode) {
        case CONTENT_COLLISION_SOLID:
            out->native_glyph = '@';
            break;
        case CONTENT_COLLISION_PASS_THROUGH:
            out->native_glyph = 'x';
            break;
        case CONTENT_COLLISION_HAZARD:
            out->native_glyph = 'X';
            break;
        case CONTENT_COLLISION_NATIVE:
        default:
            out->native_glyph = input->native_glyph;
            break;
    }
    if (input->native_glyph != '\0' && input->native_glyph != out->native_glyph) {
        set_err(err, err_cap,
                "tile native_glyph conflicts with its collision preset; omit it or use the preset glyph");
        return 0;
    }
    if (!content_registry_tile_native_glyph_allowed(out->native_glyph)) {
        set_err(err, err_cap, "tile native_glyph is not a safe single-cell vanilla glyph");
        return 0;
    }
    if (input->force_mode < CONTENT_FORCE_ADD ||
        input->force_mode > CONTENT_FORCE_SET) {
        set_err(err, err_cap, "tile force mode is invalid");
        return 0;
    }
    if ((input->force_axes & ~CONTENT_FORCE_VALID_AXES) != 0) {
        set_err(err, err_cap, "tile force axes contain unknown bits");
        return 0;
    }
    if (input->force_axes == 0 && input->force_mode != CONTENT_FORCE_ADD) {
        set_err(err, err_cap, "tile force mode requires at least one authored force axis");
        return 0;
    }
    if (!finite_between(input->force_x, -64.0f, 64.0f) ||
        !finite_between(input->force_y, -64.0f, 64.0f)) {
        set_err(err, err_cap, "tile forces must be finite and in -64..64");
        return 0;
    }
    if (!finite_between(input->max_speed_x, 0.0f, 64.0f) ||
        !finite_between(input->max_speed_y, 0.0f, 64.0f)) {
        set_err(err, err_cap, "tile force max speeds must be finite and in 0..64");
        return 0;
    }
    if ((input->force_axes & CONTENT_FORCE_AXIS_X) == 0 &&
        (input->force_x != 0.0f || input->max_speed_x != 0.0f)) {
        set_err(err, err_cap, "tile force_x/max_speed_x require the X force axis");
        return 0;
    }
    if ((input->force_axes & CONTENT_FORCE_AXIS_Y) == 0 &&
        (input->force_y != 0.0f || input->max_speed_y != 0.0f)) {
        set_err(err, err_cap, "tile force_y/max_speed_y require the Y force axis");
        return 0;
    }
    out->force_mode = input->force_mode;
    out->force_axes = input->force_axes;
    out->force_x = input->force_x;
    out->force_y = input->force_y;
    out->max_speed_x = input->max_speed_x;
    out->max_speed_y = input->max_speed_y;
    if (!normalize_sprite_sheet_key(input->sprite_sheet, out->sprite_sheet,
                                    err, err_cap)) return 0;
    if (input->sprite_index < 0 || input->sprite_index > 1000000) {
        set_err(err, err_cap, "tile sprite_index must be in 0..1000000");
        return 0;
    }
    out->sprite_index = input->sprite_index;
    out->frame_count = input->frame_count == 0 ? 1 : input->frame_count;
    out->frame_ticks = input->frame_ticks == 0 ? 1 : input->frame_ticks;
    if (out->frame_count < 1 || out->frame_count > 256) {
        set_err(err, err_cap, "tile frame_count must be in 1..256");
        return 0;
    }
    if (out->frame_ticks < 1 || out->frame_ticks > 3600) {
        set_err(err, err_cap, "tile frame_ticks must be in 1..3600");
        return 0;
    }
    if (input->animation_mode < CONTENT_ANIMATION_LOOP ||
        input->animation_mode > CONTENT_ANIMATION_ONCE) {
        set_err(err, err_cap, "tile animation_mode is invalid");
        return 0;
    }
    out->animation_mode = input->animation_mode;
    if (input->layer < 0 || input->layer > 1) {
        set_err(err, err_cap, "tile layer must be 0 or 1");
        return 0;
    }
    out->layer = input->layer;
    if ((input->flags & ~CONTENT_TILE_VALID_FLAGS) != 0) {
        set_err(err, err_cap, "tile flags contain unknown bits");
        return 0;
    }
    out->flags = input->flags;
    if (!finite_between(input->offset_x, -4096.0f, 4096.0f) ||
        !finite_between(input->offset_y, -4096.0f, 4096.0f)) {
        set_err(err, err_cap, "tile offsets must be finite and in -4096..4096");
        return 0;
    }
    out->offset_x = input->offset_x;
    out->offset_y = input->offset_y;
    out->scale_x = input->scale_x;
    out->scale_y = input->scale_y;
    if (!finite_between(out->scale_x, -64.0f, 64.0f) ||
        !finite_between(out->scale_y, -64.0f, 64.0f) ||
        out->scale_x == 0.0f || out->scale_y == 0.0f) {
        set_err(err, err_cap, "tile scales must be finite, non-zero, and in -64..64");
        return 0;
    }
    if (!finite_between(input->angle_degrees, -360000.0f, 360000.0f)) {
        set_err(err, err_cap, "tile angle must be finite and in -360000..360000 degrees");
        return 0;
    }
    out->angle_degrees = input->angle_degrees;
    for (i = 0; i < 4; i++) {
        float value = input->tint_provided ? input->tint[i] : 1.0f;
        if (!finite_between(value, 0.0f, 1.0f)) {
            set_err(err, err_cap, "tile tint channels must be finite and in 0..1");
            return 0;
        }
        out->tint[i] = value;
    }
    if (strncmp(out->sprite_sheet, "builtin:", 8) == 0) {
        if (input->asset_sha256_hex && input->asset_sha256_hex[0]) {
            set_err(err, err_cap, "tile asset_sha256 must be omitted for built-in sprite sheets");
            return 0;
        }
        if (!compute_builtin_asset_digest(out->sprite_sheet, out->asset_sha256)) {
            set_err(err, err_cap, "failed to hash built-in sprite sheet identity");
            return 0;
        }
    } else if (!parse_sha256_hex(input->asset_sha256_hex, out->asset_sha256)) {
        set_err(err, err_cap, "tile asset_sha256 must be a 64-character SHA-256 hex digest");
        return 0;
    }
    if (!compute_tile_digest(out)) {
        set_err(err, err_cap, "failed to hash tile definition");
        return 0;
    }
    return 1;
}

static int compare_tiles(const void* lhs_ptr, const void* rhs_ptr) {
    const ContentTileDef* lhs = (const ContentTileDef*)lhs_ptr;
    const ContentTileDef* rhs = (const ContentTileDef*)rhs_ptr;
    return strcmp(lhs->key, rhs->key);
}

void content_registry_init(void) {
    /* SRWLOCK uses static initialization. This function intentionally remains
     * idempotent so subsystems can safely call it before use. */
}

void content_registry_shutdown(void) {
    AcquireSRWLockExclusive(&g_content_lock);
    free(g_content_tiles);
    g_content_tiles = NULL;
    g_content_tile_count = 0;
    g_content_generation++;
    ReleaseSRWLockExclusive(&g_content_lock);
}

ContentRegistryTx* content_registry_begin(const char* owner, char* err, size_t err_cap) {
    ContentRegistryTx* tx = (ContentRegistryTx*)calloc(1, sizeof(*tx));
    if (!tx) {
        set_err(err, err_cap, "out of memory creating content transaction");
        return NULL;
    }
    if (!normalize_id_part(owner, tx->owner, sizeof(tx->owner),
                           "content owner", err, err_cap)) {
        free(tx);
        return NULL;
    }
    return tx;
}

int content_registry_tx_register_tile(ContentRegistryTx* tx,
                                      const ContentTileInput* input,
                                      char* err,
                                      size_t err_cap) {
    ContentTileDef def;
    size_t i;
    if (!tx) {
        set_err(err, err_cap, "content transaction is required");
        return 0;
    }
    if (tx->tile_count >= CONTENT_TILES_PER_OWNER_MAX) {
        set_err(err, err_cap, "content owner exceeds the 4096-tile limit");
        return 0;
    }
    if (!build_tile_def(tx->owner, input, &def, err, err_cap)) return 0;
    for (i = 0; i < tx->tile_count; i++) {
        if (strcmp(tx->tiles[i].key, def.key) == 0) {
            set_err(err, err_cap, "duplicate tile id in content transaction");
            return 0;
        }
    }
    if (tx->tile_count == tx->tile_cap) {
        size_t new_cap = tx->tile_cap ? tx->tile_cap * 2 : 8;
        ContentTileDef* bigger;
        if (new_cap > SIZE_MAX / sizeof(*bigger)) {
            set_err(err, err_cap, "content transaction is too large");
            return 0;
        }
        bigger = (ContentTileDef*)realloc(tx->tiles, new_cap * sizeof(*bigger));
        if (!bigger) {
            set_err(err, err_cap, "out of memory growing content transaction");
            return 0;
        }
        tx->tiles = bigger;
        tx->tile_cap = new_cap;
    }
    tx->tiles[tx->tile_count++] = def;
    return 1;
}

static int owner_in_transactions(ContentRegistryTx* const* transactions,
                                 size_t transaction_count,
                                 const char* owner) {
    size_t i;
    for (i = 0; i < transaction_count; i++) {
        if (transactions[i] && strcmp(transactions[i]->owner, owner) == 0) return 1;
    }
    return 0;
}

static int commit_transactions(ContentRegistryTx* const* transactions,
                               size_t transaction_count,
                               char* err,
                               size_t err_cap) {
    ContentTileDef* replacement = NULL;
    ContentTileDef* old_tiles;
    size_t survivors = 0;
    size_t total;
    size_t i;
    size_t tx_index;
    size_t write_index = 0;
    if (!transactions || transaction_count == 0) return 1;
    for (tx_index = 0; tx_index < transaction_count; tx_index++) {
        ContentRegistryTx* tx = transactions[tx_index];
        if (!tx) {
            set_err(err, err_cap, "content transaction is required");
            return 0;
        }
        if (tx->tile_count > 1) {
            qsort(tx->tiles, tx->tile_count, sizeof(*tx->tiles), compare_tiles);
        }
        for (i = tx_index + 1; i < transaction_count; i++) {
            if (transactions[i] && strcmp(tx->owner, transactions[i]->owner) == 0) {
                set_err(err, err_cap, "duplicate owner in content batch");
                return 0;
            }
        }
    }

    AcquireSRWLockExclusive(&g_content_lock);
    for (i = 0; i < g_content_tile_count; i++) {
        if (!owner_in_transactions(transactions, transaction_count,
                                   g_content_tiles[i].owner)) {
            survivors++;
        }
    }
    total = survivors;
    for (tx_index = 0; tx_index < transaction_count; tx_index++) {
        if (transactions[tx_index]->tile_count > SIZE_MAX - total) {
            ReleaseSRWLockExclusive(&g_content_lock);
            set_err(err, err_cap, "content registry is too large");
            return 0;
        }
        total += transactions[tx_index]->tile_count;
    }
    if (total > CONTENT_TILES_GLOBAL_MAX) {
        ReleaseSRWLockExclusive(&g_content_lock);
        set_err(err, err_cap, "content registry exceeds the 65535-tile limit");
        return 0;
    }
    if (total != 0) {
        if (total > SIZE_MAX / sizeof(*replacement)) {
            ReleaseSRWLockExclusive(&g_content_lock);
            set_err(err, err_cap, "content registry allocation overflows");
            return 0;
        }
        replacement = (ContentTileDef*)malloc(total * sizeof(*replacement));
        if (!replacement) {
            ReleaseSRWLockExclusive(&g_content_lock);
            set_err(err, err_cap, "out of memory committing content transaction");
            return 0;
        }
    }
    for (i = 0; i < g_content_tile_count; i++) {
        if (!owner_in_transactions(transactions, transaction_count,
                                   g_content_tiles[i].owner)) {
            replacement[write_index++] = g_content_tiles[i];
        }
    }
    for (tx_index = 0; tx_index < transaction_count; tx_index++) {
        ContentRegistryTx* tx = transactions[tx_index];
        for (i = 0; i < tx->tile_count; i++) replacement[write_index++] = tx->tiles[i];
    }
    if (total > 1) qsort(replacement, total, sizeof(*replacement), compare_tiles);
    for (i = 1; i < total; i++) {
        if (strcmp(replacement[i - 1].key, replacement[i].key) == 0) {
            free(replacement);
            ReleaseSRWLockExclusive(&g_content_lock);
            set_err(err, err_cap, "qualified content key collision");
            return 0;
        }
    }
    old_tiles = g_content_tiles;
    g_content_tiles = replacement;
    g_content_tile_count = total;
    g_content_generation++;
    ReleaseSRWLockExclusive(&g_content_lock);
    free(old_tiles);
    return 1;
}

int content_registry_commit(ContentRegistryTx* tx, char* err, size_t err_cap) {
    ContentRegistryTx* transactions[1];
    if (!tx) {
        set_err(err, err_cap, "content transaction is required");
        return 0;
    }
    transactions[0] = tx;
    if (!commit_transactions(transactions, 1, err, err_cap)) return 0;
    content_registry_abort(tx);
    return 1;
}

ContentRegistryBatch* content_registry_batch_begin(char* err, size_t err_cap) {
    ContentRegistryBatch* batch = (ContentRegistryBatch*)calloc(1, sizeof(*batch));
    if (!batch) set_err(err, err_cap, "out of memory creating content batch");
    return batch;
}

int content_registry_batch_add(ContentRegistryBatch* batch,
                               ContentRegistryTx* tx,
                               char* err,
                               size_t err_cap) {
    size_t i;
    ContentRegistryTx** bigger;
    size_t new_cap;
    if (!batch || !tx) {
        set_err(err, err_cap, "content batch and transaction are required");
        return 0;
    }
    for (i = 0; i < batch->count; i++) {
        if (strcmp(batch->transactions[i]->owner, tx->owner) == 0) {
            set_err(err, err_cap, "duplicate owner in content batch");
            return 0;
        }
    }
    if (batch->count == batch->cap) {
        new_cap = batch->cap ? batch->cap * 2u : 8u;
        if (new_cap > SIZE_MAX / sizeof(*bigger)) {
            set_err(err, err_cap, "content batch is too large");
            return 0;
        }
        bigger = (ContentRegistryTx**)realloc(batch->transactions,
                                               new_cap * sizeof(*bigger));
        if (!bigger) {
            set_err(err, err_cap, "out of memory growing content batch");
            return 0;
        }
        batch->transactions = bigger;
        batch->cap = new_cap;
    }
    batch->transactions[batch->count++] = tx;
    return 1;
}

int content_registry_batch_commit(ContentRegistryBatch* batch,
                                  char* err,
                                  size_t err_cap) {
    size_t i;
    if (!batch) {
        set_err(err, err_cap, "content batch is required");
        return 0;
    }
    if (!commit_transactions(batch->transactions, batch->count, err, err_cap)) return 0;
    for (i = 0; i < batch->count; i++) content_registry_abort(batch->transactions[i]);
    free(batch->transactions);
    memset(batch, 0, sizeof(*batch));
    free(batch);
    return 1;
}

void content_registry_batch_abort(ContentRegistryBatch* batch) {
    size_t i;
    if (!batch) return;
    for (i = 0; i < batch->count; i++) content_registry_abort(batch->transactions[i]);
    free(batch->transactions);
    memset(batch, 0, sizeof(*batch));
    free(batch);
}

void content_registry_abort(ContentRegistryTx* tx) {
    if (!tx) return;
    free(tx->tiles);
    memset(tx, 0, sizeof(*tx));
    free(tx);
}

int content_registry_remove_owner(const char* owner, char* err, size_t err_cap) {
    ContentRegistryTx* tx = content_registry_begin(owner, err, err_cap);
    if (!tx) return 0;
    if (!content_registry_commit(tx, err, err_cap)) {
        content_registry_abort(tx);
        return 0;
    }
    return 1;
}

uint64_t content_registry_generation(void) {
    uint64_t generation;
    AcquireSRWLockShared(&g_content_lock);
    generation = g_content_generation;
    ReleaseSRWLockShared(&g_content_lock);
    return generation;
}

size_t content_registry_tile_count(void) {
    size_t count;
    AcquireSRWLockShared(&g_content_lock);
    count = g_content_tile_count;
    ReleaseSRWLockShared(&g_content_lock);
    return count;
}

int content_registry_tile_get(size_t index, ContentTileDef* out) {
    int ok = 0;
    if (!out) return 0;
    AcquireSRWLockShared(&g_content_lock);
    if (index < g_content_tile_count) {
        *out = g_content_tiles[index];
        ok = 1;
    }
    ReleaseSRWLockShared(&g_content_lock);
    return ok;
}

int content_registry_tile_find(const char* qualified_key, ContentTileDef* out) {
    char normalized[CONTENT_KEY_MAX];
    size_t lo = 0;
    size_t hi;
    if (!out || !normalize_qualified_key(qualified_key, normalized, NULL, 0)) return 0;
    AcquireSRWLockShared(&g_content_lock);
    hi = g_content_tile_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int cmp = strcmp(normalized, g_content_tiles[mid].key);
        if (cmp == 0) {
            *out = g_content_tiles[mid];
            ReleaseSRWLockShared(&g_content_lock);
            return 1;
        }
        if (cmp < 0) hi = mid;
        else lo = mid + 1;
    }
    ReleaseSRWLockShared(&g_content_lock);
    return 0;
}

int content_registry_fingerprint(uint8_t out[CONTENT_SHA256_SIZE],
                                 char out_hex[CONTENT_SHA256_HEX_SIZE]) {
    static const char domain[] = "eggnoggplus/content-registry/v1";
    ContentSha256 sha;
    size_t i;
    int ok = 1;
    if (!out || !sha256_begin(&sha)) return 0;
    AcquireSRWLockShared(&g_content_lock);
    ok = sha256_update(&sha, domain, sizeof(domain) - 1) &&
         sha256_u32(&sha, (uint32_t)g_content_tile_count);
    for (i = 0; ok && i < g_content_tile_count; i++) {
        ok = sha256_string(&sha, g_content_tiles[i].key) &&
             sha256_update(&sha, g_content_tiles[i].definition_sha256,
                           sizeof(g_content_tiles[i].definition_sha256));
    }
    ReleaseSRWLockShared(&g_content_lock);
    if (!ok || !sha256_finish(&sha, out)) return 0;
    if (out_hex) sha256_to_hex(out, out_hex);
    return 1;
}
