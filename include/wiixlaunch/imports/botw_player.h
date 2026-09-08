// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// botw.player v1.1, 17 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_botw_player(GetEquippedSword); }
//     S::GetEquippedSword(...);
//
// so a mod that uses two symbols imports two, not all 17.
#pragma once

#include <cstdint>

extern "C" {
extern uint32_t wiixl_import__botw_player__GetEquippedSword(void);   // surface spells this ActorHandle
extern uint32_t wiixl_import__botw_player__GetEquippedShield(void);   // surface spells this ActorHandle
extern uint32_t wiixl_import__botw_player__GetEquippedBow(void);   // surface spells this ActorHandle
extern uint32_t wiixl_import__botw_player__ActorIsValid(uint32_t h);
extern uint32_t wiixl_import__botw_player__ActorGetName(uint32_t h, char* out, uint32_t cap);
extern uint32_t wiixl_import__botw_player__GetPosition(float* out);
extern uint32_t wiixl_import__botw_player__SupportsPosition(void);
extern uint32_t wiixl_import__botw_player__Init(void);
extern uint32_t wiixl_import__botw_player__RegisterTick(void (*fn)());
extern uint32_t wiixl_import__botw_player__ConsumeAttackEvent(void);
extern uint32_t wiixl_import__botw_player__SupportsAttackTracking(void);
extern uint32_t wiixl_import__botw_player__GetPlayerActor(void);   // surface spells this ActorHandle
extern int32_t wiixl_import__botw_player__ActorGetLife(uint32_t h);
extern int32_t wiixl_import__botw_player__ActorGetMaxLife(uint32_t h);
extern uint32_t wiixl_import__botw_player__ActorSetLife(uint32_t h, int32_t life);
extern uint32_t wiixl_import__botw_player__SupportsLife(void);
extern uintptr_t wiixl_import__botw_player__ActorUnsafeRawPointer(uint32_t h);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require botw.player@1.1 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_botw_player {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 1;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/modules.md.
#define WXL_USE_botw_player(sym) \
    inline decltype(&wiixl_import__botw_player__##sym) volatile sym = \
        &wiixl_import__botw_player__##sym
