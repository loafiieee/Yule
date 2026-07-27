#include "bytebeat_ext.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    BB_NODE_CONSTANT = 1,
    BB_NODE_T,
    BB_NODE_SAMPLE_RATE,
    BB_NODE_TIME,
    BB_NODE_UNARY,
    BB_NODE_BINARY,
    BB_NODE_TERNARY,
    BB_NODE_FUNCTION
};

enum {
    BB_OP_POSITIVE = 1,
    BB_OP_NEGATIVE,
    BB_OP_BIT_NOT,
    BB_OP_LOGICAL_NOT,
    BB_OP_MULTIPLY,
    BB_OP_DIVIDE,
    BB_OP_MODULO,
    BB_OP_ADD,
    BB_OP_SUBTRACT,
    BB_OP_SHIFT_LEFT,
    BB_OP_SHIFT_RIGHT,
    BB_OP_SHIFT_UNSIGNED,
    BB_OP_LESS,
    BB_OP_LESS_EQUAL,
    BB_OP_GREATER,
    BB_OP_GREATER_EQUAL,
    BB_OP_EQUAL,
    BB_OP_NOT_EQUAL,
    BB_OP_BIT_AND,
    BB_OP_BIT_XOR,
    BB_OP_BIT_OR,
    BB_OP_LOGICAL_AND,
    BB_OP_LOGICAL_OR,
    BB_OP_POWER
};

enum {
    BB_FN_SIN = 1,
    BB_FN_COS,
    BB_FN_TAN,
    BB_FN_ASIN,
    BB_FN_ACOS,
    BB_FN_ATAN,
    BB_FN_ATAN2,
    BB_FN_ABS,
    BB_FN_FLOOR,
    BB_FN_CEIL,
    BB_FN_ROUND,
    BB_FN_SQRT,
    BB_FN_LOG,
    BB_FN_EXP,
    BB_FN_MIN,
    BB_FN_MAX,
    BB_FN_POW,
    BB_FN_CLAMP,
    BB_FN_SIGN,
    BB_FN_FRACT,
    BB_FN_TRUNC,
    BB_FN_NOISE
};

typedef struct BytebeatParser {
    const char* expression;
    const char* cursor;
    const char* end;
    BytebeatProgram* program;
    BytebeatDiagnostic* diagnostic;
    unsigned int parse_depth;
    int failed;
} BytebeatParser;

static void bb_diag_clear(BytebeatDiagnostic* diagnostic) {
    if (!diagnostic) return;
    diagnostic->offset = 0u;
    diagnostic->message[0] = '\0';
}

static void bb_fail(BytebeatParser* parser, const char* message) {
    if (!parser || parser->failed) return;
    parser->failed = 1;
    if (parser->diagnostic) {
        parser->diagnostic->offset = (size_t)(parser->cursor - parser->expression);
        snprintf(parser->diagnostic->message,
                 sizeof(parser->diagnostic->message), "%s", message);
    }
}

static void bb_render_fail(BytebeatDiagnostic* diagnostic, const char* message) {
    if (!diagnostic) return;
    diagnostic->offset = 0u;
    snprintf(diagnostic->message, sizeof(diagnostic->message), "%s", message);
}

