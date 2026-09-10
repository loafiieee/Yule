#include "mod_json.h"

#include <luajit-2.1/lauxlib.h>

#include <errno.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct JsonBuffer {
    char* data;
    size_t length;
    size_t capacity;
    size_t limit;
} JsonBuffer;

typedef struct JsonEncoder {
    lua_State* L;
    JsonBuffer output;
    _locale_t numeric_locale;
    int nodes;
    const void* table_stack[MOD_JSON_MAX_DEPTH];
    char error[320];
} JsonEncoder;

typedef struct JsonParser {
    lua_State* L;
    const char* text;
    size_t length;
    size_t position;
    _locale_t numeric_locale;
    int nodes;
    char error[320];
} JsonParser;

typedef struct JsonObjectKey {
    const char* text;
    size_t length;
} JsonObjectKey;

static char k_json_kind_key;
static char k_json_array_kind;
static char k_json_object_kind;
static char k_json_null;

static int json_absindex(lua_State* L, int index) {
    if (index > 0 || index <= LUA_REGISTRYINDEX) return index;
    return lua_gettop(L) + index + 1;
}

static void json_buffer_free(JsonBuffer* buffer) {
    if (!buffer) return;
    free(buffer->data);
    memset(buffer, 0, sizeof(*buffer));
}

static int json_buffer_reserve(JsonBuffer* buffer, size_t extra) {
    size_t required;
    size_t capacity;
    char* grown;
    if (!buffer || extra > buffer->limit ||
        buffer->length > buffer->limit - extra) {
        return 0;
    }
    required = buffer->length + extra;
    if (required <= buffer->capacity) return 1;
    capacity = buffer->capacity ? buffer->capacity : 256u;
    while (capacity < required) {
        size_t next = capacity * 2u;
        if (next < capacity || next > buffer->limit) {
            capacity = buffer->limit;
            break;
        }
        capacity = next;
    }
    if (capacity < required) return 0;
    grown = (char*)realloc(buffer->data, capacity);
    if (!grown) return 0;
    buffer->data = grown;
    buffer->capacity = capacity;
    return 1;
}

static int json_buffer_append(JsonBuffer* buffer, const char* text,
                              size_t length) {
    if (!buffer || (!text && length != 0) ||
        !json_buffer_reserve(buffer, length)) {
        return 0;
    }
    if (length != 0) memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    return 1;
}

static int json_buffer_append_char(JsonBuffer* buffer, char c) {
    return json_buffer_append(buffer, &c, 1u);
}

static int json_utf8_sequence(const unsigned char* text, size_t available,
                              size_t* sequence_length) {
    unsigned char a;
    unsigned char b;
    unsigned char c;
    unsigned char d;
    if (!text || available == 0 || !sequence_length) return 0;
    a = text[0];
    if (a < 0x80u) {
        *sequence_length = 1;
        return 1;
    }
    if (a >= 0xc2u && a <= 0xdfu) {
        if (available < 2) return 0;
        b = text[1];
        if (b < 0x80u || b > 0xbfu) return 0;
        *sequence_length = 2;
        return 1;
    }
    if (a >= 0xe0u && a <= 0xefu) {
        if (available < 3) return 0;
        b = text[1];
        c = text[2];
        if (b < 0x80u || b > 0xbfu || c < 0x80u || c > 0xbfu) return 0;
        if (a == 0xe0u && b < 0xa0u) return 0;
        if (a == 0xedu && b > 0x9fu) return 0;
        *sequence_length = 3;
        return 1;
    }
    if (a >= 0xf0u && a <= 0xf4u) {
        if (available < 4) return 0;
        b = text[1];
        c = text[2];
        d = text[3];
        if (b < 0x80u || b > 0xbfu || c < 0x80u || c > 0xbfu ||
            d < 0x80u || d > 0xbfu) {
            return 0;
        }
        if (a == 0xf0u && b < 0x90u) return 0;
        if (a == 0xf4u && b > 0x8fu) return 0;
        *sequence_length = 4;
        return 1;
    }
    return 0;
}

static int json_utf8_valid(const char* text, size_t length) {
    size_t position = 0;
    while (position < length) {
        size_t sequence_length = 0;
        if (!json_utf8_sequence(
                (const unsigned char*)text + position,
                length - position,
                &sequence_length)) {
            return 0;
        }
        position += sequence_length;
    }
    return 1;
}

