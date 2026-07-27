#include "../launch_request.h"

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

static LaunchRequestParseResult parse(int argc,
                                      const char* const* argv,
                                      LaunchRequest* out,
                                      char* error) {
    return launch_request_parse_args(argc, argv, out, error, 160u);
}

static void expect_action(int argc,
                          const char* const* argv,
                          LaunchRequestAction action,
                          const char* target) {
    LaunchRequest request;
    char error[160];
    CHECK(parse(argc, argv, &request, error) == LAUNCH_REQUEST_PARSE_OK);
    CHECK(error[0] == '\0');
    CHECK(request.action == action);
    CHECK(strcmp(request.target, target ? target : "") == 0);
}

static void expect_error(int argc, const char* const* argv) {
    LaunchRequest request;
    char error[160];
    CHECK(parse(argc, argv, &request, error) == LAUNCH_REQUEST_PARSE_ERROR);
    CHECK(request.action == LAUNCH_REQUEST_NONE);
    CHECK(error[0] != '\0');
}

int main(void) {
    const char* none[] = {"game.exe", "-windowed", "ordinary.map"};
    const char* hub[] = {"game.exe", "--ONLINE"};
    const char* hub_uri[] = {"game.exe", "YuLe://hub"};
    const char* bare_uri[] = {"game.exe", "yule://"};
    const char* requests[] = {"game.exe", "yule://requests"};
    const char* casual[] = {"game.exe", "--queue=CASUAL"};
    const char* competitive[] = {"game.exe", "yule://queue/competitive"};
    const char* challenge[] = {"game.exe", "--challenge=player_1"};
    const char* challenge_uri[] = {"game.exe", "yule://challenge/player_1"};
    const char* duplicate[] = {
        "game.exe", "--queue=casual", "yule://queue/casual"
    };
    const char* enveloped[] = {
        "game.exe", "--yule-uri=yule://queue/competitive"
    };
    const char* injected_envelope[] = {
        "game.exe", "--yule-uri=yule://hub", "--queue=competitive"
    };
    const char* conflict[] = {
        "game.exe", "--queue=casual", "--queue=competitive"
    };
    const char* bad_queue[] = {"game.exe", "--queue=ranked"};
    const char* missing_queue[] = {"game.exe", "--queue"};
    const char* bad_name[] = {"game.exe", "yule://challenge/Player-One"};
    const char* long_name[] = {
        "game.exe", "--challenge=abcdefghijklmnopqrstuvwxy"
    };
    const char* query[] = {"game.exe", "yule://queue/casual?token=secret"};
    const char* fragment[] = {"game.exe", "yule://hub#fragment"};
    const char* encoded[] = {"game.exe", "yule://challenge/a%2fb"};
    const char* endpoint[] = {"game.exe", "yule://join/127.0.0.1:27015"};
    const char* credentials[] = {"game.exe", "yule://user:pass@hub"};
    const char* bad_scheme[] = {"game.exe", "yule:/hub"};
    const char* bad_switch[] = {"game.exe", "--online=queue"};
    const char* bad_envelope[] = {"game.exe", "--yule-uri"};
    LaunchRequest request;
    char error[160];

    CHECK(parse(3, none, &request, error) == LAUNCH_REQUEST_PARSE_NONE);
    CHECK(request.action == LAUNCH_REQUEST_NONE);
    expect_action(2, hub, LAUNCH_REQUEST_HUB, "");
    expect_action(2, hub_uri, LAUNCH_REQUEST_HUB, "");
    expect_action(2, bare_uri, LAUNCH_REQUEST_HUB, "");
    expect_action(2, requests, LAUNCH_REQUEST_REQUESTS, "");
    expect_action(2, casual, LAUNCH_REQUEST_QUEUE_CASUAL, "");
    expect_action(2, competitive, LAUNCH_REQUEST_QUEUE_COMPETITIVE, "");
    expect_action(2, challenge, LAUNCH_REQUEST_CHALLENGE, "player_1");
    expect_action(2, challenge_uri, LAUNCH_REQUEST_CHALLENGE, "player_1");
    expect_action(3, duplicate, LAUNCH_REQUEST_QUEUE_CASUAL, "");
    expect_action(2, enveloped, LAUNCH_REQUEST_QUEUE_COMPETITIVE, "");

    expect_error(3, conflict);
    expect_error(2, bad_queue);
    expect_error(2, missing_queue);
    expect_error(2, bad_name);
    expect_error(2, long_name);
    expect_error(2, query);
    expect_error(2, fragment);
    expect_error(2, encoded);
    expect_error(2, endpoint);
    expect_error(2, credentials);
    expect_error(2, bad_scheme);
    expect_error(2, bad_switch);
    expect_error(2, bad_envelope);
    expect_error(3, injected_envelope);

    CHECK(strcmp(launch_request_action_name(LAUNCH_REQUEST_CHALLENGE),
                 "challenge") == 0);
    CHECK(strcmp(launch_request_action_name((LaunchRequestAction)99),
                 "none") == 0);

    if (g_failures != 0) {
        fprintf(stderr, "%d launch request test(s) failed\n", g_failures);
        return 1;
    }
    puts("launch_request_test: all checks passed");
    return 0;
}
