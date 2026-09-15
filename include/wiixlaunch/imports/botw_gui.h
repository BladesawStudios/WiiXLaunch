// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// botw.gui v1.1, 90 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_botw_gui(Init); }
//     S::Init(...);
//
// so a mod that uses two symbols imports two, not all 90.
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
extern uint32_t wiixl_import__botw_gui__Init(void);
extern uint32_t wiixl_import__botw_gui__IsReady(void);
extern uint32_t wiixl_import__botw_gui__RegisterFrame(ModFrameFn fn);
extern uint32_t wiixl_import__botw_gui__FrameCallbackCount(void);
extern uint32_t wiixl_import__botw_gui__SetAssetPaths(const char* fontArchive, const char* layoutArchive);
extern uint32_t wiixl_import__botw_gui__LoadNow(void);
extern uint32_t wiixl_import__botw_gui__RequestFont(int32_t fontId, uint32_t load);
extern uint32_t wiixl_import__botw_gui__FontRequested(int32_t fontId);
extern uint32_t wiixl_import__botw_gui__SetBackdropBlur(uint32_t enabled, uint32_t downscale, uint32_t passes);
extern uint32_t wiixl_import__botw_gui__SetPixelSnapping(uint32_t on);
extern uint32_t wiixl_import__botw_gui__SetOutputAspect(float aspect);
extern uint32_t wiixl_import__botw_gui__GetEffectiveOutputAspect(float* out);
extern uint32_t wiixl_import__botw_gui__SetUiSounds(uint32_t on);
extern uint32_t wiixl_import__botw_gui__StyleCreate(void);
extern uint32_t wiixl_import__botw_gui__StyleRelease(uint32_t id);
extern uint32_t wiixl_import__botw_gui__StyleFont(uint32_t id, int32_t fontId);
extern uint32_t wiixl_import__botw_gui__StyleSize(uint32_t id, float x, float y);

// Top and bottom separately, because the game's own text is frequently a
// vertical gradient and collapsing them would lose that.
extern uint32_t wiixl_import__botw_gui__StyleColor(uint32_t id, uint32_t rgbaTop, uint32_t rgbaBottom);
extern uint32_t wiixl_import__botw_gui__StyleAlign(uint32_t id, int32_t align, int32_t valign);
extern uint32_t wiixl_import__botw_gui__StyleShadow(uint32_t id, uint32_t on, uint32_t rgba, float dx, float dy);
extern uint32_t wiixl_import__botw_gui__StyleScale(uint32_t id, float k);
extern uint32_t wiixl_import__botw_gui__StyleSpacing(uint32_t id, float charSpace, float lineSpace);
extern uint32_t wiixl_import__botw_gui__StyleKerning(uint32_t id, uint32_t on);
extern uint32_t wiixl_import__botw_gui__InFrame(void);
extern uint32_t wiixl_import__botw_gui__CanvasSize(float* w, float* h);
extern uint32_t wiixl_import__botw_gui__DeltaSeconds(float* out);
extern uint32_t wiixl_import__botw_gui__TimeSeconds(float* out);
extern uint32_t wiixl_import__botw_gui__FrameNumber(void);
extern uint32_t wiixl_import__botw_gui__AssetsReady(void);
extern uint32_t wiixl_import__botw_gui__PushAlpha(float factor);
extern uint32_t wiixl_import__botw_gui__PopAlpha(void);
extern uint32_t wiixl_import__botw_gui__Pressed(int32_t button);
extern uint32_t wiixl_import__botw_gui__Held(int32_t button);
extern uint32_t wiixl_import__botw_gui__Nav(int32_t direction);
extern uint32_t wiixl_import__botw_gui__Accept(void);
extern uint32_t wiixl_import__botw_gui__Cancel(void);
extern uint32_t wiixl_import__botw_gui__Sticks(float* left2, float* right2);

// Stops the game seeing the input while a menu is open. Held rather than
// latched, so a mod that stops calling it releases the game's input rather than
// wedging the controller.
extern uint32_t wiixl_import__botw_gui__CaptureInput(void);
extern uint32_t wiixl_import__botw_gui__IsInputCaptured(void);
extern uint32_t wiixl_import__botw_gui__Rect(float x, float y, float w, float h, uint32_t rgba);
extern uint32_t wiixl_import__botw_gui__RectGradient(float x, float y, float w, float h, uint32_t rgbaTop, uint32_t rgbaBottom);
extern uint32_t wiixl_import__botw_gui__RoundedBox(float x, float y, float w, float h, uint32_t rgba, float radius);
extern uint32_t wiixl_import__botw_gui__RoundedOutline(float x, float y, float w, float h, uint32_t rgba, float radius, float thickness);
extern uint32_t wiixl_import__botw_gui__FrostedBox(float x, float y, float w, float h, uint32_t rgba, float radius);
extern uint32_t wiixl_import__botw_gui__MessageWindow(float x, float y, float w, float h, uint32_t rgba, uint32_t decorations);
extern uint32_t wiixl_import__botw_gui__SelectFrame(float x, float y, float w, float h, uint32_t frame, uint32_t glow);
extern uint32_t wiixl_import__botw_gui__BlurBehind(float x, float y, float w, float h, uint32_t tint);
extern uint32_t wiixl_import__botw_gui__Text(float x, float y, const char* text, uint32_t styleId);
extern uint32_t wiixl_import__botw_gui__TextBox(float x, float y, float w, float h, const char* text, uint32_t styleId, uint32_t wrap);
extern uint32_t wiixl_import__botw_gui__MeasureText(const char* text, uint32_t styleId, float* w, float* h, float wrapWidth);

