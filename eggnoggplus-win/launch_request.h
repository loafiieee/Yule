#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LAUNCH_REQUEST_TARGET_CAP 24576u
#define LAUNCH_PREVIEW_FILE_MAX_BYTES 12288u
#define LAUNCH_PREVIEW_PACKED_MAX_BYTES 12288u

typedef enum LaunchRequestAction {
    LAUNCH_REQUEST_NONE = 0,
    LAUNCH_REQUEST_HUB,
    LAUNCH_REQUEST_REQUESTS,
    LAUNCH_REQUEST_QUEUE_CASUAL,
    LAUNCH_REQUEST_QUEUE_COMPETITIVE,
    LAUNCH_REQUEST_CHALLENGE,
    LAUNCH_REQUEST_PREVIEW_V1,
    LAUNCH_REQUEST_PREVIEW_V1_PACKED,
    LAUNCH_REQUEST_PREVIEW_SESSION
} LaunchRequestAction;

typedef struct LaunchRequest {
    LaunchRequestAction action;
    char target[LAUNCH_REQUEST_TARGET_CAP];
} LaunchRequest;

typedef enum LaunchRequestParseResult {
    LAUNCH_REQUEST_PARSE_ERROR = -1,
    LAUNCH_REQUEST_PARSE_NONE = 0,
    LAUNCH_REQUEST_PARSE_OK = 1
} LaunchRequestParseResult;

/*
 * Parse safe, external launch intents. Unknown application arguments are
 * ignored. Any malformed yule:// URI, malformed relevant switch, or conflict
 * rejects the complete request rather than selecting a surprising action.
 */
LaunchRequestParseResult launch_request_parse_args(int argc,
                                                   const char* const* argv,
                                                   LaunchRequest* out,
                                                   char* error,
                                                   size_t error_cap);

/*
 * The legacy preview target is two unpadded base64url segments separated by a
 * slash. The compact target is one bounded base64url GGP1/LZSS envelope.
 * Decoding either form allocates UTF-8 data.json and data.map buffers; the
 * caller owns them and must free them.
 */
/* A preview session token is exactly 128 bits encoded as lowercase hex. */
int launch_request_preview_session_valid(const char* token);
int launch_request_preview_target_valid(const char* target);
int launch_request_preview_packed_target_valid(const char* target);
int launch_request_decode_preview(const LaunchRequest* request,
                                  char** out_json,
                                  char** out_map,
                                  char* error,
                                  size_t error_cap);

const char* launch_request_action_name(LaunchRequestAction action);

#ifdef __cplusplus
}
#endif
