# SDK: Hooks, patches and runtime

[« Back to overview](../overview.md) · [Getting started](getting-started.md) ·
[Imports and versioning](imports-and-versioning.md)

This is the mod-side hook mechanism, separate from
[Framework: Hooks](../framework/hooks.md)'s `WIIXL_HOOK_DEFINE_TRAMPOLINE` and
`Install(switchOffset, wiiuOffset)`, which are compile-time macros for code
built into the host. A `.wxlm` reaches the same hook registry through one
runtime import instead.

## Installing a hook

```cpp
#include <wiixlaunch/imports/wiixl_core.h>

namespace C { WXL_USE_wiixl_core(InstallHook); }

uintptr_t original = C::InstallHook(target, reinterpret_cast<uintptr_t>(&MyCallback));
```

`InstallHook(target, callback)` returns the address to call to continue the
chain, or `0` if refused. Ignoring the return value replaces the function
outright; this is legal and reported in the boot log the same as a host-built
replace hook.

* No platform argument. The host picks the mechanism internally (Cemu's chain
  manager, exlaunch, WUPS); a `.wxlm` is already compiled for one
  architecture.
* No owner name. The hook is attributed to whichever module the loader is
  currently running.

`target` is a plain resolved address for your mod's architecture, not a
`(switchOffset, wiiuOffset)` pair. Use `wiixl.call:ResolveTarget(switchOffset,
wiiuOffset)` to compute it from a Ghidra-style offset pair.

Chaining, call order, and the conflict report work as described in
[Framework: Hooks](../framework/hooks.md#several-hooks-on-one-function).

## Declaring a patch

Patches declared this way are applied before your entry point runs, before
any module's entry runs (see
[Framework: The loader](../framework/loader.md#the-load-sequence)).

```cpp
#include <wiixlaunch/patch_decl.hpp>

WIIXL_DECLARE_PATCH(fix_thing, 0x02000030,
    WIIXL_PATCH_BYTES(0x7C, 0x9E, 0x23, 0x78),   // what must be there
    WIIXL_PATCH_BYTES(0x60, 0x9E, 0x00, 0x00));  // what to write
```

No code runs to declare it. The macro emits a record into a `.wxlm.patches`
ELF section, lifted verbatim into the module's header by `wxlm.py`. The
first byte list is the origin and is required: the host refuses to write
unless the target holds exactly those bytes.

Bytes are in memory order: PowerPC word `0x7C9E2378` is written
`7C 9E 23 78`; AArch64 word `0x528001E1` is written `E1 01 80 52`.

### Both architectures in one declaration

`targetAddr` cannot mean the same thing on both platforms (absolute on Wii
U/Cemu, module-relative on Switch), and the origin/replacement bytes are
different machine code entirely. Use:

```cpp
WIIXL_DECLARE_PATCH_CROSS(room_cap,
    /* Switch offset  */ 0x01B299EC,
    WIIXL_PATCH_BYTES(0xE1, 0x01, 0x80, 0x52),   // mov w1,#15
    WIIXL_PATCH_BYTES(0xA1, 0x05, 0x80, 0x52),   // mov w1,#45
    /* Wii U   address */ 0x02000030,
    WIIXL_PATCH_BYTES(0x7C, 0x9E, 0x23, 0x78),   // or  r30,r4,r4
    WIIXL_PATCH_BYTES(0x60, 0x9E, 0x00, 0x00));  // ori r30,r4,0
```

Only one record is emitted per build. For a patch that exists on one
platform only, guard the declaration with `#if WIIXL_SWITCH` instead of
inventing an address for the other side.

### Refusal reasons

| Result | Means | Whose problem |
|---|---|---|
| `BAD-SIZE` | size 0 or above 16 | the mod's build |
| `BAD-TARGET` | address 0 | the mod's build |
| `INTO-ARENA` | aimed at module memory, which moves per boot | the mod's build |
| `ORIGIN-MISMATCH` | target does not hold what was expected | game version |
| `HOOKED-WINDOW` | inside the 16 bytes a hook displaced | another mod |
| `PATCH-OVERLAP` | bytes another module already patched | another mod |

A refused patch does not fail the module. It is named and skipped; other
patches still apply.

## Picking a tick

A `.wxlm` entry runs once, at a phase. For per-frame work, use
`wiixl.core:RegisterTick`:

```cpp
namespace C { WXL_USE_wiixl_core(RegisterTick); }
C::RegisterTick(&MyPerFrameFunction);
```

Base has no concept of a frame; the source is nominated by whichever game
module is installed (`wiixlaunch-botw` calls `Tick::RunAll()` from its GX2/NVN
swap). No game module means no tick, and the host says so:

```
Tick: NO FRAME SOURCE - these callbacks will never run.
```

A game module may also publish a more specific tick as a surface symbol.
`botw.player:RegisterTick` fires at the point player state is coherent, but
does not run without a player actor (title screen, loads).
`botw.input:RegisterFrame` runs almost unconditionally, which is what a
server or menu needs to survive the title screen. Check the game module's
surfaces for what it exposes.

Call order is registration order, which is load order, which is lexical
filename order. Refusals: `NO-MODULE`, `NULL-CALLBACK`, `NO-SLOTS`,
`ALREADY-REGISTERED`. One tick per module.

## Arming

Several game subsystems are inert until something installs their hook. Until
armed, a call succeeds and does nothing, or reports failure as if the
request were invalid. Call each initializer once in `WiiXLaunch_ModEntry`:

```cpp
P::Init();                 // botw.player:   every cached player accessor
S::InputInit();            // botw.input:    injection lands nowhere without it
S::InitExtraEffects();     // botw.armour:   SetExtraEffect returns false without it
S::InitCompletion();       // botw.gamedata: the display override has no hook
S::InitBeastMarkers();     // botw.map:      markers are never collected
```

These are idempotent; several mods calling one is fine.

Two are per-frame, not per-load:

```cpp
S::TickEquipRefresh();     // botw.pouch: equips and repairs land here
S::TickWeatherHold();      // botw.world: a hold decays without this
```

If a write reports success and nothing happens in-game, check this list
first.

## Freestanding build rules

A `.wxlm` compiles `-nostdlib -nostartfiles`, linked with
`--unresolved-symbols=ignore-all`. Anything you forget links successfully
and branches to address zero, with no build error.

* No libc: no `printf`, `strlen`, `malloc`, `memcpy`.
* GCC synthesizes `memcpy`/`memset`/`memmove`/`memcmp` anyway for struct
  assignment and array init, even under `-ffreestanding`. Include
  `<wiixlaunch/mod_runtime.h>`, which defines all four plus
  `strcmp`/`strncmp`.
* Static constructors run: the loader executes `.init_array`.
* `.bss` is zeroed over a `0xCD` poison fill, so an uninitialized read is
  visibly `0xCDCDCDCD`.
* Memory comes from `wiixl.core:Alloc` against your arena grant (256 KB
  default, or your stated `heapRequest`). There is no `free`.
* No `<cmath>`, no libm. `<wiixlaunch/mod_math.h>` gives
  `WiiXLaunch::ModMath::Sqrt`, `Sin`, `Cos` (7.1e-08 relative error for sqrt,
  2.2e-07 absolute for sin/cos over ±100 radians). They do not install into
  `namespace std`.

## Next

* [Distribution](distribution.md)
* [Framework: Hooks](../framework/hooks.md)
* [Framework: The loader](../framework/loader.md)
