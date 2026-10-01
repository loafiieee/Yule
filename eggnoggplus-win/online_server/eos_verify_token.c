/* Dedicated one-shot EOS Connect ID token verifier for online_server.
 * Build on the server host against the matching EOS C SDK. The JWT arrives on
 * stdin, never argv or stdout. Successful stdout is: <account_id> <puid>\n.
 * The caller still compares account_id with its authenticated Yule account. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "eos_sdk.h"
#include "eos_connect.h"

#define TOKEN_CAP 16384

static long long monotonic_ms(void) {
#ifdef _WIN32
    return (long long)GetTickCount64();
#else
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
#endif
}

static void wait_for_tick(void) {
#ifdef _WIN32
    Sleep(10);
#else
    const struct timespec nap = {0, 10000000L};
    nanosleep(&nap, NULL);
#endif
}

static struct {
    int done;
    int accepted;
    char account_id[33];
    char puid[EOS_PRODUCTUSERID_MAX_LENGTH + 1];
} verification;

static const char* required_env(const char* name) {
    const char* value = getenv(name);
    if (!value || !value[0]) {
        fprintf(stderr, "EOS verifier configuration is incomplete\n");
        return NULL;
    }
    return value;
}

static int hex32(const char* value) {
    size_t i;
    if (!value || strlen(value) != 32u) return 0;
    for (i = 0; i < 32u; i++) {
        if (!((value[i] >= '0' && value[i] <= '9') ||
              (value[i] >= 'a' && value[i] <= 'f'))) return 0;
    }
    return 1;
}

static void EOS_CALL verified(const EOS_Connect_VerifyIdTokenCallbackInfo* info) {
    int32_t length = (int32_t)sizeof(verification.puid);
    verification.done = 1;
    if (!info || info->ResultCode != EOS_Success ||
        info->bIsAccountInfoPresent != EOS_TRUE ||
        info->AccountIdType != EOS_EAT_OPENID ||
        !hex32(info->AccountId) ||
        !info->ClientId || !info->ProductId || !info->SandboxId ||
        !info->DeploymentId ||
        strcmp(info->ClientId, getenv("EOS_CLIENT_ID")) != 0 ||
        strcmp(info->ProductId, getenv("EOS_PRODUCT_ID")) != 0 ||
        strcmp(info->SandboxId, getenv("EOS_SANDBOX_ID")) != 0 ||
        strcmp(info->DeploymentId, getenv("EOS_DEPLOYMENT_ID")) != 0 ||
        !info->ProductUserId ||
        EOS_ProductUserId_ToString(info->ProductUserId, verification.puid,
                                   &length) != EOS_Success ||
        !hex32(verification.puid)) return;
    memcpy(verification.account_id, info->AccountId, 33u);
    verification.accepted = 1;
}

int main(int argc, char** argv) {
    const char* product_id = required_env("EOS_PRODUCT_ID");
    const char* sandbox_id = required_env("EOS_SANDBOX_ID");
    const char* deployment_id = required_env("EOS_DEPLOYMENT_ID");
    const char* client_id = required_env("EOS_CLIENT_ID");
    const char* client_secret = required_env("EOS_CLIENT_SECRET");
    EOS_InitializeOptions init = {0};
    EOS_Platform_Options platform_options = {0};
    EOS_Platform_Options* platform_ptr = &platform_options;
    EOS_HPlatform platform = NULL;
    EOS_HConnect connect = NULL;
    EOS_Connect_IdToken id_token = {0};
    EOS_Connect_VerifyIdTokenOptions verify = {0};
    EOS_ProductUserId claimed = NULL;
    char jwt[TOKEN_CAP];
    size_t length;
    long long deadline;
    int code = 1;
    if (argc != 2 || !hex32(argv[1]) || !product_id || !sandbox_id ||
        !deployment_id || !client_id || !client_secret) goto done;
    if (!fgets(jwt, sizeof(jwt), stdin)) goto done;
    length = strlen(jwt);
    if (length == 0 || jwt[length - 1] != '\n') goto done;
    jwt[--length] = '\0';
    if (length < 64 || length > TOKEN_CAP - 2) goto done;
    init.ApiVersion = EOS_INITIALIZE_API_LATEST;
    init.ProductName = "Yule server";
    init.ProductVersion = "18";
    if (EOS_Initialize(&init) != EOS_Success) goto done;
    platform_ptr->ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
    platform_ptr->ProductId = product_id;
    platform_ptr->SandboxId = sandbox_id;
    platform_ptr->DeploymentId = deployment_id;
    platform_ptr->ClientCredentials.ClientId = client_id;
    platform_ptr->ClientCredentials.ClientSecret = client_secret;
    platform_ptr->bIsServer = EOS_TRUE;
    platform_ptr->Flags = EOS_PF_DISABLE_OVERLAY;
    platform_ptr->TickBudgetInMilliseconds = 2;
    platform = EOS_Platform_Create(platform_ptr);
    if (!platform) goto shutdown;
    connect = EOS_Platform_GetConnectInterface(platform);
    claimed = EOS_ProductUserId_FromString(argv[1]);
    if (!connect || !claimed || EOS_ProductUserId_IsValid(claimed) != EOS_TRUE) goto shutdown;
    id_token.ApiVersion = EOS_CONNECT_IDTOKEN_API_LATEST;
    id_token.ProductUserId = claimed;
    id_token.JsonWebToken = jwt;
    verify.ApiVersion = EOS_CONNECT_VERIFYIDTOKEN_API_LATEST;
    verify.IdToken = &id_token;
    EOS_Connect_VerifyIdToken(connect, &verify, NULL, verified);
    deadline = monotonic_ms() + 15000;
    while (!verification.done) {
        EOS_Platform_Tick(platform);
        if (monotonic_ms() >= deadline) break;
        wait_for_tick();
    }
    if (verification.accepted && strcmp(verification.puid, argv[1]) == 0) {
        printf("%s %s\n", verification.account_id, verification.puid);
        code = 0;
    }
shutdown:
    if (platform) EOS_Platform_Release(platform);
    (void)EOS_Shutdown();
done:
    memset(jwt, 0, sizeof(jwt));
    if (code != 0) fprintf(stderr, "EOS Connect token verification failed\n");
    return code;
}
