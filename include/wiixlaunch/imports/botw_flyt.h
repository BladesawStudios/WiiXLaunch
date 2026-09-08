// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// botw.flyt v1.0, 26 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_botw_flyt(Init); }
//     S::Init(...);
//
// so a mod that uses two symbols imports two, not all 26.
#pragma once

#include <cstdint>

// Callback types this surface takes. Declared rather than substituted:
// a function-pointer alias cannot be spliced into a declarator without
// moving the parameter's name inside the parens.
using ModLoadedFn = void (*)(uint32_t layoutHandle, const char* name);

extern "C" {
extern uint32_t wiixl_import__botw_flyt__Init(void);
extern uint32_t wiixl_import__botw_flyt__OnLayoutLoaded(ModLoadedFn fn);
extern uint32_t wiixl_import__botw_flyt__AddRedirect(const char* from, const char* to);
extern uint32_t wiixl_import__botw_flyt__RootPane(uint32_t layoutHandle);
extern uint32_t wiixl_import__botw_flyt__FindPane(uint32_t layoutHandle, const char* name);
extern uint32_t wiixl_import__botw_flyt__FindChild(uint32_t paneHandle, const char* name, uint32_t recursive);
extern uint32_t wiixl_import__botw_flyt__PaneName(uint32_t paneHandle, char* out, uint32_t cap);
extern uint32_t wiixl_import__botw_flyt__SetTranslate(uint32_t paneHandle, float x, float y, float z);
extern uint32_t wiixl_import__botw_flyt__GetGlobalTranslate(uint32_t paneHandle, float* out3);
extern uint32_t wiixl_import__botw_flyt__SetGlobalTranslate(uint32_t paneHandle, float x, float y, float z);
extern uint32_t wiixl_import__botw_flyt__SetRotate(uint32_t paneHandle, float x, float y, float z);
extern uint32_t wiixl_import__botw_flyt__SetScale(uint32_t paneHandle, float x, float y);
extern uint32_t wiixl_import__botw_flyt__SetSize(uint32_t paneHandle, float w, float h);
extern uint32_t wiixl_import__botw_flyt__SetVisible(uint32_t paneHandle, uint32_t visible);
extern uint32_t wiixl_import__botw_flyt__GetTranslate(uint32_t paneHandle, float* out3);
extern uint32_t wiixl_import__botw_flyt__GetParentGlobalTranslate(uint32_t paneHandle, float* out3);
extern uint32_t wiixl_import__botw_flyt__GetRotate(uint32_t paneHandle, float* out3);
extern uint32_t wiixl_import__botw_flyt__GetScale(uint32_t paneHandle, float* out2);
extern uint32_t wiixl_import__botw_flyt__GetSize(uint32_t paneHandle, float* out2);
extern uint32_t wiixl_import__botw_flyt__IsVisible(uint32_t paneHandle);
extern uint32_t wiixl_import__botw_flyt__GetAlpha(uint32_t paneHandle, uint32_t* out);
extern uint32_t wiixl_import__botw_flyt__SetAlpha(uint32_t paneHandle, uint32_t alpha);
extern uint32_t wiixl_import__botw_flyt__GetEffectiveAlpha(uint32_t paneHandle, float* out);
extern uint32_t wiixl_import__botw_flyt__SetColor(uint32_t paneHandle, uint32_t rgba);
extern uint32_t wiixl_import__botw_flyt__GetCornerColor(uint32_t paneHandle, int32_t corner, uint32_t* rgba);
extern uint32_t wiixl_import__botw_flyt__SetCornerColor(uint32_t paneHandle, int32_t corner, uint32_t rgba);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require botw.flyt@1.0 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_botw_flyt {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 0;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/modules.md.
#define WXL_USE_botw_flyt(sym) \
    inline decltype(&wiixl_import__botw_flyt__##sym) volatile sym = \
        &wiixl_import__botw_flyt__##sym