static void json_path_member(char* output, size_t capacity,
                             const char* parent, const char* key,
                             size_t key_length) {
    size_t i;
    int simple = key_length > 0 &&
                 ((key[0] >= 'a' && key[0] <= 'z') ||
                  (key[0] >= 'A' && key[0] <= 'Z') || key[0] == '_');
    for (i = 1; simple && i < key_length; ++i) {
        unsigned char c = (unsigned char)key[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_')) {
            simple = 0;
        }
    }
    if (simple && strlen(parent) + key_length + 2u < capacity) {
        snprintf(output, capacity, "%s.%.*s", parent, (int)key_length, key);
        return;
    }
    snprintf(output, capacity, "%s[\"%.*s%s\"]", parent,
             (int)(key_length > 40u ? 40u : key_length), key,
             key_length > 40u ? "..." : "");
}

static void json_path_index(char* output, size_t capacity,
                            const char* parent, size_t index) {
    snprintf(output, capacity, "%s[%u]", parent, (unsigned)index);
}

static void json_encoder_error(JsonEncoder* encoder, const char* path,
                               const char* message) {
    if (!encoder || encoder->error[0]) return;
    snprintf(encoder->error, sizeof(encoder->error),
             "JSON encode error at %s: %s",
             path ? path : "$", message ? message : "invalid value");
}

static int json_encode_string(JsonEncoder* encoder, const char* text,
                              size_t length, const char* path) {
    static const char hex[] = "0123456789abcdef";
    size_t position = 0;
    if (length > MOD_JSON_MAX_STRING_BYTES) {
        json_encoder_error(encoder, path, "string exceeds 262144 bytes");
        return 0;
    }
    if (!json_utf8_valid(text, length)) {
        json_encoder_error(encoder, path, "string is not valid UTF-8");
        return 0;
    }
    if (!json_buffer_append_char(&encoder->output, '"')) goto output_limit;
    while (position < length) {
        unsigned char c = (unsigned char)text[position];
        if (c == '"' || c == '\\') {
            char escaped[2] = {'\\', (char)c};
            if (!json_buffer_append(&encoder->output, escaped, 2u)) {
                goto output_limit;
            }
            ++position;
        } else if (c == '\b' || c == '\f' || c == '\n' ||
                   c == '\r' || c == '\t') {
            char escaped[2] = {'\\', 'n'};
            if (c == '\b') escaped[1] = 'b';
            else if (c == '\f') escaped[1] = 'f';
            else if (c == '\r') escaped[1] = 'r';
            else if (c == '\t') escaped[1] = 't';
            if (!json_buffer_append(&encoder->output, escaped, 2u)) {
                goto output_limit;
            }
            ++position;
        } else if (c < 0x20u) {
            char escaped[6] = {
                '\\', 'u', '0', '0', hex[(c >> 4) & 0xfu], hex[c & 0xfu]
            };
            if (!json_buffer_append(&encoder->output, escaped, 6u)) {
                goto output_limit;
            }
            ++position;
        } else {
            size_t sequence_length = 1;
            if (c >= 0x80u &&
                !json_utf8_sequence(
                    (const unsigned char*)text + position,
                    length - position,
                    &sequence_length)) {
                json_encoder_error(encoder, path, "string is not valid UTF-8");
                return 0;
            }
            if (!json_buffer_append(&encoder->output, text + position,
                                    sequence_length)) {
                goto output_limit;
            }
            position += sequence_length;
        }
    }
    if (!json_buffer_append_char(&encoder->output, '"')) goto output_limit;
    return 1;

output_limit:
    json_encoder_error(encoder, path,
                       "encoded output exceeds 1048576 bytes or memory is exhausted");
    return 0;
}

static void json_set_table_kind(lua_State* L, int index, void* kind) {
    index = json_absindex(L, index);
    lua_newtable(L);
    lua_pushlightuserdata(L, &k_json_kind_key);
    lua_pushlightuserdata(L, kind);
    lua_rawset(L, -3);
    lua_setmetatable(L, index);
}

static void* json_table_kind(lua_State* L, int index) {
    void* result = NULL;
    index = json_absindex(L, index);
    if (!lua_getmetatable(L, index)) return NULL;
    lua_pushlightuserdata(L, &k_json_kind_key);
    lua_rawget(L, -2);
    if (lua_islightuserdata(L, -1)) result = lua_touserdata(L, -1);
    lua_pop(L, 2);
    return result;
}

int mod_json_table_kind(lua_State* L, int index) {
    void* kind;
    if (!lua_istable(L, index)) return 0;
    kind = json_table_kind(L, index);
    return kind == &k_json_array_kind ? 1 : kind == &k_json_object_kind ? 2 : 0;
}


