#include "launch_request.h"

#include "online_control.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void set_error(char* error, size_t error_cap, const char* message) {
    if (!error || error_cap == 0u) return;
    snprintf(error, error_cap, "%s", message ? message : "invalid launch request");
}

static unsigned char ascii_lower(unsigned char c) {
    return (c >= (unsigned char)'A' && c <= (unsigned char)'Z')
        ? (unsigned char)(c + ((unsigned char)'a' - (unsigned char)'A'))
        : c;
}

static int ascii_equal_ci(const char* left, const char* right) {
    size_t i = 0u;
    if (!left || !right) return 0;
    while (left[i] && right[i] &&
           ascii_lower((unsigned char)left[i]) ==
               ascii_lower((unsigned char)right[i])) {
        i++;
    }
    return left[i] == '\0' && right[i] == '\0';
}

static int ascii_starts_ci(const char* value, const char* prefix) {
    size_t i = 0u;
    if (!value || !prefix) return 0;
    while (prefix[i]) {
        if (!value[i] ||
            ascii_lower((unsigned char)value[i]) !=
                ascii_lower((unsigned char)prefix[i])) {
            return 0;
        }
        i++;
    }
    return 1;
}

static int request_equal(const LaunchRequest* left,
                         const LaunchRequest* right) {
    return left && right && left->action == right->action &&
           strcmp(left->target, right->target) == 0;
}

static int request_merge(LaunchRequest* aggregate,
                         int* have_request,
                         const LaunchRequest* candidate,
                         char* error,
                         size_t error_cap) {
    if (!aggregate || !have_request || !candidate) return 0;
    if (!*have_request) {
        *aggregate = *candidate;
        *have_request = 1;
        return 1;
    }
    if (request_equal(aggregate, candidate)) return 1;
    set_error(error, error_cap, "conflicting launch actions");
    return 0;
}

static int parse_queue(const char* value,
                       LaunchRequest* out,
                       char* error,
                       size_t error_cap) {
    if (ascii_equal_ci(value, "casual")) {
        out->action = LAUNCH_REQUEST_QUEUE_CASUAL;
        return 1;
    }
    if (ascii_equal_ci(value, "competitive")) {
        out->action = LAUNCH_REQUEST_QUEUE_COMPETITIVE;
        return 1;
    }
    set_error(error, error_cap,
              "queue must be casual or competitive");
    return 0;
}

static int parse_challenge(const char* value,
                           LaunchRequest* out,
                           char* error,
                           size_t error_cap) {
    if (!online_control_username_is_canonical(value)) {
        set_error(error, error_cap,
                  "challenge username must be 1-24 lowercase a-z, 0-9, or _");
        return 0;
    }
    out->action = LAUNCH_REQUEST_CHALLENGE;
    snprintf(out->target, sizeof(out->target), "%s", value);
    return 1;
}

static int uri_has_forbidden_syntax(const char* uri) {
    const unsigned char* p = (const unsigned char*)uri;
    for (; p && *p; p++) {
        if (*p < 0x21u || *p > 0x7eu ||
            *p == (unsigned char)'?' ||
            *p == (unsigned char)'#' ||
            *p == (unsigned char)'%' ||
            *p == (unsigned char)'\\' ||
            *p == (unsigned char)'@') {
            return 1;
        }
    }
    return 0;
}

static int base64url_value(unsigned char c) {
    if (c >= (unsigned char)'A' && c <= (unsigned char)'Z') return (int)(c - 'A');
    if (c >= (unsigned char)'a' && c <= (unsigned char)'z') return 26 + (int)(c - 'a');
    if (c >= (unsigned char)'0' && c <= (unsigned char)'9') return 52 + (int)(c - '0');
    if (c == (unsigned char)'-') return 62;
    if (c == (unsigned char)'_') return 63;
    return -1;
}

static int preview_segment_valid(const char* text, size_t length) {
    size_t i;
    size_t decoded;
    int tail;
    if (!text || length == 0u || (length & 3u) == 1u) return 0;
    decoded = (length / 4u) * 3u;
    if ((length & 3u) == 2u) decoded += 1u;
    if ((length & 3u) == 3u) decoded += 2u;
    if (decoded == 0u || decoded > LAUNCH_PREVIEW_FILE_MAX_BYTES) return 0;
    for (i = 0u; i < length; i++) {
        if (base64url_value((unsigned char)text[i]) < 0) return 0;
    }
    /* Reject alternate encodings with non-zero unused tail bits. */
    tail = base64url_value((unsigned char)text[length - 1u]);
    if ((length & 3u) == 2u && (tail & 15) != 0) return 0;
    if ((length & 3u) == 3u && (tail & 3) != 0) return 0;
    return 1;
}

