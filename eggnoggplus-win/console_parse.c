#include "console_parse.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static char* trim_whitespace(char* text) {
    char* end;

    if (!text) return NULL;
    while (*text && isspace((unsigned char)*text)) ++text;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) --end;
    *end = '\0';
    return text;
}

char* console_parse_token(char** inout_cursor) {
    char* cursor;
    char* token;

    if (!inout_cursor || !*inout_cursor) return NULL;
    cursor = trim_whitespace(*inout_cursor);
    if (!cursor || !cursor[0]) {
        *inout_cursor = cursor;
        return NULL;
    }

    if (*cursor == '"' || *cursor == '\'') {
        char quote = *cursor++;
        token = cursor;
        while (*cursor && *cursor != quote) ++cursor;
        if (*cursor == quote) *cursor++ = '\0';
        *inout_cursor = cursor;
        return token;
    }

    token = cursor;
    while (*cursor && !isspace((unsigned char)*cursor)) ++cursor;
    if (*cursor) *cursor++ = '\0';
    *inout_cursor = cursor;
    return token;
}

int console_try_parse_long(const char* text, long* out_value) {
    char* end = NULL;
    long value;

    if (!text) return 0;
    while (*text && isspace((unsigned char)*text)) ++text;
    if (!text[0]) return 0;
    errno = 0;
    value = strtol(text, &end, 0);
    if (end == text || errno == ERANGE) return 0;
    while (*end && isspace((unsigned char)*end)) ++end;
    if (*end) return 0;
    if (out_value) *out_value = value;
    return 1;
}

int console_try_parse_double(const char* text, double* out_value) {
    char* end = NULL;
    double value;

    if (!text) return 0;
    while (*text && isspace((unsigned char)*text)) ++text;
    if (!text[0]) return 0;
    errno = 0;
    value = strtod(text, &end);
    if (end == text || errno == ERANGE || !isfinite(value)) return 0;
    while (*end && isspace((unsigned char)*end)) ++end;
    if (*end) return 0;
    if (out_value) *out_value = value;
    return 1;
}
