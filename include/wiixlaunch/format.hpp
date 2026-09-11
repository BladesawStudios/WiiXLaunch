#pragma once

// WIIXL_LOG's formatter, and a mod's too.
//
// The Cemu payload has no crt0 and a .wxlm has no libc at all, so there is
// no printf to call - this is it. It lived inside debug_log.hpp, which a
// module cannot include: that header reaches for the ring buffer, coreinit's
// OSReport and Aroma's notification module, none of which a module links
// against.
//
// EXTRACTED RATHER THAN COPIED. A second implementation is a second thing to
// get wrong, and the first one is already tested - tools/format_test checks
// this code, 23 cases, added after a width specifier was found printing
// itself literally and consuming no argument.
//
// Freestanding: <cstdint> and <cstdarg>, nothing else.

#include <cstdint>
#include <cstdarg>

namespace WiiXLaunch::Debug {

// Libc-free formatter (no crt0 in Cemu); supports %s, %p, %d, %u, %x/%X, %f, %%.
namespace impl {

inline void AppendChar(char* buf, uint32_t& len, uint32_t cap, char c) {
    if (len < cap) buf[len++] = c;
}

inline void AppendStr(char* buf, uint32_t& len, uint32_t cap, const char* s) {
    if (!s) s = "(null)";
    while (*s) AppendChar(buf, len, cap, *s++);
}

inline void AppendUInt(char* buf, uint32_t& len, uint32_t cap, unsigned long long value, int base, bool upper,
                       int width = 0, bool zeroPad = false) {
    char digits[24];
    int n = 0;
    if (value == 0) digits[n++] = '0';
    while (value != 0) {
        int d = static_cast<int>(value % static_cast<unsigned>(base));
        digits[n++] = d < 10 ? static_cast<char>('0' + d) : static_cast<char>((upper ? 'A' : 'a') + d - 10);
        value /= static_cast<unsigned>(base);
    }
    for (int i = n; i < width; ++i) AppendChar(buf, len, cap, zeroPad ? '0' : ' ');
    while (n > 0) AppendChar(buf, len, cap, digits[--n]);
}

inline void AppendInt(char* buf, uint32_t& len, uint32_t cap, long long value,
                      int width = 0, bool zeroPad = false) {
    if (value < 0) {
        AppendChar(buf, len, cap, '-');
        AppendUInt(buf, len, cap, static_cast<unsigned long long>(-value), 10, false,
                   width > 0 ? width - 1 : 0, zeroPad);
    } else {
        AppendUInt(buf, len, cap, static_cast<unsigned long long>(value), 10, false, width, zeroPad);
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

        // Minimum field width, with an optional leading-zero flag: %02X, %8d.
        //
        // These used to fall through to the default branch below, which emits
        // the '%' and one following character literally and consumes no
        // argument - so "%02X" printed as the four characters %02X and the
        // value was silently dropped. Nothing crashed (an unconsumed vararg is
        // harmless) and nothing complained; a hex dump just came out as format
        // specifiers. Width applies to the integer conversions only; %s and %f
        // ignore it.
        bool zeroPad = false;
        int width = 0;
        if (*p == '0') { zeroPad = true; p++; }
        while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); p++; }

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
            case 'd': case 'i': impl::AppendInt(text, len, cap, va_arg(args, int), width, zeroPad); break;
            case 'u': impl::AppendUInt(text, len, cap, va_arg(args, unsigned int), 10, false, width, zeroPad); break;
            case 'x': impl::AppendUInt(text, len, cap, va_arg(args, unsigned int), 16, false, width, zeroPad); break;
            case 'X': impl::AppendUInt(text, len, cap, va_arg(args, unsigned int), 16, true, width, zeroPad); break;
            case 'p':
                impl::AppendStr(text, len, cap, "0x");
                impl::AppendUInt(text, len, cap, reinterpret_cast<uintptr_t>(va_arg(args, void*)), 16, false);
                break;
            case 's': impl::AppendStr(text, len, cap, va_arg(args, const char*)); break;
            case 'f': impl::AppendFloat(text, len, cap, va_arg(args, double), precision); break;
            default:
                // Unknown conversion. Echo it rather than guessing, and note
                // that no argument is consumed - so anything after this in the
                // same call reads the wrong vararg. Better visibly wrong than
                // quietly wrong.
                impl::AppendChar(text, len, cap, '%');
                impl::AppendChar(text, len, cap, *p);
                break;
        }
    }

    // TRUNCATION SAYS SO. Running out of buffer used to end the line
    // mid-sentence and look like a message that simply ended there - which for
    // a diagnostic is the worst possible failure, because the half that gets
    // discarded is the half explaining what to do. A boot cut two patch
    // refusals off at "not the game; the" and "would corrupt a func", and
    // neither read as truncated.
    //
    // A static check on format strings (scripts/test_log_lengths.py) catches
    // the literals, but it can only be a lower bound: one %s can be arbitrarily
    // long. This is the half that catches what the gate cannot, and it catches
    // it where it happens.
    if (len >= cap) {
        const char* mark = "[..CUT]";
        uint32_t at = (cap > 7u) ? (cap - 7u) : 0u;
        for (uint32_t i = 0; mark[i] && at < cap; ++i, ++at) text[at] = mark[i];
        len = cap;
    }

    return len;
}

} // namespace WiiXLaunch::Debug
