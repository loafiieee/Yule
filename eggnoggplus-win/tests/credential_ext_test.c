#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

#include "../credential_ext.h"

static int g_failures = 0;

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); \
        g_failures++; \
    } \
} while (0)

static void test_targets(void) {
    static const char expected[] =
        "EggnoggPlus/YuleOnline/v1/server/example.com/port/17421/user/Alice%20Smith%2BQA";
    char target[CREDENTIAL_EXT_TARGET_MAX];
    char target_case[CREDENTIAL_EXT_TARGET_MAX];
    char target_collision[CREDENTIAL_EXT_TARGET_MAX];
    char error[128];
    size_t length = 0;
    CredentialExtResult result;

    result = credential_ext_build_target("Example.COM",
                                         17421,
                                         "Alice Smith+QA",
                                         target,
                                         sizeof(target),
                                         &length,
                                         error,
                                         sizeof(error));
    CHECK(result == CREDENTIAL_EXT_OK);
    CHECK(strcmp(target, expected) == 0);
    CHECK(length == strlen(expected));
    CHECK(error[0] == '\0');

    result = credential_ext_build_target("EXAMPLE.com",
                                         17421,
                                         "Alice Smith+QA",
                                         target_case,
                                         sizeof(target_case),
                                         NULL,
                                         error,
                                         sizeof(error));
    CHECK(result == CREDENTIAL_EXT_OK);
    CHECK(strcmp(target, target_case) == 0);

    result = credential_ext_build_target("example.com",
                                         17421,
                                         "alice Smith+QA",
                                         target_case,
                                         sizeof(target_case),
                                         NULL,
                                         error,
                                         sizeof(error));
    CHECK(result == CREDENTIAL_EXT_OK);
    CHECK(strcmp(target, target_case) != 0);

    CHECK(credential_ext_build_target("host/name", 17421, "user%2Fname",
                                      target_case, sizeof(target_case), NULL,
                                      error, sizeof(error)) == CREDENTIAL_EXT_OK);
    CHECK(credential_ext_build_target("host%2Fname", 17421, "user/name",
                                      target_collision, sizeof(target_collision), NULL,
                                      error, sizeof(error)) == CREDENTIAL_EXT_OK);
    CHECK(strcmp(target_case, target_collision) != 0);

    memset(target_case, 'x', sizeof(target_case));
    length = 0;
    result = credential_ext_build_target("Example.COM",
                                         17421,
                                         "Alice Smith+QA",
                                         target_case,
                                         8,
                                         &length,
                                         error,
                                         sizeof(error));
    CHECK(result == CREDENTIAL_EXT_BUFFER_TOO_SMALL);
    CHECK(target_case[0] == '\0');
    CHECK(length == strlen(expected));
    CHECK(strstr(error, "needs") != NULL);
}

static void test_validation(void) {
    char target[CREDENTIAL_EXT_TARGET_MAX];
    char error[64];
    char too_long_server[CREDENTIAL_EXT_SERVER_MAX + 2u];
    char too_long_username[CREDENTIAL_EXT_USERNAME_MAX + 2u];
    char too_long_password[CREDENTIAL_EXT_PASSWORD_MAX + 2u];
    char max_server[CREDENTIAL_EXT_SERVER_MAX + 1u];
    char max_username[CREDENTIAL_EXT_USERNAME_MAX + 1u];
    char stale_password[16];
    size_t password_len = 99;
    size_t i;
    CredentialExtResult result;

    memset(too_long_server, 's', sizeof(too_long_server));
    too_long_server[sizeof(too_long_server) - 1u] = '\0';
    memset(too_long_username, 'u', sizeof(too_long_username));
    too_long_username[sizeof(too_long_username) - 1u] = '\0';
    memset(too_long_password, 'p', sizeof(too_long_password));
    too_long_password[sizeof(too_long_password) - 1u] = '\0';
    memset(max_server, '+', sizeof(max_server));
    max_server[sizeof(max_server) - 1u] = '\0';
    memset(max_username, '+', sizeof(max_username));
    max_username[sizeof(max_username) - 1u] = '\0';

    CHECK(credential_ext_build_target(max_server, 65535u, max_username,
                                      target, sizeof(target), NULL,
                                      error, sizeof(error)) == CREDENTIAL_EXT_OK);
    CHECK(strlen(target) < sizeof(target));

    CHECK(credential_ext_build_target(NULL, 1, "user", target, sizeof(target), NULL,
                                      error, sizeof(error)) == CREDENTIAL_EXT_INVALID_ARGUMENT);
    CHECK(credential_ext_build_target("", 1, "user", target, sizeof(target), NULL,
                                      error, sizeof(error)) == CREDENTIAL_EXT_INVALID_ARGUMENT);
    CHECK(credential_ext_build_target("server", 0, "user", target, sizeof(target), NULL,
                                      error, sizeof(error)) == CREDENTIAL_EXT_INVALID_ARGUMENT);
    CHECK(credential_ext_build_target("server", 65536u, "user", target, sizeof(target), NULL,
                                      error, sizeof(error)) == CREDENTIAL_EXT_INVALID_ARGUMENT);
    CHECK(credential_ext_build_target("bad server", 1, "user", target, sizeof(target), NULL,
                                      error, sizeof(error)) == CREDENTIAL_EXT_INVALID_ARGUMENT);
    CHECK(credential_ext_build_target("server", 1, NULL, target, sizeof(target), NULL,
                                      error, sizeof(error)) == CREDENTIAL_EXT_INVALID_ARGUMENT);
    CHECK(credential_ext_build_target("server", 1, " user", target, sizeof(target), NULL,
                                      error, sizeof(error)) == CREDENTIAL_EXT_INVALID_ARGUMENT);
    CHECK(credential_ext_build_target("server", 1, "user\n", target, sizeof(target), NULL,
                                      error, sizeof(error)) == CREDENTIAL_EXT_INVALID_ARGUMENT);
    CHECK(credential_ext_build_target(too_long_server, 1, "user", target, sizeof(target), NULL,
                                      error, sizeof(error)) == CREDENTIAL_EXT_INVALID_ARGUMENT);
    CHECK(credential_ext_build_target("server", 1, too_long_username, target, sizeof(target), NULL,
                                      error, sizeof(error)) == CREDENTIAL_EXT_INVALID_ARGUMENT);
    CHECK(credential_ext_build_target("server", 1, "user", NULL, 0, NULL,
                                      error, sizeof(error)) == CREDENTIAL_EXT_INVALID_ARGUMENT);

    result = credential_ext_write_password("server", 1, "user", too_long_password,
                                           error, sizeof(error));
    CHECK(result == CREDENTIAL_EXT_SECRET_TOO_LARGE);
    CHECK(strlen(error) < sizeof(error));
    CHECK(strcmp(credential_ext_result_name(result), "secret_too_large") == 0);
    CHECK(strcmp(credential_ext_result_name((CredentialExtResult)999), "unknown") == 0);

    memset(stale_password, 'x', sizeof(stale_password));
    result = credential_ext_read_password("", 1, "user",
                                          stale_password, sizeof(stale_password),
                                          &password_len, error, sizeof(error));
    CHECK(result == CREDENTIAL_EXT_INVALID_ARGUMENT);
    CHECK(password_len == 0);
    for (i = 0; i < sizeof(stale_password); ++i) CHECK(stale_password[i] == '\0');
    CHECK(credential_ext_delete_password("", 1, "user", error, sizeof(error)) ==
          CREDENTIAL_EXT_INVALID_ARGUMENT);
}

