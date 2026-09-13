![logo](docs/res/WiiXLaunch2K_Sat_Circle_LowRes.png)

# WiiXLaunch

**WiiXLaunch** is a cross-platform C++ hooking framework for **Nintendo Switch** (AArch64 via ExLaunch), **Nintendo Wii U** (PowerPC via WUPS / libfunctionpatcher), and **Cemu** (PC Graphic Pack bare-metal code caves).

Write game mods once in C++, and build for Switch, Wii U, and Cemu (2.6+).

See [overview.md](docs/overview.md) for detailed documentation.

---

## Features

* **Single C++ Codebase**: Define hooks and memory patches once using ExLaunch-style syntax (`WIIXL_HOOK_DEFINE_TRAMPOLINE`, `Orig(...)`).
* **One file per game (`targets/<game>.json`)**: Project settings, memory sizes, Switch NPDM permissions and Wii U title IDs, per target. One checkout builds a host for any of them - `build_switch.bat totk`.
* **ExLaunch Compatibility**: Support for function replacement, trampolines, and raw memory patching (`CodePatch::Nop`).
* **Game Modules (`vendor/wiixlaunch-botw`)**: Pre-reverse engineered API methods for easy executable modding.
* **Distributable compiled mods (`.wxlm`)**: Ship a mod as a binary somebody drops on their SD card, with no source and no rebuild. Several load side by side, each named in the log, each refused by name if the host cannot give it what it asked for. See [Writing a mod](docs/writing-mods.md).

---

## Example

```cpp
#include <wiixlaunch.hpp>

// Infinite Stamina in BotW: Switch 1.5.0 | Wii U v208
WIIXL_HOOK_DEFINE_TRAMPOLINE(PlayerStaminaHook) {
    static void Callback(float amount, void* player) {
        // Prevent stamina decrease by passing 0.0f
        Orig(0.0f, player);
    }
};

extern "C" void WiiXLaunch_Init() {
#if WIIXL_WIIU
    if (!WiiXLaunch::Backend::InitWiiUBackend()) return;
#endif

    // Install hook with (SwitchOffset, WiiUOffset)
    PlayerStaminaHook::Install(0x00885bd0, 0x02d908b4);
}
```

---

## Building

### Windows Batch Scripts (Convenience)
* **Build Switch**: `build_switch.bat [target]`
* **Build Wii U**: `build_wiiu.bat [target]`
* **Build Cemu**: `build_cemu.bat [target]`
* **Build All**: `build_all.bat`

Each builds one host for one game and nothing else. Verifying and installing are
separate, on purpose:

* **Every gate**: `test.bat` (or `./test.sh`)
* **Package and install**: `python scripts/deploy.py --target <name>`

### Build via CMake

#### Switch
```bash
cmake -B build/switch -DPLATFORM=SWITCH
cmake --build build/switch
```

#### Wii U
```bash
cmake -B build/wiiu -DPLATFORM=WIIU
cmake --build build/wiiu
```

#### Cemu
```bash
cmake -B build/cemu -DPLATFORM=CEMU
cmake --build build/cemu
```

---

## Packaging & Deploying

To package your compiled binaries into SD card and emulator folder structures:

```bash
python scripts/deploy.py --target botw
```

This creates the output in `deploy/`:
* **Switch**: `deploy/switch/atmosphere/contents/<title_id>/exefs/` (`subsdk9` + `main.npdm`)
* **Wii U**: `deploy/wiiu/wiiu/environments/aroma/plugins/` (`<mod_name>.wps`)
* **Cemu**: `deploy/cemu/graphicPacks/<graphic_pack_name>/` (`rules.txt` + `patch_<mod_name>.asm`)

---

## License

Licensed under the GNU General Public License v3.0 (GPL-3.0).

---

## Credits

WiiXLaunch builds upon and integrates the following open-source projects:

* **[ExLaunch](https://github.com/shadowninja108/exlaunch)** - Created by [shadowninja108](https://github.com/shadowninja108) and contributors. Provides the AArch64 inline hooking engine, NSO loading, and memory patching for Nintendo Switch.
* **[WiiUPluginSystem (WUPS)](https://github.com/wiiu-env/WiiUPluginSystem)** - Maintained by [wiiu-env](https://github.com/wiiu-env) (Maschell and contributors). Provides the plugin architecture and Aroma integration for Nintendo Wii U.
* **[libfunctionpatcher](https://github.com/wiiu-env/libfunctionpatcher)** - Maintained by [wiiu-env](https://github.com/wiiu-env). Provides PowerPC function patching and memory page permission handling for Wii U.
* **[wut](https://github.com/devkitPro/wut)** - Maintained by [devkitPro](https://github.com/devkitPro) (fincs and contributors). Provides C/C++ headers and OS bindings for Nintendo Wii U homebrew.