int launch_request_preview_target_valid(const char* target) {
    const char* separator;
    size_t total;
    if (!target) return 0;
    total = strlen(target);
    if (total == 0u || total >= LAUNCH_REQUEST_TARGET_CAP) return 0;
    separator = strchr(target, '/');
    if (!separator || strchr(separator + 1, '/')) return 0;
    return preview_segment_valid(target, (size_t)(separator - target)) &&
           preview_segment_valid(separator + 1, strlen(separator + 1));
}

int launch_request_preview_packed_target_valid(const char* target) {
    size_t length;
    if (!target) return 0;
    length = strlen(target);
    return length > 0u && length < LAUNCH_REQUEST_TARGET_CAP &&
           preview_segment_valid(target, length) &&
           ((length / 4u) * 3u +
            ((length & 3u) == 2u ? 1u : (length & 3u) == 3u ? 2u : 0u))
               <= LAUNCH_PREVIEW_PACKED_MAX_BYTES;
}

static unsigned char* decode_preview_bytes(const char* text,
                                           size_t length,
                                           size_t* out_length,
                                           char* error,
                                           size_t error_cap) {
    size_t decoded_length = (length / 4u) * 3u +
        ((length & 3u) == 2u ? 1u : (length & 3u) == 3u ? 2u : 0u);
    unsigned char* decoded = (unsigned char*)malloc(decoded_length ? decoded_length : 1u);
    size_t source = 0u;
    size_t dest = 0u;
    if (out_length) *out_length = 0u;
    if (!decoded) {
        set_error(error, error_cap, "out of memory decoding preview map");
        return NULL;
    }
    while (source + 4u <= length) {
        unsigned value = ((unsigned)base64url_value((unsigned char)text[source]) << 18) |
                         ((unsigned)base64url_value((unsigned char)text[source + 1u]) << 12) |
                         ((unsigned)base64url_value((unsigned char)text[source + 2u]) << 6) |
                         (unsigned)base64url_value((unsigned char)text[source + 3u]);
        decoded[dest++] = (unsigned char)((value >> 16) & 0xffu);
        decoded[dest++] = (unsigned char)((value >> 8) & 0xffu);
        decoded[dest++] = (unsigned char)(value & 0xffu);
        source += 4u;
    }
    if (length - source == 2u) {
        unsigned value = ((unsigned)base64url_value((unsigned char)text[source]) << 6) |
                         (unsigned)base64url_value((unsigned char)text[source + 1u]);
        decoded[dest++] = (unsigned char)((value >> 4) & 0xffu);
    } else if (length - source == 3u) {
        unsigned value = ((unsigned)base64url_value((unsigned char)text[source]) << 12) |
                         ((unsigned)base64url_value((unsigned char)text[source + 1u]) << 6) |
                         (unsigned)base64url_value((unsigned char)text[source + 2u]);
        decoded[dest++] = (unsigned char)((value >> 10) & 0xffu);
        decoded[dest++] = (unsigned char)((value >> 2) & 0xffu);
    }
    if (out_length) *out_length = dest;
    return decoded;
}

static char* decode_preview_segment(const char* text,
                                    size_t length,
                                    char* error,
                                    size_t error_cap) {
    size_t decoded_length = 0u;
    unsigned char* bytes = decode_preview_bytes(text, length, &decoded_length,
                                                error, error_cap);
    char* decoded;
    if (!bytes) return NULL;
    decoded = (char*)realloc(bytes, decoded_length + 1u);
    if (!decoded) {
        free(bytes);
        set_error(error, error_cap, "out of memory decoding preview map");
        return NULL;
    }
    decoded[decoded_length] = '\0';
    if (memchr(decoded, '\0', decoded_length)) {
        free(decoded);
        set_error(error, error_cap, "preview files cannot contain NUL bytes");
        return NULL;
    }
    return decoded;
}

