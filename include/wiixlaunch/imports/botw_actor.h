// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// botw.actor v1.0, 49 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_botw_actor(Init); }
//     S::Init(...);
//
// so a mod that uses two symbols imports two, not all 49.
#pragma once

#include <cstdint>

extern "C" {
extern uint32_t wiixl_import__botw_actor__Init(void);
extern int32_t wiixl_import__botw_actor__Count(void);
extern uint32_t wiixl_import__botw_actor__SupportsLife(void);
extern int32_t wiixl_import__botw_actor__Query(int32_t kind, const char* name, uint32_t exactMatch);
extern int32_t wiixl_import__botw_actor__QueryMatched(void);
extern uint32_t wiixl_import__botw_actor__QueryAt(int32_t index);
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
extern uint32_t wiixl_import__botw_actor__SetPosition(uint32_t handle, float x, float y, float z);
extern uint32_t wiixl_import__botw_actor__WarpTo(uint32_t handle, float x, float y, float z);
extern uint32_t wiixl_import__botw_actor__NudgeTo(uint32_t handle, float x, float y, float z);
extern uint32_t wiixl_import__botw_actor__GetRotation(uint32_t handle, float* out3);
extern uint32_t wiixl_import__botw_actor__SetRotation(uint32_t handle, float x, float y, float z);
extern uint32_t wiixl_import__botw_actor__SpinBy(uint32_t handle, float yaw, float pitch, float roll);
extern uint32_t wiixl_import__botw_actor__GetMatrix(uint32_t handle, float* out12);
extern uint32_t wiixl_import__botw_actor__SetMatrix(uint32_t handle, const float* mtx12);
extern uint32_t wiixl_import__botw_actor__GetLinearVelocity(uint32_t handle, float* out3);
extern uint32_t wiixl_import__botw_actor__SetLinearVelocity(uint32_t handle, float x, float y, float z);
extern uint32_t wiixl_import__botw_actor__AddLinearVelocity(uint32_t handle, float x, float y, float z);
extern uint32_t wiixl_import__botw_actor__GetHavokVelocity(uint32_t handle, float* out3);
extern uint32_t wiixl_import__botw_actor__GetControllerVelocity(uint32_t handle, float* out3);
extern uint32_t wiixl_import__botw_actor__SetControllerVelocity(uint32_t handle, float x, float y, float z);
extern uint32_t wiixl_import__botw_actor__GetControllerDirection(uint32_t handle, float* out3);
extern uint32_t wiixl_import__botw_actor__SetControllerDirection(uint32_t handle, float x, float y, float z);
extern int32_t wiixl_import__botw_actor__GetLife(uint32_t handle);
extern int32_t wiixl_import__botw_actor__GetMaxLife(uint32_t handle);
extern uint32_t wiixl_import__botw_actor__SetLife(uint32_t handle, int32_t life);
extern uint32_t wiixl_import__botw_actor__SetMaxLife(uint32_t handle, int32_t maxLife);
extern uint32_t wiixl_import__botw_actor__GetHearts(uint32_t handle, float* out);
extern uint32_t wiixl_import__botw_actor__SetHearts(uint32_t handle, float hearts);
extern uint32_t wiixl_import__botw_actor__GetMaxHearts(uint32_t handle, float* out);
extern uint32_t wiixl_import__botw_actor__SetMaxHearts(uint32_t handle, float hearts);
extern uint32_t wiixl_import__botw_actor__SetMtx(uint32_t handle, const float* mtx12, uint32_t setActorMtx);
extern uint32_t wiixl_import__botw_actor__Spawn(const char* actorName, uint32_t anchorHandle, float x, float y, float z);
extern uint32_t wiixl_import__botw_actor__SpawnScaled(const char* actorName, uint32_t anchorHandle, float x, float y, float z, float scaleX, float scaleY, float scaleZ);
extern uint32_t wiixl_import__botw_actor__Delete(uint32_t handle, uint32_t reason);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require botw.actor@1.0 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_botw_actor {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 0;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/modules.md.
#define WXL_USE_botw_actor(sym) \
    inline decltype(&wiixl_import__botw_actor__##sym) volatile sym = \
        &wiixl_import__botw_actor__##sym