int bytebeat_parse_playlist_options(const char* marker,
                                    BytebeatPlaylistOptions* out_options,
                                    BytebeatDiagnostic* diagnostic) {
    static const char tag[] = "yule:bytebeat";
    const char* cursor;
    int saw_rate = 0;
    int saw_output_rate = 0;
    int saw_volume = 0;
    int saw_engine = 0;
    int saw_mode = 0;
    bb_diag_clear(diagnostic);
    if (!marker || !out_options) {
        bb_render_fail(diagnostic, "missing playlist marker or output");
        return 0;
    }
    cursor = strstr(marker, tag);
    if (!cursor) {
        bb_render_fail(diagnostic, "missing yule:bytebeat marker");
        return 0;
    }
    cursor += sizeof(tag) - 1u;
    out_options->sample_rate = BYTEBEAT_DEFAULT_PLAYLIST_SAMPLE_RATE;
    out_options->output_rate = BYTEBEAT_DEFAULT_PLAYLIST_OUTPUT_RATE;
    out_options->volume = BYTEBEAT_DEFAULT_PLAYLIST_VOLUME;
    out_options->engine = BYTEBEAT_PLAYLIST_BOUNDED;
    out_options->mode = BYTEBEAT_PLAYLIST_U8;
    for (;;) {
        char* end = NULL;
        while (*cursor && isspace((unsigned char)*cursor)) cursor++;
        if (!*cursor || *cursor == ')') return 1;
        if (strncmp(cursor, "sample_rate=", 12u) == 0) {
            unsigned long value;
            if (saw_rate) {
                if (diagnostic) {
                    diagnostic->offset = (size_t)(cursor - marker);
                    snprintf(diagnostic->message, sizeof(diagnostic->message),
                             "duplicate sample_rate option");
                }
                return 0;
            }
            cursor += 12u;
            value = strtoul(cursor, &end, 10);
            if (end == cursor || value < 4000ul || value > 48000ul) {
                if (diagnostic) {
                    diagnostic->offset = (size_t)(cursor - marker);
                    snprintf(diagnostic->message, sizeof(diagnostic->message),
                             "sample_rate must be between 4000 and 48000");
                }
                return 0;
            }
            if (*end && *end != ')' &&
                !isspace((unsigned char)*end)) {
                if (diagnostic) {
                    diagnostic->offset = (size_t)(end - marker);
                    snprintf(diagnostic->message, sizeof(diagnostic->message),
                             "invalid text after sample_rate");
                }
                return 0;
            }
            out_options->sample_rate = (uint32_t)value;
            saw_rate = 1;
            cursor = end;
            continue;
        }
        if (strncmp(cursor, "output_rate=", 12u) == 0) {
            unsigned long value;
            if (saw_output_rate) {
                if (diagnostic) {
                    diagnostic->offset = (size_t)(cursor - marker);
                    snprintf(diagnostic->message,
                             sizeof(diagnostic->message),
                             "duplicate output_rate option");
                }
                return 0;
            }
            cursor += 12u;
            value = strtoul(cursor, &end, 10);
            if (end == cursor || value < 8000ul || value > 192000ul) {
                if (diagnostic) {
                    diagnostic->offset = (size_t)(cursor - marker);
                    snprintf(diagnostic->message,
                             sizeof(diagnostic->message),
                             "output_rate must be between 8000 and 192000");
                }
                return 0;
            }
            if (*end && *end != ')' &&
                !isspace((unsigned char)*end)) {
                if (diagnostic) {
                    diagnostic->offset = (size_t)(end - marker);
                    snprintf(diagnostic->message,
                             sizeof(diagnostic->message),
                             "invalid text after output_rate");
                }
                return 0;
            }
            out_options->output_rate = (uint32_t)value;
            saw_output_rate = 1;
            cursor = end;
            continue;
        }
        if (strncmp(cursor, "volume=", 7u) == 0) {
            double value;
            if (saw_volume) {
                if (diagnostic) {
                    diagnostic->offset = (size_t)(cursor - marker);
                    snprintf(diagnostic->message, sizeof(diagnostic->message),
                             "duplicate volume option");
                }
                return 0;
            }
            cursor += 7u;
            value = strtod(cursor, &end);
            if (end == cursor || !isfinite(value) ||
                value < 0.0 || value > 1.0) {
                if (diagnostic) {
                    diagnostic->offset = (size_t)(cursor - marker);
                    snprintf(diagnostic->message, sizeof(diagnostic->message),
                             "volume must be finite and between 0 and 1");
                }
                return 0;
            }
            if (*end && *end != ')' &&
                !isspace((unsigned char)*end)) {
                if (diagnostic) {
                    diagnostic->offset = (size_t)(end - marker);
                    snprintf(diagnostic->message, sizeof(diagnostic->message),
                             "invalid text after volume");
                }
                return 0;
            }
            out_options->volume = value;
            saw_volume = 1;
            cursor = end;
            continue;
        }
        if (strncmp(cursor, "engine=", 7u) == 0) {
            const char* value;
            size_t length;
            if (saw_engine) {
                if (diagnostic) {
                    diagnostic->offset = (size_t)(cursor - marker);
                    snprintf(diagnostic->message, sizeof(diagnostic->message),
                             "duplicate engine option");
                }
                return 0;
            }
            value = cursor + 7u;
            end = (char*)value;
            while (*end && *end != ')' &&
                   !isspace((unsigned char)*end)) {
                end++;
            }
            length = (size_t)(end - value);
            if (length == 7u && strncmp(value, "bounded", length) == 0) {
                out_options->engine = BYTEBEAT_PLAYLIST_BOUNDED;
            } else if ((length == 8u &&
                        strncmp(value, "dollchan", length) == 0) ||
                       (length == 10u &&
                        strncmp(value, "javascript", length) == 0)) {
                out_options->engine = BYTEBEAT_PLAYLIST_DOLLCHAN;
            } else {
                if (diagnostic) {
                    diagnostic->offset = (size_t)(value - marker);
                    snprintf(diagnostic->message, sizeof(diagnostic->message),
                             "engine must be bounded or dollchan");
                }
                return 0;
            }
            saw_engine = 1;
            cursor = end;
            continue;
        }
        if (strncmp(cursor, "mode=", 5u) == 0) {
            const char* value;
            size_t length;
            if (saw_mode) {
                if (diagnostic) {
                    diagnostic->offset = (size_t)(cursor - marker);
                    snprintf(diagnostic->message, sizeof(diagnostic->message),
                             "duplicate mode option");
                }
                return 0;
            }
            value = cursor + 5u;
            end = (char*)value;
            while (*end && *end != ')' &&
                   !isspace((unsigned char)*end)) {
                end++;
            }
            length = (size_t)(end - value);
            if ((length == 8u && strncmp(value, "bytebeat", length) == 0) ||
                (length == 2u && strncmp(value, "u8", length) == 0)) {
                out_options->mode = BYTEBEAT_PLAYLIST_U8;
            } else if ((length == 15u &&
                        strncmp(value, "signed-bytebeat", length) == 0) ||
                       (length == 2u && strncmp(value, "s8", length) == 0)) {
                out_options->mode = BYTEBEAT_PLAYLIST_S8;
            } else if ((length == 9u &&
                        strncmp(value, "floatbeat", length) == 0) ||
                       (length == 5u &&
                        strncmp(value, "float", length) == 0)) {
                out_options->mode = BYTEBEAT_PLAYLIST_FLOAT;
            } else if (length == 8u &&
                       strncmp(value, "funcbeat", length) == 0) {
                out_options->mode = BYTEBEAT_PLAYLIST_FUNC;
            } else {
                if (diagnostic) {
                    diagnostic->offset = (size_t)(value - marker);
                    snprintf(diagnostic->message, sizeof(diagnostic->message),
                             "mode must be bytebeat, signed-bytebeat, floatbeat, or funcbeat");
                }
                return 0;
            }
            saw_mode = 1;
            cursor = end;
            continue;
        }
        if (diagnostic) {
            diagnostic->offset = (size_t)(cursor - marker);
            snprintf(diagnostic->message, sizeof(diagnostic->message),
                     "unknown yule:bytebeat marker option");
        }
        return 0;
    }
}

