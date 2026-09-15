// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// botw.memory v1.0, 5 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_botw_memory(SupportsAlloc); }
//     S::SupportsAlloc(...);
//
// so a mod that uses two symbols imports two, not all 5.
//
// The comments are the SURFACE's own, carried across - they say why a
// symbol behaves as it does, which is the half a signature cannot.
#pragma once

#include <cstdint>

extern "C" {
extern uint32_t wiixl_import__botw_memory__SupportsAlloc(void);
extern uintptr_t wiixl_import__botw_memory__Alloc(uint32_t size, int32_t align);
extern void wiixl_import__botw_memory__Free(uintptr_t ptr);
extern uintptr_t wiixl_import__botw_memory__GetMainGameHeap(void);

// The module keeps IsPlausibleHeapPtr private, so this asks the question the
// way a caller can: a pointer is plausible if it sits inside the heap the
// module hands out from. Reimplementing the module's private check would be a
// second copy of a rule that could drift; deriving it from GetMainGameHeap
// cannot.
extern uint32_t wiixl_import__botw_memory__IsPlausibleHeapPtr(uintptr_t p);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require botw.memory@1.0 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_botw_memory {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 0;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/framework/modules.md.
#define WXL_USE_botw_memory(sym) \
    inline decltype(&wiixl_import__botw_memory__##sym) volatile sym = \
        &wiixl_import__botw_memory__##sym
