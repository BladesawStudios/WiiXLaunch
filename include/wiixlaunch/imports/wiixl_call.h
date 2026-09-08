// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// wiixl.call v1.0, 2 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_wiixl_call(ResolveTarget); }
//     S::ResolveTarget(...);
//
// so a mod that uses two symbols imports two, not all 2.
#pragma once

#include <cstdint>

extern "C" {
extern uintptr_t wiixl_import__wiixl_call__ResolveTarget(uintptr_t switchOffset, uintptr_t wiiuOffset);
extern uintptr_t wiixl_import__wiixl_call__ImageBase(void);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require wiixl.call@1.0 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_wiixl_call {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 0;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/modules.md.
#define WXL_USE_wiixl_call(sym) \
    inline decltype(&wiixl_import__wiixl_call__##sym) volatile sym = \
        &wiixl_import__wiixl_call__##sym
