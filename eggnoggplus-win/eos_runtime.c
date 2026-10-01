#include "eos_runtime.h"

#include <stdio.h>
#include <string.h>

#ifndef YULE_ENABLE_EOS

int yule_eos_initialize(const YuleEosConfig* config, char* err, size_t err_cap) {
    (void)config;
    if (err && err_cap) snprintf(err, err_cap, "EOS SDK is not included in this build");
    return 0;
}
void yule_eos_tick(void) {}
void yule_eos_set_carrier_service(int (*service)(int)) { (void)service; }
int yule_eos_connect_login(const char* token, char* err, size_t err_cap) {
    (void)token;
    if (err && err_cap) snprintf(err, err_cap, "EOS SDK is not included in this build");
    return 0;
}
YuleEosState yule_eos_state(void) { return YULE_EOS_UNAVAILABLE; }
const char* yule_eos_puid(void) { return ""; }
int yule_eos_auth_expiring(void) { return 0; }
int yule_eos_login_inflight(void) { return 0; }
int yule_eos_copy_id_token(char* out, size_t cap) {
    if (out && cap) out[0] = '\0';
    return 0;
}
void yule_eos_shutdown(void) {}
void* yule_eos_sdk_module(void) { return NULL; }
void* yule_eos_p2p_handle(void) { return NULL; }
void* yule_eos_local_user_handle(void) { return NULL; }

#else

/* Release tooling must distinguish SDK support from updater filename strings. */
const char g_yule_eos_p2p_build_marker[] = "YULE_EOS_P2P=1";

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "eos_sdk_loader.h"

#include "eos_sdk.h"
#include "eos_connect.h"

typedef struct YuleEosApi {
    __typeof__(EOS_Initialize)* initialize;
    __typeof__(EOS_Shutdown)* shutdown;
    __typeof__(EOS_Platform_Create)* platform_create;
    __typeof__(EOS_Platform_Release)* platform_release;
    __typeof__(EOS_Platform_Tick)* platform_tick;
    __typeof__(EOS_Platform_GetConnectInterface)* get_connect;
    __typeof__(EOS_Platform_GetP2PInterface)* get_p2p;
    __typeof__(EOS_Connect_Login)* connect_login;
    __typeof__(EOS_Connect_CreateUser)* connect_create_user;
    __typeof__(EOS_Connect_CopyIdToken)* connect_copy_id_token;
    __typeof__(EOS_Connect_IdToken_Release)* connect_release_id_token;
    __typeof__(EOS_Connect_AddNotifyAuthExpiration)* connect_add_auth_expiration;
    __typeof__(EOS_Connect_RemoveNotifyAuthExpiration)* connect_remove_auth_expiration;
    __typeof__(EOS_Connect_AddNotifyLoginStatusChanged)* connect_add_login_status;
    __typeof__(EOS_Connect_RemoveNotifyLoginStatusChanged)* connect_remove_login_status;
    __typeof__(EOS_ProductUserId_ToString)* puid_to_string;
    __typeof__(EOS_ProductUserId_IsValid)* puid_is_valid;
} YuleEosApi;

static struct {
    HMODULE library;
    YuleEosApi api;
    EOS_HPlatform platform;
    EOS_HConnect connect;
    EOS_HP2P p2p;
    EOS_ProductUserId local_user;
    YuleEosState state;
    char puid[EOS_PRODUCTUSERID_MAX_LENGTH + 1];
    char access_token[256];
    int initialized;
    int login_inflight;
    int login_was_ready;
    int auth_expiring;
    EOS_NotificationId auth_expiration_id;
    EOS_NotificationId login_status_id;
} g_eos;
static int (*g_carrier_service)(int force);

void yule_eos_set_carrier_service(int (*service)(int)) {
    g_carrier_service = service;
}

static void set_error(char* err, size_t cap, const char* message) {
    if (err && cap) snprintf(err, cap, "%s", message);
}

static int valid_config(const YuleEosConfig* c) {
    return c && c->product_id && strlen(c->product_id) == 32 &&
        c->sandbox_id && strlen(c->sandbox_id) == 32 &&
        c->deployment_id && strlen(c->deployment_id) == 32 &&
        c->client_id && c->client_id[0] && c->client_secret &&
        c->client_secret[0];
}

static void clear_access_token(void) {
    SecureZeroMemory(g_eos.access_token, sizeof(g_eos.access_token));
}

static void EOS_CALL on_auth_expiration(const EOS_Connect_AuthExpirationCallbackInfo* info) {
    if (info && g_eos.local_user && info->LocalUserId == g_eos.local_user)
        g_eos.auth_expiring = 1;
}

