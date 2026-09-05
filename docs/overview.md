![logo](res/WiiXLaunch2K_Sat_Circle_LowRes.png)

# WiiXLaunch

WiiXLaunch is a cross-platform C++ hooking framework for **Nintendo Switch**, **Nintendo Wii U** (Aroma/WUPS), and **Cemu**. You write a hook once, in normal C++, and it builds for all three targets.

The framework hides the differences between three very different hooking mechanisms behind one API:

* **Switch**: exlaunch's inline ARM64 hooking engine, patched into the game's NSO at load time.
* **Wii U**: WUPS + libfunctionpatcher, which replaces a function in the running RPX by title ID.
* **Cemu**: a hand-written PowerPC code cave, injected via a graphic pack patch.

## Where to go next

* [Setting Up](setup.md) - installing the toolchains, configuring `wiixlaunch.json`, building and deploying for each platform.
* [Hooks](hooks.md) - writing `WIIXL_HOOK_DEFINE_TRAMPOLINE` hooks, finding offsets, raw memory patches.
* [Debugging](debugging.md) - `WIIXL_LOG`, and how it reaches you differently on each platform.
* [Cemu code cave relocation](cemu-relocation.md) - how the payload finds its own load address, and why it has to.
* [The module loader](loader.md) - how compiled mods are loaded, the per-platform load point, and what is and is not initialised when your code first runs.
* [wiixl.net](net.md) - TCP for mods: why a socket is tracked and owned rather than handed over as a raw descriptor, and **the static-import rule** - what base may and may not link against, which applies to every surface, not just this one.
* [Modules](modules.md) - optional, game-specific APIs (e.g. [wiixlaunch-botw](https://github.com/TKVSC-Team/wiixlaunch-botw)) added as submodules on top of the base framework.
* [Graphics Injection](graphics-injection.md) - drawing your own textures and meshes into a game's render loop via `BotW::NVN` (Switch) and `BotW::GX2` (Wii U/Cemu).

## Layout

* `src/` - your mod code. Starts at `WiiXLaunch_Init()` in `main.cpp`.
* `include/wiixlaunch/` - the framework itself.
* `vendor/` - exlaunch, wut, WUPS, libfunctionpatcher (git submodules).
* `scripts/` - config generation and packaging.
* `tools/` - host-side developer tools (see [Debugging](debugging.md)).
* `wiixlaunch.json` - the one file that describes your mod: name, target title IDs, memory sizes.

## Graphics injection R&D

This project is a sandbox for the in-game UI/graphics-pipeline injection work described in [wiixlaunch-botw](https://github.com/TKVSC-Team/wiixlaunch-botw)'s TODO.md - see [Graphics Injection](graphics-injection.md) for the current API and how it works on each platform.
