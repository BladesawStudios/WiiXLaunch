// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// totk.gamedata v1.0, 32 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_totk_gamedata(Supported); }
//     S::Supported(...);
//
// so a mod that uses two symbols imports two, not all 32.
//
// The comments are the SURFACE's own, carried across - they say why a
// symbol behaves as it does, which is the half a signature cannot.
#pragma once

#include <cstdint>

extern "C" {
extern uint32_t wiixl_import__totk_gamedata__Supported(void);
extern uint32_t wiixl_import__totk_gamedata__Available(void);
extern uint32_t wiixl_import__totk_gamedata__Hash(const char* name);
extern uint32_t wiixl_import__totk_gamedata__GetFlagBool(const char* n, uint32_t* out);
extern uint32_t wiixl_import__totk_gamedata__SetFlagBool(const char* n, uint32_t v);
extern uint32_t wiixl_import__totk_gamedata__GetFlagS32(const char* n, int32_t* out);
extern uint32_t wiixl_import__totk_gamedata__SetFlagS32(const char* n, int32_t v);
extern uint32_t wiixl_import__totk_gamedata__AddFlagS32(const char* n, int32_t d);
extern uint32_t wiixl_import__totk_gamedata__GetFlagU32(const char* n, uint32_t* out);
extern uint32_t wiixl_import__totk_gamedata__SetFlagU32(const char* n, uint32_t v);
extern uint32_t wiixl_import__totk_gamedata__GetFlagF32(const char* n, float* out);
extern uint32_t wiixl_import__totk_gamedata__SetFlagF32(const char* n, float v);
extern uint32_t wiixl_import__totk_gamedata__GetFlagVec3(const char* n, float* out3);
extern uint32_t wiixl_import__totk_gamedata__SetFlagVec3(const char* n, const float* in3);

// Value by NAME here, so a mod writes SetFlagEnum("Flag", "Bokoblin").
extern uint32_t wiixl_import__totk_gamedata__GetFlagEnum(const char* n, uint32_t* outValueHash);
extern uint32_t wiixl_import__totk_gamedata__SetFlagEnum(const char* n, const char* value);
extern uint32_t wiixl_import__totk_gamedata__GetBoolByHash(uint32_t h, uint32_t* out);
extern uint32_t wiixl_import__totk_gamedata__SetBoolByHash(uint32_t h, uint32_t v);
extern uint32_t wiixl_import__totk_gamedata__GetS32ByHash(uint32_t h, int32_t* out);
extern uint32_t wiixl_import__totk_gamedata__SetS32ByHash(uint32_t h, int32_t v);
extern uint32_t wiixl_import__totk_gamedata__AddS32ByHash(uint32_t h, int32_t d);
extern uint32_t wiixl_import__totk_gamedata__GetU32ByHash(uint32_t h, uint32_t* out);
extern uint32_t wiixl_import__totk_gamedata__SetU32ByHash(uint32_t h, uint32_t v);
extern uint32_t wiixl_import__totk_gamedata__GetF32ByHash(uint32_t h, float* out);
extern uint32_t wiixl_import__totk_gamedata__SetF32ByHash(uint32_t h, float v);
extern uint32_t wiixl_import__totk_gamedata__GetVec3ByHash(uint32_t h, float* out3);
extern uint32_t wiixl_import__totk_gamedata__SetVec3ByHash(uint32_t h, const float* in3);

// An enum's value is the hash of the value's name: Hash("Bokoblin") and so on.
extern uint32_t wiixl_import__totk_gamedata__GetEnumByHash(uint32_t h, uint32_t* outValueHash);
extern uint32_t wiixl_import__totk_gamedata__SetEnumByHash(uint32_t h, uint32_t valueHash);
extern uint32_t wiixl_import__totk_gamedata__GetRupees(int32_t* out);
extern uint32_t wiixl_import__totk_gamedata__SetRupees(int32_t v);
extern uint32_t wiixl_import__totk_gamedata__AddRupees(int32_t d);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require totk.gamedata@1.0 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_totk_gamedata {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 0;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/framework/modules.md.
#define WXL_USE_totk_gamedata(sym) \
    inline decltype(&wiixl_import__totk_gamedata__##sym) volatile sym = \
        &wiixl_import__totk_gamedata__##sym
