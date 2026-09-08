# WiiXLaunch mod SDK

Everything needed to build a `.wxlm`, and nothing else. No framework checkout,
no submodules, no game headers.

## Build a mod

```
python scripts/build_mod.py --source path/to/your_mod
```

Your mod is a directory holding `mod.cpp` and a `mod.json`:

```json
{ "id": "your_mod" }
```

## Write a mod

```cpp
#include <wiixlaunch/imports/wiixl_core.h>
#include <wiixlaunch/mod_runtime.h>

namespace C { WXL_USE_wiixl_core(Log); }

extern "C" __attribute__((used)) void WiiXLaunch_ModEntry() {
    if (C::Log) C::Log("hello from a mod built with no framework in sight");
}
```

`include/wiixlaunch/imports/` holds one header per surface. Each DECLARES every
symbol that surface publishes; `WXL_USE_<surface>(Name)` BINDS one, and only
what you bind becomes an import. Do not hand-write import declarations - the
signature is the one thing about a mod that nothing checks, and these are
generated from the host's own tables so it cannot be wrong.

`mod_runtime.h` defines memcpy, memset, memmove and memcmp. GCC synthesises
calls to them even under -ffreestanding, nothing else defines them, and the
link succeeds anyway - so without this a module branches to address 0 the first
time it copies a struct.

## What this SDK was cut from

24 surfaces:

| surface | version |
|---|---|
| `botw.actor` | v1.0 |
| `botw.armour` | v1.1 |
| `botw.camera` | v1.0 |
| `botw.display` | v1.0 |
| `botw.events` | v1.0 |
| `botw.flyt` | v1.0 |
| `botw.gamedata` | v1.2 |
| `botw.gfx` | v1.0 |
| `botw.gui` | v1.0 |
| `botw.input` | v1.1 |
| `botw.map` | v1.1 |
| `botw.memory` | v1.0 |
| `botw.player` | v1.1 |
| `botw.pouch` | v1.1 |
| `botw.region` | v1.0 |
| `botw.sound` | v1.0 |
| `botw.vfx` | v1.0 |
| `botw.world` | v1.0 |
| `wiixl.call` | v1.0 |
| `wiixl.core` | v1.5 |
| `wiixl.mem` | v1.0 |
| `wiixl.net` | v1.2 |
| `wiixl.patch` | v1.0 |
| `wiixl.time` | v1.0 |

A host publishes these or later minors. Your mod names what it needs and the
loader refuses it by name if the host cannot provide it, which is the point of
the arrangement.
