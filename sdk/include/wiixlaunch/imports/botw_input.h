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
//
// The comments are the SURFACE's own, carried across - they say why a
// symbol behaves as it does, which is the half a signature cannot.
#pragma once

#include <cstdint>

// Callback types this surface takes. Declared rather than substituted:
// a function-pointer alias cannot be spliced into a declarator without
// moving the parameter's name inside the parens.
using ModFrameFn = void (*)();

extern "C" {
extern uint32_t wiixl_import__botw_input__SupportsInjection(void);

// Installs the controller read hooks. Idempotent in the module, for the same
// reason Player::Init is: several mods may each need it and none can see that
// another already did.
extern uint32_t wiixl_import__botw_input__Init(void);
extern uint32_t wiixl_import__botw_input__HeldButtons(void);
extern uint32_t wiixl_import__botw_input__MaskFor(int32_t button);
extern uint32_t wiixl_import__botw_input__IsPressed(int32_t button);

// Sticks through a float[2]: x, y. Two calls rather than one taking four
// pointers, because "which stick" is a decision the call site should show.
extern uint32_t wiixl_import__botw_input__GetLeftStick(float* out2);
extern uint32_t wiixl_import__botw_input__GetRightStick(float* out2);

// Input capture stops the GAME seeing the controller while a mod owns it - a
// menu being driven with the same stick that would otherwise move Link.
//
// The held form is the one to use. SetInputCapture latches until something
// clears it, so a mod that crashes with capture on leaves the player unable to
// move; HoldInputCapture expires on its own after a few frames, so a mod that
// stops calling it simply gives control back.
extern uint32_t wiixl_import__botw_input__HoldInputCapture(uint32_t frames);
extern uint32_t wiixl_import__botw_input__SetInputCapture(uint32_t on);
extern uint32_t wiixl_import__botw_input__IsInputCaptured(void);

// Holds a mask for a number of frames, then releases on its own. A frame count
// rather than an open-ended hold, because a mod that sets a button and crashes
// should not leave the game holding it forever.
extern uint32_t wiixl_import__botw_input__Hold(uint32_t buttonMask, uint32_t frames);

// Indefinite, and deliberately its own symbol. "Press A for 8 frames" and
// "press A until I say stop" have very different failure modes, and a mod
// choosing the second should have written the second.
extern uint32_t wiixl_import__botw_input__HoldIndefinitely(uint32_t buttonMask);

// The full form, flattened. Controller::Input is a struct and a struct must not
// cross this boundary - a mod compiled against one layout and run against
// another would read a different field with no error anywhere. So the fields
// become parameters, and the two "did the caller mean to set this stick"
// booleans stay explicit rather than being inferred from a zero, because
// centring a stick and leaving it alone are different instructions.
extern uint32_t wiixl_import__botw_input__Send(uint32_t buttons, uint32_t setLeftStick, float leftX, float leftY, uint32_t setRightStick, float rightX, float rightY, uint32_t frames);

// The frame count that means "until released". Exported so a mod does not
// hard-code 0xFFFFFFFF, which is the sort of constant that stops being true
// quietly.
extern uint32_t wiixl_import__botw_input__HoldForever(void);
extern void wiixl_import__botw_input__Release(void);
extern uint32_t wiixl_import__botw_input__IsInjecting(void);
extern uint32_t wiixl_import__botw_input__InjectedButtons(void);

// How many modules have synthesised input, for the state report and for a mod
// that wants to know it is not alone.
extern uint32_t wiixl_import__botw_input__InjectorCount(void);

// The buttons held this frame, VPAD and KPAD merged. MaskFor turns one Button
// enumerator into its bit, so a mod tests `held & MaskFor(kA)` rather than
// keeping its own copy of the bit layout - the sort of table that stops
// matching without anyone noticing. Read through the module's
// own merged state rather than one source: both hooks fire every frame, and
// taking either alone means an idle GamePad zeroes a Pro Controller's input.
// Registers a per-frame callback. Init() first, because registering into a
// dispatcher nothing calls is the failure this exists to prevent.
extern uint32_t wiixl_import__botw_input__RegisterFrame(ModFrameFn fn);
extern uint32_t wiixl_import__botw_input__FrameCallbackCount(void);

// What the frame dispatcher has been doing. Reported at the load point the same
// way PlayerTick is.
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
// address - the module fails to relocate. See docs/framework/modules.md.
#define WXL_USE_botw_input(sym) \
    inline decltype(&wiixl_import__botw_input__##sym) volatile sym = \
        &wiixl_import__botw_input__##sym
