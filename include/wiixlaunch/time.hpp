#pragma once

// System (wall-clock) time.
//
// Wii U (Aroma/WUPS): coreinit is linked, so OSGetTime/OSTicksToCalendarTime
// are just called. That is the console's RTC - the clock the user set in
// System Settings, in local time.
//
// Cemu: the payload is a raw codecave blob, never a real RPL module, so it has
// no import table and coreinit cannot be linked. Both functions are reached
// through the `import.coreinit.<Name>` tail-call shims in
// src/cemu/cemu_time.asm, resolved by Cemu's own patch assembler - the same
// mechanism cemu_logging.asm uses for OSReport. Under Cemu those land on its
// HLE coreinit, which is backed by the host clock, so this reads PC time.
//
// Switch: monotonic only. nn::time is not wired up, so IsWallClockAvailable()
// answers false rather than inventing a date.
//
// Ticks are the raw Espresso timebase, matching coreinit's OSTime, so values
// here interoperate with any OSTime read out of the game.

#include <cstdint>
#include <cstddef>
#include "platform.hpp"

#if WIIXL_WIIU
#include <coreinit/time.h>
#endif

#if WIIXL_CEMU
extern "C" {
    // Patched by scripts/deploy.py at deploy time with the offset of
    // wiixlaunch_cemu_time_shim_table; left at 0 in the compiled ELF, so a
    // zero here means "deploy has not run" and the shims must not be called.
    // `used` because nothing in C++ may reference this: deploy.py writes it
    // and the paired src/cemu/*.asm table reads through it, neither of which
    // the compiler can see. Without it an inline variable no translation unit
    // odr-uses is never emitted, the symbol is absent from the ELF, deploy.py
    // has nothing to patch, and the shim table ships unreachable.
    __attribute__((section(".data"), used)) inline uint32_t g_CemuTimeShimTableOffset = 0;
}
#endif

