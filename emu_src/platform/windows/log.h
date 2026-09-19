#pragma once

#ifndef LOG_H
#define LOG_H

#include <stdio.h>

// Levelled, source-tagged logging.
//
// Output format:  [14:22:07.431] [INFO ] winmain.c:214 WinMain: message
//
// Use the LOG_* macros rather than log_write() directly - they capture the
// file, function and line for you. wrlog() is kept as an Info-level alias so
// existing call sites keep working unchanged.

#ifdef _MSC_VER
#include <sal.h>
// MSVC has no printf format attribute; SAL gives the same build-time checking.
#define LOG_FORMAT_STRING _Printf_format_string_
#define LOG_FUNC __FUNCTION__
#else
#define LOG_FORMAT_STRING
#define LOG_FUNC __func__
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
	LOG_LEVEL_DEBUG = 0,
	LOG_LEVEL_INFO = 1,
	LOG_LEVEL_WARN = 2,
	LOG_LEVEL_ERROR = 3,
	LOG_LEVEL_OFF = 4
} log_level_t;

// Opens (or truncates) the log file. Closes any previously open log first.
// Returns 0 on success, -1 on failure.
int log_open(const char *filename);
void log_close(void);

// Messages below this level are discarded. Defaults to LOG_LEVEL_INFO.
void log_set_level(log_level_t level);
log_level_t log_get_level(void);

// Mirrors output to a console window, allocating one if the process has none.
void log_set_console_enabled(int enabled);

void log_write(log_level_t level, const char *file, const char *func, int line,
	LOG_FORMAT_STRING const char *format, ...);

#define LOG_DEBUG(...) log_write(LOG_LEVEL_DEBUG, __FILE__, LOG_FUNC, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  log_write(LOG_LEVEL_INFO,  __FILE__, LOG_FUNC, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  log_write(LOG_LEVEL_WARN,  __FILE__, LOG_FUNC, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) log_write(LOG_LEVEL_ERROR, __FILE__, LOG_FUNC, __LINE__, __VA_ARGS__)

// Back-compat with the original API. Logs at Info level.
#define wrlog(...) LOG_INFO(__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif
