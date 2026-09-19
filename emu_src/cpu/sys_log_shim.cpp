// sys_log_shim.cpp - bz_emu: the Log:: functions the vendored cpu_6502
// core calls (cpu\sys_log.h), without AAE's sys_log.cpp.
//
// Why a shim: AAE's sys_log.cpp is an asynchronous file logger with its own
// worker thread, and its setConsoleOutputEnabled() calls AllocConsole() - a
// visible console window, which the Klaus runner (test_6502.cpp) asks for.
// bz_emu already has ONE log, the Windows backend's
// (c_src\platform\windows\log.c -> bz_win.log next to the exe), so the
// core's few messages ("6502 Reset", a JAM opcode, an unhandled access) are
// forwarded there.  DESIGN.md allows exactly this.
//
//   /DEMU_LOG_TO_BACKEND   bz_emu.exe: Log::write -> log_write()
//   (not defined)          obj\klaus_test.exe, a console program: -> stdout
//
// open / close / setLevel / setConsoleOutputEnabled do nothing: the backend
// owns its log file, and no console is ever allocated.
#include <cstdarg>
#include <cstdio>
#include "sys_log.h"

#ifdef EMU_LOG_TO_BACKEND
#undef LOG_DEBUG                       // sys_log.h's macros; the backend's log.h defines the same names
#undef LOG_INFO
#undef LOG_ERROR
#include "platform/windows/log.h"      // c_src (has its own extern "C" guard)
#endif

namespace Log {

bool open(const std::string&) { return true; }
void close() {}
void setLevel(Level) {}
void setConsoleOutputEnabled(bool) {}

void write(Level level, const char* file, const char* function, int line, const char* format, ...)
{
	char msg[1024];
	va_list args;
	va_start(args, format);
	vsnprintf(msg, sizeof msg, format, args);
	va_end(args);
#ifdef EMU_LOG_TO_BACKEND
	log_write(level == Level::Error ? LOG_LEVEL_ERROR : level == Level::Debug ? LOG_LEVEL_DEBUG : LOG_LEVEL_INFO,
	          file, function, line, "%s", msg);
#else
	(void)level; (void)file; (void)function; (void)line;
	printf("[cpu_6502] %s\n", msg);
#endif
}

} // namespace Log
