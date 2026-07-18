#include "../online_control.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: check failed: %s\n",                      \
                    __FILE__, __LINE__, #condition);                            \
            g_failures++;                                                       \
        }                                                                       \
    } while (0)

static void check_invalid(const char* json) {
    char error[192];
    error[0] = '\0';
    CHECK(!online_control_json_validate(json, error, sizeof(error)));
    CHECK(error[0] != '\0');
}

static void test_valid_object_and_getters(void) {
    static const char json[] =
        "{\"type\":\"auth_ok\",\"username\":\"player_1\",\"elo\":1000,"
        "\"control_protocol\":2,\"match_protocol\":2,\"p2p_protocol\":16,"
        "\"cap_p2p_auth\":1,"
        "\"ok\":true,\"disabled\":false,\"none\":null}";
    char text[32];
    int value = -1;

    CHECK(online_control_json_validate(json, NULL, 0));
    CHECK(online_control_json_get_string(json, "type", text, sizeof(text)) ==
          ONLINE_CONTROL_JSON_OK);
    CHECK(strcmp(text, "auth_ok") == 0);
    CHECK(online_control_json_get_int(json, "elo", &value) ==
          ONLINE_CONTROL_JSON_OK);
    CHECK(value == 1000);
    CHECK(online_control_json_get_int(json, "control_protocol", &value) ==
          ONLINE_CONTROL_JSON_OK);
    CHECK(value == 2);
    CHECK(online_control_json_get_int(json, "match_protocol", &value) ==
          ONLINE_CONTROL_JSON_OK);
    CHECK(value == 2);
    CHECK(online_control_json_get_int(json, "p2p_protocol", &value) ==
          ONLINE_CONTROL_JSON_OK);
    CHECK(value == 16);
    CHECK(online_control_json_get_int(json, "cap_p2p_auth", &value) ==
          ONLINE_CONTROL_JSON_OK);
    CHECK(value == 1);
    CHECK(online_control_json_get_string(json, "missing", text, sizeof(text)) ==
          ONLINE_CONTROL_JSON_NOT_FOUND);
    CHECK(text[0] == '\0');
    CHECK(online_control_json_get_int(json, "username", &value) ==
          ONLINE_CONTROL_JSON_TYPE_MISMATCH);
    CHECK(online_control_json_get_string(json, "elo", text, sizeof(text)) ==
          ONLINE_CONTROL_JSON_TYPE_MISMATCH);
    CHECK(text[0] == '\0');
}

static void test_strings_and_capacity(void) {
    static const char escaped[] =
        "{\"value\":\"quote:\\\" slash:\\\\ solid:\\/ snow:\\u2603 "
        "face:\\uD83D\\uDE00\"}";
    static const char expected[] =
        "quote:\" slash:\\ solid:/ snow:\xe2\x98\x83 face:\xf0\x9f\x98\x80";
    static const char raw_utf8[] = "{\"value\":\"\xe2\x98\x83\"}";
    char out[128];
    char exact[5];
    char short_out[4] = "xxx";

    CHECK(online_control_json_get_string(escaped, "value", out, sizeof(out)) ==
          ONLINE_CONTROL_JSON_OK);
    CHECK(strcmp(out, expected) == 0);
    CHECK(online_control_json_get_string(raw_utf8, "value", out, sizeof(out)) ==
          ONLINE_CONTROL_JSON_OK);
    CHECK(strcmp(out, "\xe2\x98\x83") == 0);

    CHECK(online_control_json_get_string("{\"v\":\"abcd\"}", "v",
                                         short_out, sizeof(short_out)) ==
          ONLINE_CONTROL_JSON_OUTPUT_TOO_SMALL);
    CHECK(short_out[0] == '\0');
    CHECK(online_control_json_get_string("{\"v\":\"abcd\"}", "v",
                                         exact, sizeof(exact)) ==
          ONLINE_CONTROL_JSON_OK);
    CHECK(strcmp(exact, "abcd") == 0);

    strcpy(out, "must clear");
    CHECK(online_control_json_get_string(NULL, "v", out, sizeof(out)) ==
          ONLINE_CONTROL_JSON_INVALID);
    CHECK(out[0] == '\0');
}