static int json_object_key_compare(const void* left, const void* right) {
    const JsonObjectKey* a = (const JsonObjectKey*)left;
    const JsonObjectKey* b = (const JsonObjectKey*)right;
    size_t common = a->length < b->length ? a->length : b->length;
    int compared = memcmp(a->text, b->text, common);
    if (compared != 0) return compared;
    if (a->length < b->length) return -1;
    if (a->length > b->length) return 1;
    return 0;
}

static int json_encode_value(JsonEncoder* encoder, int index, int depth,
                             const char* path);

static int json_encode_table(JsonEncoder* encoder, int index, int depth,
                             const char* path) {
    lua_State* L = encoder->L;
    int absolute = json_absindex(L, index);
    void* tagged_kind = json_table_kind(L, absolute);
    const void* identity = lua_topointer(L, absolute);
    size_t integer_count = 0;
    size_t string_count = 0;
    size_t maximum_index = 0;
    int i;
    int encode_array;

    if (depth >= MOD_JSON_MAX_DEPTH) {
        json_encoder_error(encoder, path, "maximum depth 32 exceeded");
        return 0;
    }
    for (i = 0; i < depth; ++i) {
        if (encoder->table_stack[i] == identity) {
            json_encoder_error(encoder, path, "table cycle detected");
            return 0;
        }
    }
    encoder->table_stack[depth] = identity;

    lua_pushnil(L);
    while (lua_next(L, absolute) != 0) {
        int key_type = lua_type(L, -2);
        if (key_type == LUA_TSTRING) {
            ++string_count;
        } else if (key_type == LUA_TNUMBER) {
            lua_Number number = lua_tonumber(L, -2);
            lua_Integer integer = lua_tointeger(L, -2);
            if (number != (lua_Number)integer || integer < 1) {
                lua_pop(L, 2);
                json_encoder_error(
                    encoder, path,
                    "array keys must be positive consecutive integers");
                return 0;
            }
            ++integer_count;
            if ((size_t)integer > maximum_index) maximum_index = (size_t)integer;
        } else {
            lua_pop(L, 2);
            json_encoder_error(
                encoder, path,
                "object keys must be strings and array keys must be integers");
            return 0;
        }
        if (integer_count + string_count > MOD_JSON_MAX_NODES) {
            lua_pop(L, 2);
            json_encoder_error(encoder, path, "table has too many members");
            return 0;
        }
        lua_pop(L, 1);
    }

    if (tagged_kind == &k_json_array_kind) {
        if (string_count != 0) {
            json_encoder_error(encoder, path,
                               "explicit JSON array contains string keys");
            return 0;
        }
        encode_array = 1;
    } else if (tagged_kind == &k_json_object_kind) {
        if (integer_count != 0) {
            json_encoder_error(encoder, path,
                               "explicit JSON object contains numeric keys");
            return 0;
        }
        encode_array = 0;
    } else if (integer_count != 0 && string_count != 0) {
        json_encoder_error(encoder, path,
                           "mixed string and numeric table keys are ambiguous");
        return 0;
    } else {
        encode_array = integer_count != 0;
    }

    if (encode_array) {
        size_t array_index;
        if (maximum_index != integer_count) {
            json_encoder_error(encoder, path,
                               "sparse JSON arrays are not supported");
            return 0;
        }
        if (!json_buffer_append_char(&encoder->output, '[')) goto output_limit;
        for (array_index = 1; array_index <= maximum_index; ++array_index) {
            char child_path[256];
            if (array_index > 1 &&
                !json_buffer_append_char(&encoder->output, ',')) {
                goto output_limit;
            }
            lua_rawgeti(L, absolute, (int)array_index);
            json_path_index(child_path, sizeof(child_path), path, array_index);
            if (!json_encode_value(encoder, -1, depth + 1, child_path)) {
                lua_pop(L, 1);
                return 0;
            }
            lua_pop(L, 1);
        }
        if (!json_buffer_append_char(&encoder->output, ']')) goto output_limit;
        return 1;
    } else {
        JsonObjectKey* keys = NULL;
        size_t key_index = 0;
        if (string_count != 0) {
            keys = (JsonObjectKey*)calloc(string_count, sizeof(*keys));
            if (!keys) {
                json_encoder_error(encoder, path,
                                   "memory exhausted while sorting object keys");
                return 0;
            }
            lua_pushnil(L);
            while (lua_next(L, absolute) != 0) {
                keys[key_index].text =
                    lua_tolstring(L, -2, &keys[key_index].length);
                ++key_index;
                lua_pop(L, 1);
            }
            qsort(keys, string_count, sizeof(*keys), json_object_key_compare);
        }
        if (!json_buffer_append_char(&encoder->output, '{')) {
            free(keys);
            goto output_limit;
        }
        for (key_index = 0; key_index < string_count; ++key_index) {
            char child_path[256];
            if (key_index != 0 &&
                !json_buffer_append_char(&encoder->output, ',')) {
                free(keys);
                goto output_limit;
            }
            json_path_member(child_path, sizeof(child_path), path,
                             keys[key_index].text, keys[key_index].length);
            if (!json_encode_string(encoder, keys[key_index].text,
                                    keys[key_index].length, child_path) ||
                !json_buffer_append_char(&encoder->output, ':')) {
                if (!encoder->error[0]) {
                    json_encoder_error(
                        encoder, child_path,
                        "encoded output exceeds 1048576 bytes or memory is exhausted"
                    );
                }
                free(keys);
                return 0;
            }
            lua_pushlstring(L, keys[key_index].text, keys[key_index].length);
            lua_rawget(L, absolute);
            if (!json_encode_value(encoder, -1, depth + 1, child_path)) {
                lua_pop(L, 1);
                free(keys);
                return 0;
            }
            lua_pop(L, 1);
        }
        free(keys);
        if (!json_buffer_append_char(&encoder->output, '}')) goto output_limit;
        return 1;
    }

output_limit:
    json_encoder_error(encoder, path,
                       "encoded output exceeds 1048576 bytes or memory is exhausted");
    return 0;
}

