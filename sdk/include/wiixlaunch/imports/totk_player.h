// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// totk.player v1.0, 14 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_totk_player(Supported); }
//     S::Supported(...);
//
// so a mod that uses two symbols imports two, not all 14.
//
// The comments are the SURFACE's own, carried across - they say why a
// symbol behaves as it does, which is the half a signature cannot.
#pragma once

#include <cstdint>

extern "C" {
extern uint32_t wiixl_import__totk_player__Supported(void);
extern uint32_t wiixl_import__totk_player__IsPresent(void);
extern uint32_t wiixl_import__totk_player__UpdateCount(void);

// x, y, z through a float[3] - botw.player's shape. 0 when there is no player.
extern uint32_t wiixl_import__totk_player__GetPosition(float* out3);
extern float wiixl_import__totk_player__GetFacing(void);
extern int32_t wiixl_import__totk_player__GetLife(void);
extern int32_t wiixl_import__totk_player__GetMaxLife(void);
extern uint32_t wiixl_import__totk_player__SetLife(int32_t value);
extern uint32_t wiixl_import__totk_player__AddLife(int32_t delta);
extern float wiixl_import__totk_player__GetStamina(void);
extern float wiixl_import__totk_player__GetMaxStamina(void);
extern float wiixl_import__totk_player__GetExtraStamina(void);
extern uint32_t wiixl_import__totk_player__SetStamina(float value);
extern uint32_t wiixl_import__totk_player__AddStamina(float delta);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require totk.player@1.0 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_totk_player {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 0;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/framework/modules.md.
#define WXL_USE_totk_player(sym) \
    inline decltype(&wiixl_import__totk_player__##sym) volatile sym = \
        &wiixl_import__totk_player__##sym
