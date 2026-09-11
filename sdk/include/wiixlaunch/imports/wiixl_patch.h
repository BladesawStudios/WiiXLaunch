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

// 1 on success. 0 when the bytes at the address are not what the caller said
// they would be - which is the version guard doing its job, not a failure of
// this call.
// THROUGH THE PATCH REGISTRY, not past it.
//
// This used to verify the origin bytes and then write, recording nothing. So a
// runtime patch got one of the four checks a DECLARED patch gets: it could land
// inside the 16 bytes a hook displaced, or on top of another module's patch, or
// into the module arena, and none of those were looked at - while the surface
// sat next to Patches, which does all of them and names the other party.
//
// That gap is the reason a mod wanting a CONFIGURABLE patch had to choose
// between the collision report and the config: a declared patch cannot depend on
// a number read at runtime, and a runtime patch was unregistered. It no longer
// is - Patches::ApplyAt checks and records, so two mods rewriting the same
// instruction collide by name whichever way either of them wrote it.
extern uint32_t wiixl_import__wiixl_patch__Write(uintptr_t addr, const void* data, uint32_t size, const void* origin, uint32_t originSize);
extern uint32_t wiixl_import__wiixl_patch__WriteUnchecked(uintptr_t addr, const void* data, uint32_t size);

// Reads bytes back out of game memory, which is how a mod verifies its own
// write rather than trusting the return value. The sixth rule in
// docs/modules.md, made available to mods.
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
// address - the module fails to relocate. See docs/modules.md.
#define WXL_USE_wiixl_patch(sym) \
    inline decltype(&wiixl_import__wiixl_patch__##sym) volatile sym = \
        &wiixl_import__wiixl_patch__##sym
