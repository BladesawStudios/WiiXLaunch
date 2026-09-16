// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// botw.world v1.0, 51 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_botw_world(SupportsWeather); }
//     S::SupportsWeather(...);
//
// so a mod that uses two symbols imports two, not all 51.
//
// The comments are the SURFACE's own, carried across - they say why a
// symbol behaves as it does, which is the half a signature cannot.
#pragma once

#include <cstdint>

extern "C" {
extern uint32_t wiixl_import__botw_world__SupportsWeather(void);
extern uint32_t wiixl_import__botw_world__SupportsTime(void);
extern uint32_t wiixl_import__botw_world__SupportsClimate(void);

// Compile-time support is not the same as "there is a world right now" - a
// title screen has neither weather nor a clock. Both questions are asked
// separately because they have different answers at different moments.
extern uint32_t wiixl_import__botw_world__WeatherAvailable(void);
extern uint32_t wiixl_import__botw_world__TimeAvailable(void);
extern uint32_t wiixl_import__botw_world__ClimateAvailable(void);
extern uint32_t wiixl_import__botw_world__GetWeather(int32_t* out);
extern uint32_t wiixl_import__botw_world__SetWeather(int32_t type, uint32_t lockOut);
extern uint32_t wiixl_import__botw_world__ClearWeather(void);
extern uint32_t wiixl_import__botw_world__WeatherName(int32_t type, char* out, uint32_t cap);
extern uint32_t wiixl_import__botw_world__WeatherFromName(const char* name, int32_t* out);
extern int32_t wiixl_import__botw_world__WeatherTypeCount(void);
extern int32_t wiixl_import__botw_world__GetWeatherOverride(void);
extern uint32_t wiixl_import__botw_world__IsWeatherOverridden(void);
extern uint32_t wiixl_import__botw_world__IsWeatherLocked(void);
extern int32_t wiixl_import__botw_world__GetMagicWeather(void);
extern uint32_t wiixl_import__botw_world__IsRaining(void);
extern uint32_t wiixl_import__botw_world__IsSnowing(void);
extern uint32_t wiixl_import__botw_world__IsThundering(void);

// A hold re-applies the weather every frame, which is a different thing from
// setting it once and a different thing again from locking it out. Three
// separate calls because they are three separate intentions.
extern uint32_t wiixl_import__botw_world__HoldWeather(int32_t type, uint32_t lockOut);
extern void wiixl_import__botw_world__ReleaseWeather(void);
extern int32_t wiixl_import__botw_world__GetWeatherHold(void);

// How many frames the hold has left. A hold that is about to lapse and one that
// will run forever are different states, and a mod refreshing a hold needs to
// know which it is looking at rather than re-applying every frame.
extern int32_t wiixl_import__botw_world__GetWeatherHoldFrames(void);

// Drives the hold. Called from a tick by whichever mod set it; without this the
// hold is set and never re-applied, which looks exactly like the weather
// override silently failing.
extern void wiixl_import__botw_world__TickWeatherHold(void);
extern int32_t wiixl_import__botw_world__GetWeatherClimate(void);
extern uint32_t wiixl_import__botw_world__ClimateName(int32_t climate, char* out, uint32_t cap);
extern uint32_t wiixl_import__botw_world__GetGameTime(float* hours);
extern uint32_t wiixl_import__botw_world__GetGameTimeHM(int32_t* hour, int32_t* minute);
extern uint32_t wiixl_import__botw_world__SetGameTime(float hours);
extern uint32_t wiixl_import__botw_world__SetGameTimeHM(int32_t hour, int32_t minute);
extern uint32_t wiixl_import__botw_world__GetGameTimeRaw(float* out);
extern uint32_t wiixl_import__botw_world__SetGameTimeRaw(float units);
extern uint32_t wiixl_import__botw_world__GetDay(int32_t* out);
extern uint32_t wiixl_import__botw_world__SetDay(int32_t day);
extern uint32_t wiixl_import__botw_world__GetDivision(int32_t* out);
extern uint32_t wiixl_import__botw_world__GetNextDivision(int32_t* out);
extern uint32_t wiixl_import__botw_world__DivisionName(int32_t division, char* out, uint32_t cap);
extern uint32_t wiixl_import__botw_world__IsNight(void);
extern uint32_t wiixl_import__botw_world__IsTimeFlowing(void);
extern uint32_t wiixl_import__botw_world__GetTimeScale(float* out);
extern uint32_t wiixl_import__botw_world__SetTimeScale(float scale);

// The two curves the game blends between over the day, and where in that blend
// it currently is. A mod predicting how cold somewhere will be at night needs
// all three - the current temperature alone cannot answer it.
extern uint32_t wiixl_import__botw_world__GetDayTemperature(float altitude, float* out);
extern uint32_t wiixl_import__botw_world__GetNightTemperature(float altitude, float* out);
extern uint32_t wiixl_import__botw_world__GetNightBlend(float* out);

// The game forcing a temperature is a different state from a mod overriding
// one, and a mod that cannot tell them apart will fight the game over a value
// it was never going to win.
extern uint32_t wiixl_import__botw_world__IsTemperatureForcedByGame(void);
extern uint32_t wiixl_import__botw_world__GetTemperature(float* celsius);
extern uint32_t wiixl_import__botw_world__GetTemperatureAt(float altitude, float* celsius);
extern uint32_t wiixl_import__botw_world__SetTemperature(float celsius);
extern uint32_t wiixl_import__botw_world__SetTemperatureOffset(float degrees);
extern uint32_t wiixl_import__botw_world__ClearTemperature(void);
extern uint32_t wiixl_import__botw_world__IsTemperatureOverridden(void);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require botw.world@1.0 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_botw_world {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 0;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/framework/modules.md.
#define WXL_USE_botw_world(sym) \
    inline decltype(&wiixl_import__botw_world__##sym) volatile sym = \
        &wiixl_import__botw_world__##sym
