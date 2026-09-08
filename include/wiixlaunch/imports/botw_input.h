// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// botw.input v1.1, 21 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_botw_input(SupportsInjection); }
//     S::SupportsInjection(...);
//
// so a mod that uses two symbols imports two, not all 21.
#pragma once

#include <cstdint>

// Callback types this surface takes. Declared rather than substituted:
// a function-pointer alias cannot be spliced into a declarator without
// moving the parameter's name inside the parens.
using ModFrameFn = void (*)();

extern "C" {
extern uint32_t wiixl_import__botw_input__SupportsInjection(void);
extern uint32_t wiixl_import__botw_input__Init(void);
extern uint32_t wiixl_import__botw_input__HeldButtons(void);
extern uint32_t wiixl_import__botw_input__MaskFor(int32_t button);
extern uint32_t wiixl_import__botw_input__IsPressed(int32_t button);
extern uint32_t wiixl_import__botw_input__GetLeftStick(float* out2);
extern uint32_t wiixl_import__botw_input__GetRightStick(float* out2);
extern uint32_t wiixl_import__botw_input__HoldInputCapture(uint32_t frames);
extern uint32_t wiixl_import__botw_input__SetInputCapture(uint32_t on);
extern uint32_t wiixl_import__botw_input__IsInputCaptured(void);
extern uint32_t wiixl_import__botw_input__Hold(uint32_t buttonMask, uint32_t frames);
extern uint32_t wiixl_import__botw_input__HoldIndefinitely(uint32_t buttonMask);
extern uint32_t wiixl_import__botw_input__Send(uint32_t buttons, uint32_t setLeftStick, float leftX, float leftY, uint32_t setRightStick, float rightX, float rightY, uint32_t frames);
extern uint32_t wiixl_import__botw_input__HoldForever(void);
extern void wiixl_import__botw_input__Release(void);
extern uint32_t wiixl_import__botw_input__IsInjecting(void);
extern uint32_t wiixl_import__botw_input__InjectedButtons(void);
extern uint32_t wiixl_import__botw_input__InjectorCount(void);
extern uint32_t wiixl_import__botw_input__RegisterFrame(ModFrameFn fn);
extern uint32_t wiixl_import__botw_input__FrameCallbackCount(void);
extern void wiixl_import__botw_input__LogFrameState(void);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require botw.input@1.1 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_botw_input {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 1;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/modules.md.
#define WXL_USE_botw_input(sym) \
    inline decltype(&wiixl_import__botw_input__##sym) volatile sym = \
        &wiixl_import__botw_input__##sym
