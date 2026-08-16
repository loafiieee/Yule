#include "launch_ipc.h"

#include "online_control.h"

#include <windows.h>
#include <shellapi.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef LAUNCH_IPC_TEST
#define LAUNCH_IPC_CLASS_NAME L"YuleLaunchBrokerWindow.test.v1"
#define LAUNCH_IPC_WINDOW_NAME L"YuleLaunchBroker.test.v1"
#define LAUNCH_IPC_MUTEX_NAME L"Local\\YuleLaunchBroker.test.v1"
#else
#define LAUNCH_IPC_CLASS_NAME L"YuleLaunchBrokerWindow.v1"
#define LAUNCH_IPC_WINDOW_NAME L"YuleLaunchBroker.v1"
#define LAUNCH_IPC_MUTEX_NAME L"Local\\YuleLaunchBroker.v1"
#endif
#define LAUNCH_IPC_COPY_MAGIC ((ULONG_PTR)0x594C5231u)
#define LAUNCH_IPC_FORWARD_WAIT_MS 2500u

static HANDLE g_launch_ipc_mutex = NULL;
static HANDLE g_launch_ipc_thread = NULL;
static HANDLE g_launch_ipc_ready = NULL;
static DWORD g_launch_ipc_thread_id = 0u;
static CRITICAL_SECTION g_launch_ipc_lock;
static int g_launch_ipc_lock_ready = 0;
static int g_launch_ipc_started = 0;
static int g_launch_ipc_is_broker = 0;
static int g_launch_ipc_pending = 0;
static LaunchRequest g_launch_ipc_request;

static size_t launch_ipc_bounded_strlen(const char* text, size_t cap) {
    size_t length = 0u;
    if (!text) return cap;
    while (length < cap && text[length]) length++;
    return length;
}

static int launch_ipc_request_valid(const LaunchRequest* request) {
    size_t target_len;
    if (!request) return 0;
    target_len = launch_ipc_bounded_strlen(request->target,
                                           sizeof(request->target));
    if (target_len >= sizeof(request->target)) return 0;
    switch (request->action) {
        case LAUNCH_REQUEST_HUB:
        case LAUNCH_REQUEST_REQUESTS:
        case LAUNCH_REQUEST_QUEUE_CASUAL:
        case LAUNCH_REQUEST_QUEUE_COMPETITIVE:
            return target_len == 0u;
        case LAUNCH_REQUEST_CHALLENGE:
            return online_control_username_is_canonical(request->target);
        case LAUNCH_REQUEST_PREVIEW_V1:
            return launch_request_preview_target_valid(request->target);
        default:
            return 0;
    }
}

static BOOL CALLBACK launch_ipc_focus_window(HWND window, LPARAM context) {
    DWORD window_pid = 0u;
    DWORD target_pid = (DWORD)context;
    if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) != NULL) return TRUE;
    GetWindowThreadProcessId(window, &window_pid);
    if (window_pid != target_pid) return TRUE;
    if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
    (void)SetForegroundWindow(window);
    return FALSE;
}

