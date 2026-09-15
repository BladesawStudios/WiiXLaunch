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
and select the **devkitPPC** package. That covers Cemu and Wii U, which are the
same module. WUPS and libfunctionpatcher are for building WiiXLaunch itself and
you do not need them.

Building for **Switch** as well? Add **devkitA64** and see step 6 — a mod is
compiled once per architecture, from the same source.

Check it, using your install path:

```
C:\devkitPro\devkitPPC\bin\powerpc-eabi-g++ --version
```

If you installed somewhere else, set `DEVKITPPC` to that directory. The build
looks at `$DEVKITPPC` first, then `C:\devkitPro\devkitPPC` and
`/opt/devkitpro/devkitPPC`, and tells you plainly if it finds nothing.

## 3. Get the SDK

The SDK is `sdk/` in the WiiXLaunch repository: a folder of plain text files,
nothing compiled. Download that folder from a release, from the repository's
web interface, or by cloning:

```
git clone https://github.com/TKVSC-Team/WiiXLaunch
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
    patch_WiiXLaunch_BotW.asm
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

For Switch, add `--target switch`. The same source, compiled with devkitA64
into `build\switch-mods\hello_mod.wxlm` — a separate file, because it is a
different architecture, and the loader refuses the wrong one by name rather
than running it. Cemu and Wii U share the one module. Either way the build
prints the path it wrote.

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

* **Python 3** — runs `scripts/generate_config.py` (turns the active
  `targets/<game>.json` into the generated headers each build reads) and
  `scripts/deploy.py`.
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
git clone --recurse-submodules https://github.com/TKVSC-Team/WiiXLaunch
```

Already cloned without it:

```bash
git submodule update --init --recursive
```

## Configuring the host: one file per game

Everything project-specific lives in `targets/<game>.json` — one file per
**game**, describing the **host** build for it. A `.wxlm` needs none of it and
carries its own `mod.json` instead.

```
targets/botw.json     Breath of the Wild   (the default)
targets/totk.json     Tears of the Kingdom
```

One checkout builds a host for any of them. Pick a target by argument, or by
setting `WIIXL_TARGET`:

```bash
build_switch.bat          # botw, the default
build_switch.bat totk     # targets/totk.json
```

Everything that needs to know which game is being built resolves through
`scripts/target.py`, and everything that resolves prints what it got. A build
that silently picks a target is the same class of problem as a gate nothing
invokes — and this is not hypothetical: `build_switch.bat` once had BotW's title
ID typed into it, so the first build of a second game copied a TotK `subsdk9`
into BotW's install. A value repeated in two places is a value that will
disagree with itself. Shell scripts that need one value ask
`scripts/target_value.py switch.title_id` rather than restating it.

### What a target file contains

* `project` — name, version, author, description, and `debug` (controls
  `EXL_DEBUG` on the Switch build).
* `memory` — heap/JIT/inline-pool sizes and the Cemu debug log buffer size.
  `jit_size` is hook trampolines: the default `0x1000` is about twenty, and a
  mod with more hooks than that aborts in `AllocForTrampoline`.
* `modules` — which `vendor/wiixlaunch-*` game modules to compile in. `[]` is a
  complete answer: TotK has no game module, and a host with none publishes the
  base surfaces only, refusing a module that needs a game surface **by name**.
* `patches` — `persist` decides whether declared patches survive the boot that
  applied them. A demo host restores them so the sample patch mod does not
  outlive itself; a host shipping real patch mods sets this true.
* `samples` — whether the framework's example mods belong on this host. They are
  BotW's, so a TotK host sets this false.
