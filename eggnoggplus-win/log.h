#pragma once
#include <stdio.h>

void log_init();
void log_write(const char* level, const char* fmt, ...);

#define LOG_INFO(...)  log_write("INFO",  __VA_ARGS__)
#define LOG_WARN(...)  log_write("WARN",  __VA_ARGS__)
#define LOG_ERROR(...) log_write("ERROR", __VA_ARGS__)