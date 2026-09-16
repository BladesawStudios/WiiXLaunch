# Framework: Graphics Injection

[« Back to overview](../overview.md)

The [wiixlaunch-botw](https://github.com/TKVSC-Team/wiixlaunch-botw) module's
`NVN` and `GX2` namespaces: drawing your own textures and meshes into Breath
of the Wild's render loop. This is the host-built API; a `.wxlm` reaches the
same functionality through `botw.gfx` and `botw.gui` (see
[SDK: Imports and versioning](../sdk/imports-and-versioning.md)).

```cpp
#include <wiixlaunch/botw/botw.hpp>   // host-built only; a .wxlm uses botw.gfx

using namespace WiiXLaunch::BotW;
```

`NVN` targets Switch's NVN API, `GX2` targets Wii U/Cemu's GX2 API. Same
function shapes, different code underneath. See
[Platform differences](#platform-differences) before assuming a mesh or
sprite call looks identical on both.

`NVN::SupportsNVN` is true on Switch only, `GX2::SupportsGX2` is true on Wii
U/Cemu only. On the platform where a namespace doesn't apply, every
function is a safe no-op. Call either unconditionally without `#if`.

## Hooking into the render loop

```cpp
void OnRender(NVN::CommandBuffer* cmdBuf, void* dstTexture, int width, int height) {
    NVN::DrawSprite(cmdBuf, dstTexture, myTexture, -0.92f, 0.50f, 0.225f, 0.40f);
}

// in WiiXLaunch_Init():
NVN::Init();
NVN::RegisterDrawCallback(OnRender);
NVN::OnInitialized([]() {
    myTexture = NVN::CreateTexture(g_MyTextureBytes, kMyTextureSize);
});
```

`GX2` has the identical three calls; swap the namespace. `Init()` installs
the hook into the game's render loop; call it once from `WiiXLaunch_Init()`
before registering anything.

* `RegisterDrawCallback(cb)`. `cb` runs once per frame, after the game
  finishes drawing but before it's presented. `dstTexture` is the frame you
  draw into; `width`/`height` are its current resolution. Up to 16
  callbacks run, in registration order, every frame.
* `OnInitialized(cb)`. `cb` runs once, the first time the graphics pipeline
  is ready to accept draws (can be several frames after `Init()`). If the
  pipeline is already ready when you call `OnInitialized`, `cb` runs
  immediately. Create textures and load meshes here, not in
  `WiiXLaunch_Init()`, since the device/context doesn't exist yet there. Up
  to 16 init callbacks can be queued.

## Textures

```cpp
// Switch: compiled-in packaged data, no file read at runtime.
NVN::TextureHandle t = NVN::CreateTexture(g_MyTextureBytes, kMyTextureSize);

// Wii U/Cemu: read a packaged file off disk.
GX2::TextureHandle t = GX2::LoadTexture("WiiXLaunch/mods/_host/mytexture.bin");
```

`NVN::CreateTexture(packagedData, packagedSize, minFilter = Linear, magFilter = Linear, wrapMode = ClampToEdge)`
expects Nintendo's packaged texture data format (a fixed header in front of
GPU-ready, already-swizzled pixel bytes), not raw RGBA8. Pack this with
[the module's tool](#packaging-your-own-assets). There's no
`NVN::LoadTexture`; Switch textures are baked into the binary at compile
time.

`GX2::CreateTexture(rgbaBytes, size, width, height, format = 0)` takes raw
RGBA8 bytes plus dimensions and does its own GX2 micro-tiling at runtime.
Normally reached through `GX2::LoadTexture(path, maxFileSize = 1MB)`, which
reads a file packaged by `scripts/pack_resources.py` via `FS::ReadFile` and
calls `CreateTexture` for you. `path` is relative to the Cemu graphic pack's
`content/` folder: `"WiiXLaunch/mods/_host/logo.bin"` resolves to
`content/WiiXLaunch/mods/_host/logo.bin`. The host's own resources live
under the reserved `_host` id, see
[Module resources](loader.md#module-resources).

Texture pools are fixed-size, no heap allocation: 16 live textures on
`NVN`, 64 on `GX2`.

## Meshes

```cpp
struct MeshVertex { float x, y, z, w, nx, ny, nz, nw; };

NVN::DrawMesh(cmdBuf, dstTexture, myVertices, myVertexCount);
```

`MeshVertex` is position plus normal, same layout on both `NVN` and `GX2`.
No material or texture on a mesh; `DrawMesh` colors each pixel from the
vertex normal.

`GX2::LoadMesh(path, maxFileSize = 64KB)` reads a mesh packaged from a
`.obj` and returns `MeshData{ vertices, vertexCount }`. `DrawMesh` re-reads
the `vertices` pointer every frame, copying fresh data into its own ring
buffer, so the backing buffer must stay alive as long as you draw the
mesh. `LoadMesh`'s returned buffer is permanent and never freed. `NVN` has
no `LoadMesh` equivalent; supply your own compiled-in vertex array.

## Drawing

```cpp
NVN::DrawSprite(cmdBuf, dstTexture, textureHandle, x, y, width, height, r = 1, g = 1, b = 1, a = 1);
NVN::DrawMesh(cmdBuf, dstTexture, vertices, vertexCount);
```

`DrawSprite` draws an alpha-blended textured quad. `x`/`y`/`width`/`height`
are in the game's own UI coordinate space, roughly -1 to 1 (`src/main.cpp`
places the host's logo at `x = -0.92, y = 0.50, width = 0.225, height =
0.40`). `r, g, b, a` tint the sprite.

`DrawMesh` draws a depth-tested triangle list. Both `NVN` and `GX2` build
their own private depth texture on first use.

## Platform differences

* `GX2::DrawMesh` scales every vertex's `x` by `height / width` to
  compensate for a non-square viewport. `NVN::DrawMesh` doesn't. A mesh
  authored for one may look stretched on the other unless you account for
  this.
* NVN textures are compiled in; GX2 textures are usually loaded from disk.
  Switch has no runtime texture-loading path today.
* `NVN::GetDevice()` returns a real `NVNdevice*`. `GX2::GetDevice()` always
  returns `nullptr`; use `GX2::GetGraphicsContext()` instead.

## Limits

* 16 draw callbacks, 16 init callbacks, per namespace.
* 16 live textures on `NVN`, 64 on `GX2`.
* Sprites use a ring buffer of fixed-size slots. Drawing more unique
  sprites in one frame than the ring has slots overwrites one still in
  flight; unlikely unless doing something unusual.
* Mesh vertex data shares one bump-allocated arena per namespace. A single
  mesh larger than the arena fails to draw; `WIIXL_LOG`/`BotW::OSLog` calls
  inside `DrawMesh`'s setup report a failed shader or buffer init.

## FS

```cpp
size_t readSize = 0;
bool ok = WiiXLaunch::FS::ReadFile("WiiXLaunch/mydata.bin", buffer, sizeof(buffer), &readSize);
```

`WiiXLaunch::FS` lives in base WiiXLaunch (`<wiixlaunch/fs.hpp>`), not a
game module. `FS::ReadFile(path, outBuffer, maxBufferSize, outReadSize =
nullptr)` and `FS::WriteFile(path, buffer, size, outWrittenSize = nullptr)`
are cross-platform file access, used internally by
`GX2::LoadTexture`/`LoadMesh` and available directly. On Cemu, `path` is
tried as-is and then against likely prefixes (`/vol/content/`, `content/`,
`/vol/content/WiiXLaunch/`), since a bare code cave has no working
directory. On Wii U it's a normal coreinit filesystem call.

## OSLog

`BotW::OSLog(fmt, ...)` is a separate, Cemu-only logger the `GX2`/`FS`
internals use for their own diagnostics. Not `WIIXL_LOG`; see
[Debugging](debugging.md#botwoslog-is-a-different-thing).

## Packaging your own assets

Switch textures are baked into the binary at compile time:

```bash
python vendor/wiixlaunch-botw/tools/pack_texture_nvn.py myimage.png MyTexture --out include/
```

Writes `include/mytexture_texture_bytes.hpp`, defining
`g_MyTextureTextureBytes` and `kMyTextureTextureSize`. Include it and pass
both to `NVN::CreateTexture`.

Wii U/Cemu textures and meshes load from disk at runtime. Drop source
files into `src/resources/` (`.png`/`.jpg` for textures, `.obj` for
meshes):

```
src/resources/logo.png
src/resources/fish.obj
```

`python scripts/deploy.py` runs `scripts/pack_resources.py` automatically,
converting each into a packaged `.bin` and copying it into the pack's
`content/WiiXLaunch/mods/_host/`. Load them with:

```cpp
auto texture = GX2::LoadTexture("WiiXLaunch/mods/_host/logo.bin");
auto mesh = GX2::LoadMesh("WiiXLaunch/mods/_host/fish.bin");
```

For a compiled-in GX2 texture instead of a disk load, use
`vendor/wiixlaunch-botw/tools/pack_texture_gx2.py <image> <Name> --out include/`.