static int json_encode_value(JsonEncoder* encoder, int index, int depth,
                             const char* path) {
    lua_State* L = encoder->L;
    int type;
    if (++encoder->nodes > MOD_JSON_MAX_NODES) {
        json_encoder_error(encoder, path, "maximum node count 65536 exceeded");
        return 0;
    }
    type = lua_type(L, index);
    switch (type) {
        case LUA_TNIL:
            json_encoder_error(
                encoder, path,
                "nil is not a JSON value; use mod.json.null for JSON null");
            return 0;
        case LUA_TBOOLEAN:
            if (!json_buffer_append(
                    &encoder->output,
                    lua_toboolean(L, index) ? "true" : "false",
                    lua_toboolean(L, index) ? 4u : 5u)) {
                json_encoder_error(
                    encoder, path,
                    "encoded output exceeds 1048576 bytes or memory is exhausted"
                );
                return 0;
            }
            return 1;
        case LUA_TNUMBER: {
            lua_Number number = lua_tonumber(L, index);
            char encoded[64];
            int length;
            if (!isfinite((double)number)) {
                json_encoder_error(encoder, path,
                                   "non-finite numbers are not valid JSON");
                return 0;
            }
            if (number == 0) number = 0;
            length = _snprintf_l(encoded, sizeof(encoded), "%.17g",
                                 encoder->numeric_locale, (double)number);
            if (length <= 0 || (size_t)length >= sizeof(encoded) ||
                !json_buffer_append(&encoder->output, encoded, (size_t)length)) {
                json_encoder_error(encoder, path,
                                   "could not encode finite number");
                return 0;
            }
            return 1;
        }
        case LUA_TSTRING: {
            size_t length = 0;
            const char* text = lua_tolstring(L, index, &length);
            return json_encode_string(encoder, text, length, path);
        }
        case LUA_TLIGHTUSERDATA:
            if (lua_touserdata(L, index) == &k_json_null) {
                if (!json_buffer_append(&encoder->output, "null", 4u)) {
                    json_encoder_error(encoder, path,
                                       "encoded output exceeds 1048576 bytes");
                    return 0;
                }
                return 1;
            }
            json_encoder_error(encoder, path,
                               "lightuserdata is not a JSON value");
            return 0;
        case LUA_TTABLE:
            return json_encode_table(encoder, index, depth, path);
        default:
            json_encoder_error(
                encoder, path,
                "supported values are boolean, finite number, UTF-8 string, table, and mod.json.null"
            );
            return 0;
    }
}

static void json_parser_error(JsonParser* parser, const char* path,
                              const char* message) {
    if (!parser || parser->error[0]) return;
    snprintf(parser->error, sizeof(parser->error),
             "JSON decode error at %s byte %u: %s",
             path ? path : "$", (unsigned)parser->position,
             message ? message : "invalid JSON");
}