static void test_rejected_json(void) {
    static const char raw_control[] = "{\"v\":\"a\x01" "b\"}";
    static const char invalid_utf8[] = "{\"v\":\"\xc0\x80\"}";
    size_t i;
    static const char* const invalid[] = {
        "",
        "[]",
        "{\"type\":\"a\",\"type\":\"b\"}",
        "{\"type\":\"a\",\"ty\\u0070e\":\"b\"}",
        "{\"type\":\"a\",\"nested\":{\"type\":\"b\"}}",
        "{\"type\":\"a\",\"nested\":[1,2]}",
        "{\"type\":\"unterminated}",
        "{\"type\":\"a\",}",
        "{\"type\":\"a\"} trailing",
        "{\"type\":\"a\\n\"}",
        "{\"type\":\"\\u0000\"}",
        "{\"type\":\"\\uD800\"}",
        "{\"type\":\"\\uDC00\"}",
        "{\"type\":truee}",
        "{\"n\":01}",
        "{\"n\":1.}",
        "{\"n\":1e}",
        "{\"a\" 1}",
        raw_control,
        invalid_utf8
    };

    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        check_invalid(invalid[i]);
    }
}

static void test_limits(void) {
    char json[4096];
    char key_json[256];
    size_t pos = 0;
    int i;

    json[pos++] = '{';
    for (i = 0; i < 65; i++) {
        int written = snprintf(json + pos, sizeof(json) - pos,
                               "%s\"k%d\":0", i ? "," : "", i);
        CHECK(written > 0 && (size_t)written < sizeof(json) - pos);
        if (written <= 0 || (size_t)written >= sizeof(json) - pos) return;
        pos += (size_t)written;
    }
    json[pos++] = '}';
    json[pos] = '\0';
    check_invalid(json);

    memset(key_json, 0, sizeof(key_json));
    key_json[0] = '{';
    key_json[1] = '"';
    memset(key_json + 2, 'k', 64);
    memcpy(key_json + 66, "\":0}", 5);
    check_invalid(key_json);
}

static void test_integer_contract(void) {
    int value = 7;

    CHECK(online_control_json_get_int("{\"n\":2147483647}", "n", &value) ==
          ONLINE_CONTROL_JSON_OK);
    CHECK(value == 2147483647);
    CHECK(online_control_json_get_int("{\"n\":-2147483648}", "n", &value) ==
          ONLINE_CONTROL_JSON_OK);
    CHECK(value == (-2147483647 - 1));

    value = 7;
    CHECK(online_control_json_get_int("{\"n\":2147483648}", "n", &value) ==
          ONLINE_CONTROL_JSON_VALUE_INVALID);
    CHECK(value == 7);
    CHECK(online_control_json_get_int("{\"n\":-2147483649}", "n", &value) ==
          ONLINE_CONTROL_JSON_VALUE_INVALID);
    CHECK(online_control_json_get_int("{\"n\":1.0}", "n", &value) ==
          ONLINE_CONTROL_JSON_VALUE_INVALID);
    CHECK(online_control_json_get_int("{\"n\":1e2}", "n", &value) ==
          ONLINE_CONTROL_JSON_VALUE_INVALID);
}

static void test_canonical_usernames(void) {
    CHECK(online_control_username_is_canonical("player_1"));
    CHECK(online_control_username_is_canonical("a"));
    CHECK(online_control_username_is_canonical("abcdefghijklmnopqrstuvwx"));
    CHECK(!online_control_username_is_canonical(NULL));
    CHECK(!online_control_username_is_canonical(""));
    CHECK(!online_control_username_is_canonical("Player_1"));
    CHECK(!online_control_username_is_canonical("player-1"));
    CHECK(!online_control_username_is_canonical("abcdefghijklmnopqrstuvwxy"));
}

static void test_deadlines(void) {
    uint32_t deadline = online_control_deadline_after(100u, 50u);
    CHECK(deadline == 150u);
    CHECK(!online_control_deadline_reached(149u, deadline));
    CHECK(online_control_deadline_reached(150u, deadline));
    CHECK(online_control_deadline_reached(151u, deadline));
    CHECK(!online_control_deadline_reached(0xffffffffu, 0u));

    deadline = online_control_deadline_after(0xfffffff0u, 0x20u);
    CHECK(deadline == 0x10u);
    CHECK(!online_control_deadline_reached(0xffffffffu, deadline));
    CHECK(!online_control_deadline_reached(0x0fu, deadline));
    CHECK(online_control_deadline_reached(0x10u, deadline));

    deadline = online_control_deadline_after(0xffffffffu, 1u);
    CHECK(deadline == 1u);
    CHECK(!online_control_deadline_reached(0u, deadline));
    CHECK(online_control_deadline_reached(1u, deadline));
}

int main(void) {
    test_valid_object_and_getters();
    test_strings_and_capacity();
    test_rejected_json();
    test_limits();
    test_integer_contract();
    test_canonical_usernames();
    test_deadlines();
    if (g_failures) {
        fprintf(stderr, "%d online control test(s) failed\n", g_failures);
        return 1;
    }
    printf("online_control tests passed\n");
    return 0;
}
