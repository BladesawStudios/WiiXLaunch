# Modules

[« Back to overview](overview.md)

## What a module is

Base WiiXLaunch stays a generic, game-agnostic hooking framework: it has no idea what game you're modding. A **module** is game-specific knowledge (offsets, vtable slots, actor spawn plumbing, whatever) promoted into a high-level API and published as its own repo, so it doesn't bloat every fresh clone of the template. You add the modules you actually need, per project.

The first one is [wiixlaunch-botw](https://github.com/TKVSC-Team/wiixlaunch-botw) - see below.

## Adding a module

Modules are plain git submodules, same as `vendor/exlaunch`, `vendor/wut`, etc.:

```bash
git submodule add https://github.com/TKVSC-Team/wiixlaunch-botw vendor/wiixlaunch-botw
git submodule update --init --recursive
```

That's it, no build script edits, no `wiixlaunch.json` changes. Every build path already scans `vendor/wiixlaunch-*` and wires up the include path automatically:

* `scripts/generate_config.py` adds it to the generated Switch `config.mk`.
* `build_cemu.bat`/`.sh` pass it straight through as `-I`.
* `build_wiiu.bat`/`.sh` stage it into the temp build dir under `modules/<name>/include`; `scripts/wiiu/Makefile`'s `INCLUDES` picks it up via `$(wildcard modules/*/include)`.
* `CMakeLists.txt` globs it too, for the direct-cmake Switch path.

Your `main.cpp`'s `#include` lines don't change either way, `#include <wiixlaunch/botw/botw.hpp>` resolves the same whether those headers are a submodule or (during early development) just copied under your own `include/`.

## Naming convention

A module must live at `vendor/wiixlaunch-<name>` for the auto-discovery to find it - the `wiixlaunch-` prefix is required. This is deliberately narrow: `vendor/wut`, `vendor/wups`, and `vendor/libfunctionpatcher` also have their own top-level `include/` dirs, but they're Wii U SDK dependencies wired up through their own dedicated Makefile flow, not something you want silently added to every platform's include path.

## Available modules

* **[wiixlaunch-botw](https://github.com/TKVSC-Team/wiixlaunch-botw)** - Breath of the Wild. `Player` (equipped sword/shield/bow, position, attack-swing detection), `Actor` (name lookup, spawning), `Controller` (unified button/stick reads), `Camera` (position/look-at/up), `OSLog` (Cemu-only logging), and `NVN`/`GX2` (drawing your own textures and meshes into the game's render loop - see [Graphics Injection](graphics-injection.md)). See its README for per-platform coverage.

## Writing your own module

A module is just headers, no separate repo scaffolding beyond `include/wiixlaunch/<module-name>/*.hpp` and a README. A few conventions worth following, taken from how `wiixlaunch-botw` is built:

* **Resolve WiiXLaunch's core via `<wiixlaunch/...>`, not `"../..."`.** A module doesn't live physically nested inside a project's `include/wiixlaunch/` tree once it's pulled in as a submodule, only `#include <wiixlaunch/platform.hpp>`-style angle-bracket includes, resolved through the consuming project's own `-I include`, work regardless of where your module's files actually sit on disk.
* **Header-only**, matching the framework itself: no separate `.cpp` to build or link into every consumer.
* **Self-installing hooks.** If your module needs a hook, give it a static `Init()` the mod calls once from its own `WiiXLaunch_Init()`; everything after that should be typed getters, not something the mod has to hook itself. See [Hooks](hooks.md) for the underlying `WIIXL_HOOK_DEFINE_TRAMPOLINE` mechanics your module builds on.
* **Don't depend on `WIIXL_LOG`/`debug_log.hpp`.** Its signature isn't standardized, different WiiXLaunch projects have forked it differently for their own diagnostics (see [Debugging](debugging.md)). A module meant to drop into any project unmodified can't assume a shape that isn't guaranteed to match.
* **Anything referenced only from outside the compiler's view needs `__attribute__((used))`, data or code.** GCC emits an `inline` definition only when some translation unit odr-uses it, and a reference the compiler cannot see does not count. This has now bitten from both directions: a *data* global written by `scripts/deploy.py` and read by a `src/cemu/*.asm` shim table (`g_CemuMemShimTableOffset`, which shipped unreachable), and an *inline function* whose only caller was a hand-written `asm()` block (`WiiXLaunch_LoadPointProbe`, which failed to link). The data case fails silently at runtime; the code case fails loudly at link. Mark both: `__attribute__((section(".data"), used)) inline uint32_t g_Whatever = 0;` and `extern "C" __attribute__((used)) inline void Whatever() {}`. `used` cannot rescue a header nobody included, though - if the symbol has to exist in every build, some translation unit has to include the header, which is why `src/cemu/bootstrap.cpp` pulls in the umbrella. deploy.py refuses to build a pack whose base `WIIXL_OFFSET_SYMBOL` did not resolve.
* **OS-level shims belong in base, not in your module.** On Cemu the payload is a raw code-cave blob with no import table, so every OS call has to go through an `import.<lib>.<Name>` tail-call shim in a `src/cemu/*.asm` table. Put a shim in your module only if the thing it reaches is *game*-specific. Anything from `coreinit` - filesystem, logging, time, memory - is present in every Wii U title, needs no game address, and lives in base WiiXLaunch (`src/cemu/cemu_fs.asm`, `cemu_logging.asm`, `cemu_mem.asm`, `cemu_time.asm`, resolved through `<wiixlaunch/cemu/*.hpp>`); base's own logger and module loader depend on those, so they cannot be behind "is a game module installed". `wiixlaunch-botw` keeps only `gx2_imports.asm`, because assuming a graphics API is exactly the kind of thing a game-agnostic framework must not do.
* **Capability flags, not runtime checks.** Every WiiXLaunch build targets exactly one platform at compile time. If a feature only works on some platforms, expose it as a `constexpr bool SupportsX`, not a function that takes a platform argument.
