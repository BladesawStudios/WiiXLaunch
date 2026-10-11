// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// totk.input v1.1, 18 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_totk_input(IsLive); }
//     S::IsLive(...);
//
// so a mod that uses two symbols imports two, not all 18.
//
// The comments are the SURFACE's own, carried across - they say why a
// symbol behaves as it does, which is the half a signature cannot.
#pragma once

#include <cstdint>

extern "C" {
extern uint32_t wiixl_import__totk_input__IsLive(void);
extern uint32_t wiixl_import__totk_input__HeldButtons(void);
extern uint32_t wiixl_import__totk_input__MaskFor(int32_t button);
extern uint32_t wiixl_import__totk_input__IsPressed(int32_t button);

// Sticks through a float[2]: x, y - botw.input's shape.
extern uint32_t wiixl_import__totk_input__GetLeftStick(float* out2);
extern uint32_t wiixl_import__totk_input__GetRightStick(float* out2);

// 0 frames means botw.input's default of 8. Returns 0 if there is no live
// frame clock to count the frames with - see input.hpp.
extern uint32_t wiixl_import__totk_input__HoldInputCapture(uint32_t frames);
extern uint32_t wiixl_import__totk_input__SetInputCapture(uint32_t on);
extern uint32_t wiixl_import__totk_input__IsInputCaptured(void);
extern uint32_t wiixl_import__totk_input__SupportsInjection(void);

// Holds a mask for a number of frames, then releases on its own.
extern uint32_t wiixl_import__totk_input__Hold(uint32_t buttonMask, uint32_t frames);

// Its own symbol, as in botw.input: "press A for 8 frames" and "press A until
// I say stop" fail differently, and a mod choosing the second should have
// written the second. The dead man's switch still ends it 300 frames after the
// last call unless it is repeated.
extern uint32_t wiixl_import__totk_input__HoldIndefinitely(uint32_t buttonMask);

// The full form, flattened - botw.input's exact signature. No struct crosses.
extern uint32_t wiixl_import__totk_input__Send(uint32_t buttons, uint32_t setLeftStick, float leftX, float leftY, uint32_t setRightStick, float rightX, float rightY, uint32_t frames);
extern uint32_t wiixl_import__totk_input__HoldForever(void);
extern void wiixl_import__totk_input__Release(void);
extern uint32_t wiixl_import__totk_input__IsInjecting(void);
extern uint32_t wiixl_import__totk_input__InjectedButtons(void);
extern uint32_t wiixl_import__totk_input__InjectorCount(void);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require totk.input@1.1 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_totk_input {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 1;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/framework/modules.md.
#define WXL_USE_totk_input(sym) \
    inline decltype(&wiixl_import__totk_input__##sym) volatile sym = \
        &wiixl_import__totk_input__##sym