static void json_parser_skip_ws(JsonParser* parser) {
    while (parser->position < parser->length) {
        char c = parser->text[parser->position];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        ++parser->position;
    }
}

static int json_hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}

static int json_buffer_append_codepoint(JsonBuffer* buffer,
                                        unsigned int codepoint) {
    char encoded[4];
    size_t length;
    if (codepoint <= 0x7fu) {
        encoded[0] = (char)codepoint;
        length = 1;
    } else if (codepoint <= 0x7ffu) {
        encoded[0] = (char)(0xc0u | (codepoint >> 6));
        encoded[1] = (char)(0x80u | (codepoint & 0x3fu));
        length = 2;
    } else if (codepoint <= 0xffffu) {
        encoded[0] = (char)(0xe0u | (codepoint >> 12));
        encoded[1] = (char)(0x80u | ((codepoint >> 6) & 0x3fu));
        encoded[2] = (char)(0x80u | (codepoint & 0x3fu));
        length = 3;
    } else if (codepoint <= 0x10ffffu) {
        encoded[0] = (char)(0xf0u | (codepoint >> 18));
        encoded[1] = (char)(0x80u | ((codepoint >> 12) & 0x3fu));
        encoded[2] = (char)(0x80u | ((codepoint >> 6) & 0x3fu));
        encoded[3] = (char)(0x80u | (codepoint & 0x3fu));
        length = 4;
    } else {
        return 0;
    }
    return json_buffer_append(buffer, encoded, length);
}

static int json_parse_hex4(JsonParser* parser, unsigned int* value,
                           const char* path) {
    int i;
    unsigned int result = 0;
    if (parser->length - parser->position < 4u) {
        json_parser_error(parser, path, "incomplete Unicode escape");
        return 0;
    }
    for (i = 0; i < 4; ++i) {
        int digit = json_hex_value(parser->text[parser->position + (size_t)i]);
        if (digit < 0) {
            json_parser_error(parser, path, "invalid Unicode escape");
            return 0;
        }
        result = (result << 4) | (unsigned int)digit;
    }
    parser->position += 4u;
    *value = result;
    return 1;
}

static int json_parse_string(JsonParser* parser, const char* path) {
    JsonBuffer decoded;
    memset(&decoded, 0, sizeof(decoded));
    decoded.limit = MOD_JSON_MAX_STRING_BYTES;
    if (parser->position >= parser->length ||
        parser->text[parser->position] != '"') {
        json_parser_error(parser, path, "expected string");
        return 0;
    }
    ++parser->position;
    while (parser->position < parser->length) {
        unsigned char c = (unsigned char)parser->text[parser->position++];
        if (c == '"') {
            lua_pushlstring(parser->L, decoded.data ? decoded.data : "",
                            decoded.length);
            json_buffer_free(&decoded);
            return 1;
        }
        if (c < 0x20u) {
            json_parser_error(parser, path,
                              "unescaped control character in string");
            json_buffer_free(&decoded);
            return 0;
        }
        if (c == '\\') {
            char escape;
            if (parser->position >= parser->length) {
                json_parser_error(parser, path, "incomplete string escape");
                json_buffer_free(&decoded);
                return 0;
            }
            escape = parser->text[parser->position++];
            if (escape == '"' || escape == '\\' || escape == '/') {
                if (!json_buffer_append_char(&decoded, escape)) goto too_large;
            } else if (escape == 'b') {
                if (!json_buffer_append_char(&decoded, '\b')) goto too_large;
            } else if (escape == 'f') {
                if (!json_buffer_append_char(&decoded, '\f')) goto too_large;
            } else if (escape == 'n') {
                if (!json_buffer_append_char(&decoded, '\n')) goto too_large;
            } else if (escape == 'r') {
                if (!json_buffer_append_char(&decoded, '\r')) goto too_large;
            } else if (escape == 't') {
                if (!json_buffer_append_char(&decoded, '\t')) goto too_large;
            } else if (escape == 'u') {
                unsigned int first;
                unsigned int codepoint;
                if (!json_parse_hex4(parser, &first, path)) {
                    json_buffer_free(&decoded);
                    return 0;
                }
                if (first >= 0xd800u && first <= 0xdbffu) {
                    unsigned int second;
                    if (parser->length - parser->position < 6u ||
                        parser->text[parser->position] != '\\' ||
                        parser->text[parser->position + 1u] != 'u') {
                        json_parser_error(parser, path,
                                          "high surrogate lacks low surrogate");
                        json_buffer_free(&decoded);
                        return 0;
                    }
                    parser->position += 2u;
                    if (!json_parse_hex4(parser, &second, path)) {
                        json_buffer_free(&decoded);
                        return 0;
                    }
                    if (second < 0xdc00u || second > 0xdfffu) {
                        json_parser_error(parser, path,
                                          "invalid low surrogate");
                        json_buffer_free(&decoded);
                        return 0;
                    }
                    codepoint = 0x10000u +
                        (((first - 0xd800u) << 10) | (second - 0xdc00u));
                } else if (first >= 0xdc00u && first <= 0xdfffu) {
                    json_parser_error(parser, path,
                                      "unexpected low surrogate");
                    json_buffer_free(&decoded);
                    return 0;
                } else {
                    codepoint = first;
                }
                if (!json_buffer_append_codepoint(&decoded, codepoint)) {
                    goto too_large;
                }
            } else {
                json_parser_error(parser, path, "invalid string escape");
                json_buffer_free(&decoded);
                return 0;
            }
        } else if (c >= 0x80u) {
            size_t sequence_length = 0;
            size_t sequence_start = parser->position - 1u;
            if (!json_utf8_sequence(
                    (const unsigned char*)parser->text + sequence_start,
                    parser->length - sequence_start,
                    &sequence_length)) {
                json_parser_error(parser, path, "string is not valid UTF-8");
                json_buffer_free(&decoded);
                return 0;
            }
            if (!json_buffer_append(&decoded,
                                    parser->text + sequence_start,
                                    sequence_length)) {
                goto too_large;
            }
            parser->position = sequence_start + sequence_length;
        } else if (!json_buffer_append_char(&decoded, (char)c)) {
            goto too_large;
        }
    }
    json_parser_error(parser, path, "unterminated string");
    json_buffer_free(&decoded);
    return 0;

too_large:
    json_parser_error(parser, path,
                      "decoded string exceeds 262144 bytes or memory is exhausted");
    json_buffer_free(&decoded);
    return 0;
}

