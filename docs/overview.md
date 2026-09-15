![logo](res/WiiXLaunch2K_Sat_Circle_LowRes.png)

# WiiXLaunch

WiiXLaunch is a cross-platform C++ hooking framework for **Nintendo Switch**, **Nintendo Wii U** (Aroma/WUPS), and **Cemu**. You write a hook once, in normal C++, and it builds for all three targets.

The framework hides the differences between three very different hooking mechanisms behind one API:

* **Switch**: exlaunch's inline ARM64 hooking engine, patched into the game's NSO at load time.
* **Wii U**: WUPS + libfunctionpatcher, which replaces a function in the running RPX by title ID.
* **Cemu**: a hand-written PowerPC code cave, injected via a graphic pack patch.

## Two ways to write a mod

This matters before anything else, because most of the pages below describe one
of them and it is not always the one you want.

**Built into the host.** Your code lives in `src/main.cpp`, starts at
`WiiXLaunch_Init()`, includes the game module's headers directly, and ships as
one payload. This is the original model and it is still how the host itself is
written. Everything is available and nothing is versioned, because there is no
boundary to version.

**Shipped as a `.wxlm`.** Your code is a separate relocatable binary that
includes no game header and contains no offset. It names the SURFACES it needs -
`wiixl.core`, `botw.player` - and the host resolves them at load. Several such
mods load side by side, each attributed, each refusable by name. This is what
lets a mod be distributed without source and survive the game module changing
underneath it.

If you are adding to this repo, you want the first. If you are writing a mod for
other people to install, you want the second: see
[Writing a mod](writing-mods.md).

## Where to go next

* [Setting Up](setup.md) - installing the toolchains, getting the SDK, writing a
  `targets/<game>.json`, building, verifying and deploying for each platform.
* [Writing a mod](writing-mods.md) - the `.wxlm` path end to end: the three
  layers, imports, picking a tick, arming, and the freestanding rules.
* [Hooks](hooks.md) - writing `WIIXL_HOOK_DEFINE_TRAMPOLINE` hooks, chaining
  several on one function, finding offsets, checked and unchecked patches.
* [Debugging](debugging.md) - `WIIXL_LOG`, and how it reaches you differently on
  each platform.
* [The module loader](loader.md) - how compiled mods are loaded, the per-platform
  load point, the arena, load order, declared patches, module resources and
  per-frame ticks.
* [Modules](modules.md) - game-specific APIs (e.g.
  [wiixlaunch-botw](https://github.com/TKVSC-Team/wiixlaunch-botw)) added as
  submodules on top of the base framework, and the standing engineering rules
  this project holds itself to.
* [wiixl.net](net.md) - TCP for mods: why a socket is tracked and owned rather
  than handed over as a raw descriptor, and **the static-import rule** - what
  base may and may not link against, which applies to every surface, not just
  this one.
* [Cemu code cave relocation](cemu-relocation.md) - how the payload finds its own
  load address, and why it has to.
* [Graphics Injection](graphics-injection.md) - drawing your own textures and
  meshes into a game's render loop via `BotW::NVN` (Switch) and `BotW::GX2`
  (Wii U/Cemu).

## Layout

* `src/` - the HOST. Starts at `WiiXLaunch_Init()` in `main.cpp`. This is also
  where a host-built mod lives; a `.wxlm` does not go here.
* `include/wiixlaunch/` - the framework itself.
* `sdk/` - the committed mod SDK: generated import headers, the freestanding
  runtime and `build_mod.py`. Everything a `.wxlm` author needs, and nothing
  else.
* `examples/` - six working `.wxlm` mods, built by `test.bat` for both
  architectures.
* `targets/<game>.json` - one file per GAME, describing the host build for it:
  name, title IDs, memory sizes, which game module to compile in. A `.wxlm`
  needs none of it - see [Writing a mod](writing-mods.md).
* `vendor/` - exlaunch, wut, WUPS, libfunctionpatcher, libnotifications and the
  game modules (git submodules).
* `scripts/` - config generation, the `.wxlm` writer, packaging and the test
  gates.
* `tools/` - host-side developer tools and test binaries (see
  [Debugging](debugging.md)).
* `docs/` - this documentation.
