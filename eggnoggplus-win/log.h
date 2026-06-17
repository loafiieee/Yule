#pragma once
#include <stdio.h>

void log_init();
void log_set_console_visible(int visible);
void log_write(const char* level, const char* fmt, ...);
int log_set_level_name(const char* level_name);
const char* log_get_level_name(void);

#define LOG_DEBUG(...) log_write("DEBUG", __VA_ARGS__)
#define LOG_INFO(...)  log_write("INFO",  __VA_ARGS__)
#define LOG_WARN(...)  log_write("WARN",  __VA_ARGS__)
#define LOG_ERROR(...) log_write("ERROR", __VA_ARGS__)
