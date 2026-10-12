#pragma once

// wiixl.rumble v1 - the GamePad's motor.
//
// The motor is on or off, nothing between: VPADControlMotor plays a pattern of bits at 120 Hz, at most 120 of them (one
// second). Strength here is therefore duty cycle - at 0.5, every other bit - spread evenly so it reads as a weaker buzz
// rather than a stutter, and it may ramp from one strength to another across the rumble, so a hit can land hard and
// trail off. A new rumble replaces whatever is still playing, the game's included.
//
// vpad.rpl, like sndcore2 for wiixl.audio, is reached through the dynamic loader (cemu_dynload.hpp). Cemu only for now;
// the surface is not registered elsewhere. Under Cemu the GamePad's motor is whatever controller the user mapped to it.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/debug_log.hpp>

#if WIIXL_CEMU
#include <wiixlaunch/cemu/cemu_dynload.hpp>
#endif

#include <cstdint>

namespace WiiXLaunch::RumbleSurface {

constexpr const char* kName = "wiixl.rumble";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

#if WIIXL_CEMU
constexpr bool kSupported = true;
#else
constexpr bool kSupported = false;
#endif

namespace impl {

constexpr uint32_t kBitsPerSecond = 120;
constexpr uint32_t kMaxBits = 120;
constexpr uint32_t kPatternBytes = 15;      // VPADControlMotor always reads fifteen
constexpr int32_t kChannel = 0;            // the first GamePad

using FnControlMotor = int32_t (*)(int32_t chan, const uint8_t* pattern, uint8_t length);
using FnStopMotor = void (*)(int32_t chan);

inline FnControlMotor g_ControlMotor = nullptr;
inline FnStopMotor g_StopMotor = nullptr;
inline bool g_Resolved = false;

inline bool Resolve() {
#if WIIXL_CEMU
    if (g_Resolved) return g_ControlMotor && g_StopMotor;
    g_Resolved = true;
    if (!Backend::CemuDynLoadAvailable()) {
        WIIXL_LOG("Rumble: the coreinit dynamic-loader shims are not in this payload, so vpad cannot be reached");
        return false;
    }
    Backend::OSDynLoadModule mod = Backend::CemuAcquireRpl("vpad.rpl");
    if (!mod) {
        WIIXL_LOG("Rumble: OSDynLoad_Acquire(vpad.rpl) failed - no rumble");
        return false;
    }
    g_ControlMotor = reinterpret_cast<FnControlMotor>(Backend::CemuFindRplExport(mod, "VPADControlMotor"));
    g_StopMotor = reinterpret_cast<FnStopMotor>(Backend::CemuFindRplExport(mod, "VPADStopMotor"));
    if (!g_ControlMotor || !g_StopMotor) {
        WIIXL_LOG("Rumble: vpad.rpl lacks VPADControlMotor or VPADStopMotor - no rumble");
        g_ControlMotor = nullptr;
        g_StopMotor = nullptr;
        return false;
    }
    WIIXL_LOG("Rumble: vpad.rpl resolved through the dynamic loader");
    return true;
#else
    return false;
#endif
}

inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// 1 when the motor can be driven here.
extern "C" inline uint32_t RumbleAvailable() { return Resolve() ? 1u : 0u; }

// Rumbles the GamePad for `milliseconds` (up to 1000), its strength ramping linearly from `strengthStart` to
// `strengthEnd` (each 0..1). Returns 1 if the pattern was sent.
extern "C" inline uint32_t RumbleRumble(uint32_t milliseconds, float strengthStart, float strengthEnd) {
    if (!Resolve()) return 0;
    uint32_t bits = (milliseconds * kBitsPerSecond + 999u) / 1000u;
    if (bits == 0) return 0;
    if (bits > kMaxBits) bits = kMaxBits;
    const float a = Clamp01(strengthStart);
    const float b = Clamp01(strengthEnd);

    // Bit i is on when the running total of strength crosses a whole number in it, so the on bits are spread as evenly
    // as the strength allows. First bit first, most significant bit of each byte first.
    uint8_t pattern[kPatternBytes] = {};
    float total = 0.0f;
    for (uint32_t i = 0; i < bits; ++i) {
        const float t = bits > 1 ? static_cast<float>(i) / static_cast<float>(bits - 1) : 0.0f;
        const float before = total;
        total += a + (b - a) * t;
        if (static_cast<uint32_t>(total) != static_cast<uint32_t>(before)) {
            pattern[i >> 3] |= static_cast<uint8_t>(0x80u >> (i & 7u));
        }
    }
    return g_ControlMotor(kChannel, pattern, static_cast<uint8_t>(bits)) >= 0 ? 1u : 0u;
}

extern "C" inline uint32_t RumbleStop() {
    if (!Resolve()) return 0;
    g_StopMotor(kChannel);
    return 1;
}

inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("Available", &RumbleAvailable),
    WIIXL_SURFACE_SYMBOL("Rumble",    &RumbleRumble),
    WIIXL_SURFACE_SYMBOL("Stop",      &RumbleStop),
};

} // namespace impl

inline bool Register() {
    if constexpr (!kSupported) {
        WIIXL_LOG("Surface: wiixl.rumble NOT registered - no VPAD path on this platform yet. A mod requiring it will "
                  "be refused by name.");
        return false;
    }
    Surface::Registration reg{};
    reg.name = kName;
    reg.versionMajor = kVersionMajor;
    reg.versionMinor = kVersionMinor;
    reg.symbols = impl::kSymbols;
    reg.symbolCount = static_cast<uint32_t>(sizeof(impl::kSymbols) / sizeof(impl::kSymbols[0]));
    return Surface::Register(reg);
}

} // namespace WiiXLaunch::RumbleSurface
