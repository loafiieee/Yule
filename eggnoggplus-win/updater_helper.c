#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "update_ext.h"

#define LAUNCHER_PATH_CAP 1024
#define LAUNCHER_COMMAND_CAP 32767
#define UPDATER_WAIT_TIMEOUT_MS 120000u

static int launcher_copy_wide(wchar_t* out, size_t cap,
                              const wchar_t* value) {
    size_t len;
    if (!out || cap == 0u || !value) return 0;
    len = wcslen(value);
    if (len >= cap) return 0;
    memcpy(out, value, (len + 1u) * sizeof(*out));
    return 1;
}

static int launcher_join_wide(wchar_t* out, size_t cap,
                              const wchar_t* left,
                              const wchar_t* right) {
    size_t a;
    size_t b;
    int separator;
    if (!out || !left || !right) return 0;
    a = wcslen(left);
    b = wcslen(right);
    separator = a > 0u && left[a - 1u] != L'\\' && left[a - 1u] != L'/';
    if (a + (size_t)separator + b + 1u > cap) return 0;
    memcpy(out, left, a * sizeof(*out));
    if (separator) out[a++] = L'\\';
    memcpy(out + a, right, (b + 1u) * sizeof(*out));
    return 1;
}

static int launcher_root(wchar_t out[LAUNCHER_PATH_CAP]) {
    DWORD len = GetModuleFileNameW(NULL, out, LAUNCHER_PATH_CAP);
    wchar_t* slash;
    if (len == 0u || len >= LAUNCHER_PATH_CAP) return 0;
    slash = wcsrchr(out, L'\\');
    if (!slash) slash = wcsrchr(out, L'/');
    if (!slash) return 0;
    *slash = L'\0';
    return out[0] != L'\0';
}

static int launcher_regular_file(const wchar_t* path) {
    DWORD attrs;
    if (!path || !path[0]) return 0;
    attrs = GetFileAttributesW(path);
    return attrs != INVALID_FILE_ATTRIBUTES &&
           !(attrs & (FILE_ATTRIBUTE_DIRECTORY |
                      FILE_ATTRIBUTE_REPARSE_POINT));
}

#ifdef UPDATE_EXT_HELPER_TEST
static int launcher_to_ansi(const wchar_t* wide, char* out, size_t cap) {
    int needed;
    if (!wide || !out || cap == 0u) return 0;
    needed = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS,
                                 wide, -1, out, (int)cap,
                                 NULL, NULL);
    return needed > 0 && (size_t)needed <= cap;
}
#endif

static int launcher_append_text(wchar_t* out, size_t cap, size_t* used,
                                const wchar_t* text, size_t len) {
    if (!out || !used || !text || *used + len + 1u > cap) return 0;
    memcpy(out + *used, text, len * sizeof(*out));
    *used += len;
    out[*used] = L'\0';
    return 1;
}

/* Quote one argv element using the exact CreateProcess/CommandLineToArgvW
 * backslash-before-quote rules. */
static int launcher_append_quoted_arg(wchar_t* out, size_t cap, size_t* used,
                                      const wchar_t* arg) {
    const wchar_t* p = arg ? arg : L"";
    size_t slashes = 0u;
    if (!launcher_append_text(out, cap, used, L"\"", 1u)) return 0;
    for (;;) {
        if (*p == L'\\') {
            slashes++;
            p++;
            continue;
        }
        if (*p == L'"') {
            while (slashes > 0u) {
                if (!launcher_append_text(out, cap, used, L"\\\\", 2u)) {
                    return 0;
                }
                slashes--;
            }
            if (!launcher_append_text(out, cap, used, L"\\\"", 2u)) return 0;
            p++;
            continue;
        }
        if (*p == L'\0') {
            while (slashes > 0u) {
                if (!launcher_append_text(out, cap, used, L"\\\\", 2u)) {
                    return 0;
                }
                slashes--;
            }
            return launcher_append_text(out, cap, used, L"\"", 1u);
        }
        while (slashes > 0u) {
            if (!launcher_append_text(out, cap, used, L"\\", 1u)) return 0;
            slashes--;
        }
        if (!launcher_append_text(out, cap, used, p, 1u)) return 0;
        p++;
    }
}

