// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// botw.map v1.1, 30 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_botw_map(SupportsMap); }
//     S::SupportsMap(...);
//
// so a mod that uses two symbols imports two, not all 30.
//
// The comments are the SURFACE's own, carried across - they say why a
// symbol behaves as it does, which is the half a signature cannot.
#pragma once

#include <cstdint>

extern "C" {
extern uint32_t wiixl_import__botw_map__SupportsMap(void);
extern uint32_t wiixl_import__botw_map__IsAvailable(void);
extern int32_t wiixl_import__botw_map__RegionFirst(void);
extern int32_t wiixl_import__botw_map__RegionCount(void);

// The flag store is only re-read when asked. A mod that has just written flags
// through botw.gamedata and then reads a region here would otherwise see the
// cached answer, which is the sort of staleness that looks like a write having
// silently failed.
extern uint32_t wiixl_import__botw_map__RefreshRegions(void);
extern uint32_t wiixl_import__botw_map__GetRegionUnlock(int32_t region, uint32_t* out);

// `force` writes the flag even where the game would have refused it. Its own
// parameter rather than a second symbol, because unlike the flag store's
// permission bypass this one is routine - unlocking a region the player has not
// reached is the ordinary reason to call this at all.
extern uint32_t wiixl_import__botw_map__SetRegionUnlock(int32_t region, uint32_t unlocked, uint32_t force);
extern int32_t wiixl_import__botw_map__SetRegionUnlockAll(uint32_t unlocked, uint32_t force);
extern uint32_t wiixl_import__botw_map__GetRegionScaleLevel(int32_t region, int32_t* out);

// The cached read, kept distinct from the live one. Reading every region live
// walks the flag store once per region; a mod drawing a whole map wants the
// cache, and a mod that just wrote wants the live value. Collapsing them would
// force one of those two to be wrong.
extern uint32_t wiixl_import__botw_map__GetCachedRegionScaleLevel(int32_t region, int32_t* out);
extern uint32_t wiixl_import__botw_map__SetRegionScaleLevel(int32_t region, int32_t level);
extern uint32_t wiixl_import__botw_map__GetRegionMarker(int32_t region, int32_t* out);
extern uint32_t wiixl_import__botw_map__SetRegionMarker(int32_t region, int32_t id);
extern uint32_t wiixl_import__botw_map__SetRegionActivated(int32_t region, uint32_t activated, uint32_t force);
extern int32_t wiixl_import__botw_map__CountUnlockedRegions(void);
extern int32_t wiixl_import__botw_map__ShrineCount(void);
extern uint32_t wiixl_import__botw_map__SetShrineUnlock(int32_t shrine, uint32_t unlocked, uint32_t force);
extern int32_t wiixl_import__botw_map__SetShrineUnlockAll(uint32_t unlocked, uint32_t force);
extern int32_t wiixl_import__botw_map__CountUnlockedShrines(void);
extern uint32_t wiixl_import__botw_map__GetShrineUnlock(int32_t shrine, uint32_t* out);
extern uint32_t wiixl_import__botw_map__InitBeastMarkers(void);
extern int32_t wiixl_import__botw_map__RefreshBeastMarkers(void);

// One marker's contents. TrackedBeastMarker is a struct and must not cross, so
// the name is copied into the caller's buffer and the two flags come back as
// out-parameters - the same shape every other read here uses.
extern uint32_t wiixl_import__botw_map__GetBeastMarker(int32_t index, char* nameOut, uint32_t cap, uint32_t* entered, uint32_t* cleared, uint32_t* flags);

// Drops what the tracker has learned, so the next refresh starts clean. For a
// mod that has moved or removed beasts and does not want the stale positions
// blended in.
extern uint32_t wiixl_import__botw_map__ForgetBeastMarkers(void);
extern int32_t wiixl_import__botw_map__BeastMarkerCount(void);

//
// level 0 writes normally, 1 clears a latch, 2 also writes flags the game marks
// event-system-only. The single-flag symbols above are level 0 and level 1
// only; these reach the third case.
extern uint32_t wiixl_import__botw_map__SetRegionUnlockLevel(int32_t region, uint32_t unlocked, int32_t level);
extern int32_t wiixl_import__botw_map__SetRegionUnlockAllLevel(uint32_t unlocked, int32_t level);
extern uint32_t wiixl_import__botw_map__SetRegionActivatedLevel(int32_t region, uint32_t activated, int32_t level);
extern uint32_t wiixl_import__botw_map__SetShrineUnlockLevel(int32_t shrine, uint32_t unlocked, int32_t level);
extern int32_t wiixl_import__botw_map__SetShrineUnlockAllLevel(uint32_t unlocked, int32_t level);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require botw.map@1.1 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_botw_map {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 1;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/modules.md.
#define WXL_USE_botw_map(sym) \
    inline decltype(&wiixl_import__botw_map__##sym) volatile sym = \
        &wiixl_import__botw_map__##sym
