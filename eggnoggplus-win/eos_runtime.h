#pragma once

#include <stddef.h>

typedef enum YuleEosState {
    YULE_EOS_UNAVAILABLE = 0,
    YULE_EOS_PLATFORM_READY = 1,
    YULE_EOS_CONNECT_PENDING = 2,
    YULE_EOS_CONNECT_READY = 3,
    YULE_EOS_CONNECT_FAILED = 4
} YuleEosState;

typedef struct YuleEosConfig {
    const char* product_id;
    const char* sandbox_id;
    const char* deployment_id;
    const char* client_id;
    const char* client_secret;
} YuleEosConfig;

/* A missing SDK or invalid client configuration is a normal auto-mode fallback.
 * No EOS client credential is ever logged by this module. */
int yule_eos_initialize(const YuleEosConfig* config, char* err, size_t err_cap);
void yule_eos_tick(void);
int yule_eos_connect_login(const char* yule_access_token, char* err, size_t err_cap);
YuleEosState yule_eos_state(void);
const char* yule_eos_state_name(void);
const char* yule_eos_puid(void);
int yule_eos_auth_expiring(void);
int yule_eos_login_inflight(void);
int yule_eos_copy_id_token(char* out, size_t cap);
void yule_eos_shutdown(void);
/* Carrier-owned deferred closes progress with the platform and are released
 * before the SDK unloads. Return the number still draining; force must free all. */
void yule_eos_set_carrier_service(int (*service)(int force));

/* Opaque SDK handles for the EOS packet carrier. Valid only while Connect is
 * ready; the carrier must close before yule_eos_shutdown. */
void* yule_eos_sdk_module(void);
void* yule_eos_p2p_handle(void);
void* yule_eos_local_user_handle(void);
