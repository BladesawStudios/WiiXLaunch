// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// botw.armour v1.1, 17 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_botw_armour(SupportsArmour); }
//     S::SupportsArmour(...);
//
// so a mod that uses two symbols imports two, not all 17.
#pragma once

#include <cstdint>

extern "C" {
extern uint32_t wiixl_import__botw_armour__SupportsArmour(void);
extern int32_t wiixl_import__botw_armour__PieceCount(void);
extern int32_t wiixl_import__botw_armour__EffectSlots(void);
extern uint32_t wiixl_import__botw_armour__Refresh(void);
extern uint32_t wiixl_import__botw_armour__IsPieceWorn(int32_t piece);
extern uint32_t wiixl_import__botw_armour__EffectName(int32_t effect, char* out, uint32_t cap);
extern int32_t wiixl_import__botw_armour__EffectFromName(const char* name);
extern uint32_t wiixl_import__botw_armour__GetPieceEffect(int32_t piece, int32_t* effect, int32_t* level);
extern uint32_t wiixl_import__botw_armour__SetPieceEffect(int32_t piece, int32_t effect, int32_t level);
extern uint32_t wiixl_import__botw_armour__SetPieceDefence(int32_t piece, int32_t defence);
extern uint32_t wiixl_import__botw_armour__SetPieceFlags(int32_t piece, uint32_t ancientPowUp, uint32_t climbWaterfall, uint32_t climbJumpless);
extern int32_t wiixl_import__botw_armour__GetArmourEffect(int32_t effect);
extern uint32_t wiixl_import__botw_armour__SetArmourEffects(int32_t effect, int32_t level);
extern uint32_t wiixl_import__botw_armour__SetExtraEffect(int32_t piece, int32_t effect, int32_t level);
extern int32_t wiixl_import__botw_armour__GetExtraEffect(int32_t piece, int32_t effect);
extern void wiixl_import__botw_armour__ClearExtraEffects(void);
extern uint32_t wiixl_import__botw_armour__InitExtraEffects(void);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require botw.armour@1.1 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_botw_armour {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 1;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/modules.md.
#define WXL_USE_botw_armour(sym) \
    inline decltype(&wiixl_import__botw_armour__##sym) volatile sym = \
        &wiixl_import__botw_armour__##sym
