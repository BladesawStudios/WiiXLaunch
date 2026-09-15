// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// botw.armour v1.2, 18 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_botw_armour(SupportsArmour); }
//     S::SupportsArmour(...);
//
// so a mod that uses two symbols imports two, not all 18.
//
// The comments are the SURFACE's own, carried across - they say why a
// symbol behaves as it does, which is the half a signature cannot.
#pragma once

#include <cstdint>

extern "C" {
extern uint32_t wiixl_import__botw_armour__SupportsArmour(void);

// Head, chest, legs. Exported rather than assumed, because "3" is a game fact
// and a mod looping over it should get it from the host.
extern int32_t wiixl_import__botw_armour__PieceCount(void);
extern int32_t wiixl_import__botw_armour__EffectSlots(void);

// The cached read of what is worn. Refresh re-reads the actors; without it a
// mod that just changed armour sees the previous set, which reads as a write
// having failed.
extern uint32_t wiixl_import__botw_armour__Refresh(void);
extern uint32_t wiixl_import__botw_armour__IsPieceWorn(int32_t piece);
extern uint32_t wiixl_import__botw_armour__EffectName(int32_t effect, char* out, uint32_t cap);
extern int32_t wiixl_import__botw_armour__EffectFromName(const char* name);

// One piece's effect: which kind, and at what level. Two out-pointers rather
// than a packed return, so "no effect" is a status of 0 and never a level that
// happens to be zero.
extern uint32_t wiixl_import__botw_armour__GetPieceEffect(int32_t piece, int32_t* effect, int32_t* level);
extern uint32_t wiixl_import__botw_armour__SetPieceEffect(int32_t piece, int32_t effect, int32_t level);
extern uint32_t wiixl_import__botw_armour__SetPieceDefence(int32_t piece, int32_t defence);
extern uint32_t wiixl_import__botw_armour__SetPieceFlags(int32_t piece, uint32_t ancientPowUp, uint32_t climbWaterfall, uint32_t climbJumpless);

// The whole set's level for one effect, which is what the game itself asks.
extern int32_t wiixl_import__botw_armour__GetArmourEffect(int32_t effect);
extern uint32_t wiixl_import__botw_armour__SetArmourEffects(int32_t effect, int32_t level);
extern uint32_t wiixl_import__botw_armour__SetExtraEffect(int32_t piece, int32_t effect, int32_t level);
extern int32_t wiixl_import__botw_armour__GetExtraEffect(int32_t piece, int32_t effect);
extern void wiixl_import__botw_armour__ClearExtraEffects(void);

// Per piece, which is what a caller naming a piece means. The all-pieces clear
// below stays, under its own name, so the two cannot be confused for each
// other by a caller that passes an argument the callee never reads.
extern uint32_t wiixl_import__botw_armour__ClearPieceExtraEffects(int32_t piece);

// Arms the extras table. SetExtraEffect REFUSES until this has run - it checks
// ExtraHookInstalled and returns false - so a mod that never calls it gets a
// silent no-op. The coverage gate said this was "installed by SetExtraEffect on
// first use", which was a sentence about code that does the opposite.
extern uint32_t wiixl_import__botw_armour__InitExtraEffects(void);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require botw.armour@1.2 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_botw_armour {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 2;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/framework/modules.md.
#define WXL_USE_botw_armour(sym) \
    inline decltype(&wiixl_import__botw_armour__##sym) volatile sym = \
        &wiixl_import__botw_armour__##sym