static uint32_t read_u32_le(const unsigned char* bytes) {
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static int decode_packed_preview(const char* target,
                                 char** out_json,
                                 char** out_map,
                                 char* error,
                                 size_t error_cap) {
    unsigned char* packed;
    unsigned char* output;
    size_t packed_length = 0u;
    size_t input_pos = 12u;
    size_t output_pos = 0u;
    size_t total_length;
    uint32_t json_length;
    uint32_t map_length;
    char* json;
    char* map;
    if (!launch_request_preview_packed_target_valid(target)) {
        set_error(error, error_cap, "invalid packed preview payload");
        return 0;
    }
    packed = decode_preview_bytes(target, strlen(target), &packed_length,
                                  error, error_cap);
    if (!packed) return 0;
    if (packed_length < 12u || memcmp(packed, "GGP1", 4u) != 0) {
        free(packed);
        set_error(error, error_cap, "packed preview header is invalid");
        return 0;
    }
    json_length = read_u32_le(packed + 4u);
    map_length = read_u32_le(packed + 8u);
    if (json_length == 0u || map_length == 0u ||
        json_length > LAUNCH_PREVIEW_FILE_MAX_BYTES ||
        map_length > LAUNCH_PREVIEW_FILE_MAX_BYTES) {
        free(packed);
        set_error(error, error_cap, "packed preview file size is invalid");
        return 0;
    }
    total_length = (size_t)json_length + (size_t)map_length;
    output = (unsigned char*)malloc(total_length);
    if (!output) {
        free(packed);
        set_error(error, error_cap, "out of memory unpacking preview map");
        return 0;
    }
    while (output_pos < total_length) {
        unsigned flags;
        unsigned bit;
        if (input_pos >= packed_length) goto corrupt;
        flags = packed[input_pos++];
        for (bit = 0u; bit < 8u && output_pos < total_length; bit++) {
            if ((flags & (1u << bit)) != 0u) {
                unsigned first;
                unsigned second;
                size_t distance;
                size_t match_length;
                size_t i;
                if (input_pos + 2u > packed_length) goto corrupt;
                first = packed[input_pos++];
                second = packed[input_pos++];
                distance = (size_t)(first | ((second & 15u) << 8)) + 1u;
                match_length = (size_t)(second >> 4) + 3u;
                if (distance > output_pos || match_length > total_length - output_pos) goto corrupt;
                for (i = 0u; i < match_length; i++) {
                    output[output_pos] = output[output_pos - distance];
                    output_pos++;
                }
            } else {
                if (input_pos >= packed_length) goto corrupt;
                output[output_pos++] = packed[input_pos++];
            }
        }
    }
    if (input_pos != packed_length ||
        memchr(output, '\0', (size_t)json_length) ||
        memchr(output + json_length, '\0', (size_t)map_length)) goto corrupt;
    json = (char*)malloc((size_t)json_length + 1u);
    map = (char*)malloc((size_t)map_length + 1u);
    if (!json || !map) {
        free(json);
        free(map);
        free(output);
        free(packed);
        set_error(error, error_cap, "out of memory unpacking preview map");
        return 0;
    }
    memcpy(json, output, (size_t)json_length);
    json[json_length] = '\0';
    memcpy(map, output + json_length, (size_t)map_length);
    map[map_length] = '\0';
    free(output);
    free(packed);
    *out_json = json;
    *out_map = map;
    return 1;

corrupt:
    free(output);
    free(packed);
    set_error(error, error_cap, "packed preview data is corrupt or truncated");
    return 0;
}

int launch_request_decode_preview(const LaunchRequest* request,
                                  char** out_json,
                                  char** out_map,
                                  char* error,
                                  size_t error_cap) {
    const char* separator;
    char* json;
    char* map;
    if (out_json) *out_json = NULL;
    if (out_map) *out_map = NULL;
    if (error && error_cap > 0u) error[0] = '\0';
    if (!request || !out_json || !out_map) {
        set_error(error, error_cap, "invalid preview payload");
        return 0;
    }
    if (request->action == LAUNCH_REQUEST_PREVIEW_V1_PACKED) {
        return decode_packed_preview(request->target, out_json, out_map,
                                     error, error_cap);
    }
    if (request->action != LAUNCH_REQUEST_PREVIEW_V1 ||
        !launch_request_preview_target_valid(request->target)) {
        set_error(error, error_cap, "invalid preview payload");
        return 0;
    }
    separator = strchr(request->target, '/');
    json = decode_preview_segment(request->target,
                                  (size_t)(separator - request->target),
                                  error, error_cap);
    if (!json) return 0;
    map = decode_preview_segment(separator + 1, strlen(separator + 1),
                                 error, error_cap);
    if (!map) {
        free(json);
        return 0;
    }
    *out_json = json;
    *out_map = map;
    return 1;
}

int launch_request_preview_session_valid(const char* token) {
    if(!token)return 0;
    for(size_t i=0;i<32;i++)if(!((token[i]>='0'&&token[i]<='9')||(token[i]>='a'&&token[i]<='f')))return 0;
    return token[32]==0;
}

static int parse_uri(const char* uri,
                     LaunchRequest* out,
                     char* error,
                     size_t error_cap) {
    char normalized_route[64];
    const char* route;
    size_t route_len;
    static const char scheme[] = "yule://";
    if (uri_has_forbidden_syntax(uri)) {
        set_error(error, error_cap,
                  "yule URI contains forbidden syntax");
        return 0;
    }
    route = uri + sizeof(scheme) - 1u;
    route_len = strlen(route);
    if(ascii_starts_ci(route,"preview/session/")) {
        const char* token=route+16u;
        if(!launch_request_preview_session_valid(token)){set_error(error,error_cap,"invalid preview session token");return 0;}
        out->action=LAUNCH_REQUEST_PREVIEW_SESSION;memcpy(out->target,token,33);return 1;
    }
    if (ascii_starts_ci(route, "preview/v1z/")) {
        const char* target = route + 12u;
        size_t target_len = strlen(target);
        if (!launch_request_preview_packed_target_valid(target)) {
            set_error(error, error_cap, "invalid compressed V1 preview payload");
            return 0;
        }
        out->action = LAUNCH_REQUEST_PREVIEW_V1_PACKED;
        memcpy(out->target, target, target_len + 1u);
        return 1;
    }
    if (ascii_starts_ci(route, "preview/v1/")) {
        const char* target = route + 11u;
        size_t target_len = strlen(target);
        if (!launch_request_preview_target_valid(target)) {
            set_error(error, error_cap, "invalid V1 preview payload");
            return 0;
        }
        if (target_len >= sizeof(out->target)) {
            set_error(error, error_cap, "preview payload is too long");
            return 0;
        }
        out->action = LAUNCH_REQUEST_PREVIEW_V1;
        memcpy(out->target, target, target_len + 1u);
        return 1;
    }
    if (route_len >= sizeof(normalized_route)) {
        set_error(error, error_cap, "yule URI action is too long");
        return 0;
    }
    memcpy(normalized_route, route, route_len + 1u);
    /*
     * Windows ShellExecute canonicalizes authority-only custom URLs such as
     * yule://hub to yule://hub/. Accept exactly that harmless terminal slash
     * (and the same browser normalization on other completed routes). A second
     * slash, query, fragment, encoding, or unknown action remains invalid.
     */
    if (route_len > 0u && normalized_route[route_len - 1u] == '/') {
        normalized_route[route_len - 1u] = '\0';
    }
    route = normalized_route;
    if (!*route || ascii_equal_ci(route, "hub")) {
        out->action = LAUNCH_REQUEST_HUB;
        return 1;
    }
    if (ascii_equal_ci(route, "requests")) {
        out->action = LAUNCH_REQUEST_REQUESTS;
        return 1;
    }
    if (ascii_starts_ci(route, "queue/")) {
        return parse_queue(route + 6, out, error, error_cap);
    }
    if (ascii_starts_ci(route, "challenge/")) {
        return parse_challenge(route + 10, out, error, error_cap);
    }
    set_error(error, error_cap, "unsupported yule URI action");
    return 0;
}

static int parse_switch(const char* arg,
                        LaunchRequest* out,
                        int* recognized,
                        char* error,
                        size_t error_cap) {
    const char* value;
    *recognized = 1;
    if (ascii_equal_ci(arg, "-online") ||
        ascii_equal_ci(arg, "--online")) {
        out->action = LAUNCH_REQUEST_HUB;
        return 1;
    }
    if (ascii_equal_ci(arg, "-requests") ||
        ascii_equal_ci(arg, "--requests") ||
        ascii_equal_ci(arg, "--online-requests")) {
        out->action = LAUNCH_REQUEST_REQUESTS;
        return 1;
    }
    if (ascii_starts_ci(arg, "-queue=")) {
        value = arg + 7;
        return parse_queue(value, out, error, error_cap);
    }
    if (ascii_starts_ci(arg, "--queue=")) {
        value = arg + 8;
        return parse_queue(value, out, error, error_cap);
    }
    if (ascii_starts_ci(arg, "-challenge=")) {
        value = arg + 11;
        return parse_challenge(value, out, error, error_cap);
    }
    if (ascii_starts_ci(arg, "--challenge=")) {
        value = arg + 12;
        return parse_challenge(value, out, error, error_cap);
    }
    if (ascii_equal_ci(arg, "-queue") ||
        ascii_equal_ci(arg, "--queue") ||
        ascii_equal_ci(arg, "-challenge") ||
        ascii_equal_ci(arg, "--challenge")) {
        set_error(error, error_cap,
                  "online launch switch requires an equals value");
        return 0;
    }
    if (ascii_starts_ci(arg, "-online") ||
        ascii_starts_ci(arg, "--online") ||
        ascii_starts_ci(arg, "-requests") ||
        ascii_starts_ci(arg, "--requests") ||
        ascii_starts_ci(arg, "-queue") ||
        ascii_starts_ci(arg, "--queue") ||
        ascii_starts_ci(arg, "-challenge") ||
        ascii_starts_ci(arg, "--challenge")) {
        set_error(error, error_cap,
                  "malformed online launch switch");
        return 0;
    }
    *recognized = 0;
    return 1;
}

LaunchRequestParseResult launch_request_parse_args(int argc,
                                                   const char* const* argv,
                                                   LaunchRequest* out,
                                                   char* error,
                                                   size_t error_cap) {
    LaunchRequest aggregate;
    int have_request = 0;
    int i;
    if (out) memset(out, 0, sizeof(*out));
    if (error && error_cap > 0u) error[0] = '\0';
    if (argc < 0 || (argc > 0 && !argv) || !out) {
        set_error(error, error_cap, "invalid launch argument array");
        return LAUNCH_REQUEST_PARSE_ERROR;
    }
    memset(&aggregate, 0, sizeof(aggregate));
    for (i = 1; i < argc; i++) {
        const char* arg = argv[i];
        LaunchRequest candidate;
        int recognized = 0;
        int ok;
        if (!arg || !*arg) continue;
        memset(&candidate, 0, sizeof(candidate));
        if (ascii_starts_ci(arg, "--yule-uri=")) {
            const char* uri = arg + 11;
            recognized = 1;
            if (argc != 2) {
                set_error(error, error_cap,
                          "protocol launch must contain exactly one URI argument");
                return LAUNCH_REQUEST_PARSE_ERROR;
            }
            if (!ascii_starts_ci(uri, "yule://")) {
                set_error(error, error_cap,
                          "protocol launch value is not a yule URI");
                return LAUNCH_REQUEST_PARSE_ERROR;
            }
            ok = parse_uri(uri, &candidate, error, error_cap);
        } else if (ascii_starts_ci(arg, "yule://")) {
            recognized = 1;
            ok = parse_uri(arg, &candidate, error, error_cap);
        } else if (ascii_starts_ci(arg, "yule:")) {
            set_error(error, error_cap, "malformed yule URI scheme");
            return LAUNCH_REQUEST_PARSE_ERROR;
        } else if (ascii_starts_ci(arg, "--yule-uri")) {
            set_error(error, error_cap, "malformed protocol URI envelope");
            return LAUNCH_REQUEST_PARSE_ERROR;
        } else {
            ok = parse_switch(arg, &candidate, &recognized, error, error_cap);
        }
        if (!ok) return LAUNCH_REQUEST_PARSE_ERROR;
        if (recognized &&
            !request_merge(&aggregate, &have_request, &candidate,
                           error, error_cap)) {
            return LAUNCH_REQUEST_PARSE_ERROR;
        }
    }
    if (!have_request) return LAUNCH_REQUEST_PARSE_NONE;
    *out = aggregate;
    return LAUNCH_REQUEST_PARSE_OK;
}

const char* launch_request_action_name(LaunchRequestAction action) {
    switch (action) {
        case LAUNCH_REQUEST_HUB: return "hub";
        case LAUNCH_REQUEST_REQUESTS: return "requests";
        case LAUNCH_REQUEST_QUEUE_CASUAL: return "queue/casual";
        case LAUNCH_REQUEST_QUEUE_COMPETITIVE: return "queue/competitive";
        case LAUNCH_REQUEST_CHALLENGE: return "challenge";
        case LAUNCH_REQUEST_PREVIEW_V1: return "preview/v1";
        case LAUNCH_REQUEST_PREVIEW_V1_PACKED: return "preview/v1z";
        case LAUNCH_REQUEST_PREVIEW_SESSION: return "preview/session";
        default: return "none";
    }
}
