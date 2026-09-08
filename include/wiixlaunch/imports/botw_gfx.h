// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// botw.gfx v1.0, 21 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_botw_gfx(IsGX2); }
//     S::IsGX2(...);
//
// so a mod that uses two symbols imports two, not all 21.
#pragma once

#include <cstdint>

// Callback types this surface takes. Declared rather than substituted:
// a function-pointer alias cannot be spliced into a declarator without
// moving the parameter's name inside the parens.
using ModDrawFn = void (*)(uintptr_t cmdBuf, uintptr_t dstTexture, int32_t w, int32_t h);

extern "C" {
extern uint32_t wiixl_import__botw_gfx__IsGX2(void);
extern uint32_t wiixl_import__botw_gfx__SupportsBatching(void);
extern uint32_t wiixl_import__botw_gfx__SupportsBackdrop(void);
extern uint32_t wiixl_import__botw_gfx__Init(void);
extern uint32_t wiixl_import__botw_gfx__RegisterDraw(ModDrawFn fn);
extern uint32_t wiixl_import__botw_gfx__DrawCallbackCount(void);
extern uint32_t wiixl_import__botw_gfx__CreateTexture(const void* rgba, uint32_t size, int32_t width, int32_t height, int32_t format);
extern uint32_t wiixl_import__botw_gfx__LoadTexture(const char* path, uint32_t maxFileSize);
extern uint32_t wiixl_import__botw_gfx__SupportsLoadTexture(void);
extern uint32_t wiixl_import__botw_gfx__GetTextureSize(uint32_t texture, uint32_t* w, uint32_t* h);
extern uint32_t wiixl_import__botw_gfx__DrawSprite(uintptr_t cmdBuf, uintptr_t dstTexture, uint32_t texture, float x, float y, float w, float h, float r, float g, float b, float a);
extern uint32_t wiixl_import__botw_gfx__DrawMesh(uintptr_t cmdBuf, uintptr_t dstTexture, const float* vertices, uint32_t vertexCount);
extern uint32_t wiixl_import__botw_gfx__LoadMesh(const char* path, uint32_t maxFileSize, uint32_t* outVertexCount);
extern uint32_t wiixl_import__botw_gfx__DrawMeshHandle(uintptr_t cmdBuf, uintptr_t dstTexture, uint32_t mesh);
extern uint32_t wiixl_import__botw_gfx__BlurBackdrop(uintptr_t dstColorBuffer, uint32_t downscale, uint32_t passes);
extern uint32_t wiixl_import__botw_gfx__BeginBatch(uintptr_t dstTexture);
extern uint32_t wiixl_import__botw_gfx__BatchQuad(uint32_t texture, const float* verts40, uint32_t floatCount);
extern uint32_t wiixl_import__botw_gfx__EndBatch(void);
extern uint32_t wiixl_import__botw_gfx__BackdropReady(void);
extern uint32_t wiixl_import__botw_gfx__BackdropTexture(void);
extern uintptr_t wiixl_import__botw_gfx__AllocMEM1(uint32_t size, uint32_t align);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require botw.gfx@1.0 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_botw_gfx {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 0;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/modules.md.
#define WXL_USE_botw_gfx(sym) \
    inline decltype(&wiixl_import__botw_gfx__##sym) volatile sym = \
        &wiixl_import__botw_gfx__##sym
