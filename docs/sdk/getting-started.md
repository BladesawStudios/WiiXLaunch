# SDK: Getting started

[« Back to overview](../overview.md)

A mod is a directory with `mod.cpp` in it, compiled to a `.wxlm` file. All
you need is the `sdk/` folder and devkitPro.

## 1. Install Python 3

```
python --version
```

## 2. Install devkitPPC

Get the installer from [devkitpro.org](https://devkitpro.org/wiki/Getting_Started)
and select devkitPPC. It covers Cemu and Wii U, which build the same module.

Building for Switch too? Also install devkitA64.

Check it:

```
C:\devkitPro\devkitPPC\bin\powerpc-eabi-g++ --version
```

If devkitPro is somewhere else, set `DEVKITPPC` to that directory.

## 3. Get the SDK

```bash
git clone https://github.com/TKVSC-Team/WiiXLaunch
```

Copy `sdk/` wherever you want it.

## 4. Install the host in Cemu

Cut the host pack from a checkout:

```bash
build_cemu.bat
python scripts\make_sdk.py --host
```

This writes `build\host\`. Copy it into Cemu's graphic pack folder:

```
Cemu\graphicPacks\WiiXLaunch_BotW\
    rules.txt
    patch_WiiXLaunch_BotW.asm
    content\WiiXLaunch\mods\        <- your mods go here
```

Enable it under Options > Graphic Packs > Breath of the Wild. Requires BotW
v208.

## 5. Scaffold a mod

```bash
python <sdk>\scripts\build_mod.py --init hello_mod
```

Creates:

```
hello_mod\
    mod.cpp        logs a line at load
    mod.json       { "id": "hello_mod" }
    .clangd
    .gitignore
```

`mod.cpp`:

```cpp
#include <wiixlaunch/imports/wiixl_core.h>
#include <wiixlaunch/mod_runtime.h>

namespace Core {
WXL_USE_wiixl_core(Log);
}

extern "C" __attribute__((used)) void WiiXLaunch_ModEntry() {
    if (Core::Log) Core::Log("hello_mod: loaded");
}
```

Include the header for a surface, bind the symbols you use with
`WXL_USE_<surface>(Symbol)`, call them. Only bound symbols are imported. See
[Imports and versioning](imports-and-versioning.md).

## 6. Build it

```bash
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

Output: `hello_mod\build\hello_mod.wxlm`. That file is the whole mod.

Add `--target switch` for AArch64: `build\switch-mods\hello_mod.wxlm`, a
separate file per architecture. Cemu and Wii U share one module.

## 7. Open it in your editor

Build first. The first build writes `compile_commands.json` (clangd) and
`.vscode\c_cpp_properties.json` (VS Code) beside your source, so headers
resolve and autocomplete works. Open the mod folder, not the SDK folder.

Unresolved includes: point clangd at the folder, or run **C/C++: Reset
IntelliSense Database** in VS Code.

## 8. Run it

Copy `hello_mod.wxlm` into the pack's `content\WiiXLaunch\mods\`, start the
game, open Cemu's log window (or `tools/ring_log_reader`, see
[Framework: Debugging](../framework/debugging.md)). Expect:

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

If a line is missing, the last one present tells you how far it got: no
`requires` line means the host has no such surface, no `LOADED` means it did
not relocate, no `phase 0` means it never ran.

## Where a mod's files live

```
Wii U / Cemu   <graphic pack>/content/WiiXLaunch/mods/hello_mod.wxlm
Switch         sd:/WiiXLaunch/mods/<TITLE ID>/hello_mod.wxlm
```

Switch paths are per title so one SD card can serve multiple games.

## mod.json

```json
{
  "id": "hello_mod",
  "entry": "mod.cpp",
  "phase": "load",
  "heapRequest": 262144,
  "include": ["include"],
  "require": ["botw.map@1.1"],
  "target": "cemu"
}
```

| key | means |
|---|---|
| `id` | module id, output filename, resource directory. Cannot start with `_`. |
| `entry` | translation unit with `WiiXLaunch_ModEntry` (default `mod.cpp`) |
| `sources` | further `.cpp` files to compile, relative to the mod |
| `phase` | when the loader calls the entry point (default `load`) |
| `heapRequest` | bytes of arena needed; omit for best effort |
| `include` | extra include directories, relative to the mod |
| `require` | surfaces at a minimum version, e.g. `botw.map@1.1` |
| `target` | `cemu` (PowerPC, also Wii U) or `switch` (AArch64) |

An unknown key is an error, not ignored. A command-line flag overrides the
manifest and prints when it does. `include` and `require` from the command
line add to the manifest's list rather than replacing it.

Works with no manifest at all: pass `--id` and the rest as flags.

## Next

* [Imports and versioning](imports-and-versioning.md)
* [Hooks, patches and runtime](hooks-patches-and-runtime.md)
* [Distribution](distribution.md)
* `examples/`: `sample_mod`, `hook_mod_a`/`hook_mod_b`, `patch_mod`,
  `player_mod`, `net_mod`
