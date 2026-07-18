#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Incoming online-control messages are deliberately a small, flat JSON
 * protocol.  These result values distinguish absence from malformed input and
 * from a destination that cannot hold the complete decoded value. */
typedef enum OnlineControlJsonResult {
    ONLINE_CONTROL_JSON_INVALID = -4,
    ONLINE_CONTROL_JSON_VALUE_INVALID = -3,
    ONLINE_CONTROL_JSON_TYPE_MISMATCH = -2,
    ONLINE_CONTROL_JSON_OUTPUT_TOO_SMALL = -1,
    ONLINE_CONTROL_JSON_NOT_FOUND = 0,
    ONLINE_CONTROL_JSON_OK = 1
} OnlineControlJsonResult;

/* Validates one complete, top-level JSON object.  Values may be strings,
 * numbers, booleans, or null.  Nested objects/arrays, duplicate decoded keys,
 * control characters, malformed escapes, excessive field/key counts, and
 * trailing bytes are rejected. */
int online_control_json_validate(const char* json, char* error, size_t error_cap);

OnlineControlJsonResult online_control_json_get_string(const char* json,
                                                        const char* key,
                                                        char* out,
                                                        size_t out_cap);
OnlineControlJsonResult online_control_json_get_int(const char* json,
                                                     const char* key,
                                                     int* out);

/* Canonical account names returned by the bundled server are normalized to
 * lowercase and constrained to [a-z0-9_], 1..24 bytes. */
int online_control_username_is_canonical(const char* username);

/* Wrap-safe GetTickCount-style deadlines.  A zero deadline means disabled. */
uint32_t online_control_deadline_after(uint32_t now, uint32_t delay_ms);
int online_control_deadline_reached(uint32_t now, uint32_t deadline);

#ifdef __cplusplus
}
#endif