static void EOS_CALL on_login_status(const EOS_Connect_LoginStatusChangedCallbackInfo* info) {
    if (!info || !g_eos.local_user || info->LocalUserId != g_eos.local_user ||
        info->CurrentStatus != EOS_LS_NotLoggedIn) return;
    g_eos.state = YULE_EOS_CONNECT_FAILED;
    g_eos.auth_expiring = 1;
    g_eos.login_inflight = 0;
    g_eos.local_user = NULL;
    g_eos.puid[0] = '\0';
    clear_access_token();
}

static void connect_complete(EOS_EResult result, EOS_ProductUserId user) {
    int32_t length = (int32_t)sizeof(g_eos.puid);
    char refreshed_puid[EOS_PRODUCTUSERID_MAX_LENGTH + 1];
    int was_ready = g_eos.login_was_ready;
    g_eos.login_inflight = 0;
    g_eos.login_was_ready = 0;
    clear_access_token();
    if (result != EOS_Success || !user ||
        g_eos.api.puid_is_valid(user) != EOS_TRUE ||
        g_eos.api.puid_to_string(user, refreshed_puid, &length) != EOS_Success ||
        length < 2 || length > (int32_t)sizeof(refreshed_puid)) {
        if (was_ready && g_eos.local_user) {
            g_eos.state = YULE_EOS_CONNECT_READY;
            g_eos.auth_expiring = 1;
            return;
        }
        g_eos.local_user = NULL;
        g_eos.puid[0] = '\0';
        g_eos.state = YULE_EOS_CONNECT_FAILED;
        return;
    }
    if (was_ready && strcmp(g_eos.puid, refreshed_puid) != 0) {
        g_eos.local_user = NULL;
        g_eos.puid[0] = '\0';
        g_eos.state = YULE_EOS_CONNECT_FAILED;
        return;
    }
    g_eos.local_user = user;
    memcpy(g_eos.puid, refreshed_puid, (size_t)length);
    g_eos.auth_expiring = 0;
    g_eos.state = YULE_EOS_CONNECT_READY;
}

static void EOS_CALL on_create_user(const EOS_Connect_CreateUserCallbackInfo* info) {
    if (!info || !g_eos.login_inflight) return;
    connect_complete(info->ResultCode, info->LocalUserId);
}

static void EOS_CALL on_login(const EOS_Connect_LoginCallbackInfo* info) {
    if (!info || !g_eos.login_inflight) return;
    if (!g_eos.login_was_ready && info->ResultCode == EOS_InvalidUser &&
        info->ContinuanceToken) {
        EOS_Connect_CreateUserOptions options;
        memset(&options, 0, sizeof(options));
        options.ApiVersion = EOS_CONNECT_CREATEUSER_API_LATEST;
        options.ContinuanceToken = info->ContinuanceToken;
        g_eos.api.connect_create_user(g_eos.connect, &options, NULL, on_create_user);
        return;
    }
    connect_complete(info->ResultCode, info->LocalUserId);
}

