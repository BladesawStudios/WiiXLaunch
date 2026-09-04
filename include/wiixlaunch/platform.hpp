#pragma once

#include <cstdint>

// WIIXL_HOST_TEST builds framework logic as an ordinary program on the
// development machine, for the parts that are pure logic rather than console
// plumbing - the .wxlm loader's validation and relocation, principally. Parsing
// attacker-shaped data is ordinary logic, and testing it needs no console.
//
// It is not a fourth platform: nothing ships with it, and no console code is
// reachable under it. It exists so tools/loader_fuzz can run thousands of
// malformed inputs through the real loader in a second.
#if defined(WIIXL_HOST_TEST)
    #define WIIXL_SWITCH 0
    #define WIIXL_WIIU   0
    #define WIIXL_CEMU   0
    #define WIIXL_HOST   1
    #if defined(__LP64__) || defined(_WIN64) || defined(__x86_64__)
        #define WIIXL_64BIT 1
        #define WIIXL_32BIT 0
    #else
        #define WIIXL_64BIT 0
        #define WIIXL_32BIT 1
    #endif
#elif defined(__SWITCH__) || defined(NN_NINTENDO_SDK) || defined(__aarch64__)
    #define WIIXL_SWITCH 1
    #define WIIXL_WIIU   0
    // Defined as 0 rather than left out, like the other two branches do. An
    // undefined macro already evaluates to 0 in #if, so this changes nothing
    // there - but it is what lets WIIXL_CEMU be used in ordinary C++, which
    // `#if` cannot reach (e.g. reporting the platform in a JSON field).
    #define WIIXL_CEMU   0
    #define WIIXL_HOST   0
    #define WIIXL_64BIT  1
    #define WIIXL_32BIT  0
#elif defined(__WIIU__) || defined(WUT) || (defined(__PPC__) && !defined(__CEMU__)) || defined(GEKKO)
    #define WIIXL_SWITCH 0
    #define WIIXL_WIIU   1
    #define WIIXL_CEMU   0
    #define WIIXL_HOST   0
    #define WIIXL_64BIT  0
    #define WIIXL_32BIT  1
#elif defined(__CEMU__)
    #define WIIXL_SWITCH 0
    #define WIIXL_WIIU   0
    #define WIIXL_CEMU   1
    #define WIIXL_HOST   0
    #define WIIXL_64BIT  0
    #define WIIXL_32BIT  1
#else
    #error "WiiXLaunch: Unsupported target platform! Must compile for Switch, Wii U, Cemu, or with -DWIIXL_HOST_TEST."
#endif

namespace WiiXLaunch {

#if WIIXL_SWITCH
    using uptr = uint64_t;
    constexpr bool IsBigEndian = false;
#elif WIIXL_HOST
    using uptr = uintptr_t;
    constexpr bool IsBigEndian = false;
#else
    using uptr = uint32_t;
    constexpr bool IsBigEndian = true;
#endif

inline uint16_t Swap16(uint16_t val) {
    return (val << 8) | (val >> 8);
}

inline uint32_t Swap32(uint32_t val) {
    return ((val << 24) & 0xFF000000) |
           ((val << 8)  & 0x00FF0000) |
           ((val >> 8)  & 0x0000FF00) |
           ((val >> 24) & 0x000000FF);
}

inline uint32_t ToHost32(uint32_t val) {
#if WIIXL_WIIU
    return val;
#else
    return Swap32(val);
#endif
}

}
