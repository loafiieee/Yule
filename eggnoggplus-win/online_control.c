#include "online_control.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define ONLINE_CONTROL_JSON_MAX_FIELDS 64u
#define ONLINE_CONTROL_JSON_MAX_KEY_BYTES 63u

typedef enum OnlineControlWantedType {
    ONLINE_CONTROL_WANT_NONE = 0,
    ONLINE_CONTROL_WANT_STRING,
    ONLINE_CONTROL_WANT_INT
} OnlineControlWantedType;

typedef enum OnlineControlValueType {
    ONLINE_CONTROL_VALUE_STRING = 1,
    ONLINE_CONTROL_VALUE_NUMBER,
    ONLINE_CONTROL_VALUE_BOOL,
    ONLINE_CONTROL_VALUE_NULL
} OnlineControlValueType;

typedef struct OnlineControlJsonCursor {
    const unsigned char* cursor;
    char* error;
    size_t error_cap;
    int failed;
} OnlineControlJsonCursor;

typedef struct OnlineControlJsonScan {
    const char* wanted_key;
    OnlineControlWantedType wanted_type;
    char* string_out;
    size_t string_cap;
    int* int_out;
    int found;
    OnlineControlJsonResult wanted_result;
} OnlineControlJsonScan;

static void json_error(OnlineControlJsonCursor* json, const char* format, ...) {
    va_list args;
    if (!json || json->failed) return;
    json->failed = 1;
    if (!json->error || json->error_cap == 0) return;
    va_start(args, format);
    vsnprintf(json->error, json->error_cap, format, args);
    va_end(args);
    json->error[json->error_cap - 1u] = '\0';
}

static void skip_ws(OnlineControlJsonCursor* json) {
    while (json && (*json->cursor == ' ' || *json->cursor == '\t' ||
                    *json->cursor == '\r' || *json->cursor == '\n')) {
        json->cursor++;
    }
}

static int hex_value(unsigned char ch) {
    if (ch >= '0' && ch <= '9') return (int)(ch - '0');
    if (ch >= 'a' && ch <= 'f') return (int)(ch - 'a') + 10;
    if (ch >= 'A' && ch <= 'F') return (int)(ch - 'A') + 10;
    return -1;
}

static int parse_hex4(OnlineControlJsonCursor* json, uint32_t* out) {
    uint32_t value = 0;
    int i;
    if (!json || !out) return 0;
    for (i = 0; i < 4; i++) {
        unsigned char ch = *json->cursor;
        int digit;
        if (!ch) {
            json_error(json, "truncated Unicode escape");
            return 0;
        }
        digit = hex_value(ch);
        if (digit < 0) {
            json_error(json, "invalid Unicode escape");
            return 0;
        }
        value = (value << 4) | (uint32_t)digit;
        json->cursor++;
    }
    *out = value;
    return 1;
}

static int append_decoded(OnlineControlJsonCursor* json,
                          char* out,
                          size_t out_cap,
                          size_t* decoded_length,
                          int* too_small,
                          const unsigned char* bytes,
                          size_t byte_count) {
    size_t old_length;
    if (!json || !decoded_length || !too_small || !bytes || byte_count == 0) return 0;
    old_length = *decoded_length;
    if (old_length > SIZE_MAX - byte_count) {
        json_error(json, "decoded JSON string is too large");
        return 0;
    }
    *decoded_length = old_length + byte_count;
    if (out) {
        if (old_length + byte_count >= out_cap) {
            *too_small = 1;
        } else if (!*too_small) {
            memcpy(out + old_length, bytes, byte_count);
        }
    }
    return 1;
}