int yule_eos_initialize(const YuleEosConfig* config, char* err, size_t err_cap) {
    EOS_InitializeOptions init;
    EOS_Platform_Options platform_options;
    char runtime_path[MAX_PATH + 64];
    char* filename;
    DWORD path_length;
    if (g_eos.state != YULE_EOS_UNAVAILABLE) return 1;
    if (!valid_config(config)) {
        set_error(err, err_cap, "EOS client configuration is incomplete");
        return 0;
    }
    /* Pin EOS to the game directory; permit Windows/VC++ dependencies from
     * System32 (SysWOW64 for this 32-bit process), never from cwd or PATH. */
    path_length = GetModuleFileNameA(NULL, runtime_path, sizeof(runtime_path));
    if (!path_length || path_length >= sizeof(runtime_path) ||
        !(filename = strrchr(runtime_path, '\\')) ||
        (size_t)(filename - runtime_path) +
            sizeof("\\EOSSDK-Win32-Shipping.dll") > sizeof(runtime_path)) {
        set_error(err, err_cap, "EOS runtime path is unavailable");
        return 0;
    }
    strcpy(filename + 1, "EOSSDK-Win32-Shipping.dll");
    g_eos.library = LoadLibraryExA(runtime_path, NULL,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!g_eos.library) {
        DWORD load_error = GetLastError();
        if (err && err_cap)
            snprintf(err, err_cap,
                "EOSSDK-Win32-Shipping.dll or its dependencies could not load (Windows error %lu)",
                (unsigned long)load_error);
        return 0;
    }
#define LOAD(member, symbol, argument_bytes) do { \
    FARPROC proc = yule_eos_sdk_proc(g_eos.library, #symbol, argument_bytes); \
    _Static_assert(sizeof(proc) == sizeof(g_eos.api.member), "EOS entry point size"); \
    memcpy(&g_eos.api.member, &proc, sizeof(proc)); \
    if (!g_eos.api.member) { set_error(err, err_cap, "EOS SDK entry point missing"); \
        yule_eos_shutdown(); return 0; } \
} while (0)
    LOAD(initialize, EOS_Initialize, 4);
    LOAD(shutdown, EOS_Shutdown, 0);
    LOAD(platform_create, EOS_Platform_Create, 4);
    LOAD(platform_release, EOS_Platform_Release, 4);
    LOAD(platform_tick, EOS_Platform_Tick, 4);
    LOAD(get_connect, EOS_Platform_GetConnectInterface, 4);
    LOAD(get_p2p, EOS_Platform_GetP2PInterface, 4);
    LOAD(connect_login, EOS_Connect_Login, 16);
    LOAD(connect_create_user, EOS_Connect_CreateUser, 16);
    LOAD(connect_copy_id_token, EOS_Connect_CopyIdToken, 12);
    LOAD(connect_release_id_token, EOS_Connect_IdToken_Release, 4);
    LOAD(connect_add_auth_expiration, EOS_Connect_AddNotifyAuthExpiration, 16);
    LOAD(connect_remove_auth_expiration, EOS_Connect_RemoveNotifyAuthExpiration, 12);
    LOAD(connect_add_login_status, EOS_Connect_AddNotifyLoginStatusChanged, 16);
    LOAD(connect_remove_login_status, EOS_Connect_RemoveNotifyLoginStatusChanged, 12);
    LOAD(puid_to_string, EOS_ProductUserId_ToString, 12);
    LOAD(puid_is_valid, EOS_ProductUserId_IsValid, 4);
#undef LOAD
    memset(&init, 0, sizeof(init));
    init.ApiVersion = EOS_INITIALIZE_API_LATEST;
    init.ProductName = "Yule";
    init.ProductVersion = "18";
    if (g_eos.api.initialize(&init) != EOS_Success) {
        set_error(err, err_cap, "EOS_Initialize failed");
        yule_eos_shutdown();
        return 0;
    }
    g_eos.initialized = 1;
    memset(&platform_options, 0, sizeof(platform_options));
    platform_options.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
    platform_options.ProductId = config->product_id;
    platform_options.SandboxId = config->sandbox_id;
    platform_options.DeploymentId = config->deployment_id;
    platform_options.ClientCredentials.ClientId = config->client_id;
    platform_options.ClientCredentials.ClientSecret = config->client_secret;
    platform_options.bIsServer = EOS_FALSE;
    platform_options.Flags = EOS_PF_DISABLE_OVERLAY;
    platform_options.TickBudgetInMilliseconds = 2;
    g_eos.platform = g_eos.api.platform_create(&platform_options);
    if (!g_eos.platform) {
        set_error(err, err_cap, "EOS_Platform_Create failed");
        yule_eos_shutdown();
        return 0;
    }
    g_eos.connect = g_eos.api.get_connect(g_eos.platform);
    g_eos.p2p = g_eos.api.get_p2p(g_eos.platform);
    if (!g_eos.connect || !g_eos.p2p) {
        set_error(err, err_cap, "EOS Connect or P2P is unavailable");
        yule_eos_shutdown();
        return 0;
    }
    {
        EOS_Connect_AddNotifyAuthExpirationOptions auth_options = {0};
        EOS_Connect_AddNotifyLoginStatusChangedOptions status_options = {0};
        auth_options.ApiVersion = EOS_CONNECT_ADDNOTIFYAUTHEXPIRATION_API_LATEST;
        status_options.ApiVersion = EOS_CONNECT_ADDNOTIFYLOGINSTATUSCHANGED_API_LATEST;
        g_eos.auth_expiration_id = g_eos.api.connect_add_auth_expiration(
            g_eos.connect, &auth_options, NULL, on_auth_expiration);
        g_eos.login_status_id = g_eos.api.connect_add_login_status(
            g_eos.connect, &status_options, NULL, on_login_status);
        if (g_eos.auth_expiration_id == EOS_INVALID_NOTIFICATIONID ||
            g_eos.login_status_id == EOS_INVALID_NOTIFICATIONID) {
            set_error(err, err_cap, "EOS Connect notifications are unavailable");
            yule_eos_shutdown();
            return 0;
        }
    }
    g_eos.state = YULE_EOS_PLATFORM_READY;
    return 1;
}

void yule_eos_tick(void) {
    if (g_eos.platform) g_eos.api.platform_tick(g_eos.platform);
    if (g_carrier_service) (void)g_carrier_service(0);
}

