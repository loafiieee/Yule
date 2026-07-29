#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../discord_rpc_ext.h"

void log_write(const char* level, const char* fmt, ...) {
    (void)level;
    (void)fmt;
}

static void require(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static uint32_t read_u32_le(const unsigned char* value) {
    return (uint32_t)value[0] |
           ((uint32_t)value[1] << 8) |
           ((uint32_t)value[2] << 16) |
           ((uint32_t)value[3] << 24);
}

static void test_application_ids(void) {
    require(discord_rpc_ext_test_validate_application_id("12345678901234567890"),
            "20-digit application ID should be valid");
    require(discord_rpc_ext_test_validate_application_id("1"),
            "one-digit nonzero application ID should be valid");
    require(!discord_rpc_ext_test_validate_application_id(""),
            "empty application ID must be invalid");
    require(!discord_rpc_ext_test_validate_application_id("0123"),
            "leading-zero application ID must be invalid");
    require(!discord_rpc_ext_test_validate_application_id("123x"),
            "nondigit application ID must be invalid");
    require(!discord_rpc_ext_test_validate_application_id("123456789012345678901"),
            "overlong application ID must be invalid");

    require(discord_rpc_ext_set_application_id("9876543210987654321"),
            "valid runtime application ID should apply");
    require(strcmp(discord_rpc_ext_application_id(),
                   "9876543210987654321") == 0,
            "runtime application ID should be readable exactly");
    require(!discord_rpc_ext_set_application_id("invalid"),
            "invalid runtime application ID should be rejected");
    require(strcmp(discord_rpc_ext_application_id(),
                   "9876543210987654321") == 0,
            "rejected runtime ID must preserve the active ID");
}

static void test_config(void) {
    int enabled = -1;
    char application_id[21];
    require(discord_rpc_ext_test_parse_config(
                "# public local integration settings\n"
                "discord_presence = off\n"
                "discord_application_id = 12345678901234567890\n"
                "unrelated=value\n",
                &enabled, application_id, sizeof(application_id)),
            "valid config should parse");
    require(enabled == 0, "disabled presence should parse");
    require(strcmp(application_id, "12345678901234567890") == 0,
            "application ID should parse exactly");

    require(discord_rpc_ext_test_parse_config(
                "discord_presence=yes\n"
                "discord_application_id=not-a-snowflake\n",
                &enabled, application_id, sizeof(application_id)),
            "invalid application ID config should still parse safely");
    require(enabled == 1, "enabled presence should parse");
    require(strcmp(application_id, "1531027934004117664") == 0,
            "invalid application ID must preserve the compiled release default");

    require(discord_rpc_ext_test_parse_config(
                "",
                &enabled, application_id, sizeof(application_id)),
            "missing config should preserve release defaults");
    require(enabled == 1, "missing presence toggle must default on");
    require(strcmp(application_id, "1531027934004117664") == 0,
            "missing application ID must use the compiled release default");
}

static void test_activity_contract(void) {
    static const char* expected[][2] = {
        { NULL, NULL },
        { "In Menus", "Available" },
        { "Local Match", "Playing Locally" },
        { "Online Hub", "Available" },
        { "Online Hub", "Signing In" },
        { "Online Hub", "Friends & Challenges" },
        { "Looking for a Match", "Casual Queue" },
        { "Looking for a Match", "Competitive Queue" },
        { "Online Match", "Connecting" },
        { "Online Match", "Casual" },
        { "Online Match", "Competitive" },
        { "Online Match", "Friend Match" }
    };
    static const char* forbidden[] = {
        "\"secrets\"", "\"party\"", "\"buttons\"", "\"username\"",
        "\"opponent\"", "\"match_id\"", "\"token\"", "\"endpoint\"",
        "\"password\"", "yule://"
    };
    int activity;
    for (activity = DISCORD_RPC_ACTIVITY_NONE;
         activity < DISCORD_RPC_ACTIVITY_COUNT; activity++) {
        char json[512];
        size_t i;
        require(discord_rpc_ext_test_build_activity(
                    (DiscordRpcActivity)activity, 4242u, 123456789u, 7u,
                    json, sizeof(json)),
                "every public activity enum should serialize");
        require(strstr(json, "\"cmd\":\"SET_ACTIVITY\"") != NULL,
                "activity must use SET_ACTIVITY");
        require(strstr(json, "\"pid\":4242") != NULL,
                "activity must bind to the current process");
        require(strstr(json, "\"nonce\":\"7\"") != NULL,
                "activity must include the local request nonce");
        if (activity == DISCORD_RPC_ACTIVITY_NONE) {
            require(strstr(json, "\"activity\":null") != NULL,
                    "none must serialize as an explicit clear");
        } else {
            require(strstr(json, expected[activity][0]) != NULL,
                    "activity must contain its fixed details");
            require(strstr(json, expected[activity][1]) != NULL,
                    "activity must contain its fixed state");
            require(strstr(json, "\"start\":123456789") != NULL,
                    "activity must contain only the coarse state start time");
            require(strstr(json, "\"instance\":false") != NULL,
                    "activity must not advertise a joinable instance");
        }
        for (i = 0u; i < sizeof(forbidden) / sizeof(forbidden[0]); i++) {
            require(strstr(json, forbidden[i]) == NULL,
                    "activity JSON must exclude private/joinable fields");
        }
    }
    {
        char json[128];
        require(!discord_rpc_ext_test_build_activity(
                    DISCORD_RPC_ACTIVITY_COUNT, 1u, 1u, 1u,
                    json, sizeof(json)),
                "out-of-range activity must fail closed");
    }
}

static void test_frames(void) {
    static const char payload[] = "{\"v\":1}";
    unsigned char frame[64];
    size_t frame_len = 0u;
    require(discord_rpc_ext_test_encode_frame(
                0u, payload, sizeof(payload) - 1u,
                frame, sizeof(frame), &frame_len),
            "bounded frame should encode");
    require(frame_len == 8u + sizeof(payload) - 1u,
            "frame should have the documented eight-byte header");
    require(read_u32_le(frame) == 0u, "frame opcode should be little-endian");
    require(read_u32_le(frame + 4u) == sizeof(payload) - 1u,
            "frame payload length should be little-endian");
    require(memcmp(frame + 8u, payload, sizeof(payload) - 1u) == 0,
            "frame payload should be exact");
    require(!discord_rpc_ext_test_encode_frame(
                1u, payload, 65537u, frame, sizeof(frame), &frame_len),
            "oversized frame must be rejected before copying");
    require(!discord_rpc_ext_test_encode_frame(
                1u, payload, sizeof(payload) - 1u,
                frame, 8u, &frame_len),
            "undersized destination must be rejected");
}

int main(void) {
    test_application_ids();
    test_config();
    test_activity_contract();
    test_frames();
    puts("discord Rich Presence tests: OK");
    return 0;
}
