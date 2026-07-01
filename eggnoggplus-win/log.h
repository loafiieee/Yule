#pragma once
#include <stdio.h>

void log_init();
void log_write(const char* level, const char* fmt, ...);
/* Bulk mode for large diagnostic dumps (e.g. the rngtrace ring): suppress the
 * per-line console write + fflush so thousands of lines don't block the caller
 * (an un-batched dump took long enough to drop the netcode connection). Wrap the
 * dump loop with begin/end; end flushes the file once. */
void log_begin_bulk(void);
void log_end_bulk(void);

/* Dedicated desync/RNG dump file (mods/desync_dump.log), opened in APPEND mode so
 * two game clients sharing a folder can both write their dumps without the second
 * launch truncating the first's data (the main modframework.log is opened "w").
 * Each line carries its own p=0/p=1 label. log_dump_line writes one line (no
 * flush); log_dump_flush flushes once after a dump loop. Diagnostic only. */
void log_dump_line(const char* fmt, ...);
void log_dump_flush(void);
int log_set_level_name(const char* level_name);
const char* log_get_level_name(void);

// Framework log console (the separate terminal window). Persisted to
// mods/modframework.cfg as "show_log_console".
void log_set_console_visible(int visible);  // applies live + saves the setting
int  log_console_visible(void);

#define LOG_DEBUG(...) log_write("DEBUG", __VA_ARGS__)
#define LOG_INFO(...)  log_write("INFO",  __VA_ARGS__)
#define LOG_WARN(...)  log_write("WARN",  __VA_ARGS__)
#define LOG_ERROR(...) log_write("ERROR", __VA_ARGS__)
