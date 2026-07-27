#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This closed enum is the privacy boundary for Discord Rich Presence. Callers
 * cannot provide arbitrary strings, identifiers, endpoints, names, or secrets.
 */
typedef enum DiscordRpcActivity {
    DISCORD_RPC_ACTIVITY_NONE = 0,
    DISCORD_RPC_ACTIVITY_MENUS,
    DISCORD_RPC_ACTIVITY_LOCAL_MATCH,
    DISCORD_RPC_ACTIVITY_ONLINE_HUB,
    DISCORD_RPC_ACTIVITY_SIGNING_IN,
    DISCORD_RPC_ACTIVITY_SOCIAL,
    DISCORD_RPC_ACTIVITY_QUEUE_CASUAL,
    DISCORD_RPC_ACTIVITY_QUEUE_COMPETITIVE,
    DISCORD_RPC_ACTIVITY_MATCH_SETUP,
    DISCORD_RPC_ACTIVITY_MATCH_CASUAL,
    DISCORD_RPC_ACTIVITY_MATCH_COMPETITIVE,
    DISCORD_RPC_ACTIVITY_MATCH_PRIVATE,
    DISCORD_RPC_ACTIVITY_COUNT
} DiscordRpcActivity;

/*
 * Lazily loads configuration and services the local Discord desktop IPC pipe.
 * The function is nonblocking and must be called only from the normal runtime
 * thread, never from DllMain.
 */
void discord_rpc_ext_pump(DiscordRpcActivity activity);

/* Closes local IPC without waiting. Safe to call more than once. */
void discord_rpc_ext_shutdown(void);

int discord_rpc_ext_enabled(void);
int discord_rpc_ext_available(void);
void discord_rpc_ext_set_enabled(int enabled);
/*
 * Replaces the public Discord Application ID at runtime and reconnects without
 * waiting. The caller owns persistence; invalid snowflakes are rejected
 * without changing the current ID.
 */
int discord_rpc_ext_set_application_id(const char* application_id);
const char* discord_rpc_ext_application_id(void);
const char* discord_rpc_ext_setting_label(void);

#ifdef DISCORD_RPC_EXT_TEST
int discord_rpc_ext_test_validate_application_id(const char* value);
int discord_rpc_ext_test_parse_config(const char* text, int* enabled,
                                      char* application_id, size_t application_id_cap);
int discord_rpc_ext_test_build_activity(DiscordRpcActivity activity,
                                        uint32_t process_id,
                                        uint64_t start_seconds,
                                        uint32_t nonce,
                                        char* out, size_t out_cap);
int discord_rpc_ext_test_encode_frame(uint32_t opcode, const void* payload,
                                      size_t payload_len, unsigned char* out,
                                      size_t out_cap, size_t* out_len);
#endif

#ifdef __cplusplus
}
#endif
