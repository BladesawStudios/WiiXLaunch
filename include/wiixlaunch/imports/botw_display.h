// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// botw.display v1.0, 4 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_botw_display(SupportsAspectRatio); }
//     S::SupportsAspectRatio(...);
//
// so a mod that uses two symbols imports two, not all 4.
#pragma once

#include <cstdint>

extern "C" {
extern uint32_t wiixl_import__botw_display__SupportsAspectRatio(void);
extern uint32_t wiixl_import__botw_display__GetAspectRatio(float* out);
extern uint32_t wiixl_import__botw_display__GetAspectTerms(float aspect, int32_t* w, int32_t* h);
extern uint32_t wiixl_import__botw_display__IsUltrawide(void);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require botw.display@1.0 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_botw_display {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 0;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/modules.md.
#define WXL_USE_botw_display(sym) \
    inline decltype(&wiixl_import__botw_display__##sym) volatile sym = \
        &wiixl_import__botw_display__##sym