static int append_codepoint(OnlineControlJsonCursor* json,
                            char* out,
                            size_t out_cap,
                            size_t* decoded_length,
                            int* too_small,
                            uint32_t codepoint) {
    unsigned char bytes[4];
    size_t count;
    if (codepoint < 0x20u || codepoint > 0x10ffffu ||
        (codepoint >= 0xd800u && codepoint <= 0xdfffu)) {
        json_error(json, "JSON protocol strings cannot contain control/invalid Unicode values");
        return 0;
    }
    if (codepoint <= 0x7fu) {
        bytes[0] = (unsigned char)codepoint;
        count = 1;
    } else if (codepoint <= 0x7ffu) {
        bytes[0] = (unsigned char)(0xc0u | (codepoint >> 6));
        bytes[1] = (unsigned char)(0x80u | (codepoint & 0x3fu));
        count = 2;
    } else if (codepoint <= 0xffffu) {
        bytes[0] = (unsigned char)(0xe0u | (codepoint >> 12));
        bytes[1] = (unsigned char)(0x80u | ((codepoint >> 6) & 0x3fu));
        bytes[2] = (unsigned char)(0x80u | (codepoint & 0x3fu));
        count = 3;
    } else {
        bytes[0] = (unsigned char)(0xf0u | (codepoint >> 18));
        bytes[1] = (unsigned char)(0x80u | ((codepoint >> 12) & 0x3fu));
        bytes[2] = (unsigned char)(0x80u | ((codepoint >> 6) & 0x3fu));
        bytes[3] = (unsigned char)(0x80u | (codepoint & 0x3fu));
        count = 4;
    }
    return append_decoded(json, out, out_cap, decoded_length, too_small,
                          bytes, count);
}

static int append_raw_utf8(OnlineControlJsonCursor* json,
                           char* out,
                           size_t out_cap,
                           size_t* decoded_length,
                           int* too_small,
                           unsigned char first) {
    uint32_t codepoint;
    uint32_t minimum;
    int continuation_count;
    int i;
    if (first >= 0xc2u && first <= 0xdfu) {
        codepoint = (uint32_t)(first & 0x1fu);
        minimum = 0x80u;
        continuation_count = 1;
    } else if (first >= 0xe0u && first <= 0xefu) {
        codepoint = (uint32_t)(first & 0x0fu);
        minimum = 0x800u;
        continuation_count = 2;
    } else if (first >= 0xf0u && first <= 0xf4u) {
        codepoint = (uint32_t)(first & 0x07u);
        minimum = 0x10000u;
        continuation_count = 3;
    } else {
        json_error(json, "invalid UTF-8 leading byte in JSON string");
        return 0;
    }
    for (i = 0; i < continuation_count; i++) {
        unsigned char ch = *json->cursor;
        if (!ch || (ch & 0xc0u) != 0x80u) {
            json_error(json, "invalid/truncated UTF-8 in JSON string");
            return 0;
        }
        json->cursor++;
        codepoint = (codepoint << 6) | (uint32_t)(ch & 0x3fu);
    }
    if (codepoint < minimum) {
        json_error(json, "overlong UTF-8 in JSON string");
        return 0;
    }
    return append_codepoint(json, out, out_cap, decoded_length, too_small,
                            codepoint);
}

static int parse_string(OnlineControlJsonCursor* json,
                        char* out,
                        size_t out_cap,
                        size_t* out_length,
                        int* out_too_small) {
    size_t decoded_length = 0;
    int too_small = 0;
    if (!json || *json->cursor != '"') {
        json_error(json, "expected JSON string");
        return 0;
    }
    if (out && out_cap > 0) out[0] = '\0';
    json->cursor++;
    while (*json->cursor) {
        unsigned char ch = *json->cursor++;
        if (ch == '"') {
            if (out) {
                if (out_cap == 0 || too_small || decoded_length >= out_cap) {
                    if (out_cap > 0) out[0] = '\0';
                    too_small = 1;
                } else {
                    out[decoded_length] = '\0';
                }
            }
            if (out_length) *out_length = decoded_length;
            if (out_too_small) *out_too_small = too_small;
            return 1;
        }
        if (ch < 0x20u) {
            json_error(json, "unescaped control character in JSON string");
            return 0;
        }
        if (ch >= 0x80u) {
            if (!append_raw_utf8(json, out, out_cap, &decoded_length,
                                 &too_small, ch)) return 0;
            continue;
        }
        if (ch != '\\') {
            if (!append_decoded(json, out, out_cap, &decoded_length,
                                &too_small, &ch, 1u)) return 0;
            continue;
        }
        ch = *json->cursor++;
        if (!ch) {
            json_error(json, "unterminated JSON escape");
            return 0;
        }
        if (ch == '"' || ch == '\\' || ch == '/') {
            if (!append_decoded(json, out, out_cap, &decoded_length,
                                &too_small, &ch, 1u)) return 0;
        } else if (ch == 'u') {
            uint32_t codepoint;
            if (!parse_hex4(json, &codepoint)) return 0;
            if (codepoint >= 0xd800u && codepoint <= 0xdbffu) {
                uint32_t low;
                if (*json->cursor != '\\') {
                    json_error(json, "high surrogate is missing its low surrogate");
                    return 0;
                }
                json->cursor++;
                if (*json->cursor != 'u') {
                    json_error(json, "high surrogate is missing its low surrogate");
                    return 0;
                }
                json->cursor++;
                if (!parse_hex4(json, &low)) return 0;
                if (low < 0xdc00u || low > 0xdfffu) {
                    json_error(json, "invalid low surrogate");
                    return 0;
                }
                codepoint = 0x10000u + ((codepoint - 0xd800u) << 10) +
                            (low - 0xdc00u);
            } else if (codepoint >= 0xdc00u && codepoint <= 0xdfffu) {
                json_error(json, "unexpected low surrogate");
                return 0;
            }
            if (!append_codepoint(json, out, out_cap, &decoded_length,
                                  &too_small, codepoint)) return 0;
        } else if (ch == 'b' || ch == 'f' || ch == 'n' ||
                   ch == 'r' || ch == 't') {
            json_error(json, "JSON protocol strings cannot contain control escapes");
            return 0;
        } else {
            json_error(json, "unsupported JSON escape");
            return 0;
        }
    }
    json_error(json, "unterminated JSON string");
    return 0;
}

