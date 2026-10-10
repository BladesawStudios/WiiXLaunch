// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// wiixl.patch v1.0, 3 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_wiixl_patch(Write); }
//     S::Write(...);
//
// so a mod that uses two symbols imports two, not all 3.
//
// The comments are the SURFACE's own, carried across - they say why a
// symbol behaves as it does, which is the half a signature cannot.
#pragma once

#include <cstdint>

extern "C" {

// 1 on success. 0 when the bytes at the address aren't what the caller
// said - the version guard doing its job. Goes through Patches::ApplyAt
// (not around it), so a runtime patch gets the same hooked-window and
// patch-overlap checks a declared patch gets, and two mods rewriting the
// same instruction collide by name either way.
extern uint32_t wiixl_import__wiixl_patch__Write(uintptr_t addr, const void* data, uint32_t size, const void* origin, uint32_t originSize);
extern uint32_t wiixl_import__wiixl_patch__WriteUnchecked(uintptr_t addr, const void* data, uint32_t size);

// Reads bytes back out of game memory, so a mod can verify its own write
// rather than trust the return value.
extern uint32_t wiixl_import__wiixl_patch__Read(uintptr_t addr, void* out, uint32_t size);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require wiixl.patch@1.0 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_wiixl_patch {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 0;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/framework/modules.md.
#define WXL_USE_wiixl_patch(sym) \
    inline decltype(&wiixl_import__wiixl_patch__##sym) volatile sym = \
        &wiixl_import__wiixl_patch__##sym