static int launcher_build_command(wchar_t* out, size_t cap,
                                  const wchar_t* game,
                                  int argc, wchar_t** argv,
                                  const unsigned char* forward) {
    size_t used = 0u;
    int i;
    if (!launcher_append_quoted_arg(out, cap, &used, game)) return 0;
    for (i = 1; i < argc; i++) {
        if (!forward[i]) continue;
        if (!launcher_append_text(out, cap, &used, L" ", 1u) ||
            !launcher_append_quoted_arg(out, cap, &used, argv[i])) {
            return 0;
        }
    }
    return 1;
}

static void launcher_log(const wchar_t* root,
                         UpdateHelperResult result,
                         const char* status) {
    wchar_t mods[LAUNCHER_PATH_CAP];
    wchar_t path[LAUNCHER_PATH_CAP];
    HANDLE file;
    SYSTEMTIME now;
    char line[512];
    DWORD written = 0u;
    int len;
    if (!launcher_join_wide(mods, LAUNCHER_PATH_CAP, root, L"mods")) return;
    (void)CreateDirectoryW(mods, NULL);
    if (!launcher_join_wide(path, LAUNCHER_PATH_CAP, mods,
                            L"updater.log")) {
        return;
    }
    file = CreateFileW(path, FILE_APPEND_DATA,
                       FILE_SHARE_READ | FILE_SHARE_WRITE |
                           FILE_SHARE_DELETE,
                       NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    GetLocalTime(&now);
    len = snprintf(line, sizeof(line),
                   "%04u-%02u-%02u %02u:%02u:%02u result=%d status=%s\r\n",
                   (unsigned int)now.wYear,
                   (unsigned int)now.wMonth,
                   (unsigned int)now.wDay,
                   (unsigned int)now.wHour,
                   (unsigned int)now.wMinute,
                   (unsigned int)now.wSecond,
                   (int)result,
                   (status && status[0]) ? status : "none");
    if (len > 0 && (size_t)len < sizeof(line)) {
        (void)WriteFile(file, line, (DWORD)len, &written, NULL);
        (void)FlushFileBuffers(file);
    }
    CloseHandle(file);
}

static int launcher_fail(const wchar_t* title, const wchar_t* message,
                         int recover_only) {
    if (!recover_only) {
        MessageBoxW(NULL, message, title,
                    MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    }
    return 2;
}

typedef struct UpdaterCloseContext {
    DWORD process_id;
    int windows_found;
} UpdaterCloseContext;

static BOOL CALLBACK updater_close_window(HWND window, LPARAM parameter) {
    UpdaterCloseContext* context =
        (UpdaterCloseContext*)(uintptr_t)parameter;
    DWORD process_id = 0u;
    if (!context) return TRUE;
    (void)GetWindowThreadProcessId(window, &process_id);
    if (process_id == context->process_id) {
        context->windows_found++;
        (void)PostMessageW(window, WM_CLOSE, 0, 0);
    }
    return TRUE;
}

static int updater_close_and_wait(DWORD process_id) {
    HANDLE process;
    DWORD wait_result;
    UpdaterCloseContext context;
    if (process_id == 0u || process_id == GetCurrentProcessId()) return 0;
    process = OpenProcess(SYNCHRONIZE, FALSE, process_id);
    if (!process) return 0;
    memset(&context, 0, sizeof(context));
    context.process_id = process_id;
    (void)EnumWindows(updater_close_window, (LPARAM)(uintptr_t)&context);
    wait_result = WaitForSingleObject(process, UPDATER_WAIT_TIMEOUT_MS);
    CloseHandle(process);
    return wait_result == WAIT_OBJECT_0;
}

/*
 * Waiting for the initiating PID is not sufficient when two clients share one
 * installation. Renaming a DLL mapped by the second client succeeds, but its
 * `.old` backup cannot be deleted, leaving a committed transaction that blocks
 * relaunch. Refuse to touch disk while another process is running the exact
 * same game executable.
 */
static int updater_same_game_process_running(const wchar_t* game) {
    HANDLE snapshot;
    PROCESSENTRY32W entry;
    DWORD self = GetCurrentProcessId();
    int running = 0;
    if (!game || !game[0]) return 1;
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0u);
    if (snapshot == INVALID_HANDLE_VALUE) return 1;
    memset(&entry, 0, sizeof(entry));
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot, &entry)) {
        CloseHandle(snapshot);
        return 1;
    }
    do {
        HANDLE process;
        wchar_t image[LAUNCHER_PATH_CAP];
        DWORD image_size = LAUNCHER_PATH_CAP;
        if (entry.th32ProcessID == 0u ||
            entry.th32ProcessID == self) {
            continue;
        }
        process = OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
            FALSE, entry.th32ProcessID);
        if (!process) continue;
        if (WaitForSingleObject(process, 0u) == WAIT_TIMEOUT &&
            QueryFullProcessImageNameW(
                process, 0u, image, &image_size) &&
            image_size > 0u && image_size < LAUNCHER_PATH_CAP) {
            image[image_size] = L'\0';
            if (_wcsicmp(image, game) == 0) running = 1;
        }
        CloseHandle(process);
        if (running) break;
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return running;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous,
                    PWSTR command_line, int show) {
    wchar_t root[LAUNCHER_PATH_CAP];
    wchar_t game[LAUNCHER_PATH_CAP];
    wchar_t sdl[LAUNCHER_PATH_CAP];
    wchar_t* command;
    wchar_t** argv;
    unsigned char* forward;
    int argc = 0;
    int i;
    int recover_only = 0;
    DWORD wait_pid = 0u;
#ifdef UPDATE_EXT_HELPER_TEST
    int test_no_wait = 0;
#endif
    UpdateHelperResult helper_result;
    char helper_status[192];
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    (void)instance;
    (void)previous;
    (void)command_line;
    (void)show;
    root[0] = L'\0';
    game[0] = L'\0';
    sdl[0] = L'\0';

    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc <= 0) {
        return launcher_fail(L"EGGNOGG+ Updater",
                             L"The Windows command line could not be parsed.",
                             0);
    }
    forward = (unsigned char*)calloc((size_t)argc, 1u);
    command = (wchar_t*)calloc(LAUNCHER_COMMAND_CAP, sizeof(*command));
    if (!forward || !command || !launcher_root(root)) {
        LocalFree(argv);
        free(forward);
        free(command);
        return launcher_fail(L"EGGNOGG+ Updater",
                             L"The updater could not initialize safely.", 0);
    }
    for (i = 1; i < argc; i++) forward[i] = 1u;
    for (i = 1; i < argc; i++) {
        if (_wcsicmp(argv[i], L"--recover-only") == 0) {
            recover_only = 1;
            forward[i] = 0u;
        }
        else if (_wcsnicmp(argv[i], L"--wait-pid=", 11u) == 0) {
            wchar_t* end = NULL;
            unsigned long parsed;
            if (wait_pid != 0u) {
                LocalFree(argv);
                free(forward);
                free(command);
                return launcher_fail(
                    L"EGGNOGG+ Updater",
                    L"The updater received more than one parent process.",
                    recover_only);
            }
            parsed = wcstoul(argv[i] + 11u, &end, 10);
            forward[i] = 0u;
            if (!end || *end != L'\0' || parsed == 0ul ||
                parsed > (unsigned long)MAXDWORD) {
                LocalFree(argv);
                free(forward);
                free(command);
                return launcher_fail(
                    L"EGGNOGG+ Updater",
                    L"The updater received an invalid parent process.",
                    recover_only);
            }
            wait_pid = (DWORD)parsed;
        }
#ifdef UPDATE_EXT_HELPER_TEST
        else if (_wcsnicmp(argv[i], L"--test-root=", 12u) == 0) {
            char root_ansi[LAUNCHER_PATH_CAP];
            forward[i] = 0u;
            if (!launcher_copy_wide(root, LAUNCHER_PATH_CAP,
                                    argv[i] + 12u) ||
                !launcher_to_ansi(root, root_ansi, sizeof(root_ansi)) ||
                !update_ext_helper_set_root_for_test(root_ansi)) {
                LocalFree(argv);
                free(forward);
                free(command);
                return launcher_fail(L"EGGNOGG+ Updater Test",
                                     L"The disposable test root is invalid.",
                                     recover_only);
            }
        } else if (_wcsnicmp(argv[i], L"--test-game=", 12u) == 0) {
            forward[i] = 0u;
            if (!launcher_copy_wide(game, LAUNCHER_PATH_CAP,
                                    argv[i] + 12u)) {
                LocalFree(argv);
                free(forward);
                free(command);
                return launcher_fail(L"EGGNOGG+ Updater Test",
                                     L"The benign test child path is invalid.",
                                     recover_only);
            }
        } else if (_wcsicmp(argv[i], L"--test-no-wait") == 0) {
            forward[i] = 0u;
            test_no_wait = 1;
        }
#endif
    }