int bytebeat_parse_playlist_marker(const char* marker,
                                   uint32_t* out_sample_rate,
                                   double* out_volume,
                                   BytebeatDiagnostic* diagnostic) {
    BytebeatPlaylistOptions options;
    if (!out_sample_rate || !out_volume) {
        bb_render_fail(diagnostic, "missing playlist marker output");
        return 0;
    }
    if (!bytebeat_parse_playlist_options(marker, &options, diagnostic)) {
        return 0;
    }
    *out_sample_rate = options.sample_rate;
    *out_volume = options.volume;
    return 1;
}

static void bb_skip_space(BytebeatParser* parser) {
    while (parser->cursor < parser->end &&
           isspace((unsigned char)*parser->cursor)) {
        parser->cursor++;
    }
}

static int bb_match(BytebeatParser* parser, const char* token) {
    size_t length;
    bb_skip_space(parser);
    length = strlen(token);
    if ((size_t)(parser->end - parser->cursor) < length ||
        memcmp(parser->cursor, token, length) != 0) {
        return 0;
    }
    parser->cursor += length;
    return 1;
}

static int bb_match_single(BytebeatParser* parser, char token, char reject_next) {
    bb_skip_space(parser);
    if (parser->cursor >= parser->end || *parser->cursor != token) return 0;
    if (reject_next && parser->cursor + 1 < parser->end &&
        parser->cursor[1] == reject_next) {
        return 0;
    }
    parser->cursor++;
    return 1;
}

static int bb_add_node(BytebeatParser* parser, uint8_t kind, uint8_t op,
                       int a, int b, int c, double value) {
    BytebeatNode* node;
    int index;
    if (parser->program->node_count >= BYTEBEAT_MAX_NODES) {
        bb_fail(parser, "expression is too complex (maximum 512 nodes)");
        return -1;
    }
    index = (int)parser->program->node_count++;
    node = &parser->program->nodes[index];
    node->kind = kind;
    node->op = op;
    node->a = (int16_t)a;
    node->b = (int16_t)b;
    node->c = (int16_t)c;
    node->value = value;
    return index;
}

static int bb_parse_conditional(BytebeatParser* parser);

static int bb_identifier(BytebeatParser* parser, char* out, size_t out_cap) {
    const char* start;
    size_t length;
    bb_skip_space(parser);
    if (parser->cursor >= parser->end ||
        !(isalpha((unsigned char)*parser->cursor) ||
          *parser->cursor == '_')) {
        return 0;
    }
    start = parser->cursor++;
    while (parser->cursor < parser->end &&
           (isalnum((unsigned char)*parser->cursor) ||
            *parser->cursor == '_')) {
        parser->cursor++;
    }
    length = (size_t)(parser->cursor - start);
    if (length + 1u > out_cap) {
        bb_fail(parser, "identifier is too long");
        return 0;
    }
    memcpy(out, start, length);
    out[length] = '\0';
    return 1;
}

static int bb_function_id(const char* name, int* argc) {
    if (!name || !argc) return 0;
#define BB_FUNCTION(n, id, count) \
    if (strcmp(name, n) == 0) { *argc = count; return id; }
    BB_FUNCTION("sin", BB_FN_SIN, 1)
    BB_FUNCTION("cos", BB_FN_COS, 1)
    BB_FUNCTION("tan", BB_FN_TAN, 1)
    BB_FUNCTION("asin", BB_FN_ASIN, 1)
    BB_FUNCTION("acos", BB_FN_ACOS, 1)
    BB_FUNCTION("atan", BB_FN_ATAN, 1)
    BB_FUNCTION("atan2", BB_FN_ATAN2, 2)
    BB_FUNCTION("abs", BB_FN_ABS, 1)
    BB_FUNCTION("floor", BB_FN_FLOOR, 1)
    BB_FUNCTION("ceil", BB_FN_CEIL, 1)
    BB_FUNCTION("round", BB_FN_ROUND, 1)
    BB_FUNCTION("sqrt", BB_FN_SQRT, 1)
    BB_FUNCTION("log", BB_FN_LOG, 1)
    BB_FUNCTION("exp", BB_FN_EXP, 1)
    BB_FUNCTION("min", BB_FN_MIN, 2)
    BB_FUNCTION("max", BB_FN_MAX, 2)
    BB_FUNCTION("pow", BB_FN_POW, 2)
    BB_FUNCTION("clamp", BB_FN_CLAMP, 3)
    BB_FUNCTION("sign", BB_FN_SIGN, 1)
    BB_FUNCTION("fract", BB_FN_FRACT, 1)
    BB_FUNCTION("trunc", BB_FN_TRUNC, 1)
    BB_FUNCTION("int", BB_FN_TRUNC, 1)
    BB_FUNCTION("noise", BB_FN_NOISE, 1)
#undef BB_FUNCTION
    return 0;
}