static LRESULT CALLBACK launch_ipc_window_proc(HWND window,
                                               UINT message,
                                               WPARAM wparam,
                                               LPARAM lparam) {
    (void)wparam;
    if (message == WM_COPYDATA) {
        const COPYDATASTRUCT* copy = (const COPYDATASTRUCT*)lparam;
        LaunchRequest request;
        if (!copy || copy->dwData != LAUNCH_IPC_COPY_MAGIC ||
            copy->cbData != sizeof(request) || !copy->lpData) {
            return FALSE;
        }
        memcpy(&request, copy->lpData, sizeof(request));
        request.target[sizeof(request.target) - 1u] = '\0';
        if (!launch_ipc_request_valid(&request)) return FALSE;
        EnterCriticalSection(&g_launch_ipc_lock);
        g_launch_ipc_request = request;
        g_launch_ipc_pending = 1;
        LeaveCriticalSection(&g_launch_ipc_lock);
        (void)EnumWindows(launch_ipc_focus_window,
                          (LPARAM)GetCurrentProcessId());
        return TRUE;
    }
    if (message == WM_CLOSE) {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static DWORD WINAPI launch_ipc_thread_main(LPVOID unused) {
    WNDCLASSEXW window_class;
    HWND window;
    MSG message;
    HINSTANCE instance = GetModuleHandleW(NULL);
    (void)unused;
    memset(&window_class, 0, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = launch_ipc_window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = LAUNCH_IPC_CLASS_NAME;
    if (!RegisterClassExW(&window_class) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        if (g_launch_ipc_ready) SetEvent(g_launch_ipc_ready);
        return 1u;
    }
    window = CreateWindowExW(
        0u,
        LAUNCH_IPC_CLASS_NAME,
        LAUNCH_IPC_WINDOW_NAME,
        0u,
        0, 0, 0, 0,
        HWND_MESSAGE,
        NULL,
        instance,
        NULL);
    if (g_launch_ipc_ready) SetEvent(g_launch_ipc_ready);
    if (!window) return 1u;
    while (GetMessageW(&message, NULL, 0u, 0u) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0u;
}

static LaunchRequestParseResult launch_ipc_parse_process_request(
    LaunchRequest* request) {
    wchar_t** wide_args;
    char** utf8_args;
    char error[192];
    int argc = 0;
    int i;
    int conversion_ok = 1;
    LaunchRequestParseResult result;
    if (request) memset(request, 0, sizeof(*request));
    wide_args = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wide_args || argc <= 0 || argc > 128 || !request) {
        if (wide_args) LocalFree(wide_args);
        return LAUNCH_REQUEST_PARSE_ERROR;
    }
    utf8_args = (char**)calloc((size_t)argc, sizeof(*utf8_args));
    if (!utf8_args) {
        LocalFree(wide_args);
        return LAUNCH_REQUEST_PARSE_ERROR;
    }
    for (i = 0; i < argc; i++) {
        int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                        wide_args[i], -1,
                                        NULL, 0, NULL, NULL);
        if (bytes <= 0 || bytes > (int)(LAUNCH_REQUEST_TARGET_CAP + 64u)) {
            conversion_ok = 0;
            break;
        }
        utf8_args[i] = (char*)malloc((size_t)bytes);
        if (!utf8_args[i] ||
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                wide_args[i], -1,
                                utf8_args[i], bytes, NULL, NULL) <= 0) {
            conversion_ok = 0;
            break;
        }
    }
    error[0] = '\0';
    result = conversion_ok
        ? launch_request_parse_args(argc,
                                    (const char* const*)utf8_args,
                                    request,
                                    error,
                                    sizeof(error))
        : LAUNCH_REQUEST_PARSE_ERROR;
    for (i = 0; i < argc; i++) free(utf8_args[i]);
    free(utf8_args);
    LocalFree(wide_args);
    return result;
}

static HWND launch_ipc_find_broker(void) {
    return FindWindowExW(HWND_MESSAGE, NULL,
                         LAUNCH_IPC_CLASS_NAME,
                         LAUNCH_IPC_WINDOW_NAME);
}

static int launch_ipc_forward(const LaunchRequest* request) {
    DWORD started = GetTickCount();
    COPYDATASTRUCT copy;
    if (!launch_ipc_request_valid(request)) return 0;
    copy.dwData = LAUNCH_IPC_COPY_MAGIC;
    copy.cbData = sizeof(*request);
    copy.lpData = (PVOID)request;
    for (;;) {
        HWND window = launch_ipc_find_broker();
        if (window) {
            DWORD broker_pid = 0u;
            DWORD_PTR response = 0u;
            GetWindowThreadProcessId(window, &broker_pid);
            if (broker_pid) (void)AllowSetForegroundWindow(broker_pid);
            if (SendMessageTimeoutW(window, WM_COPYDATA, 0u, (LPARAM)&copy,
                                    SMTO_ABORTIFHUNG | SMTO_BLOCK,
                                    1500u, &response) &&
                response == TRUE) {
                return 1;
            }
        }
        if ((DWORD)(GetTickCount() - started) >=
            LAUNCH_IPC_FORWARD_WAIT_MS) {
            return 0;
        }
        Sleep(25u);
    }
}

int launch_ipc_initialize(void) {
    LaunchRequest process_request;
    LaunchRequestParseResult parsed;
    DWORD mutex_error;
    if (g_launch_ipc_started) return 0;
    g_launch_ipc_started = 1;
    parsed = launch_ipc_parse_process_request(&process_request);
    g_launch_ipc_mutex = CreateMutexW(NULL, TRUE, LAUNCH_IPC_MUTEX_NAME);
    if (!g_launch_ipc_mutex) return 0;
    mutex_error = GetLastError();
    if (mutex_error == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_launch_ipc_mutex);
        g_launch_ipc_mutex = NULL;
        if (parsed == LAUNCH_REQUEST_PARSE_OK &&
            launch_ipc_forward(&process_request)) {
            return 1;
        }
        return 0;
    }
    InitializeCriticalSection(&g_launch_ipc_lock);
    g_launch_ipc_lock_ready = 1;
    g_launch_ipc_ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_launch_ipc_thread = CreateThread(NULL, 0u,
                                       launch_ipc_thread_main, NULL,
                                       0u, &g_launch_ipc_thread_id);
    if (g_launch_ipc_thread && g_launch_ipc_ready) {
        (void)WaitForSingleObject(g_launch_ipc_ready, 1500u);
    }
    if (g_launch_ipc_ready) {
        CloseHandle(g_launch_ipc_ready);
        g_launch_ipc_ready = NULL;
    }
    if (!g_launch_ipc_thread || !launch_ipc_find_broker()) {
        if (g_launch_ipc_thread) {
            (void)WaitForSingleObject(g_launch_ipc_thread, 1000u);
            CloseHandle(g_launch_ipc_thread);
            g_launch_ipc_thread = NULL;
        }
        CloseHandle(g_launch_ipc_mutex);
        g_launch_ipc_mutex = NULL;
        DeleteCriticalSection(&g_launch_ipc_lock);
        g_launch_ipc_lock_ready = 0;
        return 0;
    }
    g_launch_ipc_is_broker = 1;
    ReleaseMutex(g_launch_ipc_mutex);
    return 0;
}