// Sprites are the GUI's own atlas, by id. A mod's own image goes through
// botw.gfx's texture handles instead - see GuiImageTexture below.
extern uint32_t wiixl_import__botw_gui__Image(int32_t sprite, float x, float y, float w, float h, uint32_t tint);

// orient is the GUI::Orient bitmask: 0 none, 1 flip H, 2 flip V, 4 rotate 90,
// 8 rotate 180, 12 rotate 270. rotation is degrees, applied about the rect's
// centre, and is independent of the orient flags.
extern uint32_t wiixl_import__botw_gui__ImageEx(int32_t sprite, float x, float y, float w, float h, uint32_t tint, uint32_t orient, int32_t blend, float rotation);
extern uint32_t wiixl_import__botw_gui__SpriteReady(int32_t sprite);
extern uint32_t wiixl_import__botw_gui__SpriteSize(int32_t sprite, float* w, float* h);
extern uint32_t wiixl_import__botw_gui__FontReady(int32_t fontId);
extern uint32_t wiixl_import__botw_gui__Button(float x, float y, float w, float h, const char* label, uint32_t boxRgba);
extern uint32_t wiixl_import__botw_gui__PlateButton(float x, float y, float w, float h, const char* label);
extern uint32_t wiixl_import__botw_gui__Toggle(float x, float y, float w, float h, const char* label, uint32_t* value, const char* onText, const char* offText);
extern uint32_t wiixl_import__botw_gui__Slider(float x, float y, float w, float h, const char* label, float* value, float minValue, float maxValue, float step);

// options is an array of C strings the MOD owns and which must outlive the
// call - it is read during the draw and not retained.
extern uint32_t wiixl_import__botw_gui__Selector(float x, float y, float w, float h, const char* label, int32_t* index, const char* const* options, int32_t count);
extern uint32_t wiixl_import__botw_gui__List(float x, float y, float w, float h, const char* const* items, int32_t count, int32_t* index);
extern uint32_t wiixl_import__botw_gui__Label(float x, float y, float w, float h, const char* text, uint32_t styleId);
extern uint32_t wiixl_import__botw_gui__Plate(float x, float y, float w, float h, uint32_t rgba);
extern uint32_t wiixl_import__botw_gui__MessageBox(const char* text, const char* name, float alpha, uint32_t showArrow);
extern uint32_t wiixl_import__botw_gui__ButtonIcon(int32_t sprite, float x, float y, float size, uint32_t rgba);

// Returns the width it drew, so a row of hints can be laid out left to right
// without the mod measuring each one first.
extern uint32_t wiixl_import__botw_gui__KeyHint(int32_t sprite, const char* label, float x, float y, uint32_t rgba, float* outWidth);
extern uint32_t wiixl_import__botw_gui__CursorCorners(float x, float y, float w, float h, uint32_t rgba, float size);
extern uint32_t wiixl_import__botw_gui__CursorBrackets(float x, float y, float w, float h, uint32_t rgba, float size);
extern uint32_t wiixl_import__botw_gui__BoxedCursor(float x, float y, float w, float h, uint32_t rgba);
extern uint32_t wiixl_import__botw_gui__ImageUV(int32_t sprite, float x, float y, float w, float h, float u0, float v0, float u1, float v1, uint32_t tint);
extern uint32_t wiixl_import__botw_gui__ImageAt(int32_t sprite, float x, float y, uint32_t tint);
extern uint32_t wiixl_import__botw_gui__BlurBehindFaded(float x, float y, float w, float h, uint32_t tint);
extern uint32_t wiixl_import__botw_gui__DeviceSize(uint32_t* w, uint32_t* h);
extern uint32_t wiixl_import__botw_gui__PixelScale(float* out2);
extern uint32_t wiixl_import__botw_gui__ViewportOffset(float* out2);
extern uint32_t wiixl_import__botw_gui__Snap(float x, float y, float* out2);

// A 0..1 sawtooth and a -1..1 sine over the given period. Both come from the
// canvas's own clock rather than a mod's frame counter, so animations stay in
// step with the UI and with each other.
extern uint32_t wiixl_import__botw_gui__Phase(float periodSeconds, float* out);
extern uint32_t wiixl_import__botw_gui__Wave(float periodSeconds, float* out);
extern uint32_t wiixl_import__botw_gui__FramesPerSecond(float* out);
extern uint32_t wiixl_import__botw_gui__CurrentAlpha(float* out);
extern uint32_t wiixl_import__botw_gui__SetScalingMode(int32_t mode);
extern int32_t wiixl_import__botw_gui__GetScalingMode(void);
extern uint32_t wiixl_import__botw_gui__GetOutputAspect(float* out);
extern uint32_t wiixl_import__botw_gui__IsBackdropBlurEnabled(void);
extern uint32_t wiixl_import__botw_gui__SetUiSoundEvents(const char* cursorMove, const char* decide);
extern uint32_t wiixl_import__botw_gui__AreUiSoundsEnabled(void);
extern uint32_t wiixl_import__botw_gui__SetLoadBudget(uint32_t bytesPerFrame);
extern uint32_t wiixl_import__botw_gui__FontSheetBytes(int32_t fontId);
extern uint32_t wiixl_import__botw_gui__SetFocus(int32_t index);
extern uint32_t wiixl_import__botw_gui__ClaimFocus(void);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require botw.gui@1.1 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_botw_gui {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 1;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/framework/modules.md.
#define WXL_USE_botw_gui(sym) \
    inline decltype(&wiixl_import__botw_gui__##sym) volatile sym = \
        &wiixl_import__botw_gui__##sym
