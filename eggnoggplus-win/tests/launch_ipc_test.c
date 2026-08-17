#include <windows.h>

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "../launch_ipc.h"

static int spawn_child(const wchar_t* mode,
                       const wchar_t* argument,
                       DWORD* exit_code) {
    wchar_t executable[MAX_PATH];
    wchar_t command_line[1024];
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    DWORD length = GetModuleFileNameW(NULL, executable, MAX_PATH);
    if (!length || length >= MAX_PATH) return 0;
    if (argument && argument[0]) {
        if (swprintf(command_line, sizeof(command_line) / sizeof(command_line[0]),
                     L"\"%ls\" \"%ls\"", executable, argument) < 0) {
            return 0;
        }
    } else if (swprintf(command_line,
                        sizeof(command_line) / sizeof(command_line[0]),
                        L"\"%ls\"", executable) < 0) {
        return 0;
    }
    if (!SetEnvironmentVariableW(L"YULE_LAUNCH_IPC_TEST_CHILD", mode)) return 0;
    memset(&startup, 0, sizeof(startup));
    memset(&process, 0, sizeof(process));
    startup.cb = sizeof(startup);
    if (!CreateProcessW(NULL, command_line, NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, NULL,
                        &startup, &process)) {
        SetEnvironmentVariableW(L"YULE_LAUNCH_IPC_TEST_CHILD", NULL);
        return 0;
    }
    SetEnvironmentVariableW(L"YULE_LAUNCH_IPC_TEST_CHILD", NULL);
    if (WaitForSingleObject(process.hProcess, 5000u) != WAIT_OBJECT_0 ||
        !GetExitCodeProcess(process.hProcess, exit_code)) {
        TerminateProcess(process.hProcess, 99u);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 0;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 1;
}

static int poll_request(LaunchRequest* request) {
    DWORD started = GetTickCount();
    while ((DWORD)(GetTickCount() - started) < 3000u) {
        if (launch_ipc_poll(request)) return 1;
        Sleep(10u);
    }
    return 0;
}

int main(void) {
    wchar_t child_mode[32];
    DWORD exit_code = 0u;
    LaunchRequest request;
    DWORD mode_length = GetEnvironmentVariableW(
        L"YULE_LAUNCH_IPC_TEST_CHILD",
        child_mode,
        (DWORD)(sizeof(child_mode) / sizeof(child_mode[0])));
    if (mode_length > 0u && mode_length <
        sizeof(child_mode) / sizeof(child_mode[0])) {
        int forwarded = launch_ipc_initialize();
        if (wcscmp(child_mode, L"protocol") == 0) {
            return forwarded ? 0 : 10;
        }
        if (wcscmp(child_mode, L"direct") == 0) {
            if (forwarded) return 11;
            /* Must not close the broker owned by the parent process. */
            launch_ipc_shutdown();
            return 0;
        }
        return 12;
    }

    if (launch_ipc_initialize()) {
        fprintf(stderr, "parent unexpectedly forwarded\n");
        return 1;
    }
    if (!spawn_child(L"protocol",
                     L"--yule-uri=yule://challenge/player_1",
                     &exit_code) ||
        exit_code != 0u) {
        fprintf(stderr, "protocol child failed: %lu\n",
                (unsigned long)exit_code);
        launch_ipc_shutdown();
        return 2;
    }
    memset(&request, 0, sizeof(request));
    if (!poll_request(&request) ||
        request.action != LAUNCH_REQUEST_CHALLENGE ||
        strcmp(request.target, "player_1") != 0) {
        fprintf(stderr, "challenge request was not forwarded exactly\n");
        launch_ipc_shutdown();
        return 3;
    }

    if (!spawn_child(L"direct", NULL, &exit_code) || exit_code != 0u) {
        fprintf(stderr, "ordinary direct child failed: %lu\n",
                (unsigned long)exit_code);
        launch_ipc_shutdown();
        return 4;
    }
    if (launch_ipc_poll(&request)) {
        fprintf(stderr, "ordinary direct launch was incorrectly forwarded\n");
        launch_ipc_shutdown();
        return 5;
    }

    if (!spawn_child(L"protocol",
                     L"--yule-uri=yule://queue/competitive",
                     &exit_code) ||
        exit_code != 0u) {
        fprintf(stderr, "broker did not survive direct child shutdown\n");
        launch_ipc_shutdown();
        return 6;
    }
    memset(&request, 0, sizeof(request));
    if (!poll_request(&request) ||
        request.action != LAUNCH_REQUEST_QUEUE_COMPETITIVE ||
        request.target[0] != '\0') {
        fprintf(stderr, "queue request was not forwarded exactly\n");
        launch_ipc_shutdown();
        return 7;
    }
    if (!spawn_child(L"protocol",
                     L"--yule-uri=yule://preview/v1/e30/eA",
                     &exit_code) ||
        exit_code != 0u) {
        fprintf(stderr, "preview protocol child failed: %lu\n",
                (unsigned long)exit_code);
        launch_ipc_shutdown();
        return 8;
    }
    memset(&request, 0, sizeof(request));
    if (!poll_request(&request) ||
        request.action != LAUNCH_REQUEST_PREVIEW_V1 ||
        strcmp(request.target, "e30/eA") != 0) {
        fprintf(stderr, "preview request was not forwarded exactly\n");
        launch_ipc_shutdown();
        return 9;
    }
    if (!spawn_child(L"protocol",
                     L"--yule-uri=yule://preview/v1z/R0dQMQIAAAABAAAAAHt9eA",
                     &exit_code) ||
        exit_code != 0u) {
        fprintf(stderr, "compressed preview protocol child failed: %lu\n",
                (unsigned long)exit_code);
        launch_ipc_shutdown();
        return 10;
    }
    memset(&request, 0, sizeof(request));
    if (!poll_request(&request) ||
        request.action != LAUNCH_REQUEST_PREVIEW_V1_PACKED ||
        strcmp(request.target, "R0dQMQIAAAABAAAAAHt9eA") != 0) {
        fprintf(stderr, "compressed preview request was not forwarded exactly\n");
        launch_ipc_shutdown();
        return 11;
    }
    launch_ipc_shutdown();
    puts("launch IPC tests passed");
    return 0;
}
