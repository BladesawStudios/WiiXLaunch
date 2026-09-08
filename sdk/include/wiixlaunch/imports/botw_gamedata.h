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
//
// The comments are the SURFACE's own, carried across - they say why a
// symbol behaves as it does, which is the half a signature cannot.
#pragma once

#include <cstdint>

extern "C" {

//
// Separate flags, not one "supported", because they are separately confirmed:
// the stamina getters and the flag store were RE'd independently and could
// perfectly well differ on some future platform. A mod that needs one should
// not be told no because another is missing.
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

//
// FlagDebug is a struct of eight words and two four-word arrays, so it cannot
// cross as itself. Flattened into out-pointers, with the arrays as pointers to
// four elements the caller owns.
//
// This is for diagnosing a REFUSED WRITE: status bit 0x12 blocks writes
// outright, and the gates decide whether a typed call reaches the store at all.
// Without it, "the write was refused" is the whole answer a mod can give.
extern uint32_t wiixl_import__botw_gamedata__GetFlagDebug(uint32_t* manager, uint32_t* status, uint32_t* flagByte, uint32_t* mirrorGate, uint32_t* writeGate, uint32_t* slotsOut4, uint32_t* coresOut4);

// Arms the completion display override. SetDisplayedPercent has nowhere to land
// until this hook is in place; with no override set it runs the original and
// tests one bool, so arming it costs nothing until something uses it.
extern uint32_t wiixl_import__botw_gamedata__InitCompletion(void);
extern uint32_t wiixl_import__botw_gamedata__GetStamina(float* out);
extern uint32_t wiixl_import__botw_gamedata__GetMaxStamina(float* out);
extern uint32_t wiixl_import__botw_gamedata__SetStamina(float units);
extern uint32_t wiixl_import__botw_gamedata__SetMaxStamina(float units);
extern uint32_t wiixl_import__botw_gamedata__RecoverStamina(void);
extern uint32_t wiixl_import__botw_gamedata__StaminaPerWheel(float* out);

// Hearts and wheels, the units a UI actually shows. The raw forms above stay
// because the game stores those; offering only one of each pair would mean
// every mod doing the conversion, and every mod getting the rounding subtly
// different.
extern uint32_t wiixl_import__botw_gamedata__GetMaxHearts(float* out);
extern uint32_t wiixl_import__botw_gamedata__SetMaxHearts(float hearts);
extern uint32_t wiixl_import__botw_gamedata__GetStaminaWheels(float* out);
extern uint32_t wiixl_import__botw_gamedata__SetStaminaWheels(float wheels);
extern uint32_t wiixl_import__botw_gamedata__GetMaxStaminaWheels(float* out);
extern uint32_t wiixl_import__botw_gamedata__SetMaxStaminaWheels(float wheels);

// The actor's own maximum, which is not always the save's - a temporary buff
// moves one and not the other, and a mod showing a stamina bar needs the one
// the game is actually clamping against.
extern uint32_t wiixl_import__botw_gamedata__GetActorMaxStamina(float* out);

// Queues a delta for the next frame instead of applying it now. The game
// recomputes some counters after a write, so an immediate add can be undone
// within the same frame; this is the form that survives that.
extern uint32_t wiixl_import__botw_gamedata__QueueFlagS32Delta(const char* name, int32_t delta);

// How many entries a flag store holds, by store index rather than by type - for
// a mod walking every store without knowing how many kinds there are.
extern uint32_t wiixl_import__botw_gamedata__GetFlagStoreCount(int32_t storeIndex, uint32_t* offset, int32_t* count);
extern uint32_t wiixl_import__botw_gamedata__GetFlagS32(const char* name, int32_t* out);
extern uint32_t wiixl_import__botw_gamedata__SetFlagS32(const char* name, int32_t value);
extern uint32_t wiixl_import__botw_gamedata__AddFlagS32(const char* name, int32_t delta);
extern uint32_t wiixl_import__botw_gamedata__GetFlagBool(const char* name, uint32_t* out);
extern uint32_t wiixl_import__botw_gamedata__SetFlagBool(const char* name, uint32_t value);

// The permission bypass, as its own symbol rather than a parameter on the one
// above. Forcing a flag the game would have refused is a different act with
// different consequences, and it should read differently at the call site.
extern uint32_t wiixl_import__botw_gamedata__SetFlagBoolForced(const char* name, uint32_t value, uint32_t bypassPermission);
extern uint32_t wiixl_import__botw_gamedata__GetFlagF32(const char* name, float* out);
extern uint32_t wiixl_import__botw_gamedata__SetFlagF32(const char* name, float value);

// Vec3 through a float[3], never as a struct - see the ABI rules in
// loader/surface.hpp for why a struct must not cross.
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

// The completion breakdown, field by field rather than as the module's
// Breakdown struct. Twelve small calls would be worse; one call filling an
// int32[8] in a fixed order is the same shape TimeSurface uses for the calendar:
// collected, total, base, baseTotal, divineBeasts, divineBeastTotal, koroks,
// korokTotal.
extern uint32_t wiixl_import__botw_gamedata__GetCompletionBreakdown(int32_t* out8, float* outPercent);
extern uint32_t wiixl_import__botw_gamedata__GetCompletionPercent(float* out);

// The lowest and highest the percentage can currently reach - what is actually
// attainable rather than what the counter says, which differs once DLC content
// is or is not installed.
extern uint32_t wiixl_import__botw_gamedata__GetReachableRange(float* lowest, float* highest);
extern uint32_t wiixl_import__botw_gamedata__SetDisplayedPercent(float percent, uint32_t forceVisible);
extern uint32_t wiixl_import__botw_gamedata__GetCompletionParts(int32_t* whole, int32_t* hundredths);
extern uint32_t wiixl_import__botw_gamedata__GetKorokCount(int32_t* out);
extern uint32_t wiixl_import__botw_gamedata__GetKorokTotal(int32_t* out);
extern uint32_t wiixl_import__botw_gamedata__SetKorokCount(int32_t count);
extern uint32_t wiixl_import__botw_gamedata__IsGameClear(void);

// The percentage the HUD shows, overridden. Separate from the real figure on
// purpose: this changes what is displayed and nothing else, and a mod should
// not be able to confuse "the save is 40% done" with "the corner of the screen
// says 40%".
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
