// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// botw.actor v1.1, 52 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_botw_actor(Init); }
//     S::Init(...);
//
// so a mod that uses two symbols imports two, not all 52.
//
// The comments are the SURFACE's own, carried across - they say why a
// symbol behaves as it does, which is the half a signature cannot.
#pragma once

#include <cstdint>

extern "C" {

// Installs the actor-system hooks. Idempotent in the module, for the same
// reason Player::Init is: several mods may each need it and none can see that
// another already did.
extern uint32_t wiixl_import__botw_actor__Init(void);
extern int32_t wiixl_import__botw_actor__Count(void);
extern uint32_t wiixl_import__botw_actor__SupportsLife(void);
extern int32_t wiixl_import__botw_actor__Query(int32_t kind, const char* name, uint32_t exactMatch);

// How many the query actually matched, before the cap. A mod that got the cap
// back can tell the difference between "that is all of them" and "there were
// more" - which a count alone cannot say.
extern int32_t wiixl_import__botw_actor__QueryMatched(void);
extern uint32_t wiixl_import__botw_actor__QueryAt(int32_t index);

// The single-result lookups, which do not disturb the query buffer - a mod
// mid-enumeration can still ask about something by name.
extern uint32_t wiixl_import__botw_actor__FindByName(const char* name, uint32_t exactMatch);
extern uint32_t wiixl_import__botw_actor__FindById(uint32_t id);
extern uint32_t wiixl_import__botw_actor__IsValidActorName(const char* name);
extern uint32_t wiixl_import__botw_actor__IsValid(uint32_t handle);
extern uint32_t wiixl_import__botw_actor__GetName(uint32_t handle, char* out, uint32_t cap);
extern uint32_t wiixl_import__botw_actor__GetId(uint32_t handle, uint32_t* out);
extern uint32_t wiixl_import__botw_actor__GetState(uint32_t handle, uint32_t* out);
extern int32_t wiixl_import__botw_actor__GetKind(uint32_t handle);
extern uint32_t wiixl_import__botw_actor__IsActive(uint32_t handle);
extern uint32_t wiixl_import__botw_actor__IsDying(uint32_t handle);
extern uint32_t wiixl_import__botw_actor__IsSleeping(uint32_t handle);
extern uint32_t wiixl_import__botw_actor__IsAttached(uint32_t handle);
extern uint32_t wiixl_import__botw_actor__IsDynamic(uint32_t handle);
extern uint32_t wiixl_import__botw_actor__IsPlacement(uint32_t handle);
extern uint32_t wiixl_import__botw_actor__GetPosition(uint32_t handle, float* out3);

// SetPosition, WarpTo and NudgeTo are three symbols because they are three
// different acts on a physics object, and the module keeps them apart for
// reasons its own comments give at length. Collapsing them here would hand a
// mod one call whose behaviour depends on something it cannot see.
extern uint32_t wiixl_import__botw_actor__SetPosition(uint32_t handle, float x, float y, float z);
extern uint32_t wiixl_import__botw_actor__WarpTo(uint32_t handle, float x, float y, float z);
extern uint32_t wiixl_import__botw_actor__NudgeTo(uint32_t handle, float x, float y, float z);
extern uint32_t wiixl_import__botw_actor__GetRotation(uint32_t handle, float* out3);
extern uint32_t wiixl_import__botw_actor__SetRotation(uint32_t handle, float x, float y, float z);
extern uint32_t wiixl_import__botw_actor__SpinBy(uint32_t handle, float yaw, float pitch, float roll);

// The 3x4 matrix, as twelve floats the caller owns.
extern uint32_t wiixl_import__botw_actor__GetMatrix(uint32_t handle, float* out12);
extern uint32_t wiixl_import__botw_actor__SetMatrix(uint32_t handle, const float* mtx12);
extern uint32_t wiixl_import__botw_actor__GetLinearVelocity(uint32_t handle, float* out3);
extern uint32_t wiixl_import__botw_actor__SetLinearVelocity(uint32_t handle, float x, float y, float z);
extern uint32_t wiixl_import__botw_actor__AddLinearVelocity(uint32_t handle, float x, float y, float z);
extern uint32_t wiixl_import__botw_actor__GetHavokVelocity(uint32_t handle, float* out3);

// The controller vectors, which are what a walking actor is actually steered
// by - distinct from the physics velocity above, and confusing the two is the
// classic way to make an enemy vibrate in place.
extern uint32_t wiixl_import__botw_actor__GetControllerVelocity(uint32_t handle, float* out3);
extern uint32_t wiixl_import__botw_actor__SetControllerVelocity(uint32_t handle, float x, float y, float z);
extern uint32_t wiixl_import__botw_actor__GetControllerDirection(uint32_t handle, float* out3);
extern uint32_t wiixl_import__botw_actor__SetControllerDirection(uint32_t handle, float x, float y, float z);
extern int32_t wiixl_import__botw_actor__GetLife(uint32_t handle);
extern int32_t wiixl_import__botw_actor__GetMaxLife(uint32_t handle);
extern uint32_t wiixl_import__botw_actor__SetLife(uint32_t handle, int32_t life);
extern uint32_t wiixl_import__botw_actor__SetMaxLife(uint32_t handle, int32_t maxLife);

// Hearts rather than quarter-hearts. Both are offered because both are the
// natural unit somewhere: the game stores quarters, and a UI shows hearts. A
// mod converting between them itself is a mod that will one day round the wrong
// way on a half-heart.
extern uint32_t wiixl_import__botw_actor__GetHearts(uint32_t handle, float* out);
extern uint32_t wiixl_import__botw_actor__SetHearts(uint32_t handle, float hearts);
extern uint32_t wiixl_import__botw_actor__GetMaxHearts(uint32_t handle, float* out);
extern uint32_t wiixl_import__botw_actor__SetMaxHearts(uint32_t handle, float hearts);

// The matrix write that goes through the game's own setMtx, which is a
// different act from SetMatrix: it tells the engine the actor moved rather than
// editing the numbers behind its back. setActorMtx picks which of the two the
// game is told about.
extern uint32_t wiixl_import__botw_actor__SetMtx(uint32_t handle, const float* mtx12, uint32_t setActorMtx);

// Spawned relative to an anchor actor, which is how the game's own spawn works:
// the anchor supplies the manager and the heap. Pass the player's handle for
// "near Link".
extern uint32_t wiixl_import__botw_actor__Spawn(const char* actorName, uint32_t anchorHandle, float x, float y, float z);
extern uint32_t wiixl_import__botw_actor__SpawnScaled(const char* actorName, uint32_t anchorHandle, float x, float y, float z, float scaleX, float scaleY, float scaleZ);
extern uint32_t wiixl_import__botw_actor__Delete(uint32_t handle, uint32_t reason);

// v1.1. Every actor the ray passes within maxMiss of, between minAlong and
// maxAlong along it, NEAREST FIRST - the order matters more than it looks:
// when more than kMaxQueryResults match, the farthest are the ones dropped,
// so the cap costs a picker the actors it was least likely to want.
//
// origin3/dir3 are float[3]; dir need not be unit length, it is normalised
// here. kind is Query's selector (0 dynamic, 1 static, 2 all). Read the
// results with QueryAt, QueryMatched and QueryScanned, exactly as Query's.
//
// flags is a mask. 1 (kRayHorizontal) flattens both the ray and each offset
// onto the ground plane, for an aim direction that has a yaw and no pitch: the
// miss is then measured sideways only, and height is the mod's to judge.
//
// 2 (kRaySkipOrigin) exists because the cap would otherwise be spent on noise.
// Event tags, system actors and anything never placed report (0,0,0), and
// they are most of the live list - one debug-tool dump found 3380 of 4278
// actors "on the ray" because the player faced the map centre. A real actor
// at exactly the origin is lost with them.
//
// The handles are made after the walk, outside the proc manager's lock, so an
// actor can die between being measured and being handed out. That is the same
// promise every handle makes - a generation check, not a liveness guarantee -
// and IsValid answers the rest.
extern int32_t wiixl_import__botw_actor__QueryRay(const float* origin3, const float* dir3, float minAlong, float maxAlong, float maxMiss, int32_t kind, uint32_t flags);

// v1.1. How many actors the last Query or QueryRay visited, matched or not. A
// picker reporting "nothing on the ray" needs to say whether it looked at
// four thousand actors or at none - an empty world and a broken walk give the
// same empty result otherwise.
extern int32_t wiixl_import__botw_actor__QueryScanned(void);

// v1.1. The physics object behind a live actor, from the actor's own vtable
// slot 0x30C - a game call, which is why it is here and not left to a mod
// holding a raw pointer. 0 for a placement, a dead handle, or an actor
// without physics.
extern uintptr_t wiixl_import__botw_actor__GetPhysicsObject(uint32_t handle);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require botw.actor@1.1 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_botw_actor {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 1;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/framework/modules.md.
#define WXL_USE_botw_actor(sym) \
    inline decltype(&wiixl_import__botw_actor__##sym) volatile sym = \
        &wiixl_import__botw_actor__##sym