#ifndef UPDATE_EXT_HELPER_TEST
    if (!launcher_join_wide(game, LAUNCHER_PATH_CAP, root,
                            L"eggnoggplus.exe")) {
        LocalFree(argv);
        free(forward);
        free(command);
        return launcher_fail(L"EGGNOGG+ Updater",
                             L"The game path is too long.", recover_only);
    }
#else
    if (!game[0] &&
        !launcher_join_wide(game, LAUNCHER_PATH_CAP, root,
                            L"eggnoggplus.exe")) {
        LocalFree(argv);
        free(forward);
        free(command);
        return launcher_fail(L"EGGNOGG+ Updater Test",
                             L"The test child path is too long.", recover_only);
    }
#endif

#ifndef UPDATE_EXT_HELPER_TEST
    if (wait_pid == 0u) {
        LocalFree(argv);
        free(forward);
        free(command);
        return launcher_fail(
            L"EGGNOGG+ Updater",
            L"This helper is launched automatically when an update is ready.",
            recover_only);
    }
#else
    if (wait_pid == 0u && !test_no_wait) {
        LocalFree(argv);
        free(forward);
        free(command);
        return launcher_fail(
            L"EGGNOGG+ Updater Test",
            L"The updater test requires a wait mode.", recover_only);
    }
