// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// totk.camera v1.0, 11 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_totk_camera(Supported); }
//     S::Supported(...);
//
// so a mod that uses two symbols imports two, not all 11.
//
// The comments are the SURFACE's own, carried across - they say why a
// symbol behaves as it does, which is the half a signature cannot.
#pragma once

#include <cstdint>

extern "C" {
extern uint32_t wiixl_import__totk_camera__Supported(void);
extern uint32_t wiixl_import__totk_camera__IsPresent(void);
extern uint32_t wiixl_import__totk_camera__UpdateCount(void);
extern uint32_t wiixl_import__totk_camera__GetPosition(float* out3);
extern uint32_t wiixl_import__totk_camera__GetLookAt(float* out3);
extern uint32_t wiixl_import__totk_camera__GetUp(float* out3);
extern uint32_t wiixl_import__totk_camera__GetForward(float* out3);
extern uint32_t wiixl_import__totk_camera__Override(const float* pos3, const float* at3, const float* up3, uint32_t frames);
extern uint32_t wiixl_import__totk_camera__HoldForever(void);
extern void wiixl_import__totk_camera__Release(void);
extern uint32_t wiixl_import__totk_camera__IsOverridden(void);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require totk.camera@1.0 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_totk_camera {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 0;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/framework/modules.md.
#define WXL_USE_totk_camera(sym) \
    inline decltype(&wiixl_import__totk_camera__##sym) volatile sym = \
        &wiixl_import__totk_camera__##sym
