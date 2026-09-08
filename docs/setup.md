# Setting Up

[« Back to overview](overview.md)

**Which setup do you want?** There are two, and they are very different sizes.

* **Writing a mod** - a `.wxlm` that drops into an installed host. Python,
  devkitPPC, an SDK folder. No repo, no submodules, no game headers, one
  toolchain. Start at [Setting up to write mods](#setting-up-to-write-mods)
  below; the rest of this page is not for you.
* **Working on the framework or a game module** - the host itself, the surfaces,
  the loader. Everything from [Prerequisites](#prerequisites) onward.

---

## Setting up to write mods

### 1. Python 3

Runs the build script. Nothing else.

### 2. devkitPPC

**Only devkitPPC.** Not devkitA64, not WUPS, not libfunctionpatcher - those are
for building the host on other platforms, and a mod is built once for the
PowerPC target. Install from [devkitpro.org](https://devkitpro.org/wiki/Getting_Started)
and take the `devkitPPC` package.

`build_mod.py` finds it via `$DEVKITPPC`, then `C:\devkitPro\devkitPPC` and
`/opt/devkitpro/devkitPPC`. If it cannot, it says so and stops rather than
building something wrong:

```
[build_mod] SETUP PROBLEM - not a broken source tree.
  powerpc-eabi-g++ was not found. Set DEVKITPPC, or install devkitPPC.
```

### 3. An SDK

31 files: the build scripts, one generated header per surface, and the
freestanding runtime. Cut one with:

```
python scripts/make_sdk.py build/sdk
```

**You need a framework checkout once to cut it, or someone who has one to hand
you the folder.** There is no published SDK release yet - see
[What is still manual](#what-is-still-manual).

### 4. A host to run against

A mod is inert on its own; it needs the host that loads it. On Cemu that is a
graphic pack folder, about 4.5 MB:

```
WiiXLaunch_BotW_SampleMod/
    rules.txt
    patch_BotW_SampleMod.asm
    content/WiiXLaunch/mods/        <- your .wxlm goes here
```

Drop it in Cemu's `graphicPacks/` and enable it. **Same caveat: today the pack
comes from building this repo.**

### 5. Write and build

```
mkdir my_mod
```

`my_mod/mod.json`:

```json
{ "id": "my_mod" }
```

`my_mod/mod.cpp`:

```cpp
#include <wiixlaunch/imports/wiixl_core.h>
#include <wiixlaunch/mod_runtime.h>

namespace C { WXL_USE_wiixl_core(Log); }

extern "C" __attribute__((used)) void WiiXLaunch_ModEntry() {
    if (C::Log) C::Log("my_mod: loaded");
}
```

```
python sdk/scripts/build_mod.py --source my_mod
```

Copy `my_mod.wxlm` into the pack's `content/WiiXLaunch/mods/`, launch the game,
and read the log. [Writing a mod](writing-mods.md) is the rest of it.

### What is still manual

Being straight about where the seams are:

* **No published SDK or host.** Both are produced by building this repo. Nothing
  packages them for release, so a mod author needs a checkout once or a friend
  with one. The pieces are self-contained and could be published; nobody has.
* **The host pack ships with the sample mods inside it.** A host install for
  someone else wants `content/WiiXLaunch/mods/` empty apart from `_host/` and
  `probe.bin`. Clearing it by hand works.
* **Cemu only, in practice.** Switch and Wii U hosts build but have never been
  run. A mod targets the surfaces rather than a platform, so it should follow -
  "should" being exactly the word that has cost time before.

---

## Prerequisites

* **Python 3** - runs `scripts/generate_config.py` (turns `wiixlaunch.json` into the generated headers each target build reads) and `scripts/deploy.py` (packages build output).
* **devkitPro**, with the `devkitPPC` and `devkitA64` toolchains - required for Wii U, Cemu, and Switch builds. Install from [devkitpro.org](https://devkitpro.org/wiki/Getting_Started).
  * On Windows, if devkitPro isn't installed at the default `C:\devkitPro`, set `DEVKITPRO_WIN` to your install directory before building.
* **Some form of Visual Studio (20XX)** (Windows only) - only needed if you're also building host-side tools like `tools/ring_log_reader` (see [Debugging](debugging.md)); the console mods themselves don't need it.
* For Wii U: WUPS, libfunctionpatcher, and libnotifications installed into your devkitPro environment. All three ship as submodules under `vendor/`. On Linux, `scripts/setup_wiiu_deps.sh` builds and installs all three for you; on other platforms see their READMEs for `make install` instructions into `/opt/devkitpro`. libnotifications is what powers the on-screen toasts described in [Debugging](debugging.md).

## Getting the source

The vendored dependencies (exlaunch, wut, WUPS, libfunctionpatcher) are git submodules:

```bash
git clone --recurse-submodules <your-fork-url>
```

If you already cloned without `--recurse-submodules`:

```bash
git submodule update --init --recursive
```

## Configuring the host

Everything project-specific lives in [`wiixlaunch.json`](../wiixlaunch.json) at the repo root. This describes the HOST build - a `.wxlm` needs none of it and carries its own `mod.json` instead:

* `project` - name, version, author, description, and `debug` (controls `EXL_DEBUG` on the Switch build).
* `memory` - heap/JIT/inline-pool sizes and the Cemu debug log buffer size.
* `switch` - title ID, subsdk name, thread stack size/priority.
* `wiiu` - the plugin's `.wps` filename and the target title IDs it patches.
* `cemu` - the entry hook address, graphic pack path/version for your target game build, and `module_matches` (the `moduleMatches` field written into the generated `.asm` patch). usually doesn't require changing, V208 is the only launchable version from my understanding.

Running `python scripts/generate_config.py` (the build scripts do this for you) turns this into generated headers under `build/generated/include/` and platform config files under `build/generated/switch/` - these are regenerated on every build, so edit `wiixlaunch.json`, not the generated files.

## Building

Convenience scripts (Windows, there are Linux equivalents available as well):

```bash
build_switch.bat (builds switch, deploys all)
build_wiiu.bat (builds wiiu, deploys all)
build_cemu.bat (builds cemu, deploys all)
build_all.bat (builds all, deploys all)
```

Or via CMake directly (Switch only builds this way; Wii U and Cemu use devkitPPC/the WUPS Makefile flow directly, since their output isn't a normal CMake executable target (see `build_wiiu.bat` and `build_cemu.bat`):

```bash
cmake -B build/switch -DPLATFORM=SWITCH
cmake --build build/switch
```

## Deploying

```bash
python scripts/deploy.py
```

Packages whatever you've built into `deploy/`:

* **Switch**: `deploy/switch/atmosphere/contents/<title_id>/exefs/`
* **Wii U**: `deploy/wiiu/wiiu/environments/aroma/plugins/<mod_name>.wps`
* **Cemu**: `deploy/cemu/graphicPacks/<graphic_pack_name>/`

Copy the relevant folder onto your SD card, place it in your emulator as a regular exefs mod, or place the mod in Cemus graphic pack folder.