static int bb_parse_number(BytebeatParser* parser) {
    const char* start;
    char* parsed_end = NULL;
    double value;
    bb_skip_space(parser);
    start = parser->cursor;
    if (parser->end - start >= 2 && start[0] == '0' &&
        (start[1] == 'x' || start[1] == 'X' ||
         start[1] == 'b' || start[1] == 'B')) {
        unsigned long result = 0u;
        int base = (start[1] == 'x' || start[1] == 'X') ? 16 : 2;
        const char* p = start + 2;
        int digits = 0;
        while (p < parser->end) {
            int digit;
            if (*p >= '0' && *p <= '9') digit = *p - '0';
            else if (*p >= 'a' && *p <= 'f') digit = *p - 'a' + 10;
            else if (*p >= 'A' && *p <= 'F') digit = *p - 'A' + 10;
            else break;
            if (digit >= base) break;
            result = result * (unsigned long)base + (unsigned long)digit;
            digits++;
            p++;
        }
        if (digits == 0) {
            bb_fail(parser, "expected digits after numeric prefix");
            return -1;
        }
        parser->cursor = p;
        return bb_add_node(parser, BB_NODE_CONSTANT, 0u, -1, -1, -1,
                           (double)result);
    }
    if (start >= parser->end ||
        (!isdigit((unsigned char)*start) && *start != '.')) {
        return -1;
    }
    value = strtod(start, &parsed_end);
    if (parsed_end == start || parsed_end > parser->end) return -1;
    parser->cursor = parsed_end;
    return bb_add_node(parser, BB_NODE_CONSTANT, 0u, -1, -1, -1, value);
}

static int bb_parse_primary(BytebeatParser* parser) {
    int node;
    char identifier[32];
    const char* saved;
    bb_skip_space(parser);
    if (bb_match(parser, "(")) {
        parser->parse_depth++;
        if (parser->parse_depth > BYTEBEAT_MAX_DEPTH) {
            bb_fail(parser, "parentheses are nested too deeply");
            return -1;
        }
        node = bb_parse_conditional(parser);
        parser->parse_depth--;
        if (!bb_match(parser, ")")) {
            bb_fail(parser, "expected ')'");
            return -1;
        }
        return node;
    }

    saved = parser->cursor;
    node = bb_parse_number(parser);
    if (node >= 0 || parser->failed) return node;
    parser->cursor = saved;

    if (bb_identifier(parser, identifier, sizeof(identifier))) {
        char member[32];
        const char* name = identifier;
        if (strcmp(identifier, "Math") == 0 && bb_match(parser, ".")) {
            if (!bb_identifier(parser, member, sizeof(member))) {
                bb_fail(parser, "expected Math member name");
                return -1;
            }
            name = member;
        }
        if (strcmp(name, "t") == 0) {
            return bb_add_node(parser, BB_NODE_T, 0u, -1, -1, -1, 0.0);
        }
        if (strcmp(name, "sr") == 0 || strcmp(name, "sampleRate") == 0 ||
            strcmp(name, "sample_rate") == 0) {
            return bb_add_node(parser, BB_NODE_SAMPLE_RATE, 0u,
                               -1, -1, -1, 0.0);
        }
        if (strcmp(name, "time") == 0 || strcmp(name, "seconds") == 0) {
            return bb_add_node(parser, BB_NODE_TIME, 0u,
                               -1, -1, -1, 0.0);
        }
        if (strcmp(name, "pi") == 0 || strcmp(name, "PI") == 0) {
            return bb_add_node(parser, BB_NODE_CONSTANT, 0u,
                               -1, -1, -1, 3.14159265358979323846);
        }
        if (strcmp(name, "e") == 0 || strcmp(name, "E") == 0) {
            return bb_add_node(parser, BB_NODE_CONSTANT, 0u,
                               -1, -1, -1, 2.71828182845904523536);
        }
        {
            int argc = 0;
            int function_id = bb_function_id(name, &argc);
            int args[3] = { -1, -1, -1 };
            int i;
            if (!function_id) {
                bb_fail(parser, "unknown variable or function");
                return -1;
            }
            if (!bb_match(parser, "(")) {
                bb_fail(parser, "expected '(' after function name");
                return -1;
            }
            for (i = 0; i < argc; i++) {
                args[i] = bb_parse_conditional(parser);
                if (args[i] < 0) return -1;
                if (i + 1 < argc && !bb_match(parser, ",")) {
                    bb_fail(parser, "expected ',' between function arguments");
                    return -1;
                }
            }
            if (!bb_match(parser, ")")) {
                bb_fail(parser, "expected ')' after function arguments");
                return -1;
            }
            return bb_add_node(parser, BB_NODE_FUNCTION,
                               (uint8_t)function_id,
                               args[0], args[1], args[2], 0.0);
        }
    }
    if (!parser->failed) {
        bb_fail(parser, "expected number, t, time, sr, function, or '('");
    }
    return -1;
}

