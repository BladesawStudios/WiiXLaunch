#pragma once

#include "platform.hpp"
#include <cstdint>
#include <cstdarg>

#if WIIXL_SWITCH
#include <lib.hpp>
#include <string_view>
#elif WIIXL_WIIU
#include <notifications/notifications.h>
#elif WIIXL_CEMU
// Optional: only present when the BotW module (vendor/wiixlaunch-botw) is
// installed. Base WiiXLaunch's own ring buffer below works without it -
// this just adds a relay to the real OSReport when it's available.
#if __has_include(<wiixlaunch/botw/platform/cemu_logging.hpp>)
#include <wiixlaunch/botw/platform/cemu_logging.hpp>
#define WIIXL_HAS_BOTW_CEMU_LOGGING 1
#endif
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

// Libc-free formatter (no crt0 in Cemu); supports %s, %p, %d, %u, %x/%X, %f, %%.
namespace impl {

inline void AppendChar(char* buf, uint32_t& len, uint32_t cap, char c) {
    if (len < cap) buf[len++] = c;
}

inline void AppendStr(char* buf, uint32_t& len, uint32_t cap, const char* s) {
    if (!s) s = "(null)";
    while (*s) AppendChar(buf, len, cap, *s++);
}

inline void AppendUInt(char* buf, uint32_t& len, uint32_t cap, unsigned long long value, int base, bool upper) {
    char digits[24];
    int n = 0;
    if (value == 0) digits[n++] = '0';
    while (value != 0) {
        int d = static_cast<int>(value % static_cast<unsigned>(base));
        digits[n++] = d < 10 ? static_cast<char>('0' + d) : static_cast<char>((upper ? 'A' : 'a') + d - 10);
        value /= static_cast<unsigned>(base);
    }
    while (n > 0) AppendChar(buf, len, cap, digits[--n]);
}

inline void AppendInt(char* buf, uint32_t& len, uint32_t cap, long long value) {
    if (value < 0) {
        AppendChar(buf, len, cap, '-');
        AppendUInt(buf, len, cap, static_cast<unsigned long long>(-value), 10, false);
    } else {
        AppendUInt(buf, len, cap, static_cast<unsigned long long>(value), 10, false);
    }
}

inline void AppendFloat(char* buf, uint32_t& len, uint32_t cap, double value, int precision) {
    if (precision < 0) precision = 6;
    if (precision > 9) precision = 9; // keeps pow10 well within unsigned long long range
    if (value < 0) {
        AppendChar(buf, len, cap, '-');
        value = -value;
    }
    unsigned long long pow10 = 1;
    for (int i = 0; i < precision; i++) pow10 *= 10;
    unsigned long long scaled = static_cast<unsigned long long>(value * static_cast<double>(pow10) + 0.5);
    unsigned long long intPart = scaled / pow10;
    AppendUInt(buf, len, cap, intPart, 10, false);
    if (precision > 0) {
        AppendChar(buf, len, cap, '.');
        unsigned long long fracPart = scaled - intPart * pow10;
        unsigned long long divisor = pow10 / 10;
        for (int i = 0; i < precision; i++) {
            unsigned long long digit = (fracPart / divisor) % 10;
            AppendChar(buf, len, cap, static_cast<char>('0' + digit));
            divisor /= 10;
        }
    }
}

}

// Shared libc-free formatter; cap should leave room for null terminator.
inline uint32_t FormatText(char* text, uint32_t cap, const char* fmt, va_list args) {
    uint32_t len = 0;

    for (const char* p = fmt; *p != '\0'; p++) {
        if (*p != '%') {
            impl::AppendChar(text, len, cap, *p);
            continue;
        }
        p++;
        if (*p == '\0') break;
        if (*p == '%') {
            impl::AppendChar(text, len, cap, '%');
            continue;
        }

        int precision = -1;
        if (*p == '.') {
            p++;
            precision = 0;
            while (*p >= '0' && *p <= '9') {
                precision = precision * 10 + (*p - '0');
                p++;
            }
        }

        switch (*p) {
            case 'd': case 'i': impl::AppendInt(text, len, cap, va_arg(args, int)); break;
            case 'u': impl::AppendUInt(text, len, cap, va_arg(args, unsigned int), 10, false); break;
            case 'x': impl::AppendUInt(text, len, cap, va_arg(args, unsigned int), 16, false); break;
            case 'X': impl::AppendUInt(text, len, cap, va_arg(args, unsigned int), 16, true); break;
            case 'p':
                impl::AppendStr(text, len, cap, "0x");
                impl::AppendUInt(text, len, cap, reinterpret_cast<uintptr_t>(va_arg(args, void*)), 16, false);
                break;
            case 's': impl::AppendStr(text, len, cap, va_arg(args, const char*)); break;
            case 'f': impl::AppendFloat(text, len, cap, va_arg(args, double), precision); break;
            default:
                impl::AppendChar(text, len, cap, '%');
                impl::AppendChar(text, len, cap, *p);
                break;
        }
    }

    return len;
}

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

#if WIIXL_HAS_BOTW_CEMU_LOGGING
    // Guard before resolving. CemuLoggingShimTable() is
    // g_CodeCaveBase + g_CemuLoggingShimTableOffset and ResolveCemuLogging
    // dereferences it unconditionally - unlike cemu_net.hpp's ResolveCemuNet,
    // which checks CemuNetAvailable() first. Both values are zero until
    // deploy.py patches them, so calling this before that happens reads
    // through a null pointer and takes the process down. That is reachable
    // three ways: a host build (tools/ws_test compiles this header as the Cemu
    // target), a log emitted before the codecave base is computed, and a build
    // with src/cemu/cemu_logging.asm removed.
    //
    // The ring buffer above is written either way, so nothing is lost when
    // this is skipped - tools that read the ring still see the entry.
    if (WiiXLaunch::Backend::g_CodeCaveBase != 0 && ::g_CemuLoggingShimTableOffset != 0) {
        using OSReportFn = void (*)(const char*, ...);
        auto osReport = WiiXLaunch::Backend::ResolveCemuLogging<OSReportFn>(WiiXLaunch::Backend::CemuLogImport::OSReport);
        if (osReport) {
            osReport("%s\n", text);
        }
    }
#endif // WIIXL_HAS_BOTW_CEMU_LOGGING
#endif
}

}

#define WIIXL_LOG(...) ::WiiXLaunch::Debug::DebugPrint(__VA_ARGS__)