static int json_parse_value(JsonParser* parser, int depth, const char* path);

static int json_parse_array(JsonParser* parser, int depth, const char* path) {
    size_t index = 1;
    lua_newtable(parser->L);
    json_set_table_kind(parser->L, -1, &k_json_array_kind);
    ++parser->position;
    json_parser_skip_ws(parser);
    if (parser->position < parser->length &&
        parser->text[parser->position] == ']') {
        ++parser->position;
        return 1;
    }
    while (parser->position < parser->length) {
        char child_path[256];
        json_path_index(child_path, sizeof(child_path), path, index);
        if (!json_parse_value(parser, depth + 1, child_path)) {
            lua_pop(parser->L, 1);
            return 0;
        }
        lua_rawseti(parser->L, -2, (int)index);
        ++index;
        json_parser_skip_ws(parser);
        if (parser->position < parser->length &&
            parser->text[parser->position] == ',') {
            ++parser->position;
            json_parser_skip_ws(parser);
            continue;
        }
        if (parser->position < parser->length &&
            parser->text[parser->position] == ']') {
            ++parser->position;
            return 1;
        }
        json_parser_error(parser, path, "expected ',' or ']'");
        lua_pop(parser->L, 1);
        return 0;
    }
    json_parser_error(parser, path, "unterminated array");
    lua_pop(parser->L, 1);
    return 0;
}

static int json_parse_object(JsonParser* parser, int depth,
                             const char* path) {
    lua_newtable(parser->L);
    json_set_table_kind(parser->L, -1, &k_json_object_kind);
    ++parser->position;
    json_parser_skip_ws(parser);
    if (parser->position < parser->length &&
        parser->text[parser->position] == '}') {
        ++parser->position;
        return 1;
    }
    while (parser->position < parser->length) {
        const char* key;
        size_t key_length;
        char child_path[256];
        if (!json_parse_string(parser, path)) {
            lua_pop(parser->L, 1);
            return 0;
        }
        key = lua_tolstring(parser->L, -1, &key_length);
        json_path_member(child_path, sizeof(child_path), path, key, key_length);

        lua_pushvalue(parser->L, -1);
        lua_rawget(parser->L, -3);
        if (!lua_isnil(parser->L, -1)) {
            lua_pop(parser->L, 2);
            json_parser_error(parser, child_path, "duplicate object key");
            lua_pop(parser->L, 1);
            return 0;
        }
        lua_pop(parser->L, 1);

        json_parser_skip_ws(parser);
        if (parser->position >= parser->length ||
            parser->text[parser->position] != ':') {
            lua_pop(parser->L, 2);
            json_parser_error(parser, child_path, "expected ':'");
            return 0;
        }
        ++parser->position;
        if (!json_parse_value(parser, depth + 1, child_path)) {
            lua_pop(parser->L, 2);
            return 0;
        }
        lua_rawset(parser->L, -3);
        json_parser_skip_ws(parser);
        if (parser->position < parser->length &&
            parser->text[parser->position] == ',') {
            ++parser->position;
            json_parser_skip_ws(parser);
            continue;
        }
        if (parser->position < parser->length &&
            parser->text[parser->position] == '}') {
            ++parser->position;
            return 1;
        }
        json_parser_error(parser, path, "expected ',' or '}'");
        lua_pop(parser->L, 1);
        return 0;
    }
    json_parser_error(parser, path, "unterminated object");
    lua_pop(parser->L, 1);
    return 0;
}

