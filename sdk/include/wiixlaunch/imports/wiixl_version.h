// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// wiixl.version v1.0, 3 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_wiixl_version(Fingerprint); }
//     S::Fingerprint(...);
//
// so a mod that uses two symbols imports two, not all 3.
//
// The comments are the SURFACE's own, carried across - they say why a
// symbol behaves as it does, which is the half a signature cannot.
#pragma once

#include <cstdint>

extern "C" {
extern uint32_t wiixl_import__wiixl_version__Fingerprint(void);

// Null when unrecognised, not the string "unknown" - a literal name would
// let a mod match against every unenrolled build.
extern const char* wiixl_import__wiixl_version__Name(void);

// Whether this host can fingerprint at all, distinct from "it can, and
// this build isn't one it knows."
extern uint32_t wiixl_import__wiixl_version__Configured(void);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require wiixl.version@1.0 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_wiixl_version {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 0;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/framework/modules.md.
#define WXL_USE_wiixl_version(sym) \
    inline decltype(&wiixl_import__wiixl_version__##sym) volatile sym = \
        &wiixl_import__wiixl_version__##sym
