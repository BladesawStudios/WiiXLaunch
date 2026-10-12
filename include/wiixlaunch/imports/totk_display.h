// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// totk.display v1.0, 27 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_totk_display(FpsSupported); }
//     S::FpsSupported(...);
//
// so a mod that uses two symbols imports two, not all 27.
//
// The comments are the SURFACE's own, carried across - they say why a
// symbol behaves as it does, which is the half a signature cannot.
#pragma once

#include <cstdint>

extern "C" {
extern uint32_t wiixl_import__totk_display__FpsSupported(void);
extern uint32_t wiixl_import__totk_display__ResolutionSupported(void);
extern uint32_t wiixl_import__totk_display__IsUltraCamLoaded(void);
extern uint32_t wiixl_import__totk_display__HoldForever(void);
extern uint32_t wiixl_import__totk_display__GetPresentInterval(void);
extern uint32_t wiixl_import__totk_display__GetFramePeriodUs(void);
extern uint32_t wiixl_import__totk_display__GetFrameWorkUs(void);
extern uint32_t wiixl_import__totk_display__SetPresentInterval(uint32_t interval, uint32_t frames);
extern void wiixl_import__totk_display__ReleasePresentInterval(void);
extern uint32_t wiixl_import__totk_display__IsPresentIntervalOverridden(void);
extern uint32_t wiixl_import__totk_display__SetPresentIntervalRange(uint32_t fastest, uint32_t slowest, uint32_t frames);
extern void wiixl_import__totk_display__ReleasePresentIntervalRange(void);
extern uint32_t wiixl_import__totk_display__IsPresentIntervalRangeOverridden(void);
extern uint32_t wiixl_import__totk_display__GetResolutionScale(float* out);
extern uint32_t wiixl_import__totk_display__SetOutputResolution(uint32_t w, uint32_t h);
extern uint32_t wiixl_import__totk_display__SetDockedRenderSize(uint32_t w, uint32_t h);
extern uint32_t wiixl_import__totk_display__SetVariableFrameRate(uint32_t frames);
extern void wiixl_import__totk_display__ReleaseVariableFrameRate(void);
extern uint32_t wiixl_import__totk_display__IsVariableFrameRateOn(void);
extern uint32_t wiixl_import__totk_display__GetGameStepUs(void);
extern uint32_t wiixl_import__totk_display__ReserveResolution(uint32_t w, uint32_t h);
extern uint32_t wiixl_import__totk_display__GetMaxResolutionScale(float* out);
extern uint32_t wiixl_import__totk_display__GetRenderSize(uint32_t* w, uint32_t* h);
extern uint32_t wiixl_import__totk_display__IsDynamicResolutionOn(void);
extern uint32_t wiixl_import__totk_display__SetResolutionScale(float scale, uint32_t frames);
extern void wiixl_import__totk_display__ReleaseResolutionScale(void);
extern uint32_t wiixl_import__totk_display__IsResolutionScaleOverridden(void);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require totk.display@1.0 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_totk_display {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 0;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/framework/modules.md.
#define WXL_USE_totk_display(sym) \
    inline decltype(&wiixl_import__totk_display__##sym) volatile sym = \
        &wiixl_import__totk_display__##sym