static int json_parse_number(JsonParser* parser, const char* path) {
    size_t start = parser->position;
    size_t length;
    char number_text[128];
    char* end = NULL;
    double value;

    if (parser->text[parser->position] == '-') ++parser->position;
    if (parser->position >= parser->length) goto invalid;
    if (parser->text[parser->position] == '0') {
        ++parser->position;
        if (parser->position < parser->length &&
            parser->text[parser->position] >= '0' &&
            parser->text[parser->position] <= '9') {
            goto invalid;
        }
    } else if (parser->text[parser->position] >= '1' &&
               parser->text[parser->position] <= '9') {
        do {
            ++parser->position;
        } while (parser->position < parser->length &&
                 parser->text[parser->position] >= '0' &&
                 parser->text[parser->position] <= '9');
    } else {
        goto invalid;
    }
    if (parser->position < parser->length &&
        parser->text[parser->position] == '.') {
        ++parser->position;
        if (parser->position >= parser->length ||
            parser->text[parser->position] < '0' ||
            parser->text[parser->position] > '9') {
            goto invalid;
        }
        do {
            ++parser->position;
        } while (parser->position < parser->length &&
                 parser->text[parser->position] >= '0' &&
                 parser->text[parser->position] <= '9');
    }
    if (parser->position < parser->length &&
        (parser->text[parser->position] == 'e' ||
         parser->text[parser->position] == 'E')) {
        ++parser->position;
        if (parser->position < parser->length &&
            (parser->text[parser->position] == '+' ||
             parser->text[parser->position] == '-')) {
            ++parser->position;
        }
        if (parser->position >= parser->length ||
            parser->text[parser->position] < '0' ||
            parser->text[parser->position] > '9') {
            goto invalid;
        }
        do {
            ++parser->position;
        } while (parser->position < parser->length &&
                 parser->text[parser->position] >= '0' &&
                 parser->text[parser->position] <= '9');
    }
    length = parser->position - start;
    if (length == 0 || length >= sizeof(number_text)) goto invalid;
    memcpy(number_text, parser->text + start, length);
    number_text[length] = '\0';
    errno = 0;
    value = _strtod_l(number_text, &end, parser->numeric_locale);
    if (errno == ERANGE || !end || *end != '\0' || !isfinite(value)) {
        json_parser_error(parser, path,
                          "number is outside the finite double range");
        return 0;
    }
    lua_pushnumber(parser->L, (lua_Number)value);
    return 1;

invalid:
    json_parser_error(parser, path, "invalid JSON number");
    return 0;
}

static int json_parse_literal(JsonParser* parser, const char* path,
                              const char* literal, int kind) {
    size_t length = strlen(literal);
    if (parser->length - parser->position < length ||
        memcmp(parser->text + parser->position, literal, length) != 0) {
        json_parser_error(parser, path, "invalid JSON literal");
        return 0;
    }
    parser->position += length;
    if (kind == 0) lua_pushboolean(parser->L, 0);
    else if (kind == 1) lua_pushboolean(parser->L, 1);
    else mod_json_lua_push_null(parser->L);
    return 1;
}

