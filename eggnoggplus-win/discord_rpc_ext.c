#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <windows.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "discord_rpc_ext.h"
#include "log.h"

#ifndef EGGNOGGPLUS_DISCORD_APPLICATION_ID
#define EGGNOGGPLUS_DISCORD_APPLICATION_ID "1531027934004117664"
#endif

#define DISCORD_RPC_CONFIG_PATH "mods/modframework.cfg"
#define DISCORD_RPC_MAX_APPLICATION_ID 20u
#define DISCORD_RPC_MAX_PAYLOAD (64u * 1024u)
#define DISCORD_RPC_FRAME_HEADER 8u
#define DISCORD_RPC_RECONNECT_MS 15000u
#define DISCORD_RPC_UPDATE_INTERVAL_MS 4100u

enum {
    DISCORD_RPC_OPCODE_HANDSHAKE = 0,
    DISCORD_RPC_OPCODE_FRAME = 1,
    DISCORD_RPC_OPCODE_CLOSE = 2,
    DISCORD_RPC_OPCODE_PING = 3,
    DISCORD_RPC_OPCODE_PONG = 4
};

typedef struct DiscordRpcRuntime {
    int booted;
    int enabled;
    int available;
    int ready;
    int logged_unavailable;
    char application_id[DISCORD_RPC_MAX_APPLICATION_ID + 1u];

    HANDLE pipe;
    HANDLE read_event;
    HANDLE write_event;
    OVERLAPPED read_overlapped;
    OVERLAPPED write_overlapped;
    int read_active;
    int write_active;

    unsigned char read_header[DISCORD_RPC_FRAME_HEADER];
    size_t read_header_used;
    unsigned char read_payload[DISCORD_RPC_MAX_PAYLOAD + 1u];
    size_t read_payload_len;
    size_t read_payload_used;
    int reading_payload;

    unsigned char write_frame[DISCORD_RPC_FRAME_HEADER + DISCORD_RPC_MAX_PAYLOAD];
    size_t write_frame_len;
    unsigned char pending_frame[DISCORD_RPC_FRAME_HEADER + DISCORD_RPC_MAX_PAYLOAD];
    size_t pending_frame_len;

    DiscordRpcActivity activity;
    uint64_t activity_start_seconds;
    uint32_t nonce;
    int activity_dirty;
    ULONGLONG reconnect_at_ms;
    ULONGLONG next_activity_ms;
} DiscordRpcRuntime;

static DiscordRpcRuntime g_discord_rpc;

static uint32_t discord_read_u32_le(const unsigned char* value) {
    return (uint32_t)value[0] |
           ((uint32_t)value[1] << 8) |
           ((uint32_t)value[2] << 16) |
           ((uint32_t)value[3] << 24);
}

static void discord_write_u32_le(unsigned char* out, uint32_t value) {
    out[0] = (unsigned char)(value & 0xffu);
    out[1] = (unsigned char)((value >> 8) & 0xffu);
    out[2] = (unsigned char)((value >> 16) & 0xffu);
    out[3] = (unsigned char)((value >> 24) & 0xffu);
}

static int discord_validate_application_id(const char* value) {
    size_t len;
    size_t i;
    if (!value) return 0;
    len = strlen(value);
    if (len == 0u || len > DISCORD_RPC_MAX_APPLICATION_ID) return 0;
    if (value[0] < '1' || value[0] > '9') return 0;
    for (i = 1u; i < len; i++) {
        if (value[i] < '0' || value[i] > '9') return 0;
    }
    return 1;
}