static int bb_parse_unary(BytebeatParser* parser) {
    uint8_t op = 0u;
    if (bb_match(parser, "+")) op = BB_OP_POSITIVE;
    else if (bb_match(parser, "-")) op = BB_OP_NEGATIVE;
    else if (bb_match(parser, "~")) op = BB_OP_BIT_NOT;
    else if (bb_match_single(parser, '!', '=')) op = BB_OP_LOGICAL_NOT;
    if (op) {
        int child = bb_parse_unary(parser);
        if (child < 0) return -1;
        return bb_add_node(parser, BB_NODE_UNARY, op, child, -1, -1, 0.0);
    }
    return bb_parse_primary(parser);
}

static int bb_parse_power(BytebeatParser* parser) {
    int left = bb_parse_unary(parser);
    if (left < 0) return -1;
    if (bb_match(parser, "**")) {
        int right = bb_parse_power(parser);
        if (right < 0) return -1;
        return bb_add_node(parser, BB_NODE_BINARY, BB_OP_POWER,
                           left, right, -1, 0.0);
    }
    return left;
}

#define BB_PARSE_LEFT_ASSOC(name, lower, BODY) \
    static int name(BytebeatParser* parser) { \
        int left = lower(parser); \
        if (left < 0) return -1; \
        for (;;) { \
            uint8_t op = 0u; \
            BODY \
            if (!op) break; \
            { \
                int right = lower(parser); \
                if (right < 0) return -1; \
                left = bb_add_node(parser, BB_NODE_BINARY, op, \
                                   left, right, -1, 0.0); \
                if (left < 0) return -1; \
            } \
        } \
        return left; \
    }

BB_PARSE_LEFT_ASSOC(bb_parse_multiplicative, bb_parse_power,
    if (bb_match_single(parser, '*', '*')) op = BB_OP_MULTIPLY;
    else if (bb_match(parser, "/")) op = BB_OP_DIVIDE;
    else if (bb_match(parser, "%")) op = BB_OP_MODULO;
)

BB_PARSE_LEFT_ASSOC(bb_parse_additive, bb_parse_multiplicative,
    if (bb_match(parser, "+")) op = BB_OP_ADD;
    else if (bb_match(parser, "-")) op = BB_OP_SUBTRACT;
)

BB_PARSE_LEFT_ASSOC(bb_parse_shift, bb_parse_additive,
    if (bb_match(parser, ">>>")) op = BB_OP_SHIFT_UNSIGNED;
    else if (bb_match(parser, "<<")) op = BB_OP_SHIFT_LEFT;
    else if (bb_match(parser, ">>")) op = BB_OP_SHIFT_RIGHT;
)

BB_PARSE_LEFT_ASSOC(bb_parse_relational, bb_parse_shift,
    if (bb_match(parser, "<=")) op = BB_OP_LESS_EQUAL;
    else if (bb_match(parser, ">=")) op = BB_OP_GREATER_EQUAL;
    else if (bb_match_single(parser, '<', '<')) op = BB_OP_LESS;
    else if (bb_match_single(parser, '>', '>')) op = BB_OP_GREATER;
)

BB_PARSE_LEFT_ASSOC(bb_parse_equality, bb_parse_relational,
    if (bb_match(parser, "===") || bb_match(parser, "==")) op = BB_OP_EQUAL;
    else if (bb_match(parser, "!==") || bb_match(parser, "!=")) op = BB_OP_NOT_EQUAL;
)

BB_PARSE_LEFT_ASSOC(bb_parse_bit_and, bb_parse_equality,
    if (bb_match_single(parser, '&', '&')) op = BB_OP_BIT_AND;
)

BB_PARSE_LEFT_ASSOC(bb_parse_bit_xor, bb_parse_bit_and,
    if (bb_match(parser, "^")) op = BB_OP_BIT_XOR;
)

BB_PARSE_LEFT_ASSOC(bb_parse_bit_or, bb_parse_bit_xor,
    if (bb_match_single(parser, '|', '|')) op = BB_OP_BIT_OR;
)

BB_PARSE_LEFT_ASSOC(bb_parse_logical_and, bb_parse_bit_or,
    if (bb_match(parser, "&&")) op = BB_OP_LOGICAL_AND;
)

BB_PARSE_LEFT_ASSOC(bb_parse_logical_or, bb_parse_logical_and,
    if (bb_match(parser, "||")) op = BB_OP_LOGICAL_OR;
)

#undef BB_PARSE_LEFT_ASSOC

static int bb_parse_conditional(BytebeatParser* parser) {
    int condition = bb_parse_logical_or(parser);
    if (condition < 0) return -1;
    if (bb_match(parser, "?")) {
        int if_true = bb_parse_conditional(parser);
        int if_false;
        if (if_true < 0) return -1;
        if (!bb_match(parser, ":")) {
            bb_fail(parser, "expected ':' in conditional expression");
            return -1;
        }
        if_false = bb_parse_conditional(parser);
        if (if_false < 0) return -1;
        return bb_add_node(parser, BB_NODE_TERNARY, 0u,
                           condition, if_true, if_false, 0.0);
    }
    return condition;
}

