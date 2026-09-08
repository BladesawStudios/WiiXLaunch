// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// botw.gamedata v1.2, 57 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_botw_gamedata(SupportsRupees); }
//     S::SupportsRupees(...);
//
// so a mod that uses two symbols imports two, not all 57.
#pragma once

#include <cstdint>

extern "C" {
extern uint32_t wiixl_import__botw_gamedata__SupportsRupees(void);
extern uint32_t wiixl_import__botw_gamedata__SupportsFlags(void);
extern uint32_t wiixl_import__botw_gamedata__SupportsStamina(void);
extern uint32_t wiixl_import__botw_gamedata__SupportsMaxLife(void);
extern uint32_t wiixl_import__botw_gamedata__SupportsCompletion(void);
extern uint32_t wiixl_import__botw_gamedata__GetRupees(int32_t* out);
extern uint32_t wiixl_import__botw_gamedata__SetRupees(int32_t value);
extern uint32_t wiixl_import__botw_gamedata__AddRupees(int32_t delta);
extern int32_t wiixl_import__botw_gamedata__GetMaxLife(void);
extern uint32_t wiixl_import__botw_gamedata__SetMaxLife(int32_t rawUnits);
extern uint32_t wiixl_import__botw_gamedata__GetFlagDebug(uint32_t* manager, uint32_t* status, uint32_t* flagByte, uint32_t* mirrorGate, uint32_t* writeGate, uint32_t* slotsOut4, uint32_t* coresOut4);
extern uint32_t wiixl_import__botw_gamedata__InitCompletion(void);
extern uint32_t wiixl_import__botw_gamedata__GetStamina(float* out);
extern uint32_t wiixl_import__botw_gamedata__GetMaxStamina(float* out);
extern uint32_t wiixl_import__botw_gamedata__SetStamina(float units);
extern uint32_t wiixl_import__botw_gamedata__SetMaxStamina(float units);
extern uint32_t wiixl_import__botw_gamedata__RecoverStamina(void);
extern uint32_t wiixl_import__botw_gamedata__StaminaPerWheel(float* out);
extern uint32_t wiixl_import__botw_gamedata__GetMaxHearts(float* out);
extern uint32_t wiixl_import__botw_gamedata__SetMaxHearts(float hearts);
extern uint32_t wiixl_import__botw_gamedata__GetStaminaWheels(float* out);
extern uint32_t wiixl_import__botw_gamedata__SetStaminaWheels(float wheels);
extern uint32_t wiixl_import__botw_gamedata__GetMaxStaminaWheels(float* out);
extern uint32_t wiixl_import__botw_gamedata__SetMaxStaminaWheels(float wheels);
extern uint32_t wiixl_import__botw_gamedata__GetActorMaxStamina(float* out);
extern uint32_t wiixl_import__botw_gamedata__QueueFlagS32Delta(const char* name, int32_t delta);
extern uint32_t wiixl_import__botw_gamedata__GetFlagStoreCount(int32_t storeIndex, uint32_t* offset, int32_t* count);
extern uint32_t wiixl_import__botw_gamedata__GetFlagS32(const char* name, int32_t* out);
extern uint32_t wiixl_import__botw_gamedata__SetFlagS32(const char* name, int32_t value);
extern uint32_t wiixl_import__botw_gamedata__AddFlagS32(const char* name, int32_t delta);
extern uint32_t wiixl_import__botw_gamedata__GetFlagBool(const char* name, uint32_t* out);
extern uint32_t wiixl_import__botw_gamedata__SetFlagBool(const char* name, uint32_t value);
extern uint32_t wiixl_import__botw_gamedata__SetFlagBoolForced(const char* name, uint32_t value, uint32_t bypassPermission);
extern uint32_t wiixl_import__botw_gamedata__GetFlagF32(const char* name, float* out);
extern uint32_t wiixl_import__botw_gamedata__SetFlagF32(const char* name, float value);
extern uint32_t wiixl_import__botw_gamedata__GetFlagVec3(const char* name, float* out3);
extern uint32_t wiixl_import__botw_gamedata__SetFlagVec3(const char* name, const float* in3);
extern int32_t wiixl_import__botw_gamedata__FlagS32Count(void);
extern int32_t wiixl_import__botw_gamedata__FlagBoolCount(void);
extern int32_t wiixl_import__botw_gamedata__FlagF32Count(void);
extern int32_t wiixl_import__botw_gamedata__FlagVec3Count(void);
extern uint32_t wiixl_import__botw_gamedata__GetFlagS32ByIndex(int32_t index, uint32_t* hash, int32_t* value);
extern uint32_t wiixl_import__botw_gamedata__GetFlagBoolByIndex(int32_t index, uint32_t* hash, uint32_t* value);
extern uint32_t wiixl_import__botw_gamedata__GetFlagF32ByIndex(int32_t index, uint32_t* hash, float* value);
extern uint32_t wiixl_import__botw_gamedata__GetFlagVec3ByIndex(int32_t index, uint32_t* hash, float* out3);
extern uint32_t wiixl_import__botw_gamedata__GetCompletionBreakdown(int32_t* out8, float* outPercent);
extern uint32_t wiixl_import__botw_gamedata__GetCompletionPercent(float* out);
extern uint32_t wiixl_import__botw_gamedata__GetReachableRange(float* lowest, float* highest);
extern uint32_t wiixl_import__botw_gamedata__SetDisplayedPercent(float percent, uint32_t forceVisible);
extern uint32_t wiixl_import__botw_gamedata__GetCompletionParts(int32_t* whole, int32_t* hundredths);
extern uint32_t wiixl_import__botw_gamedata__GetKorokCount(int32_t* out);
extern uint32_t wiixl_import__botw_gamedata__GetKorokTotal(int32_t* out);
extern uint32_t wiixl_import__botw_gamedata__SetKorokCount(int32_t count);
extern uint32_t wiixl_import__botw_gamedata__IsGameClear(void);
extern uint32_t wiixl_import__botw_gamedata__SetDisplayedParts(int32_t whole, int32_t hundredths, uint32_t forceVisible);
extern void wiixl_import__botw_gamedata__ClearDisplayOverride(void);
extern uint32_t wiixl_import__botw_gamedata__IsDisplayOverridden(void);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require botw.gamedata@1.2 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_botw_gamedata {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 2;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/modules.md.
#define WXL_USE_botw_gamedata(sym) \
    inline decltype(&wiixl_import__botw_gamedata__##sym) volatile sym = \
        &wiixl_import__botw_gamedata__##sym
