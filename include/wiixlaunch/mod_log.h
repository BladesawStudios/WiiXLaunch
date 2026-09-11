#pragma once

// WIIXL_LOG, for a module.
//
// The host's WIIXL_LOG lives in wiixlaunch/debug_log.hpp, which a .wxlm cannot
// include: that header talks to the ring buffer, coreinit's OSReport and
// Aroma's notification module, none of which a module links against. What a
// module has is wiixl.core:Log, which takes one finished string.
//
// So this is the other half - the same formatter the host uses, from
// wiixlaunch/format.hpp, writing into a buffer that is then handed to the
// import. A mod that already says WIIXL_LOG("x %d", n) keeps saying it.
//
// WHY THE SAME FORMATTER AND NOT A SMALLER ONE. The first module-side logger
// written for this project supported %s %u %x %d %c and stopped there, which
// is fine until a mod says %p or %.2f - and the first mod ported after it used
// both, 34 times. tools/format_test covers format.hpp, so sharing it means the
// module path is covered by the same 23 cases rather than by nothing.
//
// LINE LENGTH. wiixl.core:Log takes a pointer, and the host's ring buffer has
// its own bound; anything longer than this is truncated here rather than
// somewhere less visible. Matches the host's kMaxLogTextLen.

#include <wiixlaunch/format.hpp>
#include <wiixlaunch/imports/wiixl_core.h>

#include <cstdint>
#include <cstdarg>

namespace WiiXLaunch::ModLog {

constexpr uint32_t kLineMax = 512;

// volatile for the reason in docs/modules.md: the loader writes this pointer at
// relocation time, and without volatile the compiler folds it into a direct
// branch that cannot reach a host address.
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

// The host spells it WIIXL_LOG and so does every mod written against the host.
// Guarded rather than unconditional: a translation unit that somehow has both
// should keep the one it already had rather than have this quietly win.
#ifndef WIIXL_LOG
#define WIIXL_LOG(...) ::WiiXLaunch::ModLog::LogFormat(__VA_ARGS__)
#endif
