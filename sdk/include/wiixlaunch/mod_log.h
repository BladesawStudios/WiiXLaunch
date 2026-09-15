#pragma once

// WIIXL_LOG for a module. The host's WIIXL_LOG (wiixlaunch/debug_log.hpp)
// talks to the ring buffer, coreinit's OSReport, and Aroma's notification
// module, none of which a mod links against. A mod has wiixl.core:Log,
// which takes one finished string, so this formats into a buffer using the
// same formatter as the host (wiixlaunch/format.hpp, covered by
// tools/format_test) and hands the result to that import.

#include <wiixlaunch/format.hpp>
#include <wiixlaunch/imports/wiixl_core.h>

#include <cstdint>
#include <cstdarg>

namespace WiiXLaunch::ModLog {

constexpr uint32_t kLineMax = 512;

// volatile: the loader writes this pointer at relocation time; without it
// the compiler folds the call into a direct branch that can't reach a host
// address.
using LogFn = void (*)(const char*);
inline LogFn volatile g_Log = &wiixl_import__wiixl_core__Log;

inline void LogFormat(const char* fmt, ...) {
    LogFn log = g_Log;
    if (!log || !fmt) return;

    char line[kLineMax];
    va_list args;
    va_start(args, fmt);
    const uint32_t len = Debug::FormatText(line, sizeof(line) - 1, fmt, args);
    va_end(args);

    line[len < sizeof(line) ? len : sizeof(line) - 1] = '\0';
    log(line);
}

} // namespace WiiXLaunch::ModLog

// Guarded, not unconditional: a translation unit with both should keep the
// one it already had.
#ifndef WIIXL_LOG
#define WIIXL_LOG(...) ::WiiXLaunch::ModLog::LogFormat(__VA_ARGS__)
#endif
