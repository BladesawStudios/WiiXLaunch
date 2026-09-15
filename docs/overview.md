![logo](res/WiiXLaunch2K_Sat_Circle_LowRes.png)

# WiiXLaunch

A cross-platform C++ hooking framework for Nintendo Switch, Nintendo Wii U
(Aroma/WUPS), and Cemu.

| I want to... | go to |
|---|---|
| Write a mod (`.wxlm`) against a built host | [SDK: Getting started](sdk/getting-started.md) |
| Build or change the host, a game module, or the loader | [Framework: Setup](framework/setup.md) |

## Host hooks vs. mod hooks

Host code (`src/main.cpp`, compiled into the payload) installs a hook with
`WIIXL_HOOK_DEFINE_TRAMPOLINE` and a compile-time `(switchOffset, wiiuOffset)`
pair. See [Framework: Hooks](framework/hooks.md).

A `.wxlm` mod does not use that macro. It calls one runtime import,
`wiixl_core:InstallHook(target, callback)`, resolved at load time against a
surface registry. See [SDK: Hooks, patches and runtime](sdk/hooks-patches-and-runtime.md).

Both dispatch into the same hook registry and the same three platform
backends: exlaunch, WUPS/libfunctionpatcher, and a Cemu trampoline pool.
Conflicts are reported the same way everywhere. Host macros will not compile
inside a `.wxlm`.

## Layout

* `src/`: the host. Entry point `WiiXLaunch_Init()` in `main.cpp`.
* `include/wiixlaunch/`: the framework, used only when building the host.
* `sdk/`: the committed mod SDK. Generated import headers, the freestanding
  runtime, `build_mod.py` (includes `--init`), and `wxlm.py`. See
  [sdk/README.md](../sdk/README.md).
* `examples/`: reference `.wxlm` mods, built by `test.bat`.
* `targets/<game>.json`: one file per game. Host build settings, title IDs,
  which game module to compile in.
* `vendor/`: exlaunch, wut, WUPS, libfunctionpatcher, libnotifications, game
  modules. Framework-only, git submodules.
* `scripts/`: config generation, the `.wxlm` writer, packaging, test gates.
* `tools/`: host-side developer tools and test binaries.
* `docs/framework/`: building and changing the host.
* `docs/sdk/`: writing, building, and shipping a `.wxlm` mod.

## Platform status

| Platform | Host builds | Mods loaded and run |
|---|---|---|
| Cemu | yes | yes |
| Switch | yes | yes (under Ryujinx) |
| Wii U | yes | not yet, needs Aroma on real hardware |

See [Framework: Setup, known gaps](framework/setup.md#known-gaps).

## License

GPL-3.0. See [LICENSE](../LICENSE).

## Credits

* [ExLaunch](https://github.com/shadowninja108/exlaunch): AArch64 inline
  hooking, NSO loading, memory patching for Switch.
* [WiiUPluginSystem (WUPS)](https://github.com/wiiu-env/WiiUPluginSystem):
  plugin architecture and Aroma integration for Wii U.
* [libfunctionpatcher](https://github.com/wiiu-env/libfunctionpatcher):
  PowerPC function patching and page permissions for Wii U.
* [libnotifications](https://github.com/wiiu-env/libnotifications): on-screen
  notifications used by `WIIXL_LOG` on Wii U.
* [wut](https://github.com/devkitPro/wut): C/C++ headers and OS bindings for
  Wii U homebrew.
