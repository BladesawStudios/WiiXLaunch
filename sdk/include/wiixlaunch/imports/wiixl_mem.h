// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// wiixl.mem v1.0, 5 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_wiixl_mem(ShimsAvailable); }
//     S::ShimsAvailable(...);
//
// so a mod that uses two symbols imports two, not all 5.
//
// The comments are the SURFACE's own, carried across - they say why a
// symbol behaves as it does, which is the half a signature cannot.
#pragma once

#include <cstdint>

extern "C" {
extern uint32_t wiixl_import__wiixl_mem__ShimsAvailable(void);

// heap: 0 MEM1, 1 MEM2, 2 FG - matching Mem::BaseHeap. Returned as an opaque
// address; a mod passes it back and never dereferences it.
extern uintptr_t wiixl_import__wiixl_mem__GetBaseHeapHandle(uint32_t heap);
extern uint32_t wiixl_import__wiixl_mem__GetAllocatableSize(uintptr_t heap, int32_t align);
extern uintptr_t wiixl_import__wiixl_mem__AllocFromExpHeap(uintptr_t heap, uint32_t size, int32_t align);
extern void wiixl_import__wiixl_mem__FreeToExpHeap(uintptr_t heap, uintptr_t block);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require wiixl.mem@1.0 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_wiixl_mem {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 0;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/modules.md.
#define WXL_USE_wiixl_mem(sym) \
    inline decltype(&wiixl_import__wiixl_mem__##sym) volatile sym = \
        &wiixl_import__wiixl_mem__##sym