static unsigned int bb_tree_depth(const BytebeatProgram* program, int index) {
    const BytebeatNode* node;
    unsigned int a = 0u;
    unsigned int b = 0u;
    unsigned int c = 0u;
    unsigned int maximum;
    if (!program || index < 0 || index >= (int)program->node_count) return 0u;
    node = &program->nodes[index];
    if (node->a >= 0) a = bb_tree_depth(program, node->a);
    if (node->b >= 0) b = bb_tree_depth(program, node->b);
    if (node->c >= 0) c = bb_tree_depth(program, node->c);
    maximum = a > b ? a : b;
    if (c > maximum) maximum = c;
    return maximum + 1u;
}

int bytebeat_compile(const char* expression, BytebeatMode mode,
                     BytebeatProgram* out_program,
                     BytebeatDiagnostic* diagnostic) {
    BytebeatParser parser;
    size_t expression_len;
    int root;
    bb_diag_clear(diagnostic);
    if (!out_program) {
        bb_render_fail(diagnostic, "missing output program");
        return 0;
    }
    memset(out_program, 0, sizeof(*out_program));
    out_program->root = -1;
    if (!expression) {
        bb_render_fail(diagnostic, "missing expression");
        return 0;
    }
    expression_len = strlen(expression);
    if (expression_len == 0u) {
        bb_render_fail(diagnostic, "expression is empty");
        return 0;
    }
    if (expression_len > BYTEBEAT_MAX_EXPRESSION) {
        bb_render_fail(diagnostic, "expression exceeds 1024 bytes");
        return 0;
    }
    if (mode != BYTEBEAT_MODE_U8 && mode != BYTEBEAT_MODE_FLOAT) {
        bb_render_fail(diagnostic, "invalid bytebeat mode");
        return 0;
    }
    memset(&parser, 0, sizeof(parser));
    parser.expression = expression;
    parser.cursor = expression;
    parser.end = expression + expression_len;
    parser.program = out_program;
    parser.diagnostic = diagnostic;
    out_program->mode = (uint8_t)mode;
    root = bb_parse_conditional(&parser);
    bb_skip_space(&parser);
    if (!parser.failed && parser.cursor != parser.end) {
        bb_fail(&parser, "unexpected trailing token");
    }
    if (parser.failed || root < 0) {
        memset(out_program, 0, sizeof(*out_program));
        out_program->root = -1;
        return 0;
    }
    out_program->root = (int16_t)root;
    out_program->max_depth = (uint16_t)bb_tree_depth(out_program, root);
    if (out_program->max_depth > BYTEBEAT_MAX_DEPTH) {
        bb_render_fail(diagnostic, "expression evaluation is too deeply nested");
        memset(out_program, 0, sizeof(*out_program));
        out_program->root = -1;
        return 0;
    }
    return 1;
}

static uint32_t bb_to_uint32(double value) {
    double truncated;
    double wrapped;
    if (!isfinite(value) || value == 0.0) return 0u;
    truncated = value < 0.0 ? ceil(value) : floor(value);
    wrapped = fmod(truncated, 4294967296.0);
    if (wrapped < 0.0) wrapped += 4294967296.0;
    return (uint32_t)wrapped;
}

static int32_t bb_to_int32(double value) {
    return (int32_t)bb_to_uint32(value);
}

static int bb_truthy(double value) {
    return !isnan(value) && value != 0.0;
}

