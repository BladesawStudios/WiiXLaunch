// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// totk.world v1.0, 17 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_totk_world(Supported); }
//     S::Supported(...);
//
// so a mod that uses two symbols imports two, not all 17.
//
// The comments are the SURFACE's own, carried across - they say why a
// symbol behaves as it does, which is the half a signature cannot.
#pragma once

#include <cstdint>

extern "C" {
extern uint32_t wiixl_import__totk_world__Supported(void);
extern uint32_t wiixl_import__totk_world__IsPresent(void);
extern uint32_t wiixl_import__totk_world__UpdateCount(void);
extern uint32_t wiixl_import__totk_world__GetSecondsOfDay(float* out);
extern uint32_t wiixl_import__totk_world__SetSecondsOfDay(float seconds);
extern uint32_t wiixl_import__totk_world__GetGameTime(float* hours);
extern uint32_t wiixl_import__totk_world__GetGameTimeHM(int32_t* hour, int32_t* minute);
extern uint32_t wiixl_import__totk_world__SetGameTime(float hours);
extern uint32_t wiixl_import__totk_world__SetGameTimeHM(int32_t hour, int32_t minute);
extern uint32_t wiixl_import__totk_world__GetDay(int32_t* out);
extern uint32_t wiixl_import__totk_world__GetWeather(int32_t* out);
extern uint32_t wiixl_import__totk_world__SetWeather(int32_t type, uint32_t lockOut);
extern uint32_t wiixl_import__totk_world__ClearWeather(void);
extern uint32_t wiixl_import__totk_world__IsWeatherOverridden(void);
extern uint32_t wiixl_import__totk_world__WeatherName(int32_t type, char* out, uint32_t cap);
extern uint32_t wiixl_import__totk_world__WeatherFromName(const char* name, int32_t* out);
extern int32_t wiixl_import__totk_world__WeatherTypeCount(void);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require totk.world@1.0 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_totk_world {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 0;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/framework/modules.md.
#define WXL_USE_totk_world(sym) \
    inline decltype(&wiixl_import__totk_world__##sym) volatile sym = \
        &wiixl_import__totk_world__##sym