int yule_eos_connect_login(const char* token, char* err, size_t err_cap) {
    EOS_Connect_Credentials credentials;
    EOS_Connect_LoginOptions options;
    size_t length;
    if (!g_eos.connect || !token || g_eos.login_inflight) {
        set_error(err, err_cap, "EOS Connect is not ready for login");
        return 0;
    }
    length = strlen(token);
    if (length < 32 || length >= sizeof(g_eos.access_token)) {
        set_error(err, err_cap, "EOS Connect token has invalid length");
        return 0;
    }
    clear_access_token();
    memcpy(g_eos.access_token, token, length + 1);
    memset(&credentials, 0, sizeof(credentials));
    credentials.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
    credentials.Token = g_eos.access_token;
    credentials.Type = EOS_ECT_OPENID_ACCESS_TOKEN;
    memset(&options, 0, sizeof(options));
    options.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
    options.Credentials = &credentials;
    g_eos.login_was_ready = g_eos.state == YULE_EOS_CONNECT_READY;
    g_eos.login_inflight = 1;
    if (!g_eos.login_was_ready) g_eos.state = YULE_EOS_CONNECT_PENDING;
    g_eos.api.connect_login(g_eos.connect, &options, NULL, on_login);
    return 1;
}

YuleEosState yule_eos_state(void) { return g_eos.state; }
const char* yule_eos_puid(void) { return g_eos.puid; }
int yule_eos_auth_expiring(void) { return g_eos.auth_expiring; }
int yule_eos_login_inflight(void) { return g_eos.login_inflight; }

int yule_eos_copy_id_token(char* out, size_t cap) {
    EOS_Connect_CopyIdTokenOptions options;
    EOS_Connect_IdToken* token = NULL;
    size_t length;
    if (out && cap) out[0] = '\0';
    if (!out || !cap || g_eos.state != YULE_EOS_CONNECT_READY) return 0;
    memset(&options, 0, sizeof(options));
    options.ApiVersion = EOS_CONNECT_COPYIDTOKEN_API_LATEST;
    options.LocalUserId = g_eos.local_user;
    if (g_eos.api.connect_copy_id_token(g_eos.connect, &options, &token) != EOS_Success ||
        !token || !token->JsonWebToken) return 0;
    length = strlen(token->JsonWebToken);
    if (length + 1 > cap) {
        g_eos.api.connect_release_id_token(token);
        return 0;
    }
    memcpy(out, token->JsonWebToken, length + 1);
    g_eos.api.connect_release_id_token(token);
    return 1;
}

void yule_eos_shutdown(void) {
    /* Process/account shutdown is allowed a short bounded drain. Ordinary match
     * teardown is asynchronous and never sleeps in the gameplay update. */
    if (g_carrier_service) {
        DWORD start = GetTickCount();
        while (g_eos.platform && g_carrier_service(0) &&
               (DWORD)(GetTickCount() - start) < 150u) {
            g_eos.api.platform_tick(g_eos.platform);
            Sleep(1);
        }
        (void)g_carrier_service(1);
        g_carrier_service = NULL;
    }
    if (g_eos.connect && g_eos.auth_expiration_id &&
        g_eos.api.connect_remove_auth_expiration)
        g_eos.api.connect_remove_auth_expiration(g_eos.connect,
                                                 g_eos.auth_expiration_id);
    if (g_eos.connect && g_eos.login_status_id &&
        g_eos.api.connect_remove_login_status)
        g_eos.api.connect_remove_login_status(g_eos.connect,
                                              g_eos.login_status_id);
    if (g_eos.platform && g_eos.api.platform_release) {
        g_eos.api.platform_release(g_eos.platform);
    }
    if (g_eos.initialized && g_eos.api.shutdown) g_eos.api.shutdown();
    if (g_eos.library) FreeLibrary(g_eos.library);
    clear_access_token();
    memset(&g_eos, 0, sizeof(g_eos));
}

void* yule_eos_sdk_module(void) { return g_eos.library; }
void* yule_eos_p2p_handle(void) {
    return g_eos.state == YULE_EOS_CONNECT_READY ? g_eos.p2p : NULL;
}
void* yule_eos_local_user_handle(void) {
    return g_eos.state == YULE_EOS_CONNECT_READY ? g_eos.local_user : NULL;
}

#endif

const char* yule_eos_state_name(void) {
    switch (yule_eos_state()) {
        case YULE_EOS_UNAVAILABLE: return "unavailable";
        case YULE_EOS_PLATFORM_READY: return "platform_ready";
        case YULE_EOS_CONNECT_PENDING: return "connect_pending";
        case YULE_EOS_CONNECT_READY: return "connect_ready";
        case YULE_EOS_CONNECT_FAILED: return "connect_failed";
        default: return "unknown";
    }
}
