// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// botw.region v1.0, 31 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_botw_region(SupportsRegion); }
//     S::SupportsRegion(...);
//
// so a mod that uses two symbols imports two, not all 31.
//
// The comments are the SURFACE's own, carried across - they say why a
// symbol behaves as it does, which is the half a signature cannot.
#pragma once

#include <cstdint>

extern "C" {
extern uint32_t wiixl_import__botw_region__SupportsRegion(void);
extern int32_t wiixl_import__botw_region__GetPlayerRegion(void);
extern int32_t wiixl_import__botw_region__GetRegionAt(float x, float z);
extern uint32_t wiixl_import__botw_region__GetUnlockMask(void);
extern uint32_t wiixl_import__botw_region__SetUnlockMask(uint32_t mask);

// Rebuilds the mask from the tower flags, which is what the player's actual
// progress says. A mod that has been overriding the mask calls this to give
// control back.
extern int32_t wiixl_import__botw_region__SyncFromTowers(void);
extern int32_t wiixl_import__botw_region__CountLockedRegions(void);
extern uint32_t wiixl_import__botw_region__AllowedAt(float x, float z);
extern uint32_t wiixl_import__botw_region__IsBlockedAt(float x, float z);
extern uint32_t wiixl_import__botw_region__SetWallsEnabled(uint32_t enabled);
extern uint32_t wiixl_import__botw_region__GetWallsEnabled(void);
extern uint32_t wiixl_import__botw_region__SetWallGeometry(float cellSize, float height, float thickness);
extern uint32_t wiixl_import__botw_region__GetWallGeometry(float* out3);
extern uint32_t wiixl_import__botw_region__SetBuildRadius(float radius);
extern uint32_t wiixl_import__botw_region__GetBuildRadius(float* out);

// Pushback shoves the player back out rather than only blocking them, which is
// a different feel and a different failure mode - its own switch because a mod
// wanting one rarely wants the other by accident.
extern uint32_t wiixl_import__botw_region__SetPushbackEnabled(uint32_t enabled);
extern uint32_t wiixl_import__botw_region__GetPushbackEnabled(void);
extern uint32_t wiixl_import__botw_region__SetWallYaw(int32_t index);
extern int32_t wiixl_import__botw_region__GetWallYaw(void);
extern uint32_t wiixl_import__botw_region__SetWallPitch(int32_t index);
extern int32_t wiixl_import__botw_region__GetWallPitch(void);
extern int32_t wiixl_import__botw_region__GetWallCount(void);
extern int32_t wiixl_import__botw_region__RemoveAllWalls(void);
extern uint32_t wiixl_import__botw_region__ShowMarkerAt(float x, float y, float z, float towardX, float towardZ);
extern uint32_t wiixl_import__botw_region__HideMarker(void);
extern uint32_t wiixl_import__botw_region__SetMarkerEnabled(uint32_t on);
extern uint32_t wiixl_import__botw_region__GetMarkerEnabled(void);
extern uint32_t wiixl_import__botw_region__SetMarkerScale(float scale);
extern uint32_t wiixl_import__botw_region__GetMarkerScale(float* out);
extern uint32_t wiixl_import__botw_region__SetMarkerOffset(float units);
extern uint32_t wiixl_import__botw_region__GetMarkerOffset(float* out);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require botw.region@1.0 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_botw_region {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 0;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/framework/modules.md.
#define WXL_USE_botw_region(sym) \
    inline decltype(&wiixl_import__botw_region__##sym) volatile sym = \
        &wiixl_import__botw_region__##sym
