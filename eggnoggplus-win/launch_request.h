#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LAUNCH_REQUEST_TARGET_CAP 25u

typedef enum LaunchRequestAction {
    LAUNCH_REQUEST_NONE = 0,
    LAUNCH_REQUEST_HUB,
    LAUNCH_REQUEST_REQUESTS,
    LAUNCH_REQUEST_QUEUE_CASUAL,
    LAUNCH_REQUEST_QUEUE_COMPETITIVE,
    LAUNCH_REQUEST_CHALLENGE
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

const char* launch_request_action_name(LaunchRequestAction action);

#ifdef __cplusplus
}
#endif