static int json_parse_value(JsonParser* parser, int depth, const char* path) {
    char c;
    json_parser_skip_ws(parser);
    if (++parser->nodes > MOD_JSON_MAX_NODES) {
        json_parser_error(parser, path, "maximum node count 65536 exceeded");
        return 0;
    }
    if (depth >= MOD_JSON_MAX_DEPTH) {
        json_parser_error(parser, path, "maximum depth 32 exceeded");
        return 0;
    }
    if (parser->position >= parser->length) {
        json_parser_error(parser, path, "expected value");
        return 0;
    }
    c = parser->text[parser->position];
    if (c == '"') return json_parse_string(parser, path);
    if (c == '{') return json_parse_object(parser, depth, path);
    if (c == '[') return json_parse_array(parser, depth, path);
    if (c == 't') return json_parse_literal(parser, path, "true", 1);
    if (c == 'f') return json_parse_literal(parser, path, "false", 0);
    if (c == 'n') return json_parse_literal(parser, path, "null", 2);
    if (c == '-' || (c >= '0' && c <= '9')) {
        return json_parse_number(parser, path);
    }
    json_parser_error(parser, path, "unexpected token");
    return 0;
}

int mod_json_lua_encode(lua_State* L) {
    JsonEncoder encoder;
    memset(&encoder, 0, sizeof(encoder));
    encoder.L = L;
    encoder.output.limit = MOD_JSON_MAX_OUTPUT_BYTES;
    encoder.numeric_locale = _create_locale(LC_NUMERIC, "C");
    if (!encoder.numeric_locale) {
        lua_pushnil(L);
        lua_pushstring(L,
                       "JSON encode error at $: could not create "
                       "locale-independent number formatter");
        return 2;
    }
    if (!json_encode_value(&encoder, 1, 0, "$")) {
        _free_locale(encoder.numeric_locale);
        json_buffer_free(&encoder.output);
        lua_pushnil(L);
        lua_pushstring(L, encoder.error[0] ? encoder.error :
                       "JSON encode error: unknown failure");
        return 2;
    }
    lua_pushlstring(L, encoder.output.data ? encoder.output.data : "",
                    encoder.output.length);
    _free_locale(encoder.numeric_locale);
    json_buffer_free(&encoder.output);
    return 1;
}

int mod_json_lua_decode(lua_State* L) {
    JsonParser parser;
    size_t length = 0;
    const char* text = luaL_checklstring(L, 1, &length);
    memset(&parser, 0, sizeof(parser));
    parser.L = L;
    parser.text = text;
    parser.length = length;
    if (length > MOD_JSON_MAX_INPUT_BYTES) {
        lua_pushnil(L);
        lua_pushstring(L, "JSON decode error at $ byte 0: input exceeds 1048576 bytes");
        return 2;
    }
    parser.numeric_locale = _create_locale(LC_NUMERIC, "C");
    if (!parser.numeric_locale) {
        lua_pushnil(L);
        lua_pushstring(L,
                       "JSON decode error at $ byte 0: could not create "
                       "locale-independent number parser");
        return 2;
    }
    if (!json_parse_value(&parser, 0, "$")) {
        _free_locale(parser.numeric_locale);
        lua_pushnil(L);
        lua_pushstring(L, parser.error[0] ? parser.error :
                       "JSON decode error at $: unknown failure");
        return 2;
    }
    json_parser_skip_ws(&parser);
    if (parser.position != parser.length) {
        lua_pop(L, 1);
        json_parser_error(&parser, "$", "unexpected trailing data");
        _free_locale(parser.numeric_locale);
        lua_pushnil(L);
        lua_pushstring(L, parser.error);
        return 2;
    }
    _free_locale(parser.numeric_locale);
    return 1;
}

static int json_lua_copy_with_kind(lua_State* L, void* kind) {
    int source = 0;
    lua_newtable(L);
    if (!lua_isnoneornil(L, 1)) {
        luaL_checktype(L, 1, LUA_TTABLE);
        source = json_absindex(L, 1);
        lua_pushnil(L);
        while (lua_next(L, source) != 0) {
            lua_pushvalue(L, -2);
            lua_pushvalue(L, -2);
            lua_rawset(L, -5);
            lua_pop(L, 1);
        }
    }
    json_set_table_kind(L, -1, kind);
    return 1;
}

int mod_json_lua_array(lua_State* L) {
    return json_lua_copy_with_kind(L, &k_json_array_kind);
}

int mod_json_lua_object(lua_State* L) {
    return json_lua_copy_with_kind(L, &k_json_object_kind);
}

int mod_json_lua_is_null(lua_State* L) {
    lua_pushboolean(L, lua_islightuserdata(L, 1) &&
                        lua_touserdata(L, 1) == &k_json_null);
    return 1;
}

void mod_json_lua_push_null(lua_State* L) {
    lua_pushlightuserdata(L, &k_json_null);
}
