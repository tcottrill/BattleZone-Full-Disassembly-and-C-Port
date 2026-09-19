#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "log.h"

#pragma warning ( disable:4996 )

static FILE *logfile = NULL;
static log_level_t min_level = LOG_LEVEL_INFO;
static int console_enabled = 0;
static int console_allocated = 0;
static CRITICAL_SECTION log_lock;
static int lock_ready = 0;

static const char *const level_names[] = { "DEBUG", "INFO ", "WARN ", "ERROR" };

// __FILE__ carries the full build path; only the basename is useful in a log.
static const char *basename_of(const char *path)
{
	const char *slash;

	if (!path) return "?";
	slash = strrchr(path, '\\');
	if (!slash) slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

int log_open(const char *filename)
{
	if (!lock_ready)
	{
		InitializeCriticalSection(&log_lock);
		lock_ready = 1;
	}

	EnterCriticalSection(&log_lock);

	// Re-opening used to leak the previous handle.
	if (logfile)
	{
		fclose(logfile);
		logfile = NULL;
	}

	logfile = fopen(filename, "w");

	LeaveCriticalSection(&log_lock);

	return logfile ? 0 : -1;
}

void log_close(void)
{
	if (!lock_ready) return;

	EnterCriticalSection(&log_lock);

	if (logfile)
	{
		fclose(logfile);
		logfile = NULL;
	}

	if (console_allocated)
	{
		FreeConsole();
		console_allocated = 0;
	}
	console_enabled = 0;

	LeaveCriticalSection(&log_lock);

	DeleteCriticalSection(&log_lock);
	lock_ready = 0;
}

void log_set_level(log_level_t level)
{
	min_level = level;
}

log_level_t log_get_level(void)
{
	return min_level;
}

void log_set_console_enabled(int enabled)
{
	if (enabled && !console_enabled)
	{
		// GetConsoleWindow() is NULL for a Windows-subsystem app with no console.
		if (GetConsoleWindow() == NULL)
		{
			if (!AllocConsole()) return;
			console_allocated = 1;
		}
		if (!freopen("CONOUT$", "w", stdout))
		{
			if (console_allocated) { FreeConsole(); console_allocated = 0; }
			return;
		}
		console_enabled = 1;
	}
	else if (!enabled && console_enabled)
	{
		if (console_allocated)
		{
			FreeConsole();
			console_allocated = 0;
		}
		console_enabled = 0;
	}
}

void log_write(log_level_t level, const char *file, const char *func, int line,
	const char *format, ...)
{
	char message[1024];
	SYSTEMTIME st;
	va_list args;

	if (level < min_level || level >= LOG_LEVEL_OFF) return;
	if (!logfile && !console_enabled) return;

	va_start(args, format);
	vsnprintf(message, sizeof(message), format, args);
	va_end(args);

	GetLocalTime(&st);

	if (lock_ready) EnterCriticalSection(&log_lock);

	if (logfile)
	{
		fprintf(logfile, "[%02d:%02d:%02d.%03d] [%s] %s:%d %s: %s\n",
			st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
			level_names[level], basename_of(file), line, func ? func : "?", message);
		fflush(logfile);
	}

	if (console_enabled)
	{
		printf("[%02d:%02d:%02d.%03d] [%s] %s:%d %s: %s\n",
			st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
			level_names[level], basename_of(file), line, func ? func : "?", message);
		fflush(stdout);
	}

	if (lock_ready) LeaveCriticalSection(&log_lock);
}