static int parse_number(OnlineControlJsonCursor* json,
                        const unsigned char** out_start,
                        const unsigned char** out_end) {
    const unsigned char* start;
    if (!json) return 0;
    start = json->cursor;
    if (*json->cursor == '-') json->cursor++;
    if (*json->cursor == '0') {
        json->cursor++;
        if (*json->cursor >= '0' && *json->cursor <= '9') {
            json_error(json, "JSON number has a leading zero");
            return 0;
        }
    } else if (*json->cursor >= '1' && *json->cursor <= '9') {
        while (*json->cursor >= '0' && *json->cursor <= '9') json->cursor++;
    } else {
        json_error(json, "invalid JSON number");
        return 0;
    }
    if (*json->cursor == '.') {
        json->cursor++;
        if (*json->cursor < '0' || *json->cursor > '9') {
            json_error(json, "JSON fraction has no digits");
            return 0;
        }
        while (*json->cursor >= '0' && *json->cursor <= '9') json->cursor++;
    }
    if (*json->cursor == 'e' || *json->cursor == 'E') {
        json->cursor++;
        if (*json->cursor == '+' || *json->cursor == '-') json->cursor++;
        if (*json->cursor < '0' || *json->cursor > '9') {
            json_error(json, "JSON exponent has no digits");
            return 0;
        }
        while (*json->cursor >= '0' && *json->cursor <= '9') json->cursor++;
    }
    if (out_start) *out_start = start;
    if (out_end) *out_end = json->cursor;
    return 1;
}

static int parse_literal(OnlineControlJsonCursor* json,
                         const char* literal) {
    const unsigned char* expected = (const unsigned char*)literal;
    while (*expected) {
        if (!*json->cursor || *json->cursor != *expected) {
            json_error(json, "invalid JSON literal");
            return 0;
        }
        json->cursor++;
        expected++;
    }
    return 1;
}

static OnlineControlJsonResult parse_integer_value(const unsigned char* start,
                                                    const unsigned char* end,
                                                    int* out) {
    uint64_t magnitude = 0;
    uint64_t limit;
    int negative = 0;
    const unsigned char* cursor = start;
    if (!start || !end || start >= end || !out) return ONLINE_CONTROL_JSON_VALUE_INVALID;
    if (*cursor == '-') {
        negative = 1;
        cursor++;
    }
    limit = negative ? (uint64_t)INT_MAX + 1u : (uint64_t)INT_MAX;
    for (; cursor < end; cursor++) {
        unsigned int digit;
        if (*cursor < '0' || *cursor > '9') {
            return ONLINE_CONTROL_JSON_VALUE_INVALID;
        }
        digit = (unsigned int)(*cursor - '0');
        if (magnitude > (limit - digit) / 10u) {
            return ONLINE_CONTROL_JSON_VALUE_INVALID;
        }
        magnitude = magnitude * 10u + digit;
    }
    if (negative) {
        if (magnitude == (uint64_t)INT_MAX + 1u) *out = INT_MIN;
        else *out = -(int)magnitude;
    } else {
        *out = (int)magnitude;
    }
    return ONLINE_CONTROL_JSON_OK;
}

static int keys_equal(char stored[][ONLINE_CONTROL_JSON_MAX_KEY_BYTES + 1u],
                      size_t count,
                      const char* key) {
    size_t i;
    for (i = 0; i < count; i++) {
        if (strcmp(stored[i], key) == 0) return 1;
    }
    return 0;
}

static OnlineControlJsonResult scan_object(const char* input,
                                           OnlineControlJsonScan* scan,
                                           char* error,
                                           size_t error_cap) {
    OnlineControlJsonCursor json;
    char keys[ONLINE_CONTROL_JSON_MAX_FIELDS]
             [ONLINE_CONTROL_JSON_MAX_KEY_BYTES + 1u];
    size_t field_count = 0;
    if (error && error_cap > 0) error[0] = '\0';
    if (!scan) return ONLINE_CONTROL_JSON_INVALID;
    if (scan->string_out && scan->string_cap > 0) scan->string_out[0] = '\0';
    if (!input) {
        if (error && error_cap > 0) {
            snprintf(error, error_cap, "online message is null");
            error[error_cap - 1u] = '\0';
        }
        return ONLINE_CONTROL_JSON_INVALID;
    }
    scan->found = 0;
    scan->wanted_result = ONLINE_CONTROL_JSON_NOT_FOUND;
    json.cursor = (const unsigned char*)input;
    json.error = error;
    json.error_cap = error_cap;
    json.failed = 0;
    skip_ws(&json);
    if (*json.cursor != '{') {
        json_error(&json, "online message must be a JSON object");
        return ONLINE_CONTROL_JSON_INVALID;
    }
    json.cursor++;
    skip_ws(&json);
    if (*json.cursor == '}') {
        json.cursor++;
    } else {
        while (!json.failed) {
            char key[ONLINE_CONTROL_JSON_MAX_KEY_BYTES + 1u];
            size_t key_length = 0;
            int key_too_small = 0;
            int wanted;
            OnlineControlValueType value_type;
            const unsigned char* number_start = NULL;
            const unsigned char* number_end = NULL;
            int string_too_small = 0;

            if (field_count >= ONLINE_CONTROL_JSON_MAX_FIELDS) {
                json_error(&json, "online JSON object has too many fields");
                break;
            }
            if (!parse_string(&json, key, sizeof(key), &key_length,
                              &key_too_small)) break;
            if (key_too_small || key_length == 0) {
                json_error(&json, "online JSON key is empty or too long");
                break;
            }
            if (keys_equal(keys, field_count, key)) {
                json_error(&json, "duplicate online JSON key: %s", key);
                break;
            }
            memcpy(keys[field_count], key, key_length + 1u);
            field_count++;
            wanted = scan->wanted_key && strcmp(scan->wanted_key, key) == 0;

            skip_ws(&json);
            if (*json.cursor != ':') {
                json_error(&json, "expected ':' after online JSON key");
                break;
            }
            json.cursor++;
            skip_ws(&json);
            if (*json.cursor == '"') {
                value_type = ONLINE_CONTROL_VALUE_STRING;
                if (!parse_string(&json,
                                  wanted && scan->wanted_type == ONLINE_CONTROL_WANT_STRING
                                      ? scan->string_out : NULL,
                                  wanted && scan->wanted_type == ONLINE_CONTROL_WANT_STRING
                                      ? scan->string_cap : 0u,
                                  NULL,
                                  &string_too_small)) {
                    break;
                }
            } else if (*json.cursor == '-' ||
                       (*json.cursor >= '0' && *json.cursor <= '9')) {
                value_type = ONLINE_CONTROL_VALUE_NUMBER;
                if (!parse_number(&json, &number_start, &number_end)) break;
            } else if (*json.cursor == 't') {
                value_type = ONLINE_CONTROL_VALUE_BOOL;
                if (!parse_literal(&json, "true")) break;
            } else if (*json.cursor == 'f') {
                value_type = ONLINE_CONTROL_VALUE_BOOL;
                if (!parse_literal(&json, "false")) break;
            } else if (*json.cursor == 'n') {
                value_type = ONLINE_CONTROL_VALUE_NULL;
                if (!parse_literal(&json, "null")) break;
            } else if (*json.cursor == '{' || *json.cursor == '[') {
                json_error(&json, "nested online JSON values are not allowed");
                break;
            } else {
                json_error(&json, "invalid online JSON value");
                break;
            }

            if (wanted) {
                scan->found = 1;
                if ((scan->wanted_type == ONLINE_CONTROL_WANT_STRING &&
                     value_type != ONLINE_CONTROL_VALUE_STRING) ||
                    (scan->wanted_type == ONLINE_CONTROL_WANT_INT &&
                     value_type != ONLINE_CONTROL_VALUE_NUMBER)) {
                    scan->wanted_result = ONLINE_CONTROL_JSON_TYPE_MISMATCH;
                } else if (scan->wanted_type == ONLINE_CONTROL_WANT_STRING) {
                    scan->wanted_result = string_too_small
                        ? ONLINE_CONTROL_JSON_OUTPUT_TOO_SMALL
                        : ONLINE_CONTROL_JSON_OK;
                } else if (scan->wanted_type == ONLINE_CONTROL_WANT_INT) {
                    scan->wanted_result = parse_integer_value(number_start,
                                                              number_end,
                                                              scan->int_out);
                }
            }

            skip_ws(&json);
            if (*json.cursor == ',') {
                json.cursor++;
                skip_ws(&json);
                if (*json.cursor == '}') {
                    json_error(&json, "trailing comma in online JSON object");
                    break;
                }
                continue;
            }
            if (*json.cursor == '}') {
                json.cursor++;
                break;
            }
            json_error(&json, "expected ',' or '}' in online JSON object");
            break;
        }
    }
    if (!json.failed) {
        skip_ws(&json);
        if (*json.cursor != '\0') {
            json_error(&json, "trailing bytes after online JSON object");
        }
    }
    if (json.failed) {
        if (scan->string_out && scan->string_cap > 0) scan->string_out[0] = '\0';
        return ONLINE_CONTROL_JSON_INVALID;
    }
    return scan->found ? scan->wanted_result : ONLINE_CONTROL_JSON_NOT_FOUND;
}

