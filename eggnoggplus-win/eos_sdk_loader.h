#pragma once

#include <windows.h>
#include <stdio.h>

/* Win32 EOS exports use stdcall decoration; Win64 exports are undecorated.
 * Each caller supplies the Win32 argument byte count, including 8-byte
 * EOS_NotificationId values. Preserve the SDK's declared calling convention. */
static inline FARPROC yule_eos_sdk_proc(HMODULE library, const char* name,
                                       unsigned int win32_argument_bytes) {
    FARPROC proc = GetProcAddress(library, name);
#if !defined(_WIN64)
    if (!proc) {
        char decorated[128];
        int length = snprintf(decorated, sizeof(decorated), "_%s@%u", name,
                              win32_argument_bytes);
        if (length > 0 && length < (int)sizeof(decorated))
            proc = GetProcAddress(library, decorated);
    }
#else
    (void)win32_argument_bytes;
#endif
    return proc;
}