* `switch` — title ID, subsdk name, NPDM settings, and `load_point` (see
  [When the loader runs](#when-the-loader-runs-switch) below).
* `wiiu` — the plugin's `.wps` filename and the target title IDs it patches.
* `cemu` — the entry hook address, graphic pack path/version for your target
  game build, and `module_matches`. Usually needs no change; v208 is the only
  launchable version.

A section a target does not declare is a section that platform does not build.
`targets/totk.json` has no `wiiu` or `cemu` block, because there is no Wii U
Tears of the Kingdom, and the deploy says so instead of producing an empty
plugin.

`python scripts/generate_config.py` (every build script runs it) turns the
active target into headers under `build/generated/include/` and platform config
under `build/generated/switch/`. These are regenerated every build — edit the
target file, not the generated ones.

### Which build of the game is this?

Every offset in a host is written against one build, and a host that starts on
the wrong one installs hooks at addresses that mean something else. The
`identity` block is how a host can tell:

```json
"identity": {
  "switch": { "offset": "0x1000",     "length": 4096 },
  "wiiu":   { "address": "0x02000030", "length": 4096 },
  "known": [
    { "name": "1.5.0", "platform": "switch", "fingerprint": "0x1A2B3C4D" }
  ]
}
```

The host CRCs that slice of the running game and matches the result against
`known`. **A build's bytes are the least ambiguous name it has** — there is no
version string that works on all three platforms (Switch's `nn::oe` has no
binding in the vendored exlaunch, a Wii U title version can be shared by a
re-release, and Cemu has no notion of one), but every platform has the game's
own code.

Switch declares an **offset** and Wii U an **address**, for the same reason
every other pair in this project is split that way: an NSO is relocated on every
launch, so no constant here could name an address in one.

**Enrol a build from its dump, not by hand.** The bytes being hashed are
read-only data, so they are the same bytes that sit in the dump on disk:

```bash
python scripts/enrol_build.py --target totk --name 1.2.1 --nso <dump>/exefs/main
python scripts/enrol_build.py --target botw --name v208  --rpx <path>/U-King.rpx
```

It decompresses the NSO or RPX, computes the fingerprint the host will compute
at boot, and writes the row. Pointed at a directory of dumps it enrols a whole
version history in one pass. **The slice comes from the target file**, never
restated in the tool, so the two cannot disagree about which bytes are hashed.

For a build you can run but have no dump of, the boot log prints the number and
`--fingerprint 0x… --platform switch` takes it directly.

Each row also records the dump's **build id** — the thing a loader prints. That
is the evidence: a name is whatever somebody typed, and enrolling eleven builds
from the names of the folders they sat in is exactly how a wrong one gets in. A
build id can be checked against a boot log by anyone, later, with neither this
tool nor the dumps.

There is deliberately no guessing at runtime: an unrecognised build is reported
as unrecognised, because "probably 1.5.0" is the assumption that corrupts a save
three hours later. A target with no `identity` block says *that* instead, which
is a different state and reads differently.

**Pick the offset against the smallest build you intend to support.** TOTK
1.0.0-1.3.0 carry ~23.6 MB of read-only data and 1.4.0-1.4.3 only ~10.1 MB, so
an offset chosen against the larger ones is past the end on every 1.4.x - the
host bounds-checks and reports "cannot fingerprint", which is the right failure
but still a build that can never be enrolled.

Mods see this through `wiixl.version` and can carry offsets per build — see
[Writing a mod](writing-mods.md#one-mod-several-game-versions).

### When the loader runs (Switch)

`switch.load_point` decides when modules are loaded, and the right answer is a
fact about the game's SDK rather than a preference:

* `exl_main` — as soon as exlaunch hands over. Fine on BotW's nnSdk 4.4.0.
* `fs_ready` — at the first file the **game** opens. Required on nnSdk 15.x
  (TotK): `nn::fs` has no allocator until nnSdk installs one during init, so
  mounting the SD card any earlier calls through a null pointer and dies inside
  `nn::fs::fsa::Register`. Waiting for the game to open a file is a statement
  about the filesystem being up, rather than a guess about which function runs
  when.

## Building

```bash
build_cemu.bat      # the Cemu host
build_wiiu.bat      # the Wii U host
build_switch.bat    # the Switch host
build_all.bat       # all three hosts
```

A build script builds **one host for one game and nothing else**. It does not
run the gates, does not build the example modules, does not package, and does
not copy anything into an emulator. Pass a target to pick the game:
`build_switch.bat totk`.

Or CMake directly, Switch only — Wii U and Cemu use devkitPPC and the WUPS
Makefile flow, since their output is not a normal CMake executable target:

```bash
cmake -B build/switch -DPLATFORM=SWITCH
cmake --build build/switch
```

## Verifying

```bash
test.bat            # or ./test.sh
```

Every gate, in one place: the loader fuzzer, the host and format tests, surface
coverage, the import-header freshness check, an SDK cut and verified by building
a module from it, and the six example modules built for both machine types. A
gate failing fails the run.

Two gates want a host to have been built first - `test_host` links its own, but
`test_switch_module` reads `build/switch/wiixlaunch-switch.elf`, and says so
rather than skipping if it is missing.

These used to live inside `build_cemu` and `build_switch`, which meant you could
not build a host without also running every gate and publishing the result.
`scripts/audit_gates.py` checks that each gate is still invoked from `test.bat`
and `test.sh`, so moving one out fails rather than quietly losing coverage.

## Deploying

Packaging and installing is its own step, and always explicit:

```bash
python scripts/deploy.py --target botw
```

It writes `deploy/` and copies into the emulator directories it finds. Nothing
else calls it - a deploy writes the **whole** mods directory, so running it as
part of a build meant building a host for one game could overwrite the modules
installed for another.

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

* **The host is not published as a release yet.** `sdk/` is committed and can
  be downloaded directly, but the host graphic pack is still cut by building
  this repo (`python scripts/make_sdk.py --host`). It is a self-contained folder
  and belongs on a release page.
* **Wii U has never been run.** The Wii U host builds every time. It enumerates
  its mods directory, reserves an arena from the default heap, flushes caches
  and loads from `ON_APPLICATION_START` — and none of that has been executed.
  It needs Aroma on real hardware to verify; Cemu cannot run WUPS plugins, so
  the Cemu target proves nothing about it. Treat it as untested rather than as
  working. Cemu and Switch have both loaded and run modules for real.
* **The Switch host has no hook-probe target**, so `wiixl.core:HookProbeTarget`
  returns null there and the two-module hook-collision demo reports "not
  hooking" instead of running. Loading, relocation, imports, `.init_array` and
  arena accounting are all exercised on that platform; hook chaining is not.
* **A Switch mod is built separately**: `--target switch` produces an AArch64
  module in `build/switch-mods/`, and a mod is compiled once per architecture.
  The Wii U and Cemu module is the same file — a `.wxlm` names surfaces, and
  neither the code nor the format knows which of those two is running it.