int launch_ipc_poll(LaunchRequest* out) {
    int found = 0;
    if (!out || !g_launch_ipc_lock_ready) return 0;
    EnterCriticalSection(&g_launch_ipc_lock);
    if (g_launch_ipc_pending) {
        *out = g_launch_ipc_request;
        memset(&g_launch_ipc_request, 0, sizeof(g_launch_ipc_request));
        g_launch_ipc_pending = 0;
        found = 1;
    }
    LeaveCriticalSection(&g_launch_ipc_lock);
    return found;
}

void launch_ipc_shutdown(void) {
    HWND window;
    if (!g_launch_ipc_started || !g_launch_ipc_is_broker) return;
    window = launch_ipc_find_broker();
    if (window) PostMessageW(window, WM_CLOSE, 0u, 0);
    if (g_launch_ipc_thread) {
        (void)WaitForSingleObject(g_launch_ipc_thread, 1000u);
        CloseHandle(g_launch_ipc_thread);
        g_launch_ipc_thread = NULL;
    }
    if (g_launch_ipc_mutex) {
        CloseHandle(g_launch_ipc_mutex);
        g_launch_ipc_mutex = NULL;
    }
    if (g_launch_ipc_lock_ready) {
        DeleteCriticalSection(&g_launch_ipc_lock);
        g_launch_ipc_lock_ready = 0;
    }
    g_launch_ipc_is_broker = 0;
}