static double bb_eval_node(const BytebeatProgram* program, int index,
                           uint32_t t, uint32_t sample_rate) {
    const BytebeatNode* node = &program->nodes[index];
    double a;
    double b;
    switch (node->kind) {
        case BB_NODE_CONSTANT: return node->value;
        case BB_NODE_T: return (double)t;
        case BB_NODE_SAMPLE_RATE: return (double)sample_rate;
        case BB_NODE_TIME: return (double)t / (double)sample_rate;
        case BB_NODE_UNARY:
            a = bb_eval_node(program, node->a, t, sample_rate);
            switch (node->op) {
                case BB_OP_POSITIVE: return a;
                case BB_OP_NEGATIVE: return -a;
                case BB_OP_BIT_NOT: return (double)(int32_t)(~bb_to_uint32(a));
                case BB_OP_LOGICAL_NOT: return bb_truthy(a) ? 0.0 : 1.0;
                default: return NAN;
            }
        case BB_NODE_TERNARY:
            a = bb_eval_node(program, node->a, t, sample_rate);
            return bb_eval_node(program, bb_truthy(a) ? node->b : node->c,
                                t, sample_rate);
        case BB_NODE_BINARY:
            a = bb_eval_node(program, node->a, t, sample_rate);
            if (node->op == BB_OP_LOGICAL_AND && !bb_truthy(a)) return a;
            if (node->op == BB_OP_LOGICAL_OR && bb_truthy(a)) return a;
            b = bb_eval_node(program, node->b, t, sample_rate);
            switch (node->op) {
                case BB_OP_MULTIPLY: return a * b;
                case BB_OP_DIVIDE: return a / b;
                case BB_OP_MODULO: return fmod(a, b);
                case BB_OP_ADD: return a + b;
                case BB_OP_SUBTRACT: return a - b;
                case BB_OP_POWER: return pow(a, b);
                case BB_OP_SHIFT_LEFT:
                    return (double)(int32_t)(bb_to_uint32(a) <<
                                             (bb_to_uint32(b) & 31u));
                case BB_OP_SHIFT_RIGHT:
                    return (double)(bb_to_int32(a) >>
                                    (bb_to_uint32(b) & 31u));
                case BB_OP_SHIFT_UNSIGNED:
                    return (double)(bb_to_uint32(a) >>
                                    (bb_to_uint32(b) & 31u));
                case BB_OP_LESS: return a < b ? 1.0 : 0.0;
                case BB_OP_LESS_EQUAL: return a <= b ? 1.0 : 0.0;
                case BB_OP_GREATER: return a > b ? 1.0 : 0.0;
                case BB_OP_GREATER_EQUAL: return a >= b ? 1.0 : 0.0;
                case BB_OP_EQUAL: return a == b ? 1.0 : 0.0;
                case BB_OP_NOT_EQUAL: return a != b ? 1.0 : 0.0;
                case BB_OP_BIT_AND:
                    return (double)(int32_t)(bb_to_uint32(a) & bb_to_uint32(b));
                case BB_OP_BIT_XOR:
                    return (double)(int32_t)(bb_to_uint32(a) ^ bb_to_uint32(b));
                case BB_OP_BIT_OR:
                    return (double)(int32_t)(bb_to_uint32(a) | bb_to_uint32(b));
                case BB_OP_LOGICAL_AND:
                case BB_OP_LOGICAL_OR:
                    return b;
                default: return NAN;
            }
        case BB_NODE_FUNCTION:
            a = bb_eval_node(program, node->a, t, sample_rate);
            b = node->b >= 0
                ? bb_eval_node(program, node->b, t, sample_rate) : 0.0;
            {
                double c = node->c >= 0
                    ? bb_eval_node(program, node->c, t, sample_rate) : 0.0;
                switch (node->op) {
                    case BB_FN_SIN: return sin(a);
                    case BB_FN_COS: return cos(a);
                    case BB_FN_TAN: return tan(a);
                    case BB_FN_ASIN: return asin(a);
                    case BB_FN_ACOS: return acos(a);
                    case BB_FN_ATAN: return atan(a);
                    case BB_FN_ATAN2: return atan2(a, b);
                    case BB_FN_ABS: return fabs(a);
                    case BB_FN_FLOOR: return floor(a);
                    case BB_FN_CEIL: return ceil(a);
                    case BB_FN_ROUND: return floor(a + 0.5);
                    case BB_FN_SQRT: return sqrt(a);
                    case BB_FN_LOG: return log(a);
                    case BB_FN_EXP: return exp(a);
                    case BB_FN_MIN: return a < b ? a : b;
                    case BB_FN_MAX: return a > b ? a : b;
                    case BB_FN_POW: return pow(a, b);
                    case BB_FN_CLAMP:
                        if (b > c) return NAN;
                        if (a < b) return b;
                        if (a > c) return c;
                        return a;
                    case BB_FN_SIGN:
                        if (isnan(a) || a == 0.0) return a;
                        return a < 0.0 ? -1.0 : 1.0;
                    case BB_FN_FRACT: return a - floor(a);
                    case BB_FN_TRUNC: return a < 0.0 ? ceil(a) : floor(a);
                    case BB_FN_NOISE: {
                        uint32_t x = bb_to_uint32(a) + 0x9e3779b9u;
                        x ^= x >> 16;
                        x *= 0x7feb352du;
                        x ^= x >> 15;
                        x *= 0x846ca68bu;
                        x ^= x >> 16;
                        return (double)(x & 0x00ffffffu) / 16777216.0;
                    }
                    default: return NAN;
                }
            }
        default: return NAN;
    }
}

int bytebeat_evaluate(const BytebeatProgram* program, uint32_t t,
                      uint32_t sample_rate, double* out_value) {
    if (!program || !out_value || program->root < 0 ||
        program->root >= (int)program->node_count ||
        program->node_count == 0u ||
        program->node_count > BYTEBEAT_MAX_NODES ||
        program->max_depth == 0u ||
        program->max_depth > BYTEBEAT_MAX_DEPTH ||
        sample_rate == 0u) {
        return 0;
    }
    *out_value = bb_eval_node(program, program->root, t, sample_rate);
    return 1;
}

static int bb_validate_render(const BytebeatProgram* program,
                              const BytebeatRenderOptions* options,
                              BytebeatDiagnostic* diagnostic) {
    uint64_t operations;
    bb_diag_clear(diagnostic);
    if (!program || !options || program->root < 0 ||
        program->root >= (int)program->node_count ||
        program->node_count == 0u) {
        bb_render_fail(diagnostic, "invalid compiled program");
        return 0;
    }
    if (options->sample_rate < 4000u || options->sample_rate > 48000u) {
        bb_render_fail(diagnostic, "sample_rate must be between 4000 and 48000");
        return 0;
    }
    if (options->sample_count == 0u ||
        options->sample_count > BYTEBEAT_MAX_SAMPLES) {
        bb_render_fail(diagnostic, "sample count exceeds the 262144-sample limit");
        return 0;
    }
    if (options->fade_samples > options->sample_count / 2u) {
        bb_render_fail(diagnostic, "fade exceeds half the rendered duration");
        return 0;
    }
    if (!isfinite(options->gain) || options->gain < 0.0 ||
        options->gain > 4.0) {
        bb_render_fail(diagnostic, "gain must be finite and between 0 and 4");
        return 0;
    }
    operations = (uint64_t)program->node_count *
                 (uint64_t)options->sample_count;
    if (operations > BYTEBEAT_MAX_RENDER_OPERATIONS) {
        bb_render_fail(diagnostic, "render exceeds the 8388608-operation budget");
        return 0;
    }
    return 1;
}

