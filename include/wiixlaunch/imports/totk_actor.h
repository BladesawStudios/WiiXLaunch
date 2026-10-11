// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// totk.actor v1.0, 14 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_totk_actor(Supported); }
//     S::Supported(...);
//
// so a mod that uses two symbols imports two, not all 14.
//
// The comments are the SURFACE's own, carried across - they say why a
// symbol behaves as it does, which is the half a signature cannot.
#pragma once

#include <cstdint>

extern "C" {
extern uint32_t wiixl_import__totk_actor__Supported(void);
extern uint32_t wiixl_import__totk_actor__IsPresent(void);
extern uint32_t wiixl_import__totk_actor__Count(void);
extern uint32_t wiixl_import__totk_actor__Generation(void);
extern uint32_t wiixl_import__totk_actor__HandleAt(uint32_t i);

// Caller-owned buffer; returns the length written, 0 for a stale handle.
extern uint32_t wiixl_import__totk_actor__GetName(uint32_t h, char* out, uint32_t cap);
extern uint32_t wiixl_import__totk_actor__GetPosition(uint32_t h, float* out3);
extern uint32_t wiixl_import__totk_actor__FindByName(const char* name, uint32_t from);

// nameOrNull may be null for "any actor".
extern uint32_t wiixl_import__totk_actor__FindNearest(const char* nameOrNull, float x, float y, float z, float maxDist);
extern uint32_t wiixl_import__totk_actor__Spawn(const char* name, float x, float y, float z);
extern uint32_t wiixl_import__totk_actor__Delete(uint32_t h);
extern uint32_t wiixl_import__totk_actor__Move(uint32_t h, float x, float y, float z);
extern int32_t wiixl_import__totk_actor__LastSpawnError(void);
extern uint32_t wiixl_import__totk_actor__DroppedCount(void);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require totk.actor@1.0 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_totk_actor {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 0;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/framework/modules.md.
#define WXL_USE_totk_actor(sym) \
    inline decltype(&wiixl_import__totk_actor__##sym) volatile sym = \
        &wiixl_import__totk_actor__##sym