static char* discord_trim(char* value) {
    char* end;
    while (*value && isspace((unsigned char)*value)) value++;
    end = value + strlen(value);
    while (end > value && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return value;
}

static int discord_parse_bool(const char* value, int* out) {
    if (!value || !out) return 0;
    if (_stricmp(value, "1") == 0 || _stricmp(value, "true") == 0 ||
        _stricmp(value, "yes") == 0 || _stricmp(value, "on") == 0) {
        *out = 1;
        return 1;
    }
    if (_stricmp(value, "0") == 0 || _stricmp(value, "false") == 0 ||
        _stricmp(value, "no") == 0 || _stricmp(value, "off") == 0) {
        *out = 0;
        return 1;
    }
    return 0;
}

static void discord_parse_config_line(char* line, int* enabled,
                                      char application_id[DISCORD_RPC_MAX_APPLICATION_ID + 1u]) {
    char* key;
    char* value;
    char* equals;
    if (!line || !enabled || !application_id) return;
    key = discord_trim(line);
    if (!key[0] || key[0] == '#' || key[0] == ';') return;
    equals = strchr(key, '=');
    if (!equals) return;
    *equals = '\0';
    value = discord_trim(equals + 1);
    key = discord_trim(key);
    if (_stricmp(key, "discord_presence") == 0) {
        int parsed;
        if (discord_parse_bool(value, &parsed)) *enabled = parsed;
    } else if (_stricmp(key, "discord_application_id") == 0) {
        if (discord_validate_application_id(value)) {
            memcpy(application_id, value, strlen(value) + 1u);
        }
        /* The compiled release ID is public, valid, and usable without a
         * config file. A stale/blank local override must not silently disable
         * Rich Presence for upgraded installs; invalid overrides are ignored. */
    }
}

static int discord_parse_config_text(const char* text, int* enabled,
                                     char application_id[DISCORD_RPC_MAX_APPLICATION_ID + 1u]) {
    const char* cursor;
    if (!text || !enabled || !application_id) return 0;
    cursor = text;
    while (*cursor) {
        char line[512];
        size_t len = 0u;
        while (cursor[len] && cursor[len] != '\r' && cursor[len] != '\n') len++;
        if (len < sizeof(line)) {
            memcpy(line, cursor, len);
            line[len] = '\0';
            discord_parse_config_line(line, enabled, application_id);
        }
        cursor += len;
        while (*cursor == '\r' || *cursor == '\n') cursor++;
    }
    return 1;
}

static void discord_load_config(void) {
    FILE* file;
    char line[512];
    const char* compiled_id = EGGNOGGPLUS_DISCORD_APPLICATION_ID;
    g_discord_rpc.enabled = 1;
    g_discord_rpc.application_id[0] = '\0';
    if (discord_validate_application_id(compiled_id)) {
        memcpy(g_discord_rpc.application_id, compiled_id, strlen(compiled_id) + 1u);
    }

    file = fopen(DISCORD_RPC_CONFIG_PATH, "rb");
    if (file) {
        while (fgets(line, sizeof(line), file)) {
            size_t len = strlen(line);
            if (len > 0u && line[len - 1u] != '\n' && !feof(file)) {
                int ch;
                while ((ch = fgetc(file)) != '\n' && ch != EOF) {
                }
                continue;
            }
            discord_parse_config_line(line, &g_discord_rpc.enabled,
                                      g_discord_rpc.application_id);
        }
        fclose(file);
    }
    g_discord_rpc.available =
        discord_validate_application_id(g_discord_rpc.application_id);
}

static int discord_encode_frame(uint32_t opcode, const void* payload,
                                size_t payload_len, unsigned char* out,
                                size_t out_cap, size_t* out_len) {
    if (!out || !out_len || payload_len > DISCORD_RPC_MAX_PAYLOAD ||
        out_cap < DISCORD_RPC_FRAME_HEADER + payload_len ||
        (payload_len > 0u && !payload)) {
        return 0;
    }
    discord_write_u32_le(out, opcode);
    discord_write_u32_le(out + 4u, (uint32_t)payload_len);
    if (payload_len > 0u) memcpy(out + DISCORD_RPC_FRAME_HEADER, payload, payload_len);
    *out_len = DISCORD_RPC_FRAME_HEADER + payload_len;
    return 1;
}

static void discord_reset_io_state(void) {
    g_discord_rpc.read_active = 0;
    g_discord_rpc.write_active = 0;
    g_discord_rpc.read_header_used = 0u;
    g_discord_rpc.read_payload_len = 0u;
    g_discord_rpc.read_payload_used = 0u;
    g_discord_rpc.reading_payload = 0;
    g_discord_rpc.write_frame_len = 0u;
    g_discord_rpc.pending_frame_len = 0u;
    memset(&g_discord_rpc.read_overlapped, 0, sizeof(g_discord_rpc.read_overlapped));
    memset(&g_discord_rpc.write_overlapped, 0, sizeof(g_discord_rpc.write_overlapped));
    g_discord_rpc.read_overlapped.hEvent = g_discord_rpc.read_event;
    g_discord_rpc.write_overlapped.hEvent = g_discord_rpc.write_event;
}

static void discord_close_pipe(int retry, int log_disconnect) {
    if (g_discord_rpc.pipe && g_discord_rpc.pipe != INVALID_HANDLE_VALUE) {
        CancelIoEx(g_discord_rpc.pipe, NULL);
        CloseHandle(g_discord_rpc.pipe);
        if (log_disconnect) {
            LOG_INFO("[discord] desktop IPC disconnected");
        }
    }
    g_discord_rpc.pipe = INVALID_HANDLE_VALUE;
    g_discord_rpc.ready = 0;
    discord_reset_io_state();
    if (g_discord_rpc.read_event) ResetEvent(g_discord_rpc.read_event);
    if (g_discord_rpc.write_event) ResetEvent(g_discord_rpc.write_event);
    if (retry && g_discord_rpc.enabled && g_discord_rpc.available) {
        g_discord_rpc.reconnect_at_ms =
            GetTickCount64() + (ULONGLONG)DISCORD_RPC_RECONNECT_MS;
    } else {
        g_discord_rpc.reconnect_at_ms = 0u;
    }
}

static int discord_queue_frame(uint32_t opcode, const void* payload,
                               size_t payload_len) {
    size_t encoded_len = 0u;
    if (!discord_encode_frame(opcode, payload, payload_len,
                              g_discord_rpc.pending_frame,
                              sizeof(g_discord_rpc.pending_frame),
                              &encoded_len)) {
        return 0;
    }
    g_discord_rpc.pending_frame_len = encoded_len;
    return 1;
}

static int discord_start_write(void) {
    DWORD wrote = 0u;
    BOOL result;
    if (g_discord_rpc.write_active || g_discord_rpc.write_frame_len > 0u ||
        g_discord_rpc.pending_frame_len == 0u ||
        g_discord_rpc.pipe == INVALID_HANDLE_VALUE) {
        return 1;
    }
    memcpy(g_discord_rpc.write_frame, g_discord_rpc.pending_frame,
           g_discord_rpc.pending_frame_len);
    g_discord_rpc.write_frame_len = g_discord_rpc.pending_frame_len;
    g_discord_rpc.pending_frame_len = 0u;
    ResetEvent(g_discord_rpc.write_event);
    result = WriteFile(g_discord_rpc.pipe, g_discord_rpc.write_frame,
                       (DWORD)g_discord_rpc.write_frame_len, &wrote,
                       &g_discord_rpc.write_overlapped);
    if (result) {
        if ((size_t)wrote != g_discord_rpc.write_frame_len) return 0;
        g_discord_rpc.write_frame_len = 0u;
        return 1;
    }
    if (GetLastError() == ERROR_IO_PENDING) {
        g_discord_rpc.write_active = 1;
        return 1;
    }
    return 0;
}

static int discord_pump_write(void) {
    int loops;
    for (loops = 0; loops < 4; loops++) {
        if (g_discord_rpc.write_active) {
            DWORD wrote = 0u;
            if (!GetOverlappedResult(g_discord_rpc.pipe,
                                     &g_discord_rpc.write_overlapped,
                                     &wrote, FALSE)) {
                if (GetLastError() == ERROR_IO_INCOMPLETE) return 1;
                return 0;
            }
            g_discord_rpc.write_active = 0;
            if ((size_t)wrote != g_discord_rpc.write_frame_len) return 0;
            g_discord_rpc.write_frame_len = 0u;
        }
        if (g_discord_rpc.pending_frame_len == 0u) return 1;
        if (!discord_start_write()) return 0;
        if (g_discord_rpc.write_active) return 1;
    }
    return 1;
}

static int discord_ready_event(const char* json) {
    const char* event_key;
    const char* cursor;
    if (!json) return 0;
    event_key = strstr(json, "\"evt\"");
    if (!event_key) return 0;
    cursor = event_key + 5;
    while (*cursor && isspace((unsigned char)*cursor)) cursor++;
    if (*cursor++ != ':') return 0;
    while (*cursor && isspace((unsigned char)*cursor)) cursor++;
    return strncmp(cursor, "\"READY\"", 7u) == 0;
}

static int discord_process_frame(uint32_t opcode, const unsigned char* payload,
                                 size_t payload_len) {
    if (opcode == DISCORD_RPC_OPCODE_CLOSE) return 0;
    if (opcode == DISCORD_RPC_OPCODE_PING) {
        return discord_queue_frame(DISCORD_RPC_OPCODE_PONG, payload, payload_len);
    }
    if (opcode == DISCORD_RPC_OPCODE_FRAME) {
        g_discord_rpc.read_payload[payload_len] = '\0';
        if (discord_ready_event((const char*)g_discord_rpc.read_payload)) {
            if (!g_discord_rpc.ready) {
                LOG_INFO("[discord] Rich Presence connected");
            }
            g_discord_rpc.ready = 1;
            g_discord_rpc.activity_dirty = 1;
        }
    }
    return 1;
}

static int discord_issue_read(void) {
    unsigned char* target;
    size_t remaining;
    DWORD read = 0u;
    BOOL result;
    if (g_discord_rpc.read_active ||
        g_discord_rpc.pipe == INVALID_HANDLE_VALUE) {
        return 1;
    }
    if (g_discord_rpc.reading_payload) {
        target = g_discord_rpc.read_payload + g_discord_rpc.read_payload_used;
        remaining = g_discord_rpc.read_payload_len - g_discord_rpc.read_payload_used;
    } else {
        target = g_discord_rpc.read_header + g_discord_rpc.read_header_used;
        remaining = DISCORD_RPC_FRAME_HEADER - g_discord_rpc.read_header_used;
    }
    if (remaining == 0u) return 1;
    ResetEvent(g_discord_rpc.read_event);
    result = ReadFile(g_discord_rpc.pipe, target, (DWORD)remaining, &read,
                      &g_discord_rpc.read_overlapped);
    if (result) {
        if (read == 0u) return 0;
        if (g_discord_rpc.reading_payload) {
            g_discord_rpc.read_payload_used += (size_t)read;
        } else {
            g_discord_rpc.read_header_used += (size_t)read;
        }
        return 1;
    }
    if (GetLastError() == ERROR_IO_PENDING) {
        g_discord_rpc.read_active = 1;
        return 1;
    }
    return 0;
}

static int discord_pump_read(void) {
    int loops;
    for (loops = 0; loops < 8; loops++) {
        if (g_discord_rpc.read_active) {
            DWORD read = 0u;
            if (!GetOverlappedResult(g_discord_rpc.pipe,
                                     &g_discord_rpc.read_overlapped,
                                     &read, FALSE)) {
                if (GetLastError() == ERROR_IO_INCOMPLETE) return 1;
                return 0;
            }
            g_discord_rpc.read_active = 0;
            if (read == 0u) return 0;
            if (g_discord_rpc.reading_payload) {
                g_discord_rpc.read_payload_used += (size_t)read;
            } else {
                g_discord_rpc.read_header_used += (size_t)read;
            }
        }

        if (!g_discord_rpc.reading_payload &&
            g_discord_rpc.read_header_used == DISCORD_RPC_FRAME_HEADER) {
            uint32_t payload_len = discord_read_u32_le(g_discord_rpc.read_header + 4u);
            if (payload_len > DISCORD_RPC_MAX_PAYLOAD) return 0;
            g_discord_rpc.read_payload_len = (size_t)payload_len;
            g_discord_rpc.read_payload_used = 0u;
            g_discord_rpc.reading_payload = 1;
        }
        if (g_discord_rpc.reading_payload &&
            g_discord_rpc.read_payload_used == g_discord_rpc.read_payload_len) {
            uint32_t opcode = discord_read_u32_le(g_discord_rpc.read_header);
            if (!discord_process_frame(opcode, g_discord_rpc.read_payload,
                                       g_discord_rpc.read_payload_len)) {
                return 0;
            }
            g_discord_rpc.read_header_used = 0u;
            g_discord_rpc.read_payload_len = 0u;
            g_discord_rpc.read_payload_used = 0u;
            g_discord_rpc.reading_payload = 0;
        }
        if (!discord_issue_read()) return 0;
        if (g_discord_rpc.read_active) return 1;
    }
    return 1;
}

static int discord_activity_strings(DiscordRpcActivity activity,
                                    const char** details, const char** state) {
    if (!details || !state) return 0;
    switch (activity) {
        case DISCORD_RPC_ACTIVITY_MENUS:
            *details = "In Menus"; *state = "Available"; return 1;
        case DISCORD_RPC_ACTIVITY_LOCAL_MATCH:
            *details = "Local Match"; *state = "Playing Locally"; return 1;
        case DISCORD_RPC_ACTIVITY_ONLINE_HUB:
            *details = "Online Hub"; *state = "Available"; return 1;
        case DISCORD_RPC_ACTIVITY_SIGNING_IN:
            *details = "Online Hub"; *state = "Signing In"; return 1;
        case DISCORD_RPC_ACTIVITY_SOCIAL:
            *details = "Online Hub"; *state = "Friends & Challenges"; return 1;
        case DISCORD_RPC_ACTIVITY_QUEUE_CASUAL:
            *details = "Looking for a Match"; *state = "Casual Queue"; return 1;
        case DISCORD_RPC_ACTIVITY_QUEUE_COMPETITIVE:
            *details = "Looking for a Match"; *state = "Competitive Queue"; return 1;
        case DISCORD_RPC_ACTIVITY_MATCH_SETUP:
            *details = "Online Match"; *state = "Connecting"; return 1;
        case DISCORD_RPC_ACTIVITY_MATCH_CASUAL:
            *details = "Online Match"; *state = "Casual"; return 1;
        case DISCORD_RPC_ACTIVITY_MATCH_COMPETITIVE:
            *details = "Online Match"; *state = "Competitive"; return 1;
        case DISCORD_RPC_ACTIVITY_MATCH_PRIVATE:
            *details = "Online Match"; *state = "Friend Match"; return 1;
        default:
            *details = NULL; *state = NULL; return 0;
    }
}

static int discord_build_activity_json(DiscordRpcActivity activity,
                                       uint32_t process_id,
                                       uint64_t start_seconds,
                                       uint32_t nonce,
                                       char* out, size_t out_cap) {
    const char* details = NULL;
    const char* state = NULL;
    int written;
    if (!out || out_cap == 0u) return 0;
    if (activity == DISCORD_RPC_ACTIVITY_NONE) {
        written = snprintf(
            out, out_cap,
            "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%lu,\"activity\":null},"
            "\"nonce\":\"%lu\"}",
            (unsigned long)process_id, (unsigned long)nonce);
    } else {
        if (!discord_activity_strings(activity, &details, &state)) return 0;
        written = snprintf(
            out, out_cap,
            "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%lu,\"activity\":{"
            "\"details\":\"%s\",\"state\":\"%s\","
            "\"timestamps\":{\"start\":%llu},\"instance\":false}},"
            "\"nonce\":\"%lu\"}",
            (unsigned long)process_id, details, state,
            (unsigned long long)start_seconds, (unsigned long)nonce);
    }
    return written >= 0 && (size_t)written < out_cap;
}

static int discord_queue_activity(void) {
    char json[512];
    g_discord_rpc.nonce++;
    if (g_discord_rpc.nonce == 0u) g_discord_rpc.nonce = 1u;
    if (!discord_build_activity_json(g_discord_rpc.activity,
                                     (uint32_t)GetCurrentProcessId(),
                                     g_discord_rpc.activity_start_seconds,
                                     g_discord_rpc.nonce,
                                     json, sizeof(json))) {
        return 0;
    }
    return discord_queue_frame(DISCORD_RPC_OPCODE_FRAME, json, strlen(json));
}

static void discord_connect(void) {
    int index;
    wchar_t pipe_name[64];
    char handshake[96];
    int handshake_len;
    for (index = 0; index < 10; index++) {
        HANDLE pipe;
        swprintf(pipe_name, sizeof(pipe_name) / sizeof(pipe_name[0]),
                 L"\\\\?\\pipe\\discord-ipc-%d", index);
        pipe = CreateFileW(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                           OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
        if (pipe != INVALID_HANDLE_VALUE) {
            g_discord_rpc.pipe = pipe;
            discord_reset_io_state();
            handshake_len = snprintf(
                handshake, sizeof(handshake),
                "{\"v\":1,\"client_id\":\"%s\"}",
                g_discord_rpc.application_id);
            if (handshake_len <= 0 || (size_t)handshake_len >= sizeof(handshake) ||
                !discord_queue_frame(DISCORD_RPC_OPCODE_HANDSHAKE,
                                     handshake, (size_t)handshake_len) ||
                !discord_start_write() || !discord_issue_read()) {
                discord_close_pipe(1, 0);
            }
            return;
        }
    }
    g_discord_rpc.reconnect_at_ms =
        GetTickCount64() + (ULONGLONG)DISCORD_RPC_RECONNECT_MS;
}

static void discord_boot(void) {
    if (g_discord_rpc.booted) return;
    memset(&g_discord_rpc, 0, sizeof(g_discord_rpc));
    g_discord_rpc.pipe = INVALID_HANDLE_VALUE;
    g_discord_rpc.read_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_discord_rpc.write_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    discord_load_config();
    g_discord_rpc.activity = DISCORD_RPC_ACTIVITY_NONE;
    g_discord_rpc.activity_start_seconds = (uint64_t)time(NULL);
    g_discord_rpc.booted = 1;
    discord_reset_io_state();
    if (!g_discord_rpc.available) {
        LOG_INFO("[discord] Rich Presence unavailable; configure a Discord application ID");
        g_discord_rpc.logged_unavailable = 1;
    } else if (!g_discord_rpc.read_event || !g_discord_rpc.write_event) {
        LOG_WARN("[discord] Rich Presence disabled: IPC event allocation failed");
        g_discord_rpc.available = 0;
    }
}

void discord_rpc_ext_pump(DiscordRpcActivity activity) {
    ULONGLONG now;
    discord_boot();
    if (activity <= DISCORD_RPC_ACTIVITY_NONE ||
        activity >= DISCORD_RPC_ACTIVITY_COUNT) {
        activity = DISCORD_RPC_ACTIVITY_MENUS;
    }
    if (activity != g_discord_rpc.activity) {
        g_discord_rpc.activity = activity;
        g_discord_rpc.activity_start_seconds = (uint64_t)time(NULL);
        g_discord_rpc.activity_dirty = 1;
    }
    if (!g_discord_rpc.enabled || !g_discord_rpc.available ||
        !g_discord_rpc.read_event || !g_discord_rpc.write_event) {
        return;
    }

    now = GetTickCount64();
    if (g_discord_rpc.pipe == INVALID_HANDLE_VALUE) {
        if (g_discord_rpc.reconnect_at_ms == 0u ||
            now >= g_discord_rpc.reconnect_at_ms) {
            discord_connect();
        }
        return;
    }
    if (!discord_pump_write() || !discord_pump_read()) {
        discord_close_pipe(1, g_discord_rpc.ready);
        return;
    }
    if (g_discord_rpc.ready && g_discord_rpc.activity_dirty &&
        g_discord_rpc.pending_frame_len == 0u &&
        !g_discord_rpc.write_active &&
        now >= g_discord_rpc.next_activity_ms) {
        if (!discord_queue_activity() || !discord_start_write()) {
            discord_close_pipe(1, 1);
            return;
        }
        g_discord_rpc.activity_dirty = 0;
        g_discord_rpc.next_activity_ms =
            now + (ULONGLONG)DISCORD_RPC_UPDATE_INTERVAL_MS;
    }
}

void discord_rpc_ext_shutdown(void) {
    if (!g_discord_rpc.booted) return;
    discord_close_pipe(0, 0);
    if (g_discord_rpc.read_event) CloseHandle(g_discord_rpc.read_event);
    if (g_discord_rpc.write_event) CloseHandle(g_discord_rpc.write_event);
    memset(&g_discord_rpc, 0, sizeof(g_discord_rpc));
    g_discord_rpc.pipe = INVALID_HANDLE_VALUE;
}

int discord_rpc_ext_enabled(void) {
    discord_boot();
    return g_discord_rpc.enabled;
}

int discord_rpc_ext_available(void) {
    discord_boot();
    return g_discord_rpc.available;
}

void discord_rpc_ext_set_enabled(int enabled) {
    discord_boot();
    g_discord_rpc.enabled = enabled ? 1 : 0;
    if (!g_discord_rpc.enabled) {
        discord_close_pipe(0, 0);
    } else if (g_discord_rpc.available) {
        g_discord_rpc.reconnect_at_ms = 0u;
        g_discord_rpc.activity_dirty = 1;
    }
}

int discord_rpc_ext_set_application_id(const char* application_id) {
    size_t length;
    discord_boot();
    if (!discord_validate_application_id(application_id)) return 0;
    if (strcmp(g_discord_rpc.application_id, application_id) == 0) return 1;

    /*
     * Switching applications must discard every frame prepared for the old
     * public ID before the next nonblocking handshake is attempted.
     */
    discord_close_pipe(0, 0);
    length = strlen(application_id);
    memcpy(g_discord_rpc.application_id, application_id, length + 1u);
    g_discord_rpc.available =
        g_discord_rpc.read_event != NULL && g_discord_rpc.write_event != NULL;
    g_discord_rpc.logged_unavailable = 0;
    g_discord_rpc.activity_dirty = 1;
    g_discord_rpc.activity_start_seconds = (uint64_t)time(NULL);
    g_discord_rpc.next_activity_ms = 0u;
    g_discord_rpc.reconnect_at_ms = 0u;
    return 1;
}

const char* discord_rpc_ext_application_id(void) {
    discord_boot();
    return g_discord_rpc.application_id;
}

const char* discord_rpc_ext_setting_label(void) {
    discord_boot();
    if (!g_discord_rpc.available) return "UNAVAILABLE";
    return g_discord_rpc.enabled ? "ON" : "OFF";
}

#ifdef DISCORD_RPC_EXT_TEST
int discord_rpc_ext_test_validate_application_id(const char* value) {
    return discord_validate_application_id(value);
}

int discord_rpc_ext_test_parse_config(const char* text, int* enabled,
                                      char* application_id,
                                      size_t application_id_cap) {
    char parsed_id[DISCORD_RPC_MAX_APPLICATION_ID + 1u];
    int parsed_enabled = 1;
    if (!enabled || !application_id || application_id_cap == 0u) return 0;
    memcpy(parsed_id, EGGNOGGPLUS_DISCORD_APPLICATION_ID,
           sizeof(EGGNOGGPLUS_DISCORD_APPLICATION_ID));
    if (!discord_parse_config_text(text, &parsed_enabled, parsed_id)) return 0;
    if (strlen(parsed_id) + 1u > application_id_cap) return 0;
    *enabled = parsed_enabled;
    memcpy(application_id, parsed_id, strlen(parsed_id) + 1u);
    return 1;
}

int discord_rpc_ext_test_build_activity(DiscordRpcActivity activity,
                                        uint32_t process_id,
                                        uint64_t start_seconds,
                                        uint32_t nonce,
                                        char* out, size_t out_cap) {
    return discord_build_activity_json(activity, process_id, start_seconds,
                                       nonce, out, out_cap);
}

int discord_rpc_ext_test_encode_frame(uint32_t opcode, const void* payload,
                                      size_t payload_len, unsigned char* out,
                                      size_t out_cap, size_t* out_len) {
    return discord_encode_frame(opcode, payload, payload_len,
                                out, out_cap, out_len);
}
#endif
