// The handful of functions every non-trivial .wxlm needs and cannot get
// anywhere.
//
// A module is compiled -ffreestanding -nostdlib, so nothing defines memcpy.
// GCC does not care: it SYNTHESISES calls to memcpy, memset, memmove and memcmp
// for struct assignment, array initialisation and comparison, even under
// -ffreestanding, because they are part of the freestanding contract it assumes
// the target provides.
//
// And build_mod.py links with --unresolved-symbols=ignore-all, which it has to -
// that is what lets an import stay undefined until the loader resolves it. So a
// module missing memcpy LINKS CLEANLY and branches to address 0 the first time
// a struct is copied. No build error, no load error; a crash somewhere unrelated
// to the line that caused it.
//
// Including this header is the whole fix. It is header-only and a module is one
// translation unit, so there is nothing to link and no way to include it twice.
//
//     #include <wiixlaunch/mod_runtime.h>
//
// `used` on each: nothing in a module references these by name. GCC emits the
// calls itself, after the point where its own dead-code pass could see a use,
// so without the attribute they are removed as unreferenced and the problem
// comes back with the fix apparently applied.
#pragma once

#include <cstdint>
#include <cstddef>

extern "C" {

__attribute__((used))
inline void* memcpy(void* dst, const void* src, size_t n) {
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}

__attribute__((used))
inline void* memset(void* dst, int value, size_t n) {
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t v = static_cast<uint8_t>(value);
    for (size_t i = 0; i < n; ++i) d[i] = v;
    return dst;
}

// Overlap-safe, unlike memcpy. GCC picks this one when it cannot prove the
// ranges are distinct, so a module that defines only memcpy still branches to
// zero on exactly the copies that were ambiguous.
__attribute__((used))
inline void* memmove(void* dst, const void* src, size_t n) {
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    if (d == s || n == 0) return dst;
    if (d < s) {
        for (size_t i = 0; i < n; ++i) d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; --i) d[i - 1] = s[i - 1];
    }
    return dst;
}

// Byte ranges, not NUL-terminated strings. A buffer that came off the wire or
// off disk is not NUL-terminated, and pretending otherwise is how a parser
// reads past what actually arrived.
__attribute__((used))
inline int memcmp(const void* a, const void* b, size_t n) {
    const uint8_t* x = static_cast<const uint8_t*>(a);
    const uint8_t* y = static_cast<const uint8_t*>(b);
    for (size_t i = 0; i < n; ++i) {
        if (x[i] != y[i]) return static_cast<int>(x[i]) - static_cast<int>(y[i]);
    }
    return 0;
}

// strcmp and strncmp, which <cstring> DECLARES and nothing defines.
//
// Not synthesised by the compiler the way memcpy is - a module has to call
// these by name - but the ending is identical: <cstring> is available under
// -ffreestanding, so `std::strcmp` compiles, and the link lets it stay
// undefined. AIPuppet calls strcmp seventeen times to compare BotW AI state
// names, and every one of them would have branched to 0.
//
// UNSIGNED CHAR. The sign of the result is the entire contract, and on a
// target where plain char is signed - PowerPC is the other way, AArch64 this
// way - comparing as char makes any byte over 0x7F sort BELOW ASCII. Actor
// names are ASCII today; the first UTF-8 one would invert a comparison
// silently.
__attribute__((used))
inline int strcmp(const char* a, const char* b) {
    const unsigned char* x = reinterpret_cast<const unsigned char*>(a);
    const unsigned char* y = reinterpret_cast<const unsigned char*>(b);
    while (*x && *x == *y) { ++x; ++y; }
    return static_cast<int>(*x) - static_cast<int>(*y);
}

__attribute__((used))
inline int strncmp(const char* a, const char* b, size_t n) {
    const unsigned char* x = reinterpret_cast<const unsigned char*>(a);
    const unsigned char* y = reinterpret_cast<const unsigned char*>(b);
    for (size_t i = 0; i < n; ++i) {
        if (x[i] != y[i]) return static_cast<int>(x[i]) - static_cast<int>(y[i]);
        if (!x[i]) break;               // both ended; the rest of n is not read
    }
    return 0;
}

} // extern "C"

namespace wiixl {

// Length of a NUL-terminated string, bounded. Not strlen: an unbounded walk
// over something that turned out not to be terminated is the same failure this
// header exists to prevent, one level up.
inline size_t StrLenBounded(const char* s, size_t cap) {
    size_t n = 0;
    while (s && n < cap && s[n]) ++n;
    return n;
}

} // namespace wiixl