namespace WiiXLaunch::Time {

using Ticks = int64_t;

#if WIIXL_SWITCH
// CNTFRQ_EL0 - 19.2MHz on all current hardware.
constexpr int64_t kTicksPerSecond = 19200000;
#else
// Espresso timebase = bus clock (248.625MHz) / 4. This is what coreinit's
// OSTimerClockSpeed (OSGetSystemInfo()->busClockSpeed / 4) evaluates to on
// every retail Wii U, and what Cemu uses for its emulated timebase.
constexpr int64_t kTicksPerSecond = 62156250;
#endif

// Layout-compatible with coreinit's OSCalendarTime so it can be passed
// straight to OSTicksToCalendarTime. Field names are shortened; the ranges are
// coreinit's, including mon being 0-based while mday is 1-based.
struct CalendarTime {
    int32_t sec;   // 0-59
    int32_t min;   // 0-59
    int32_t hour;  // 0-23
    int32_t mday;  // 1-31
    int32_t mon;   // 0-11
    int32_t year;  // AD, e.g. 2026
    int32_t wday;  // 0-6, Sunday = 0
    int32_t yday;  // 0-365
    int32_t msec;  // 0-999
    int32_t usec;  // 0-999
};
static_assert(sizeof(CalendarTime) == 0x28, "CalendarTime must match OSCalendarTime");

#if WIIXL_WIIU
static_assert(sizeof(CalendarTime) == sizeof(OSCalendarTime),
              "CalendarTime drifted from the coreinit struct it is cast to");
#endif

// ----------------------------------------------------------------------------
// Monotonic clock
// ----------------------------------------------------------------------------

// Raw timebase. Monotonic and always available - no OS call, no import - but
// it is *not* wall clock: the epoch is unspecified (on Cemu it is roughly
// emulator start). Use it for durations, not dates.
inline Ticks GetMonotonicTicks() {
#if WIIXL_SWITCH
    uint64_t t;
    asm volatile("mrs %0, cntvct_el0" : "=r"(t));
    return static_cast<Ticks>(t);
#else
    // mftb, not mfspr: reading TBL/TBU through mfspr is supervisor-only and
    // would trap. Re-read the upper half to discard a carry landing between
    // the two reads.
    uint32_t hi, lo, hiAgain;
    do {
        asm volatile("mftbu %0" : "=r"(hi));
        asm volatile("mftb  %0" : "=r"(lo));
        asm volatile("mftbu %0" : "=r"(hiAgain));
    } while (hi != hiAgain);
    return (static_cast<Ticks>(hi) << 32) | lo;
#endif
}

constexpr int64_t TicksToSeconds(Ticks t)         { return t / kTicksPerSecond; }
constexpr int64_t TicksToMilliseconds(Ticks t)    { return (t * 1000) / kTicksPerSecond; }
constexpr int64_t TicksToMicroseconds(Ticks t)    { return (t * 1000000) / kTicksPerSecond; }
constexpr Ticks   SecondsToTicks(int64_t s)       { return s * kTicksPerSecond; }
constexpr Ticks   MillisecondsToTicks(int64_t ms) { return (ms * kTicksPerSecond) / 1000; }

// ----------------------------------------------------------------------------
// Wall clock
// ----------------------------------------------------------------------------

namespace detail {

// Rejects a struct we would not want to show a user or do arithmetic on. Also
// the tripwire for a mis-ordered shim table: garbage in these fields is the
// cheapest signal that the entry we called was not the function we wanted.
inline bool IsPlausible(const CalendarTime& t) {
    return t.year >= 2000 && t.year <= 2100 &&
           t.mon  >= 0    && t.mon  <= 11   &&
           t.mday >= 1    && t.mday <= 31   &&
           t.hour >= 0    && t.hour <= 23   &&
           t.min  >= 0    && t.min  <= 59   &&
           t.sec  >= 0    && t.sec  <= 60; // 60 leaves room for a leap second
}

#if WIIXL_CEMU

// Keep this enum's order EXACTLY in sync with wiixlaunch_cemu_time_shim_table
// in src/cemu/cemu_time.asm - each entry here is that table's Nth slot.
enum class CemuTimeImport : uint32_t {
    OSGetTime = 0,
    OSTicksToCalendarTime,
    Count
};

using OSGetTimeFn             = int64_t (*)();
using OSTicksToCalendarTimeFn = void (*)(int64_t, CalendarTime*);

extern "C" uintptr_t g_CodeCaveBase;

inline uintptr_t* CemuTimeShimTable() {
    return reinterpret_cast<uintptr_t*>(g_CodeCaveBase + g_CemuTimeShimTableOffset);
}

template <typename FnPtr>
inline FnPtr ResolveCemuTime(CemuTimeImport fn) {
    if (g_CemuTimeShimTableOffset == 0) return nullptr; // deploy never patched it
    return reinterpret_cast<FnPtr>(CemuTimeShimTable()[static_cast<uint32_t>(fn)]);
}

// Latches false if a resolved call ever returns something implausible, so a
// broken table costs one bad call rather than one per frame forever.
inline bool g_WallClockRejected = false;

#endif // WIIXL_CEMU

} // namespace detail

// True if GetWallClockTicks/GetCalendarTime can actually answer.
inline bool IsWallClockAvailable() {
#if WIIXL_WIIU
    return true;
#elif WIIXL_CEMU
    if (detail::g_WallClockRejected) return false;
    return detail::ResolveCemuTime<detail::OSGetTimeFn>(detail::CemuTimeImport::OSGetTime) != nullptr;
#else
    return false; // Switch: nn::time is not wired up
#endif
}

// Wall-clock time in timebase ticks, i.e. a coreinit OSTime. 0 when
// unavailable. The epoch is coreinit's, so don't do date math on this
// directly - go through GetCalendarTime().
inline Ticks GetWallClockTicks() {
#if WIIXL_WIIU
    return OSGetTime();
#elif WIIXL_CEMU
    auto fn = detail::ResolveCemuTime<detail::OSGetTimeFn>(detail::CemuTimeImport::OSGetTime);
    if (!fn || detail::g_WallClockRejected) return 0;
    return fn();
#else
    return 0;
#endif
}

// Converts an OSTime to broken-down local time. Returns false if the platform
// has no wall clock, or if the result failed the plausibility check.
inline bool TicksToCalendarTime(Ticks ticks, CalendarTime* out) {
    if (!out) return false;
#if WIIXL_WIIU
    OSTicksToCalendarTime(ticks, reinterpret_cast<OSCalendarTime*>(out));
    return detail::IsPlausible(*out);
#elif WIIXL_CEMU
    if (detail::g_WallClockRejected) return false;
    auto fn = detail::ResolveCemuTime<detail::OSTicksToCalendarTimeFn>(
        detail::CemuTimeImport::OSTicksToCalendarTime);
    if (!fn) return false;
    fn(ticks, out);
    if (!detail::IsPlausible(*out)) {
        detail::g_WallClockRejected = true;
        return false;
    }
    return true;
#else
    (void)ticks;
    return false;
#endif
}

// Current local date and time. This is the console clock on hardware and the
// host PC clock under Cemu.
inline bool GetCalendarTime(CalendarTime* out) {
    if (!out) return false;
    if (!IsWallClockAvailable()) return false;
    return TicksToCalendarTime(GetWallClockTicks(), out);
}

// ----------------------------------------------------------------------------
// Conversions and formatting
// ----------------------------------------------------------------------------

// Days since 1970-01-01 for a proleptic Gregorian y/m/d (m is 1-12).
constexpr int64_t DaysFromCivil(int64_t y, int64_t m, int64_t d) {
    y -= (m <= 2);
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;                                   // 0-399
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;  // 0-365
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;           // 0-146096
    return era * 146097 + doe - 719468;
}

// Seconds since the Unix epoch. The Wii U clock is local time and carries no
// zone, so this treats the wall-clock reading as if it were UTC - a convenient
// linear timestamp, not a true UTC instant.
constexpr int64_t ToUnixSeconds(const CalendarTime& t) {
    return DaysFromCivil(t.year, t.mon + 1, t.mday) * 86400 +
           t.hour * 3600 + t.min * 60 + t.sec;
}

namespace detail {

inline char* WriteDigits(char* p, int32_t value, int32_t width) {
    for (int32_t i = width - 1; i >= 0; i--) {
        p[i] = static_cast<char>('0' + (value % 10));
        value /= 10;
    }
    return p + width;
}

} // namespace detail

// Writes "YYYY-MM-DD HH:MM:SS" plus a NUL - 20 bytes. Hand-rolled rather than
// snprintf'd because the Cemu payload has no crt0 and no business pulling in
// stdio. Returns false (writing an empty string) if the buffer is too small.
inline bool FormatDateTime(const CalendarTime& t, char* out, size_t size) {
    if (!out || size == 0) return false;
    if (size < 20) { out[0] = '\0'; return false; }

    char* p = out;
    p = detail::WriteDigits(p, t.year, 4);
    *p++ = '-';
    p = detail::WriteDigits(p, t.mon + 1, 2);
    *p++ = '-';
    p = detail::WriteDigits(p, t.mday, 2);
    *p++ = ' ';
    p = detail::WriteDigits(p, t.hour, 2);
    *p++ = ':';
    p = detail::WriteDigits(p, t.min, 2);
    *p++ = ':';
    p = detail::WriteDigits(p, t.sec, 2);
    *p = '\0';
    return true;
}

// Convenience: current time straight into a caller's buffer. Writes
// "unavailable" if there is no wall clock, so callers can print the result
// unconditionally.
inline bool FormatNow(char* out, size_t size) {
    CalendarTime t;
    if (GetCalendarTime(&t) && FormatDateTime(t, out, size)) return true;

    const char* fallback = "unavailable";
    if (!out || size == 0) return false;
    size_t i = 0;
    for (; fallback[i] != '\0' && i + 1 < size; i++) out[i] = fallback[i];
    out[i] = '\0';
    return false;
}

} // namespace WiiXLaunch::Time
