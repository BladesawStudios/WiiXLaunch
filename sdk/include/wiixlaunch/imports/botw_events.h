// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// botw.events v1.0, 8 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_botw_events(SupportsEvents); }
//     S::SupportsEvents(...);
//
// so a mod that uses two symbols imports two, not all 8.
//
// The comments are the SURFACE's own, carried across - they say why a
// symbol behaves as it does, which is the half a signature cannot.
#pragma once

#include <cstdint>

extern "C" {
extern uint32_t wiixl_import__botw_events__SupportsEvents(void);

// Arms the event system, and takes the korok callback slot once. Idempotent:
// several mods will each call this and only the first does anything.
extern uint32_t wiixl_import__botw_events__Init(void);
extern uint32_t wiixl_import__botw_events__IsArmed(void);
extern uint32_t wiixl_import__botw_events__Resync(void);

// Drive from a tick. The module's own Tick is what notices state changes; a
// host with no mod calling this simply never reports an event.
extern void wiixl_import__botw_events__Tick(void);
extern uint32_t wiixl_import__botw_events__ConsumeShrineComplete(void);
extern uint32_t wiixl_import__botw_events__ConsumeTowerOpen(void);
extern uint32_t wiixl_import__botw_events__ConsumeKorokGet(int32_t* total, int32_t* gained);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require botw.events@1.0 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_botw_events {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 0;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/modules.md.
#define WXL_USE_botw_events(sym) \
    inline decltype(&wiixl_import__botw_events__##sym) volatile sym = \
        &wiixl_import__botw_events__##sym
