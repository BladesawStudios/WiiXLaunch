#pragma once

#include "platform.hpp"
// The formatter is its own header so a module can have it too - a .wxlm cannot
// include this file. tools/format_test checks that header.
#include <wiixlaunch/format.hpp>
#include <cstdint>
#include <cstdarg>

#if WIIXL_SWITCH
#include <lib.hpp>
#include <string_view>
#elif WIIXL_WIIU
#include <notifications/notifications.h>
#elif WIIXL_CEMU
// The coreinit OSReport shim table. This is base-framework plumbing: it used
// to live in the BotW module and get pulled in with __has_include, which meant
// the framework's own logger only reached Cemu's log window when someone
// happened to have vendored a game module. OSReport is coreinit, not BotW.
#include <wiixlaunch/cemu/cemu_logging.hpp>
#endif

// Platform-specific logging: Switch (SvcLogger), Wii U (toast), Cemu (ring buffer).

namespace WiiXLaunch::Debug {

constexpr uint32_t kMaxLogTextLen = 200; // per-call cap, shared by all platforms

#if WIIXL_CEMU

constexpr uint64_t kBufferMagic = 0x5749495847313031ULL;
constexpr uint8_t kEntryStart[4] = { 'W', 'X', '[', '[' };
constexpr uint8_t kEntryEnd[4]   = { 'W', 'X', ']', ']' };
constexpr uint32_t kBufferCapacity = 4096;

struct LogRingBuffer {
    uint64_t magic;

    volatile uint32_t writeIndex;
    uint32_t capacity;
    volatile uint8_t data[kBufferCapacity];
};

inline LogRingBuffer g_DebugLog = { kBufferMagic, 0, kBufferCapacity, {} };

inline void WriteRingEntry(const char* text, int len) {
    uint32_t idx = g_DebugLog.writeIndex;
    auto put = [&](uint8_t b) {
        g_DebugLog.data[idx % kBufferCapacity] = b;
        idx++;
    };

    for (uint8_t b : kEntryStart) put(b);
    for (int i = 0; i < len; i++) put(static_cast<uint8_t>(text[i]));
    for (uint8_t b : kEntryEnd) put(b);

    g_DebugLog.writeIndex = idx;
}

#endif // WIIXL_CEMU



inline void DebugPrint(const char* fmt, ...) {
    char text[kMaxLogTextLen];
    constexpr uint32_t cap = sizeof(text) - 1;

    va_list args;
    va_start(args, fmt);
    uint32_t len = FormatText(text, cap, fmt, args);
    va_end(args);

    text[len] = '\0';
    if (len == 0) return;

#if WIIXL_SWITCH
    ::Logging.Log(std::string_view{ text, static_cast<size_t>(len) });
#elif WIIXL_WIIU
    NotificationModule_AddInfoNotification(text);
#elif WIIXL_CEMU
    WriteRingEntry(text, len);

    // Guard before resolving. CemuLoggingShimTable() is
    // g_CodeCaveBase + g_CemuLoggingShimTableOffset and ResolveCemuLogging
    // dereferences it unconditionally. Both values are zero until deploy.py
    // patches the offset in and the bootstrap computes the base, so calling
    // this before that happens reads through a null pointer and takes the
    // process down. That is reachable two ways: a host build (tools/ws_test
    // compiles this header as the Cemu target), and a log emitted before the
    // codecave base is computed.
    //
    // The ring buffer above is written either way, so nothing is lost when
    // this is skipped - tools that read the ring still see the entry.
    if (WiiXLaunch::Backend::CemuLoggingAvailable()) {
        using OSReportFn = void (*)(const char*, ...);
        auto osReport = WiiXLaunch::Backend::ResolveCemuLogging<OSReportFn>(WiiXLaunch::Backend::CemuLogImport::OSReport);
        if (osReport) {
            // Cemu's OSReport line-buffers and only flushes to the log on
            // '\n', so the newline is not cosmetic.
            osReport("%s\n", text);
        }
    }
#endif
}

}

#define WIIXL_LOG(...) ::WiiXLaunch::Debug::DebugPrint(__VA_ARGS__)
