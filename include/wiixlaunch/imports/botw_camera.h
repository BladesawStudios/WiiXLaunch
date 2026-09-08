// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// botw.camera v1.0, 6 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_botw_camera(GetPosition); }
//     S::GetPosition(...);
//
// so a mod that uses two symbols imports two, not all 6.
#pragma once

#include <cstdint>

extern "C" {
extern uint32_t wiixl_import__botw_camera__GetPosition(uintptr_t camera, float* out3);
extern uint32_t wiixl_import__botw_camera__SetPosition(uintptr_t camera, float x, float y, float z);
extern uint32_t wiixl_import__botw_camera__GetLookAt(uintptr_t camera, float* out3);
extern uint32_t wiixl_import__botw_camera__SetLookAt(uintptr_t camera, float x, float y, float z);
extern uint32_t wiixl_import__botw_camera__GetUp(uintptr_t camera, float* out3);
extern uint32_t wiixl_import__botw_camera__SetUp(uintptr_t camera, float x, float y, float z);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require botw.camera@1.0 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_botw_camera {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 0;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/modules.md.
#define WXL_USE_botw_camera(sym) \
    inline decltype(&wiixl_import__botw_camera__##sym) volatile sym = \
        &wiixl_import__botw_camera__##sym