#endif
    if (wait_pid != 0u && !updater_close_and_wait(wait_pid)) {
        LocalFree(argv);
        free(forward);
        free(command);
        return launcher_fail(
            L"EGGNOGG+ Updater",
            L"Eggnogg did not close cleanly. The update was left staged and "
            L"no files were replaced.",
            recover_only);
    }
    if (updater_same_game_process_running(game)) {
        LocalFree(argv);
        free(forward);
        free(command);
        return launcher_fail(
            L"EGGNOGG+ Updater",
            L"Another Eggnogg process is using this installation. Close every "
            L"Eggnogg window and start the game again; the verified update "
            L"was left staged and no files were replaced.",
            recover_only);
    }

    helper_status[0] = '\0';
    helper_result = update_ext_helper_service(helper_status,
                                              sizeof(helper_status));
    launcher_log(root, helper_result, helper_status);
    if (helper_result == UPDATE_HELPER_BLOCKED) {
        wchar_t message[512];
        wchar_t status_wide[256];
        if (!MultiByteToWideChar(CP_ACP, 0,
                                 helper_status[0] ? helper_status :
                                     "update recovery is blocked",
                                 -1, status_wide,
                                 (int)(sizeof(status_wide) /
                                       sizeof(status_wide[0])))) {
            launcher_copy_wide(status_wide,
                               sizeof(status_wide) / sizeof(status_wide[0]),
                               L"update recovery is blocked");
        }
        _snwprintf(message,
                   sizeof(message) / sizeof(message[0]),
                   L"EGGNOGG+ was not started because the verified update "
                   L"transaction needs attention.\n\n%ls\n\n"
                   L"See mods\\updater.log.",
                   status_wide);
        message[(sizeof(message) / sizeof(message[0])) - 1u] = L'\0';
        LocalFree(argv);
        free(forward);
        free(command);
        return launcher_fail(L"EGGNOGG+ Update Recovery", message,
                             recover_only);
    }
    if (recover_only) {
        LocalFree(argv);
        free(forward);
        free(command);
        return 0;
    }
    if (!launcher_regular_file(game) ||
        !launcher_join_wide(sdl, LAUNCHER_PATH_CAP, root, L"SDL2.dll") ||
        !launcher_regular_file(sdl)) {
        LocalFree(argv);
        free(forward);
        free(command);
        return launcher_fail(
            L"EGGNOGG+ Updater",
            L"The game executable or SDL2.dll is missing, unsafe, or not a "
            L"regular file. The game was not started.",
            0);
    }
    if (!launcher_build_command(command, LAUNCHER_COMMAND_CAP,
                                game, argc, argv, forward)) {
        LocalFree(argv);
        free(forward);
        free(command);
        return launcher_fail(L"EGGNOGG+ Updater",
                             L"The forwarded command line is too long.", 0);
    }
    memset(&startup, 0, sizeof(startup));
    memset(&process, 0, sizeof(process));
    startup.cb = sizeof(startup);
    if (!CreateProcessW(game, command, NULL, NULL, FALSE, 0,
                        NULL, root, &startup, &process)) {
        LocalFree(argv);
        free(forward);
        free(command);
        return launcher_fail(L"EGGNOGG+ Updater",
                             L"Windows could not start eggnoggplus.exe.", 0);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    LocalFree(argv);
    free(forward);
    free(command);
    return 0;
}
