// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// botw.vfx v1.0, 5 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_botw_vfx(SupportsVfx); }
//     S::SupportsVfx(...);
//
// so a mod that uses two symbols imports two, not all 5.
//
// The comments are the SURFACE's own, carried across - they say why a
// symbol behaves as it does, which is the half a signature cannot.
#pragma once

#include <cstdint>

extern "C" {
extern uint32_t wiixl_import__botw_vfx__SupportsVfx(void);
extern uint32_t wiixl_import__botw_vfx__Spawn(const char* esetlistPath, float x, float y, float z, float sx, float sy, float sz, float rx, float ry, float rz);
extern uint32_t wiixl_import__botw_vfx__Update(uint32_t handle, float x, float y, float z, float sx, float sy, float sz, float rx, float ry, float rz);
extern uint32_t wiixl_import__botw_vfx__IsValid(uint32_t handle);

// Stops the effect AND releases the slot, so the mod's handle goes stale in the
// same call. An effect that has been stopped is not something a later Update
// should quietly do nothing to.
extern uint32_t wiixl_import__botw_vfx__Stop(uint32_t handle);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require botw.vfx@1.0 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_botw_vfx {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 0;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/modules.md.
#define WXL_USE_botw_vfx(sym) \
    inline decltype(&wiixl_import__botw_vfx__##sym) volatile sym = \
        &wiixl_import__botw_vfx__##sym
