// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// wiixl.time v1.0, 5 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_wiixl_time(GetMonotonicTicks); }
//     S::GetMonotonicTicks(...);
//
// so a mod that uses two symbols imports two, not all 5.
//
// The comments are the SURFACE's own, carried across - they say why a
// symbol behaves as it does, which is the half a signature cannot.
#pragma once

#include <cstdint>

extern "C" {

// int64 through two uint32s. A 64-bit value crosses this boundary perfectly
// well by ABI, but every other entry in every surface is 32-bit, and one call
// with a different width is one call whose calling convention has to be right
// in a place nobody would think to check.
extern void wiixl_import__wiixl_time__GetMonotonicTicks(uint32_t* hi, uint32_t* lo);
extern uint32_t wiixl_import__wiixl_time__TicksPerSecond(void);
extern uint32_t wiixl_import__wiixl_time__IsWallClockAvailable(void);

// The calendar, flattened. CalendarTime is a struct of ten ints and must not
// cross; the caller passes an int32[10] and the order is fixed here forever:
// sec, min, hour, mday, mon, year, wday, yday, msec, usec.
extern uint32_t wiixl_import__wiixl_time__GetCalendarTime(int32_t* out10);

// "2026-09-05 12:39:29" into a caller-owned buffer, because a mod has no
// formatter of its own and this is the one string every mod wants.
extern uint32_t wiixl_import__wiixl_time__FormatNow(char* out, uint32_t cap);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require wiixl.time@1.0 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_wiixl_time {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 0;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/modules.md.
#define WXL_USE_wiixl_time(sym) \
    inline decltype(&wiixl_import__wiixl_time__##sym) volatile sym = \
        &wiixl_import__wiixl_time__##sym