int bytebeat_render_pcm16(const BytebeatProgram* program,
                          const BytebeatRenderOptions* options,
                          int16_t* out_samples, size_t out_sample_capacity,
                          BytebeatDiagnostic* diagnostic) {
    uint32_t i;
    if (!out_samples ||
        !bb_validate_render(program, options, diagnostic)) {
        if (!out_samples) bb_render_fail(diagnostic, "missing output sample buffer");
        return 0;
    }
    if (out_sample_capacity < options->sample_count) {
        bb_render_fail(diagnostic, "output sample buffer is too small");
        return 0;
    }
    for (i = 0u; i < options->sample_count; i++) {
        double value;
        double sample;
        double envelope = 1.0;
        if (!bytebeat_evaluate(program, i, options->sample_rate, &value)) {
            bb_render_fail(diagnostic, "program evaluation failed");
            return 0;
        }
        if (program->mode == BYTEBEAT_MODE_FLOAT) {
            if (!isfinite(value)) value = 0.0;
            if (value < -1.0) value = -1.0;
            if (value > 1.0) value = 1.0;
            sample = value * 32767.0;
        } else {
            uint32_t byte_value = bb_to_uint32(value) & 0xffu;
            sample = ((double)(int)byte_value - 128.0) * 256.0;
        }
        if (options->fade_samples > 0u) {
            if (i < options->fade_samples) {
                envelope = (double)i / (double)options->fade_samples;
            }
            if (options->sample_count - 1u - i < options->fade_samples) {
                double tail = (double)(options->sample_count - 1u - i) /
                              (double)options->fade_samples;
                if (tail < envelope) envelope = tail;
            }
        }
        sample *= options->gain * envelope;
        if (sample < -32768.0) sample = -32768.0;
        if (sample > 32767.0) sample = 32767.0;
        out_samples[i] = (int16_t)(sample < 0.0
            ? ceil(sample - 0.5) : floor(sample + 0.5));
    }
    return 1;
}

static void bb_write_u16_le(unsigned char* out, uint16_t value) {
    out[0] = (unsigned char)(value & 0xffu);
    out[1] = (unsigned char)((value >> 8) & 0xffu);
}

static void bb_write_u32_le(unsigned char* out, uint32_t value) {
    out[0] = (unsigned char)(value & 0xffu);
    out[1] = (unsigned char)((value >> 8) & 0xffu);
    out[2] = (unsigned char)((value >> 16) & 0xffu);
    out[3] = (unsigned char)((value >> 24) & 0xffu);
}

size_t bytebeat_wav_size(uint32_t sample_count) {
    if (sample_count == 0u || sample_count > BYTEBEAT_MAX_SAMPLES) return 0u;
    return 44u + (size_t)sample_count * 2u;
}

int bytebeat_render_wav(const BytebeatProgram* program,
                        const BytebeatRenderOptions* options,
                        unsigned char* out_wav, size_t out_capacity,
                        size_t* out_size, BytebeatDiagnostic* diagnostic) {
    size_t required;
    uint32_t i;
    int16_t* samples;
    if (!out_size) {
        bb_render_fail(diagnostic, "missing output size");
        return 0;
    }
    *out_size = 0u;
    if (!bb_validate_render(program, options, diagnostic)) return 0;
    required = bytebeat_wav_size(options->sample_count);
    if (!out_wav || out_capacity < required) {
        bb_render_fail(diagnostic, "output WAV buffer is too small");
        return 0;
    }
    samples = (int16_t*)(void*)(out_wav + 44u);
    if (!bytebeat_render_pcm16(program, options, samples,
                               options->sample_count, diagnostic)) {
        return 0;
    }
    memcpy(out_wav, "RIFF", 4u);
    bb_write_u32_le(out_wav + 4u, (uint32_t)(required - 8u));
    memcpy(out_wav + 8u, "WAVEfmt ", 8u);
    bb_write_u32_le(out_wav + 16u, 16u);
    bb_write_u16_le(out_wav + 20u, 1u);
    bb_write_u16_le(out_wav + 22u, 1u);
    bb_write_u32_le(out_wav + 24u, options->sample_rate);
    bb_write_u32_le(out_wav + 28u, options->sample_rate * 2u);
    bb_write_u16_le(out_wav + 32u, 2u);
    bb_write_u16_le(out_wav + 34u, 16u);
    memcpy(out_wav + 36u, "data", 4u);
    bb_write_u32_le(out_wav + 40u, options->sample_count * 2u);
    /*
     * The PCM was written through an aligned int16_t view. Normalize each
     * sample explicitly so the WAV remains little-endian on every host.
     */
    for (i = 0u; i < options->sample_count; i++) {
        uint16_t value = (uint16_t)samples[i];
        bb_write_u16_le(out_wav + 44u + (size_t)i * 2u, value);
    }
    *out_size = required;
    return 1;
}
