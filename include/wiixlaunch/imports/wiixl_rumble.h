// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// wiixl.rumble v1.0, 3 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_wiixl_rumble(Available); }
//     S::Available(...);
//
// so a mod that uses two symbols imports two, not all 3.
//
// The comments are the SURFACE's own, carried across - they say why a
// symbol behaves as it does, which is the half a signature cannot.
#pragma once

#include <cstdint>

extern "C" {

// 1 when the motor can be driven here.
extern uint32_t wiixl_import__wiixl_rumble__Available(void);

// Rumbles the GamePad for `milliseconds` (up to 1000), its strength ramping linearly from `strengthStart` to
// `strengthEnd` (each 0..1). Returns 1 if the pattern was sent.
extern uint32_t wiixl_import__wiixl_rumble__Rumble(uint32_t milliseconds, float strengthStart, float strengthEnd);
extern uint32_t wiixl_import__wiixl_rumble__Stop(void);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require wiixl.rumble@1.0 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_wiixl_rumble {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 0;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/framework/modules.md.
#define WXL_USE_wiixl_rumble(sym) \
    inline decltype(&wiixl_import__wiixl_rumble__##sym) volatile sym = \
        &wiixl_import__wiixl_rumble__##sym
