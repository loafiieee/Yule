#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

int wmain(int argc, wchar_t** argv) {
    wchar_t output[1024];
    wchar_t hold_text[32];
    FILE* file;
    int i;
    DWORD got = GetEnvironmentVariableW(
        L"YULE_LAUNCHER_ARG_OUTPUT", output,
        (DWORD)(sizeof(output) / sizeof(output[0])));
    if (got == 0u || got >= sizeof(output) / sizeof(output[0])) return 2;
    file = _wfopen(output, L"wb");
    if (!file) return 3;
    fprintf(file, "%d\n", argc);
    for (i = 0; i < argc; i++) {
        int bytes = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1,
                                        NULL, 0, NULL, NULL);
        char* utf8;
        if (bytes <= 0) {
            fclose(file);
            return 4;
        }
        utf8 = (char*)malloc((size_t)bytes);
        if (!utf8 ||
            !WideCharToMultiByte(CP_UTF8, 0, argv[i], -1,
                                 utf8, bytes, NULL, NULL)) {
            free(utf8);
            fclose(file);
            return 5;
        }
        fprintf(file, "%s\n", utf8);
        free(utf8);
    }
    if (fclose(file) != 0) return 6;
    got = GetEnvironmentVariableW(
        L"YULE_LAUNCHER_HOLD_MS", hold_text,
        (DWORD)(sizeof(hold_text) / sizeof(hold_text[0])));
    if (got > 0u && got < sizeof(hold_text) / sizeof(hold_text[0])) {
        wchar_t* end = NULL;
        unsigned long hold_ms = wcstoul(hold_text, &end, 10);
        if (end && *end == L'\0' && hold_ms > 0ul &&
            hold_ms <= 30000ul) {
            Sleep((DWORD)hold_ms);
        }
    }
    return 0;
}
