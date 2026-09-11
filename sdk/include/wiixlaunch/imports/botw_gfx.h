// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// botw.gfx v1.1, 22 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_botw_gfx(IsGX2); }
//     S::IsGX2(...);
//
// so a mod that uses two symbols imports two, not all 22.
//
// The comments are the SURFACE's own, carried across - they say why a
// symbol behaves as it does, which is the half a signature cannot.
#pragma once

#include <cstdint>

// Callback types this surface takes. Declared rather than substituted:
// a function-pointer alias cannot be spliced into a declarator without
// moving the parameter's name inside the parens.
using ModDrawFn = void (*)(uintptr_t cmdBuf, uintptr_t dstTexture, int32_t w, int32_t h);
using ModInitFn = void (*)();

extern "C" {
extern uint32_t wiixl_import__botw_gfx__IsGX2(void);

// GX2 has a sprite batcher, MEM1, a backdrop blur and surface allocation that
// NVN does not. A mod asks rather than discovering by getting zeroes.
extern uint32_t wiixl_import__botw_gfx__SupportsBatching(void);
extern uint32_t wiixl_import__botw_gfx__SupportsBackdrop(void);
extern uint32_t wiixl_import__botw_gfx__Init(void);
extern uint32_t wiixl_import__botw_gfx__RegisterDraw(ModDrawFn fn);

// Called once when the graphics device exists, or immediately if it already
// does. One per module, like a draw callback.
extern uint32_t wiixl_import__botw_gfx__OnInitialized(ModInitFn fn);
extern uint32_t wiixl_import__botw_gfx__DrawCallbackCount(void);
extern uint32_t wiixl_import__botw_gfx__CreateTexture(const void* rgba, uint32_t size, int32_t width, int32_t height, int32_t format);

// Loads from the title's filesystem. The path is whatever the module's loader
// accepts; a mod's own resources live under its scoped directory and are read
// through wiixl.core, so this is for game content and host resources.
extern uint32_t wiixl_import__botw_gfx__LoadTexture(const char* path, uint32_t maxFileSize);
extern uint32_t wiixl_import__botw_gfx__SupportsLoadTexture(void);
extern uint32_t wiixl_import__botw_gfx__GetTextureSize(uint32_t texture, uint32_t* w, uint32_t* h);
extern uint32_t wiixl_import__botw_gfx__DrawSprite(uintptr_t cmdBuf, uintptr_t dstTexture, uint32_t texture, float x, float y, float w, float h, float r, float g, float b, float a);

// Vertices as a flat float array: eight floats each - x,y,z,w then nx,ny,nz,nw,
// matching MeshVertex exactly. Flat rather than a struct pointer for the usual
// reason, and the count is vertices, not floats, because getting that wrong is
// a buffer overrun rather than a wrong picture.
extern uint32_t wiixl_import__botw_gfx__DrawMesh(uintptr_t cmdBuf, uintptr_t dstTexture, const float* vertices, uint32_t vertexCount);

// Loads a mesh off the title's filesystem. MeshData is {pointer, count} and
// cannot cross, so the vertices stay in host memory and the mod gets a handle
// plus the count - it draws with DrawMeshHandle rather than shipping the
// vertices back across for every frame.
extern uint32_t wiixl_import__botw_gfx__LoadMesh(const char* path, uint32_t maxFileSize, uint32_t* outVertexCount);
extern uint32_t wiixl_import__botw_gfx__DrawMeshHandle(uintptr_t cmdBuf, uintptr_t dstTexture, uint32_t mesh);

//
// Renders the frame so far into a blurred texture a mod can draw over. It costs
// two render targets and several full-target draws EVERY FRAME IT IS USED, so
// it is asked for rather than always on, and the handle it returns is the same
// kind of texture handle everything else here uses.
extern uint32_t wiixl_import__botw_gfx__BlurBackdrop(uintptr_t dstColorBuffer, uint32_t downscale, uint32_t passes);
extern uint32_t wiixl_import__botw_gfx__BeginBatch(uintptr_t dstTexture);

// Four vertices, ten floats each, in TextureVertex order:
// x,y,z,w, u,v, r,g,b,a. Forty floats, and the count is checked rather than
// trusted because a short array here is a read past the end of a mod's buffer.
extern uint32_t wiixl_import__botw_gfx__BatchQuad(uint32_t texture, const float* verts40, uint32_t floatCount);
extern uint32_t wiixl_import__botw_gfx__EndBatch(void);
extern uint32_t wiixl_import__botw_gfx__BackdropReady(void);
extern uint32_t wiixl_import__botw_gfx__BackdropTexture(void);
extern uintptr_t wiixl_import__botw_gfx__AllocMEM1(uint32_t size, uint32_t align);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require botw.gfx@1.1 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_botw_gfx {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 1;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/modules.md.
#define WXL_USE_botw_gfx(sym) \
    inline decltype(&wiixl_import__botw_gfx__##sym) volatile sym = \
        &wiixl_import__botw_gfx__##sym
