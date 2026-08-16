#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LAUNCH_REQUEST_TARGET_CAP 24576u
#define LAUNCH_PREVIEW_FILE_MAX_BYTES 12288u

typedef enum LaunchRequestAction {
    LAUNCH_REQUEST_NONE = 0,
    LAUNCH_REQUEST_HUB,
    LAUNCH_REQUEST_REQUESTS,
    LAUNCH_REQUEST_QUEUE_CASUAL,
    LAUNCH_REQUEST_QUEUE_COMPETITIVE,
    LAUNCH_REQUEST_CHALLENGE,
    LAUNCH_REQUEST_PREVIEW_V1
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
 * A preview target is exactly two unpadded base64url segments separated by a
 * slash: UTF-8 data.json followed by UTF-8 data.map. Decoding allocates both
 * NUL-terminated buffers; the caller owns them and must free them.
 */
int launch_request_preview_target_valid(const char* target);
int launch_request_decode_preview(const LaunchRequest* request,
                                  char** out_json,
                                  char** out_map,
                                  char* error,
                                  size_t error_cap);

const char* launch_request_action_name(LaunchRequestAction action);

#ifdef __cplusplus
}
#endif
