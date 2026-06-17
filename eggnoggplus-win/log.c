#include "log.h"
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>

static FILE* log_file = NULL;
static int g_log_min_level = 1; // INFO

static int level_rank_from_name(const char* level) {
    if (!level) return 1;
    if (_stricmp(level, "DEBUG") == 0) return 0;
    if (_stricmp(level, "INFO") == 0) return 1;
    if (_stricmp(level, "WARN") == 0) return 2;
    if (_stricmp(level, "ERROR") == 0) return 3;
    return 1;
}

static const char* level_name_from_rank(int rank) {
    switch (rank) {
        case 0: return "DEBUG";
        case 1: return "INFO";
        case 2: return "WARN";
        case 3: return "ERROR";
        default: return "INFO";
    }
}

void log_init() {
    // Console
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);

    // Log file
    log_file = fopen("mods/modframework.log", "w");
}

void log_write(const char* level, const char* fmt, ...) {
    va_list args;
    char buf[1024];

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (level_rank_from_name(level) < g_log_min_level) {
        return;
    }

    // Timestamp
    time_t t = time(NULL);
    struct tm* tm = localtime(&t);
    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", tm);

    printf("[%s] [%s] %s\n", timebuf, level, buf);
    if (log_file) {
        fprintf(log_file, "[%s] [%s] %s\n", timebuf, level, buf);
        fflush(log_file);
    }
}

void log_set_console_visible(int visible) {
    // Hide/show the AllocConsole window without freeing it, so stdout stays
    // valid and the toggle is fully reversible at runtime.
    HWND hwnd = GetConsoleWindow();
    if (hwnd) {
        ShowWindow(hwnd, visible ? SW_SHOW : SW_HIDE);
    }
}

int log_set_level_name(const char* level_name) {
    if (!level_name) return 0;
    if (_stricmp(level_name, "debug") == 0) {
        g_log_min_level = 0;
        return 1;
    }
    if (_stricmp(level_name, "info") == 0) {
        g_log_min_level = 1;
        return 1;
    }
    if (_stricmp(level_name, "warn") == 0) {
        g_log_min_level = 2;
        return 1;
    }
    if (_stricmp(level_name, "error") == 0) {
        g_log_min_level = 3;
        return 1;
    }
    return 0;
}

const char* log_get_level_name(void) {
    return level_name_from_rank(g_log_min_level);
}
