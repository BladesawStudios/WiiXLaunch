# Setting Up

[« Back to overview](overview.md)

Two different jobs, two different setups. Pick the one you are doing.

| I want to… | go to | how long |
|---|---|---|
| **write a mod** that drops into an installed WiiXLaunch | [Writing mods](#writing-mods) | ~15 min |
| **work on the framework**, the loader, or a game module | [Working on the framework](#working-on-the-framework) | ~1 hour |

---

# Writing mods

You need two tools, two folders, and about fifteen minutes.

## 1. Install Python 3

Any version from the last few years. Check it:

```
python --version
```

## 2. Install devkitPPC

Get the installer from [devkitpro.org](https://devkitpro.org/wiki/Getting_Started)
and select the **devkitPPC** package. That is the only one you need — devkitA64,
WUPS and libfunctionpatcher are for building WiiXLaunch itself, not for building
a mod.

Check it, using your install path:

```
C:\devkitPro\devkitPPC\bin\powerpc-eabi-g++ --version
```

If you installed somewhere else, set `DEVKITPPC` to that directory. The build
looks at `$DEVKITPPC` first, then `C:\devkitPro\devkitPPC` and
`/opt/devkitpro/devkitPPC`, and tells you plainly if it finds nothing.

## 3. Get the SDK and a host

Two folders. If someone handed you a `wiixlaunch-sdk` and a
`WiiXLaunch_BotW` folder, skip to step 4.

To make them yourself, you need a WiiXLaunch checkout once:

```
git clone --recurse-submodules <repo>
cd WiiXLaunch
build_cemu.bat
python scripts\make_sdk.py --host
```

That produces both:

```
build\sdk\      the SDK — you build mods against this
build\host\     the host — this goes into Cemu
```

Copy them somewhere you will keep them. Nothing else from the checkout is
needed, and you never have to build WiiXLaunch again unless you want a newer
host.

## 4. Install the host in Cemu

Copy `build\host\` into Cemu's graphic pack folder, naming it whatever you like:

```
Cemu\graphicPacks\WiiXLaunch_BotW\
    rules.txt
    patch_BotW_SampleMod.asm
    content\WiiXLaunch\mods\        ← your mods go here
```

In Cemu: **Options → Graphic Packs**, find it under Breath of the Wild, tick it.

You need **BotW v208** — it is the only version the patches target.

## 5. Write a mod

Make a folder with two files in it.

`hello_mod\mod.json`

```json
{ "id": "hello" }
```

`hello_mod\mod.cpp`

```cpp
#include <wiixlaunch/imports/wiixl_core.h>
#include <wiixlaunch/mod_runtime.h>

namespace C { WXL_USE_wiixl_core(Log); }

extern "C" __attribute__((used)) void WiiXLaunch_ModEntry() {
    if (C::Log) C::Log("hello: my first mod");
}
```

## 6. Build it

```
python <sdk>\scripts\build_mod.py --source hello_mod
```

You get `hello_moduild\hello.wxlm`. It is one file, and it is the only
thing you ship.

## 7. Run it

Copy `hello.wxlm` into the pack's `content\WiiXLaunch\mods\`, start the game,
and open Cemu's log window. You are looking for:

```
[loader] 1 module(s) found
[loader:hello] requires wiixl.core v1.0 - present
[loader:hello] LOADED, entry at 0x...
hello: my first mod
```

**The log is the tool.** Every module is named as it loads, and anything
refused is named with the reason. If a line is missing, the last one present
tells you how far it got.

## Next

[Writing a mod](writing-mods.md) is the real guide: what the surfaces offer,
how to pick a per-frame tick, which subsystems need arming, and what the
freestanding build will and will not do for you.

---

# Working on the framework

For changing WiiXLaunch itself, a game module, or the surfaces. This builds the
host for all three platforms.

## Prerequisites

* **Python 3** — runs `scripts/generate_config.py` (turns `wiixlaunch.json` into
  the generated headers each target build reads) and `scripts/deploy.py`.
* **devkitPro**, with **devkitPPC** and **devkitA64** — for Wii U, Cemu and
  Switch. From [devkitpro.org](https://devkitpro.org/wiki/Getting_Started). On
  Windows, if devkitPro is not at `C:\devkitPro`, set `DEVKITPRO_WIN`.
* **Visual Studio** (Windows only) — only for host-side tools like
  `tools/ring_log_reader` (see [Debugging](debugging.md)). The console builds do
  not need it.
* **For Wii U**: WUPS, libfunctionpatcher and libnotifications installed into
  devkitPro. All three are submodules under `vendor/`. On Linux,
  `scripts/setup_wiiu_deps.sh` builds and installs them; elsewhere see their
  READMEs for `make install` into `/opt/devkitpro`. libnotifications powers the
  on-screen toasts in [Debugging](debugging.md).

## Getting the source

```bash
git clone --recurse-submodules <your-fork-url>
```

Already cloned without it:

```bash
git submodule update --init --recursive
```

## Configuring the host

Everything project-specific lives in [`wiixlaunch.json`](../wiixlaunch.json).
This describes the **host** build — a `.wxlm` needs none of it and carries its
own `mod.json` instead.

* `project` — name, version, author, description, and `debug` (controls
  `EXL_DEBUG` on the Switch build).
* `memory` — heap/JIT/inline-pool sizes and the Cemu debug log buffer size.
* `switch` — title ID, subsdk name, thread stack size/priority.
* `wiiu` — the plugin's `.wps` filename and the target title IDs it patches.
* `cemu` — the entry hook address, graphic pack path/version for your target
  game build, and `module_matches`. Usually needs no change; v208 is the only
  launchable version.

`python scripts/generate_config.py` (the build scripts run it for you) turns
this into headers under `build/generated/include/` and platform config under
`build/generated/switch/`. These are regenerated every build — edit
`wiixlaunch.json`, not the generated files.

## Building

```bash
build_cemu.bat      # Cemu, plus every gate
build_wiiu.bat      # Wii U
build_switch.bat    # Switch
build_all.bat       # all three
```

Or CMake directly, Switch only — Wii U and Cemu use devkitPPC and the WUPS
Makefile flow, since their output is not a normal CMake executable target:

```bash
cmake -B build/switch -DPLATFORM=SWITCH
cmake --build build/switch
```

`build_cemu` runs the gates: the loader fuzzer, the host and format tests,
surface coverage, the import-header freshness check, and an SDK cut and
verified by building a module from it. A gate failing fails the build.

## Deploying

```bash
python scripts/deploy.py
```

Packages what you have built into `deploy/`:

* **Switch** — `deploy/switch/atmosphere/contents/<title_id>/exefs/`
* **Wii U** — `deploy/wiiu/wiiu/environments/aroma/plugins/<mod_name>.wps`
* **Cemu** — `deploy/cemu/graphicPacks/<graphic_pack_name>/`

Copy onto your SD card, or into Cemu's graphic pack folder.

To cut the two things a mod author needs from a built tree:

```bash
python scripts/make_sdk.py --host
```

## Known gaps

* **Nothing publishes releases.** The SDK and host are produced by building this
  repo. They are self-contained folders and could be published; nobody has, so
  today a mod author needs a checkout once or a friend with one.
* **Cemu is the only platform that has run.** The Switch and Wii U hosts build
  every time and have never been executed. A mod targets surfaces rather than a
  platform, so it should follow — but nothing has demonstrated that.