int online_control_json_validate(const char* json, char* error, size_t error_cap) {
    OnlineControlJsonScan scan;
    memset(&scan, 0, sizeof(scan));
    return scan_object(json, &scan, error, error_cap) != ONLINE_CONTROL_JSON_INVALID;
}

OnlineControlJsonResult online_control_json_get_string(const char* json,
                                                        const char* key,
                                                        char* out,
                                                        size_t out_cap) {
    OnlineControlJsonScan scan;
    if (out && out_cap > 0) out[0] = '\0';
    if (!key || !key[0] || !out || out_cap == 0) return ONLINE_CONTROL_JSON_INVALID;
    memset(&scan, 0, sizeof(scan));
    scan.wanted_key = key;
    scan.wanted_type = ONLINE_CONTROL_WANT_STRING;
    scan.string_out = out;
    scan.string_cap = out_cap;
    return scan_object(json, &scan, NULL, 0);
}

OnlineControlJsonResult online_control_json_get_int(const char* json,
                                                     const char* key,
                                                     int* out) {
    OnlineControlJsonScan scan;
    if (!key || !key[0] || !out) return ONLINE_CONTROL_JSON_INVALID;
    memset(&scan, 0, sizeof(scan));
    scan.wanted_key = key;
    scan.wanted_type = ONLINE_CONTROL_WANT_INT;
    scan.int_out = out;
    return scan_object(json, &scan, NULL, 0);
}

int online_control_username_is_canonical(const char* username) {
    size_t length = 0;
    if (!username) return 0;
    while (username[length]) {
        unsigned char ch = (unsigned char)username[length];
        if (!((ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') || ch == '_')) {
            return 0;
        }
        length++;
        if (length > 24u) return 0;
    }
    return length > 0;
}

uint32_t online_control_deadline_after(uint32_t now, uint32_t delay_ms) {
    uint32_t deadline = now + delay_ms;
    return deadline == 0u ? 1u : deadline;
}

int online_control_deadline_reached(uint32_t now, uint32_t deadline) {
    return deadline != 0u && (int32_t)(now - deadline) >= 0;
}
