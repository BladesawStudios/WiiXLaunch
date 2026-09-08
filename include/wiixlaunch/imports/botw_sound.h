// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// botw.sound v1.0, 3 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_botw_sound(Available); }
//     S::Available(...);
//
// so a mod that uses two symbols imports two, not all 3.
#pragma once

#include <cstdint>

extern "C" {
extern uint32_t wiixl_import__botw_sound__Available(void);
extern uint32_t wiixl_import__botw_sound__Play(const char* eventName);
extern uint32_t wiixl_import__botw_sound__DumpEvents(uint32_t maxNames);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require botw.sound@1.0 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_botw_sound {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 0;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/modules.md.
#define WXL_USE_botw_sound(sym) \
    inline decltype(&wiixl_import__botw_sound__##sym) volatile sym = \
        &wiixl_import__botw_sound__##sym
