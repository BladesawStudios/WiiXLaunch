# Setting Up

[« Back to overview](overview.md)

Two different jobs, two different setups. Pick the one you are doing.

| I want to… | go to | how long |
|---|---|---|
| **write a mod** that drops into an installed WiiXLaunch | [Writing mods](#writing-mods) | ~15 min |
| **work on the framework**, the loader, or a game module | [Working on the framework](#working-on-the-framework) | ~1 hour |

---

# Writing mods

Two things to install, two folders to download, and one command that writes
the mod for you. About fifteen minutes.

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

## 3. Get the SDK

The SDK is `sdk/` in the WiiXLaunch repository. It is 31 text files. Download
that folder — from a release, from the repo's web interface, or by cloning:

```
git clone <repo>
```

You do not need the submodules, a toolchain, or to build anything. Copy `sdk/`
somewhere you will keep it. That is the whole step.

## 4. Install the host in Cemu

The host is the WiiXLaunch graphic pack. If you were given one, use it. To cut
one from a checkout:

```
build_cemu.bat
python scripts\make_sdk.py --host
```

which writes `build\host\` — the pack with no modules in it, ready for yours.

Copy it into Cemu's graphic pack folder, naming it whatever you like:

```
Cemu\graphicPacks\WiiXLaunch_BotW\
    rules.txt
    patch_BotW_SampleMod.asm
    content\WiiXLaunch\mods\        ← your mods go here
```

In Cemu: **Options → Graphic Packs**, find it under Breath of the Wild, tick it.

You need **BotW v208** — it is the only version the patches target.

## 5. Create the mod

```
python <sdk>\scripts\build_mod.py --init hello_mod
```

You get a folder that opens in an editor and builds:

```
hello_mod\
    mod.cpp        a working module - logs a line at load
    mod.json       { "id": "hello_mod" }
    .clangd        editor config for clangd
    .gitignore     build output, and the generated editor files
```

## 6. Build it

```
python <sdk>\scripts\build_mod.py --source hello_mod
```

```
[wxlm] hello_mod.wxlm  id=hello_mod v1.0.0  phase=load
[wxlm]   import wiixl.core:Log
[wxlm]   payload 252 B, 6 relocs, 1 imports, 0 exports, 1 required, 16 B strings
[wxlm]   entry WiiXLaunch_ModEntry @0x0, init_array 0, bss 0 B, heap request 0 B
[wxlm]   0 declared patch(es)
[wxlm]   file 484 B, content crc32 0x6BDF5CF6
[build_mod] mod.json: id=hello_mod phase=load
```

You get `hello_mod\build\hello_mod.wxlm`. It is one file, and it is the only
thing you ship. Every import it uses is named in that output — which is also
the list the host will be asked for at load.

## 7. Open it in your editor

**Do the build first.** A mod folder has no build system in it, so nothing
tells an editor where the headers are until that first build writes two files
beside your source:

* `compile_commands.json` — read by clangd, and by anything else that speaks
  the compilation-database format
* `.vscode\c_cpp_properties.json` — read by the VS Code C/C++ extension

Now open the **mod folder** — not the SDK — and `#include
<wiixlaunch/imports/...>` resolves, symbols autocomplete, and jumping to a
declaration lands in the generated header for that surface, with the surface's
own notes sitting above it.

Both files are written from the exact command that compiles your mod, so they
cannot drift from it, and both are rewritten every build. They are written
*before* the compile, so the editor is configured even when the code does not
build yet — which is when you most want it working.

Still seeing unresolved includes? Your editor is reading neither file. Point
clangd at the folder, or run **C/C++: Reset IntelliSense Database** in VS Code.

## 8. Run it

Copy `hello_mod.wxlm` into the pack's `content\WiiXLaunch\mods\`, start the
game, and open Cemu's log window. You are looking for:

```
[loader] 1 module(s) found; load order is lexical by filename, ...
[loader]   1. hello_mod.wxlm
[loader:hello_mod] v1.0.0  payload 252 B, bss 0 B, 6 relocs, 1 imports, phase 0
[loader:hello_mod] integrity OK (crc32 0x6BDF5CF6 over 340 bytes)
[loader:hello_mod] requires wiixl.core v1.0 - present
[loader:hello_mod] LOADED, entry at 0x..., waiting for phase 0. Arena: ...
[loader:hello_mod] phase 0 reached, calling entry at 0x... (module 1 of 1 ...)
hello_mod: loaded
```

**The log is the tool.** Every module is named as it loads, and anything
refused is named with the reason. If a line is missing, the last one present
tells you how far it got: no `requires` line means the host has no such
surface, no `LOADED` means it did not relocate, no `phase 0` means it never
ran.

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

* **The host is not published.** `sdk/` is committed and can be downloaded
  directly, but the host graphic pack is still cut by building this repo. It is
  a self-contained folder and could be attached to a release; nobody has.
* **Cemu is the only platform that has run.** The Switch and Wii U hosts build
  every time and have never been executed. A mod targets surfaces rather than a
  platform, so it should follow — but nothing has demonstrated that.
