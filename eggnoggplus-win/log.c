#include "log.h"
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>

static FILE* log_file = NULL;
static int g_log_min_level = 1; // INFO
static int g_console_visible = 0; // framework log console (AllocConsole window); default hidden for releases

#define FRAMEWORK_CFG_PATH "mods/modframework.cfg"

// Read the persisted "show_log_console" setting (default 0 = hidden).
static int read_show_log_console_setting(void) {
    FILE* f = fopen(FRAMEWORK_CFG_PATH, "r");
    int val = 0;
    char line[256];
    if (!f) return val;
    while (fgets(line, sizeof(line), f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (_strnicmp(p, "show_log_console", 16) != 0) continue;
        p += 16;
        while (*p == ' ' || *p == '\t' || *p == '=' || *p == ':') p++;
        val = (atoi(p) != 0) ? 1 : 0;
        break;
    }
    fclose(f);
    return val;
}

// Persist the setting, preserving any other lines already in the file.
static void write_show_log_console_setting(int visible) {
    char lines[64][256];
    int count = 0;
    int replaced = 0;
    FILE* f = fopen(FRAMEWORK_CFG_PATH, "r");
    if (f) {
        while (count < 64 && fgets(lines[count], sizeof(lines[count]), f)) {
            char* p = lines[count];
            while (*p == ' ' || *p == '\t') p++;
            if (_strnicmp(p, "show_log_console", 16) == 0) {
                snprintf(lines[count], sizeof(lines[count]), "show_log_console=%d\n", visible ? 1 : 0);
                replaced = 1;
            }
            count++;
        }
        fclose(f);
    }
    CreateDirectoryA("mods", NULL);
    f = fopen(FRAMEWORK_CFG_PATH, "w");
    if (!f) return;
    for (int i = 0; i < count; i++) fputs(lines[i], f);
    if (!replaced) fprintf(f, "show_log_console=%d\n", visible ? 1 : 0);
    fclose(f);
}

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

    // Log file (a bare install has no mods/ yet - create it or file logging
    // silently dies and first-boot debugging becomes impossible)
    CreateDirectoryA("mods", NULL);
    log_file = fopen("mods/modframework.log", "w");

    // Honor the persisted "show framework log console" setting. We always
    // allocate the console (so logging works and the menu can re-show it live)
    // but hide the window immediately if the user turned it off. Launch args
    // -log / -nolog override the saved setting for this run.
    g_console_visible = read_show_log_console_setting();
    {
        const char* cl = GetCommandLineA();
        if (cl) {
            if (strstr(cl, "-nolog") || strstr(cl, "--nolog")) g_console_visible = 0;
            else if (strstr(cl, "-log") || strstr(cl, "--log")) g_console_visible = 1;
        }
    }
    if (!g_console_visible) {
        HWND hwnd = GetConsoleWindow();
        if (hwnd) ShowWindow(hwnd, SW_HIDE);
    }
}

void log_set_console_visible(int visible) {
    HWND hwnd;
    g_console_visible = visible ? 1 : 0;
    hwnd = GetConsoleWindow();
    if (hwnd) ShowWindow(hwnd, g_console_visible ? SW_SHOW : SW_HIDE);
    write_show_log_console_setting(g_console_visible);
}

int log_console_visible(void) {
    return g_console_visible;
}

static int g_log_bulk = 0;

void log_begin_bulk(void) { g_log_bulk = 1; }
void log_end_bulk(void) {
    g_log_bulk = 0;
    if (log_file) fflush(log_file);
}

static FILE* g_dump_file = NULL;

void log_dump_line(const char* fmt, ...) {
    va_list args;
    char buf[1024];
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (!g_dump_file) {
        CreateDirectoryA("mods", NULL);
        /* Append mode: never truncate, so both clients (and successive runs)
         * accumulate into one shared, diffable file. Delete it to start fresh. */
        g_dump_file = fopen("mods/desync_dump.log", "a");
        if (!g_dump_file) return;
    }
    fprintf(g_dump_file, "%s\n", buf);
}

void log_dump_flush(void) {
    if (g_dump_file) fflush(g_dump_file);
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

    // Bulk mode: file only, no console write, no per-line flush. Used for large
    // diagnostic dumps so they don't block the caller (and drop the netcode link).
    if (g_log_bulk) {
        if (log_file) fprintf(log_file, "[%s] [%s] %s\n", timebuf, level, buf);
        return;
    }

    printf("[%s] [%s] %s\n", timebuf, level, buf);
    if (log_file) {
        fprintf(log_file, "[%s] [%s] %s\n", timebuf, level, buf);
        fflush(log_file);
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
