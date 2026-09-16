// memcpy, memset, memmove, memcmp, strcmp, strncmp for a freestanding
// module. GCC synthesises calls to the first four for struct assignment,
// array init, and comparison even under -ffreestanding, so a module missing
// them links cleanly (--unresolved-symbols=ignore-all is required for
// imports to stay undefined) and branches to address 0 the first time a
// struct is copied. `<cstring>` declares strcmp/strncmp without defining
// them, so the same failure applies once a mod calls either by name.
//
//     #include <wiixlaunch/mod_runtime.h>
//
// `used` on each: GCC emits these calls itself, after its own dead-code pass
// could see a use, so without the attribute they're stripped as unreferenced.
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

// Overlap-safe, unlike memcpy: GCC picks this one when it can't prove the
// ranges are distinct.
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

// Byte ranges, not NUL-terminated strings.
__attribute__((used))
inline int memcmp(const void* a, const void* b, size_t n) {
    const uint8_t* x = static_cast<const uint8_t*>(a);
    const uint8_t* y = static_cast<const uint8_t*>(b);
    for (size_t i = 0; i < n; ++i) {
        if (x[i] != y[i]) return static_cast<int>(x[i]) - static_cast<int>(y[i]);
    }
    return 0;
}

// Unsigned char comparison: on a target where plain char is signed, a byte
// over 0x7F would sort below ASCII.
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
        if (!x[i]) break;
    }
    return 0;
}

} // extern "C"

namespace wiixl {

// Bounded, not strlen: an unbounded walk over a non-terminated buffer is
// the same failure this header exists to prevent.
inline size_t StrLenBounded(const char* s, size_t cap) {
    size_t n = 0;
    while (s && n < cap && s[n]) ++n;
    return n;
}

} // namespace wiixl
