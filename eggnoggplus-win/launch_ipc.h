#pragma once

#include "launch_request.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Starts the per-login-session deep-link broker. If this process was launched
 * with a valid Yule intent and another broker already exists, forwards the
 * request and returns 1 so the caller can exit before SDL creates a window.
 */
int launch_ipc_initialize(void);

/* Called only from the game/update thread. Returns one validated request. */
int launch_ipc_poll(LaunchRequest* out);

/* Best-effort orderly shutdown; process termination remains the hard bound. */
void launch_ipc_shutdown(void);

#ifdef __cplusplus
}
#endif
