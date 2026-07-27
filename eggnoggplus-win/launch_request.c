#include "launch_request.h"

#include "online_control.h"

#include <stdio.h>
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
    set_error(error, error_cap, "conflicting online launch actions");
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
        default: return "none";
    }
}
