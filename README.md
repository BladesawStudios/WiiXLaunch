![logo](docs/res/WiiXLaunch2K_Sat_Circle_LowRes.png)

# WiiXLaunch

**WiiXLaunch** is a cross-platform C++ hooking framework for **Nintendo Switch** (AArch64 via ExLaunch), **Nintendo Wii U** (PowerPC via WUPS / libfunctionpatcher), and **Cemu** (PC graphic-pack code caves).

Write a game mod once in C++ and build it for Switch, Wii U and Cemu (2.6+). Ship it either compiled into the host, or as a standalone `.wxlm` binary that other people drop next to their game and that loads side by side with other mods.

Full documentation starts at [docs/overview.md](docs/overview.md).

---

## Features

* **One C++ codebase, three platforms.** Hooks and memory patches are written once with ExLaunch-style syntax (`WIIXL_HOOK_DEFINE_TRAMPOLINE`, `Orig(...)`) and dispatched to the right mechanism at compile time.
* **Distributable compiled mods (`.wxlm`).** A mod is a relocatable binary that names the *surfaces* it needs (`wiixl.core`, `botw.player`) and contains no game offsets. The host resolves them at load, several mods load together, each is named in the log, and each is refused by name if the host cannot give it what it asked for. See [Writing a mod](docs/writing-mods.md).
* **A committed SDK (`sdk/`).** Everything a mod author needs to build a `.wxlm`: generated import headers with real signatures, the freestanding runtime, and the build script. No framework checkout, no submodules.
* **One file per game (`targets/<game>.json`).** Project settings, memory sizes, Switch NPDM permissions and Wii U title IDs, per target. One checkout builds a host for any of them: `build_switch.bat totk`.
* **Game modules (`vendor/wiixlaunch-*`).** Game-specific knowledge promoted into a high-level API and published as versioned surfaces. The first is [wiixlaunch-botw](https://github.com/TKVSC-Team/wiixlaunch-botw) for Breath of the Wild.
* **Checked patches and chained hooks.** Patches carry the bytes they expect to find and are refused on a different game build. Any number of mods can hook one function, and the boot log names everyone involved.

---

## Example

A hook compiled into the host:

```cpp
#include <wiixlaunch.hpp>

// Infinite stamina in BotW: Switch 1.5.0 | Wii U v208
WIIXL_HOOK_DEFINE_TRAMPOLINE(PlayerStaminaHook) {
    static void Callback(float amount, void* player) {
        Orig(0.0f, player);   // never let stamina decrease
    }
};

extern "C" void WiiXLaunch_Init() {
#if WIIXL_WIIU
    if (!WiiXLaunch::Backend::InitWiiUBackend()) return;
#elif WIIXL_CEMU
    if (!WiiXLaunch::Backend::InitCemuBackend()) return;
#endif

    // Install with (SwitchOffset, WiiUOffset); Cemu reuses the Wii U offset.
    PlayerStaminaHook::Install(0x00885bd0, 0x02d908b4);
}
```

The smallest standalone `.wxlm`:

```cpp
#include <wiixlaunch/imports/wiixl_core.h>
#include <wiixlaunch/mod_runtime.h>

namespace C { WXL_USE_wiixl_core(Log); }

extern "C" __attribute__((used)) void WiiXLaunch_ModEntry() {
    if (C::Log) C::Log("hello from a compiled mod");
}
```

```
python sdk/scripts/build_mod.py --source hello_mod
```

---

## Building

Two different jobs. [Setting Up](docs/setup.md) walks through both.

**Writing a mod** needs Python 3, devkitPPC (and devkitA64 for Switch) and the `sdk/` folder. About fifteen minutes.

**Working on the framework** builds the host itself. Each script builds one host for one game and nothing else:

| | Windows | Linux / macOS |
|---|---|---|
| Switch host | `build_switch.bat [target]` | `./build_switch.sh [target]` |
| Wii U host | `build_wiiu.bat [target]` | `./build_wiiu.sh [target]` |
| Cemu host | `build_cemu.bat [target]` | `./build_cemu.sh [target]` |
| All three | `build_all.bat` | |
| Every test gate | `test.bat` | `./test.sh` |

The Switch host can also be built directly with CMake:

```bash
cmake -B build/switch -DPLATFORM=SWITCH
cmake --build build/switch
```

---

## Packaging and deploying

Packaging is a separate, explicit step:

```bash
python scripts/deploy.py --target botw
```

This writes `deploy/` and copies into any emulator directories it finds:

* **Switch**: `deploy/switch/atmosphere/contents/<title_id>/exefs/` (`subsdk9` + `main.npdm`)
* **Wii U**: `deploy/wiiu/wiiu/environments/aroma/plugins/` (`<mod_name>.wps`)
* **Cemu**: `deploy/cemu/graphicPacks/<graphic_pack_name>/` (`rules.txt` + `patch_<mod_name>.asm`)

To cut the host graphic pack a mod author installs, with no modules in it:

```bash
python scripts/make_sdk.py --host
```

---

## Platform status

| Platform | Host builds | Modules loaded and run |
|---|---|---|
| Cemu | yes | yes |
| Switch | yes | yes (under Ryujinx) |
| Wii U | yes | **not yet** - needs Aroma on real hardware |

See [Known gaps](docs/setup.md#known-gaps).

---

## License

Licensed under the GNU General Public License v3.0 (GPL-3.0). See [LICENSE](LICENSE).

---

## Credits

WiiXLaunch builds upon and integrates the following open-source projects:

* **[ExLaunch](https://github.com/shadowninja108/exlaunch)** - Created by [shadowninja108](https://github.com/shadowninja108) and contributors. Provides the AArch64 inline hooking engine, NSO loading, and memory patching for Nintendo Switch.
* **[WiiUPluginSystem (WUPS)](https://github.com/wiiu-env/WiiUPluginSystem)** - Maintained by [wiiu-env](https://github.com/wiiu-env) (Maschell and contributors). Provides the plugin architecture and Aroma integration for Nintendo Wii U.
* **[libfunctionpatcher](https://github.com/wiiu-env/libfunctionpatcher)** - Maintained by [wiiu-env](https://github.com/wiiu-env). Provides PowerPC function patching and memory page permission handling for Wii U.
* **[libnotifications](https://github.com/wiiu-env/libnotifications)** - Maintained by [wiiu-env](https://github.com/wiiu-env). Provides the on-screen notifications the Wii U host uses for `WIIXL_LOG`.
* **[wut](https://github.com/devkitPro/wut)** - Maintained by [devkitPro](https://github.com/devkitPro) (fincs and contributors). Provides C/C++ headers and OS bindings for Nintendo Wii U homebrew.
