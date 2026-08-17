#include "mod_api.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/*
 * Capability identifiers are stable for the lifetime of API major 1. New
 * identifiers may be appended alongside an additive revision bump; existing
 * identifiers must not change meaning or disappear until the next major.
 */
static const char* const k_mod_api_capabilities[] = {
    "api.capabilities",
    "assets.spritesheets",
    "audio.bytebeat",
    "audio.dollchan_js",
    "console.commands",
    "content.tiles.v1",
    "events.removable",
    "fs.pick_file",
    "game.rollback_state",
    "http.async_get",
    "http.response_metadata",
    "input.bindings",
    "interop.services",
    "json.v1",
    "map.lua.v1",
    "net.tcp",
    "online.status",
    "storage.v1",
    "ui.native"
};

static void mod_api_set_error(char* error, size_t capacity, const char* message) {
    if (!error || capacity == 0) return;
    snprintf(error, capacity, "%s", message ? message : "incompatible mod API");
}

int mod_api_capability_name_valid(const char* name) {
    size_t length;
    size_t i;
    if (!name || !name[0]) return 0;
    length = strlen(name);
    if (length >= MOD_API_CAPABILITY_NAME_MAX) return 0;
    if (name[0] < 'a' || name[0] > 'z') return 0;
    for (i = 1; i < length; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (!(c >= 'a' && c <= 'z') &&
            !(c >= '0' && c <= '9') &&
            c != '.' && c != '_' && c != '-') {
            return 0;
        }
    }
    return 1;
}

int mod_api_capability_count(void) {
    return (int)(sizeof(k_mod_api_capabilities) /
                 sizeof(k_mod_api_capabilities[0]));
}

const char* mod_api_capability_at(int index) {
    if (index < 0 || index >= mod_api_capability_count()) return NULL;
    return k_mod_api_capabilities[index];
}

int mod_api_has_capability(const char* name) {
    int i;
    if (!mod_api_capability_name_valid(name)) return 0;
    for (i = 0; i < mod_api_capability_count(); ++i) {
        if (strcmp(k_mod_api_capabilities[i], name) == 0) return 1;
    }
    return 0;
}

int mod_api_check_compatibility(
    int required_major,
    int minimum_revision,
    const char* required_capabilities,
    int required_capability_count,
    char* error,
    size_t error_capacity
) {
    int i;
    if (error && error_capacity > 0) error[0] = '\0';

    if (required_major != MOD_API_MAJOR) {
        char message[128];
        snprintf(message, sizeof(message),
                 "requires API major %d; framework provides major %d",
                 required_major, MOD_API_MAJOR);
        mod_api_set_error(error, error_capacity, message);
        return 0;
    }
    if (minimum_revision < 0) {
        mod_api_set_error(error, error_capacity,
                          "api_revision must be zero or greater");
        return 0;
    }
    if (minimum_revision > MOD_API_REVISION) {
        char message[128];
        snprintf(message, sizeof(message),
                 "requires API revision %d; framework provides revision %d",
                 minimum_revision, MOD_API_REVISION);
        mod_api_set_error(error, error_capacity, message);
        return 0;
    }
    if (required_capability_count < 0 ||
        required_capability_count > MOD_API_REQUIRED_CAPABILITIES_MAX) {
        mod_api_set_error(error, error_capacity,
                          "api_requires has an invalid entry count");
        return 0;
    }
    if (required_capability_count > 0 && !required_capabilities) {
        mod_api_set_error(error, error_capacity,
                          "api_requires storage is missing");
        return 0;
    }

    for (i = 0; i < required_capability_count; ++i) {
        const char* capability =
            required_capabilities +
            ((size_t)i * MOD_API_CAPABILITY_NAME_MAX);
        if (!mod_api_capability_name_valid(capability)) {
            char message[160];
            snprintf(message, sizeof(message),
                     "api_requires[%d] is not a valid capability identifier", i + 1);
            mod_api_set_error(error, error_capacity, message);
            return 0;
        }
        if (!mod_api_has_capability(capability)) {
            char message[192];
            snprintf(message, sizeof(message),
                     "requires unavailable API capability \"%s\"", capability);
            mod_api_set_error(error, error_capacity, message);
            return 0;
        }
    }
    return 1;
}
