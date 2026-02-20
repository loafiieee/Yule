#include "log.h"
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

static FILE* log_file = NULL;

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