static void test_secure_zero(void) {
    unsigned char bytes[32];
    size_t i;
    memset(bytes, 0xa5, sizeof(bytes));
    credential_ext_secure_zero(bytes, sizeof(bytes));
    for (i = 0; i < sizeof(bytes); ++i) CHECK(bytes[i] == 0);
    credential_ext_secure_zero(NULL, 0);
}

static void test_live_roundtrip_if_requested(void) {
    const char* enabled = getenv("EGGNOGGPLUS_CREDENTIAL_TEST_LIVE");
    char server[96];
    char username[96];
    char password[96];
    char error[256];
    size_t password_len = 0;
    DWORD process_id;
    DWORD tick;
    CredentialExtResult result;

    if (!enabled || strcmp(enabled, "1") != 0) {
        printf("Credential Manager live roundtrip skipped (set "
               "EGGNOGGPLUS_CREDENTIAL_TEST_LIVE=1 to enable).\n");
        return;
    }

    process_id = GetCurrentProcessId();
    tick = GetTickCount();
    snprintf(server, sizeof(server), "credential-ext-test-%lu.invalid", (unsigned long)process_id);
    snprintf(username, sizeof(username), "test-%lu-%lu",
             (unsigned long)process_id, (unsigned long)tick);

    /* The identity is unique. A best-effort pre-delete and unconditional
     * cleanup keep an interrupted prior run from changing the assertion. */
    credential_ext_delete_password(server, 17421, username, error, sizeof(error));
    result = credential_ext_write_password(server, 17421, username,
                                           "roundtrip-secret", error, sizeof(error));
    CHECK(result == CREDENTIAL_EXT_OK);
    result = credential_ext_read_password(server, 17421, username,
                                          password, sizeof(password), &password_len,
                                          error, sizeof(error));
    CHECK(result == CREDENTIAL_EXT_OK);
    CHECK(password_len == strlen("roundtrip-secret"));
    CHECK(strcmp(password, "roundtrip-secret") == 0);
    credential_ext_secure_zero(password, sizeof(password));

    result = credential_ext_delete_password(server, 17421, username, error, sizeof(error));
    CHECK(result == CREDENTIAL_EXT_OK);
    result = credential_ext_read_password(server, 17421, username,
                                          password, sizeof(password), &password_len,
                                          error, sizeof(error));
    CHECK(result == CREDENTIAL_EXT_NOT_FOUND);
    credential_ext_delete_password(server, 17421, username, error, sizeof(error));
    credential_ext_secure_zero(password, sizeof(password));
}

int main(void) {
    test_targets();
    test_validation();
    test_secure_zero();
    test_live_roundtrip_if_requested();

    if (g_failures != 0) {
        fprintf(stderr, "%d credential extension test(s) failed\n", g_failures);
        return 1;
    }
    printf("credential_ext tests passed\n");
    return 0;
}
