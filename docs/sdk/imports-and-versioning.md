# SDK: Imports and versioning

[« Back to overview](../overview.md) · [Getting started](getting-started.md)

A mod never includes a game header and never contains a game offset. It names
the surfaces it needs (`wiixl.core`, `botw.player`) and the host resolves
each import at load time. If the game module underneath changes, the
surface's signatures stay put and your compiled `.wxlm` keeps working.

## Use the generated headers

`include/wiixlaunch/imports/` has one header per surface: `wiixl_core.h`,
`botw_player.h`, `botw_map.h`. Use the generated ones. Do not hand-write an
import declaration.

```cpp
namespace P {
WXL_USE_botw_player(Init);
WXL_USE_botw_player(ActorGetLife);
}
...
P::Init();
const int32_t life = P::ActorGetLife(handle);
```

A header declares every symbol on a surface; declaring costs nothing. Only a
bound symbol becomes an import. A mod including `wiixl_core.h`,
`botw_player.h`, and `botw_actor.h`, binding three symbols, packs as 3
imports and 2 required surfaces. `wxlm.py` prints the real counts on build.

## Naming convention

```
wiixl_import__<surface, dots as underscores>__<Symbol>
```

`scripts/wxlm.py` walks the compiled ELF's undefined symbols, decodes each
one against this convention, and writes them into the `.wxlm` import table.
You never repeat the list on a command line.

## The volatile rule

Every import pointer must be `volatile`:

```cpp
#define WXL_USE_wiixl_core(sym) \
    inline decltype(&wiixl_import__wiixl_core__##sym) volatile sym = \
        &wiixl_import__wiixl_core__##sym
```

Without it the compiler folds the indirect call into a direct branch, which
emits a relocation `wxlm.py` cannot fix up:

```
[wxlm] wiixl_import__wiixl_core__Log is imported by R_PPC_REL24, which the
  loader cannot fix up. An import's ADDRESS must be taken into a variable,
  never called directly.
```

`WXL_USE_*` applies `volatile` for you.

## Required surfaces

Every surface an import names is added to the module's required list at
v1.0, automatically. `[loader:yourmod] requires botw.map v1.0 - present`
appears because you called a `botw.map` symbol, not because you declared it.

Need a symbol only present from v1.1? Pack with:

```
--require botw.map@1.1
```

or add it to `mod.json`'s `require`.

## What is available

Every boot logs the registry:

```
Surface: 25 surface(s) registered on this host:
Surface:   wiixl.core v1.5 (17 symbols)
Surface:   botw.player v1.1 (17 symbols)
...
```

Base surfaces:

| surface | for |
|---|---|
| `wiixl.core` | logging, arena allocation, file reads, hooks, per-frame tick |
| `wiixl.net` | TCP sockets, tracked per module, non-blocking |
| `wiixl.time` | monotonic ticks and wall clock |
| `wiixl.mem` | the game's own expanded heap |
| `wiixl.call` | resolving a target address, image base |
| `wiixl.patch` | writing bytes with an origin check |
| `wiixl.version` | which build of the game is underneath |

The BotW module adds eighteen more: `botw.player`, `botw.actor`, `botw.gfx`,
`botw.gui`, `botw.vfx`, `botw.flyt`, `botw.region`, `botw.camera`,
`botw.display`, `botw.events`, `botw.sound`, `botw.memory`, `botw.gamedata`,
`botw.world`, `botw.input`, `botw.map`, `botw.pouch`, `botw.armour`. See
`sdk/README.md` for exact versions.

For symbol signatures, read the generated header in
`include/wiixlaunch/imports/`.

## Versioning

`(major, minor)` per surface. Major must match exactly. Minor must be at
least what you asked for.

* A symbol added: minor bumps, your mod still resolves.
* A symbol changed or removed: major bumps, your mod is refused by name at
  load.

## One mod, several game versions

A mod resolving raw offsets (any game with no WiiXLaunch module) is built
against one game build. Pointed at another build, addresses still resolve
and mean something else, with nothing crashing at the mistake.

`<wiixlaunch/mod_version.h>` refuses that instead:

```cpp
#include <wiixlaunch/mod_version.h>

static constexpr WiiXLaunch::BuildOffset kRoomCap[] = {
    { "1.2.1", 0x01B299EC },
    { "1.2.0", 0x01B28A40 },
};
static const WiiXLaunch::VersionedOffset g_RoomCap("roomCap", kRoomCap);

uintptr_t addr = g_RoomCap.Resolve();   // 0, and one log line, on anything else
```

Build names come from the host's enrolled `identity.known` list (see
[Framework: Setup](../framework/setup.md#which-build-of-the-game-is-this)),
not from the mod.

An unmatched build logs one of three reasons:

```
version: roomCap - this host cannot fingerprint the game ...
version: roomCap - the host does not recognise this build (0x1A2B3C4D) ...
version: roomCap has no row for build "1.2.0" (2 known) ...
```

The first is a target with no identity slice declared. The second is a build
nobody has enrolled. The third is an enrolled build this mod doesn't have an
entry for. Only the third is your problem.

A version table narrows the failure window; it does not close it. A declared
patch still names its origin bytes and the host still reads back after
writing.

## Settings in a file beside your module

```cpp
#include <wiixlaunch/mod_config.h>

WiiXLaunch::Config cfg;
cfg.Load("config.ini");
int rooms = cfg.GetIntClamped("rooms", 45, 16, 128);
bool loud = cfg.GetBool("verbose", false);
```

`Load` is scoped: `config.ini` means `mods/<your id>/config.ini`. Put the
file in your mod's `data/` directory; `build_mod.py` stages it next to the
`.wxlm`.

Notes on the parser:

* Fixed 2048-byte buffer, no allocator. A larger file is refused entirely.
* A malformed value is not zero. `rooms = fourty` reports failure and you
  get your default, not `0`.
* `GetIntClamped` states the range so an out-of-range value lands inside it.

CRLF, missing trailing newlines, prefix keys, and comments (`#`, `;`, `//`)
are all handled.
