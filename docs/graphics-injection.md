# Graphics Injection

[« Back to overview](overview.md)

This is the [wiixlaunch-botw](https://github.com/TKVSC-Team/wiixlaunch-botw) module's `NVN` and `GX2` namespaces: drawing your own textures and meshes directly into Breath of the Wild's render loop, on top of the game's own frame. This is the host-built API; a `.wxlm` reaches the same functionality through the `botw.gfx` and `botw.gui` surfaces (see [Writing a mod](writing-mods.md)).

```cpp
#include <wiixlaunch/botw/botw.hpp>   // host-built only; a .wxlm uses botw.gfx

using namespace WiiXLaunch::BotW;
```

`NVN` targets Switch's NVN graphics API. `GX2` targets Wii U/Cemu's GX2 graphics API. They expose the same shape of functions on purpose, so a draw callback written for one reads the same as the other, but they are not the same code underneath. See [Platform differences](#platform-differences) below before assuming a mesh or sprite call looks identical on both.

Neither is available on every platform: `NVN::SupportsNVN` is `true` on Switch only, `GX2::SupportsGX2` is `true` on Wii U/Cemu only. On the platform where a namespace doesn't apply, every function in it is a safe no-op (`Init()` does nothing, `CreateTexture` returns `0`, etc), so you can call either one unconditionally without wrapping every call in `#if`.

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

`GX2` has the identical three calls (`GX2::Init()`, `GX2::RegisterDrawCallback`, `GX2::OnInitialized`), just swap the namespace. `Init()` installs the hook into the game's actual render loop; call it once from `WiiXLaunch_Init()` before registering anything.

* `RegisterDrawCallback(cb)` - `cb` runs once per frame, after the game has finished drawing its own frame but before it's presented. `dstTexture` is the frame you draw into, `width`/`height` are its current resolution. Up to 16 callbacks can be registered; they all run, in registration order, every frame.
* `OnInitialized(cb)` - `cb` runs once, the first time the graphics pipeline is actually ready to accept draws (this can be several frames after `Init()`, once the game's own renderer has finished setting up). If the pipeline is already ready by the time you call `OnInitialized`, `cb` runs immediately instead of being queued. This is where you create textures and load meshes, not `WiiXLaunch_Init()` itself, since the device/context doesn't exist yet at that point. Up to 16 init callbacks can be queued.

## Textures

```cpp
// Switch: compiled-in packaged data, no file read at runtime.
NVN::TextureHandle t = NVN::CreateTexture(g_MyTextureBytes, kMyTextureSize);

// Wii U/Cemu: read a packaged file off disk.
GX2::TextureHandle t = GX2::LoadTexture("WiiXLaunch/mods/_host/mytexture.bin");
```

`NVN::CreateTexture(packagedData, packagedSize, minFilter = Linear, magFilter = Linear, wrapMode = ClampToEdge)` expects Nintendo's own "packaged texture data" container format (a fixed header directly in front of GPU-ready, already-swizzled pixel bytes), not raw RGBA8. You don't write this by hand: see [Packaging your own assets](#packaging-your-own-assets) below. There's no `NVN::LoadTexture` that reads from disk; Switch textures are baked into the binary at compile time as a `.hpp` array and passed straight to `CreateTexture`.

`GX2::CreateTexture(rgbaBytes, size, width, height, format = 0)` takes plain raw RGBA8 bytes plus explicit dimensions, and does its own GX2 micro-tiling of them into a proper surface at runtime. You'd normally reach this through `GX2::LoadTexture` rather than calling it directly:

`GX2::LoadTexture(path, maxFileSize = 1MB)` reads a file packaged by `scripts/pack_resources.py` (see below) off disk via `FS::ReadFile`, then calls `CreateTexture` for you. `path` is resolved relative to the Cemu graphic pack's `content/` folder, e.g. `"WiiXLaunch/mods/_host/logo.bin"` resolves to `content/WiiXLaunch/mods/_host/logo.bin`. The host's own resources live under the reserved `_host` id - see [Module resources](loader.md#module-resources).

Texture pools are fixed-size, with no heap allocation involved: 16 live textures on `NVN`, 64 on `GX2`.

## Meshes

```cpp
struct MeshVertex { float x, y, z, w, nx, ny, nz, nw; };

NVN::DrawMesh(cmdBuf, dstTexture, myVertices, myVertexCount);
```

`MeshVertex` is position (`x, y, z, w`) plus normal (`nx, ny, nz, nw`), and is the same layout on both `NVN` and `GX2`. There's no material/texture on a mesh; `DrawMesh` colors each pixel from the vertex normal (a debug-normals shader), which is enough to see shape and lighting-ish shading without needing a full shading pipeline.

`GX2::LoadMesh(path, maxFileSize = 64KB)` reads a mesh packaged from a `.obj` by `scripts/pack_resources.py` and returns a `MeshData{ vertices, vertexCount }`. Unlike a texture, `DrawMesh` re-reads the `vertices` pointer every single frame (it copies fresh data into its own ring buffer each call), so whatever buffer backs it has to stay alive for as long as you keep drawing the mesh - `LoadMesh`'s returned buffer is a permanent allocation, it's never freed. `NVN` has no `LoadMesh` equivalent; supply your own compiled-in vertex array.

## Drawing

```cpp
NVN::DrawSprite(cmdBuf, dstTexture, textureHandle, x, y, width, height, r = 1, g = 1, b = 1, a = 1);
NVN::DrawMesh(cmdBuf, dstTexture, vertices, vertexCount);
```

`DrawSprite` draws a real alpha-blended textured quad. `x`/`y`/`width`/`height` are in the same coordinate space the game's own UI draws in, roughly -1 to 1 across the screen (see `src/main.cpp`, which places the host's logo at `x = -0.92, y = 0.50, width = 0.225, height = 0.40`, a small badge in the upper-left). `r, g, b, a` tint the sprite (`1, 1, 1, 1` is untinted, full alpha).

`DrawMesh` draws a real depth-tested triangle list. Both `NVN` and `GX2` build their own private depth texture on first use rather than reusing one of the game's, since neither has a reliably reachable live depth buffer to borrow.

## Platform differences

* **GX2's `DrawMesh` corrects for aspect ratio, NVN's doesn't.** `GX2::DrawMesh` scales every vertex's `x` by `height / width` before drawing, to compensate for a non-square viewport; `NVN::DrawMesh` draws vertices exactly as given. A mesh authored to look right on one may look horizontally stretched or squashed on the other unless you account for this yourself.
* **NVN textures are compiled in, GX2 textures are usually loaded from disk.** This isn't just a style choice, Switch has no equivalent of `GX2::LoadTexture`/`FS::ReadFile` in this API wired up for texture packages, so there's no runtime-loading path on Switch at all today.
* **`GetDevice()` means different things.** `NVN::GetDevice()` returns a real `NVNdevice*`. `GX2::GetDevice()` always returns `nullptr`, GX2 has no equivalent device object; use `GX2::GetGraphicsContext()` instead if you need a GX2-side handle.

## Limits

* 16 draw callbacks, 16 init callbacks, per namespace.
* 16 live textures on `NVN`, 64 on `GX2`.
* Sprites are written into a ring buffer of fixed-size slots (so many `DrawSprite` calls in the same frame are safe), but the ring does wrap - drawing more unique sprites in a single frame than the ring has slots for would overwrite one still in flight. In practice you won't hit this unless you're doing something unusual.
* Mesh vertex data shares one bump-allocated arena per namespace (sized with headroom over any mesh this project has actually pushed through it). A single mesh larger than the whole arena will fail to draw; `WIIXL_LOG`/`BotW::OSLog` calls inside `DrawMesh`'s pipeline setup will tell you if a shader or buffer failed to initialize.

## FS

```cpp
size_t readSize = 0;
bool ok = WiiXLaunch::FS::ReadFile("WiiXLaunch/mydata.bin", buffer, sizeof(buffer), &readSize);
```

`WiiXLaunch::FS` lives in base WiiXLaunch (`<wiixlaunch/fs.hpp>`), not in a game module - coreinit's filesystem is reachable in any Wii U title, and the loader needs it. `FS::ReadFile(path, outBuffer, maxBufferSize, outReadSize = nullptr)` and `FS::WriteFile(path, buffer, size, outWrittenSize = nullptr)` are cross-platform file access, used internally by `GX2::LoadTexture`/`LoadMesh`, and also available directly for your own data files. On Cemu, `path` is tried as-is and then against a few likely prefixes (`/vol/content/`, `content/`, `/vol/content/WiiXLaunch/`) since a bare code cave has no working directory of its own to resolve relative paths against. On Wii U it's a normal `coreinit` filesystem call.

## `OSLog`

`BotW::OSLog(fmt, ...)` is a separate, Cemu-only logger the `GX2`/`FS` internals use for their own diagnostics. It is not `WIIXL_LOG` - see [Debugging](debugging.md#botwoslog-is-a-different-thing) for how the two differ and where each one's output actually goes.

## Packaging your own assets

**Switch textures** are baked into the binary at compile time, there's no runtime loading. Pack an image with the module's tool:

```bash
python vendor/wiixlaunch-botw/tools/pack_texture_nvn.py myimage.png MyTexture --out include/
```

This writes `include/mytexture_texture_bytes.hpp`, defining `g_MyTextureTextureBytes` and `kMyTextureTextureSize`. `#include` it and pass those two to `NVN::CreateTexture`.

**Wii U/Cemu textures and meshes** are loaded from disk at runtime instead. Drop source files into `src/resources/` (`.png`/`.jpg` for textures, `.obj` for meshes):

```
src/resources/logo.png
src/resources/fish.obj
```

`python scripts/deploy.py` runs `scripts/pack_resources.py` automatically, which converts each file into a packaged `.bin` and copies it into the Cemu graphic pack's `content/WiiXLaunch/mods/_host/` folder. After deploying, load them with:

```cpp
auto texture = GX2::LoadTexture("WiiXLaunch/mods/_host/logo.bin");
auto mesh = GX2::LoadMesh("WiiXLaunch/mods/_host/fish.bin");
```

If you'd rather have a GX2 texture compiled in instead of loaded from disk, `vendor/wiixlaunch-botw/tools/pack_texture_gx2.py <image> <Name> --out include/` produces a header for `GX2::CreateTexture` the same way `pack_texture_nvn.py` does for `NVN::CreateTexture`.
