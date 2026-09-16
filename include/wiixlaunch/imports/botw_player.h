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
//
// The comments are the SURFACE's own, carried across - they say why a
// symbol behaves as it does, which is the half a signature cannot.
#pragma once

#include <cstdint>

extern "C" {
extern uint32_t wiixl_import__botw_player__GetEquippedSword(void);   // surface spells this ActorHandle
extern uint32_t wiixl_import__botw_player__GetEquippedShield(void);   // surface spells this ActorHandle
extern uint32_t wiixl_import__botw_player__GetEquippedBow(void);   // surface spells this ActorHandle

// 1 if the handle still refers to a live actor, 0 otherwise. A mod holding a
// handle across frames should check this rather than assume.
extern uint32_t wiixl_import__botw_player__ActorIsValid(uint32_t h);

// Copies the actor's name into a caller-owned buffer and returns the number of
// characters written, excluding the terminator. 0 means "no name available",
// which includes a stale handle.
//
// Returning data this way rather than as `const char*` is deliberate: a pointer
// into the module's own static buffer would be valid only until the next call,
// and nothing in the signature would say so.
extern uint32_t wiixl_import__botw_player__ActorGetName(uint32_t h, char* out, uint32_t cap);

// Writes x, y, z into out[0..2]. Returns 1 on success.
//
// Not `bool` - bool's size is not guaranteed across a compiled boundary, and
// this is exactly the kind of detail that is invisible until it is wrong.
extern uint32_t wiixl_import__botw_player__GetPosition(float* out);

// Capability flag, per the module convention: a mod can ask rather than
// discovering by getting zeroes. Switch has no RE'd position offset.
extern uint32_t wiixl_import__botw_player__SupportsPosition(void);

// Installs Player's per-frame tick hook, which every cached accessor here needs
// - position, the attack counter, and the player pointer itself.
//
// Idempotent in the module, and it has to be: several mods may each call this
// because each needs the cached state, and none can see that another already
// did. Returns 1 if THIS call installed it, 0 if it was already up. Both are
// success; the distinction is there so a boot log can show which module paid
// for it rather than leaving "already installed" indistinguishable from
// "failed".
extern uint32_t wiixl_import__botw_player__Init(void);

// A per-frame callback fired right after Player's own cached state has been
// refreshed for this frame.
//
// NOT Player::OnTick. That is a single slot documented as "call again to
// replace it", which is workable for a source mod that can see the whole tree
// and chain by hand, and unworkable for compiled binaries: two .wxlm mods
// cannot see each other, so the second would silently evict the first and the
// mod that stops working is the one that did nothing wrong. This registers into
// a slot of its own, attributed to the calling module.
//
// Distinct from wiixl.core's RegisterTick, which fires at the GX2 swap after
// the frame is drawn. This one is the only place a mod can read THIS frame's
// position or consume THIS frame's attack event. A mod that needs one does not
// want the other.
//
// Returns 1 on success, 0 if refused - the log names which refusal.
extern uint32_t wiixl_import__botw_player__RegisterTick(void (*fn)());

// 1 if the player swung since the last call, and CLEARS the flag.
//
// Consuming is the point: two mods both polling would otherwise each see the
// same swing, or race for it. One consumer per swing, first tick to ask.
extern uint32_t wiixl_import__botw_player__ConsumeAttackEvent(void);
extern uint32_t wiixl_import__botw_player__SupportsAttackTracking(void);

// A handle to Link himself, so the life accessors below need no player-specific
// duplicates - and so the escape hatch has exactly one shape.
//
// Needs PlayerInit to have run and at least one frame to have passed; returns 0
// until then, which is the same answer a despawned actor gives.
extern uint32_t wiixl_import__botw_player__GetPlayerActor(void);   // surface spells this ActorHandle

// Current life, or 0 for a stale handle, an unsupported platform, or a pointer
// that failed validation. 0 is also a legitimate value (a dead actor), so a mod
// that needs to tell those apart checks ActorIsValid first.
extern int32_t wiixl_import__botw_player__ActorGetLife(uint32_t h);
extern int32_t wiixl_import__botw_player__ActorGetMaxLife(uint32_t h);

// Returns 1 if the write happened, 0 if the handle was stale or the platform
// does not support it. NOT void: "I set it and nothing changed" and "I never
// set it" are different problems, and a void return makes them identical.
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
// address - the module fails to relocate. See docs/framework/modules.md.
#define WXL_USE_botw_player(sym) \
    inline decltype(&wiixl_import__botw_player__##sym) volatile sym = \
        &wiixl_import__botw_player__##sym
