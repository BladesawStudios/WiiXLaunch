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
#pragma once

#include <cstdint>

extern "C" {
extern void wiixl_import__wiixl_time__GetMonotonicTicks(uint32_t* hi, uint32_t* lo);
extern uint32_t wiixl_import__wiixl_time__TicksPerSecond(void);
extern uint32_t wiixl_import__wiixl_time__IsWallClockAvailable(void);
extern uint32_t wiixl_import__wiixl_time__GetCalendarTime(int32_t* out10);
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
