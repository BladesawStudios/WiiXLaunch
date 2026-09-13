# Writing a mod

This is the guide for the thing WiiXLaunch became. It used to be a template you
copied and built on; now it is a host with a versioned ABI, and a mod is a
separate compiled binary that asks the host for what it needs by name.

If you have read `docs/loader.md` this repeats a little of it on purpose - that
document explains how the loader works, this one explains how to use it.

---

## 1. The three layers, and why you care

```
  WiiXLaunch (base)         game-agnostic. Loader, hooks, patches, arena,
                            sockets, filesystem. Publishes wiixl.* surfaces.
        |
  vendor/wiixlaunch-botw    the game module. Knows BotW's offsets, structures
                            and hooks. Publishes botw.* surfaces.
        |
  your mod (.wxlm)          a relocatable blob. Knows NEITHER. It names the
                            surfaces it needs and the host resolves them.
```

The rule that makes this worth the trouble: **your mod never includes a game
header and never contains an offset.** When BotW's structures move, the game
module changes, and your compiled `.wxlm` keeps working because it only ever
asked for `botw.player:Life`, not for `*(int*)(link + 0x13C)`.

That is also the constraint. Anything you want to do has to exist as a surface
symbol. If it doesn't, the answer is to add it to the surface - not to reach
around the boundary.

---

## 2. Your first mod

A mod is a directory with a `mod.cpp` in it. Nothing else is required, though
one more file is worth having from the start:

```
hello_mod/
    mod.cpp
    mod.json      what the mod IS - see section 3
```

```cpp
// hello.wxlm - the smallest complete module.

// One generated header per surface. It DECLARES every symbol the surface
// publishes, with the signature taken from the surface's own table - so a
// signature cannot drift, because nobody typed it twice.
#include <wiixlaunch/imports/wiixl_core.h>

// Declaring is free; BINDING is what makes something an import. Name only what
// you use, and only that is imported. The macro applies the volatile the
// relocation requires - see section 4.
namespace C { WXL_USE_wiixl_core(Log); }

// The loader calls this once, at load. `used` because nothing in this
// translation unit references it and the optimizer would otherwise drop it.
extern "C" __attribute__((used)) void WiiXLaunch_ModEntry() {
    if (C::Log) C::Log("hello: I am a compiled mod and I resolved wiixl.core");
}
```

With `{"id": "hello"}` in `mod.json`, build it:

```
python scripts/build_mod.py --source path/to/hello_mod
```

The id is the module's name everywhere: the output `hello.wxlm`, its resource
directory, and the name in every log line it causes. It may not start with `_`
(that namespace is the host's).

Deploy it by dropping `hello.wxlm` next to the others. Where that is depends on
the platform, because only one of them has storage the game does not own:

```
Wii U / Cemu   <graphic pack>/content/WiiXLaunch/mods/hello.wxlm
Switch         sd:/WiiXLaunch/mods/<TITLE ID>/hello.wxlm
```

The Switch path is per title on purpose - one SD card serves every game, and a
flat directory would offer your mod to all of them. See
[the module loader](loader.md#where-that-directory-is-per-platform).

Boot, and the log should read (these are the real numbers - this example was
built to get them):

```
[loader:hello] v1.0.0  payload 96 B, bss 0 B, 6 relocs, 1 imports, phase 0
[loader:hello] integrity OK (crc32 ... )
[loader:hello] requires wiixl.core v1.0 - present
[loader:hello] relocated 6 entries (1 resolved through the registry)
[loader:hello] LOADED, entry at 0x..., waiting for phase 0
[loader:hello] phase 0 reached, calling entry at 0x...
hello: I am a compiled mod and I resolved wiixl.core
```

If any of those lines is missing, the one that *is* missing tells you where it
stopped. That is the whole debugging method and it is covered in section 10.

---

## 3. mod.json

**What a mod IS belongs in the mod's directory. What a BUILD is stays on the
command line.** Source location, output directory and which WiiXLaunch to build
against change per machine and per invocation; the mod's identity does not.

```json
{
  "id": "botw_api",
  "entry": "src/mod.cpp",
  "include": ["include"],
  "phase": "load",
  "heapRequest": 262144,
  "require": ["botw.map@1.1"]
}
```

| key | means |
|---|---|
| `id` | module id; the output filename and the resource directory |
| `entry` | the translation unit, relative to `--source` (default `mod.cpp`) |
| `include` | extra include directories, **relative to the mod** |
| `phase` | when the loader calls the entry point (default `load`) |
| `heapRequest` | bytes of arena needed; omit for best effort |
| `require` | surfaces at a minimum version, for a symbol from a later minor |

`entry` is what lets `--source` point at a repo root rather than at the
directory holding the `.cpp`, so the manifest sits next to your `README` where
you would look for it.

Three rules worth knowing:

- **An unknown key is an error**, not something ignored. A typo in a manifest
  that silently does nothing reads as configured and is not - the same failure
  as a gate that cannot fail. `heapRequst` stops the build and lists the keys
  that exist.
- **A flag beats the manifest, and says so**: `--id overrides mod.json's
  'from_manifest'`. A one-off build should not need you to edit the mod, and a
  flag quietly shadowing a file is how you debug the wrong thing for twenty
  minutes.
- **Lists accumulate.** A manifest naming what the mod needs and a command line
  adding one more are not in conflict, so `include` and `require` concatenate
  rather than replace.

Everything still works with no manifest at all - pass `--id` and the rest as
flags, exactly as before.

---

## 4. Imports

### Use the generated headers

`include/wiixlaunch/imports/` holds one header per surface -
`wiixl_core.h`, `botw_player.h`, `botw_map.h` - emitted from the surface tables
by `scripts/gen_imports.py`. **Do not hand-write import declarations.** The
signature of an import is the one thing about a mod that nothing else checks:
get it wrong and it compiles, links, packs, loads, and corrupts the stack at run
time. The generator exists so that cannot happen, and `--check` runs in the
build so a header cannot drift from its surface.

Binding is per symbol:

```cpp
namespace P {
WXL_USE_botw_player(Init);
WXL_USE_botw_player(ActorGetLife);
}
...
P::Init();
const int32_t life = P::ActorGetLife(handle);
```

**Declaring is not importing.** A header declares everything on its surface, and
a declaration nothing references costs nothing. Only what you bind becomes an
undefined reference, which is what `wxlm.py` turns into an import. A mod that
includes `wiixl_core.h`, `botw_player.h` and `botw_actor.h` - 66 declared
symbols - and binds three, packs as **3 imports and 2 required surfaces**;
`botw.actor` is not even required, because nothing bound from it. Those are
measured numbers, not an estimate - build the mod and `wxlm.py` prints the
import and surface counts it wrote.

### The naming convention underneath

```
wiixl_import__<surface with dots as underscores>__<Symbol>
   |                    |                            |
   prefix          wiixl.core                       Log
```

`scripts/wxlm.py` walks the ELF's undefined symbols, decodes each one, and
writes them into the `.wxlm` header. **You never repeat the list on a command
line**, which means the list and the code cannot disagree. The generated headers
are just this convention, written for you with the right types.

### Required surfaces are derived, not declared

Every surface an import names is automatically added to the module's required
list at v1.0. You do not have to declare anything. The consequence is worth
knowing: `[loader:yourmod] requires botw.map v1.0 - present` appears because you
called a `botw.map` symbol somewhere, not because you asked for it.

If you need a *newer* minor - a symbol that only exists from v1.1 - say so when
packing, so a mod that would silently miss the symbol is refused by name
instead:

```
--require botw.map@1.1
```

### The volatile rule

Every import pointer a mod holds must be `volatile`. Without it the compiler
folds the indirect call into a direct branch and emits a relocation kind that
cannot reach a host address - the module fails to relocate. `WXL_USE_*` applies
it for you, which is most of why binding goes through a macro rather than being
written out.

---

## 5. What is available

Ask the host. Every boot logs the full registry at the load point:

```
Surface: 24 surface(s) registered on this host:
Surface:   wiixl.core v1.5 (17 symbols)
Surface:   botw.player v1.1 (17 symbols)
...
```

The base publishes seven:

| surface | what it is for |
|---|---|
| `wiixl.core` | logging, arena allocation, file reads, hooks, the per-frame tick |
| `wiixl.net` | TCP sockets, tracked per module, non-blocking enforced |
| `wiixl.time` | monotonic ticks and the wall clock |
| `wiixl.mem` | the game's own expanded heap |
| `wiixl.call` | resolving a target address, the image base |
| `wiixl.patch` | writing bytes with an origin check |
| `wiixl.version` | which build of the game is underneath |

The BotW module publishes eighteen - `botw.player`, `botw.actor`, `botw.gfx`,
`botw.gui`, `botw.vfx`, `botw.flyt`, `botw.region`, `botw.camera`,
`botw.display`, `botw.events`, `botw.sound`, `botw.memory`, `botw.gamedata`,
`botw.world`, `botw.input`, `botw.map`, `botw.pouch`, `botw.armour`.

For what is in each, read the **generated header** - `include/wiixlaunch/imports/`
has one per surface, listing every symbol with its real signature and the version
it came from. For *why* a symbol behaves as it does, read the surface itself in
`vendor/wiixlaunch-botw/include/wiixlaunch/botw/surfaces/*.hpp`; the comments
above each function are the contract.

### One mod, several game versions

A mod that resolves raw offsets — which is every mod for a game with no
WiiXLaunch module — is written against **one build**. Point it at another and
the addresses still resolve, still install, and mean something else. Nothing
crashes at the mistake; something else does later, somewhere unrelated.

`<wiixlaunch/mod_version.h>` turns that into a refusal:

```cpp
#include <wiixlaunch/mod_version.h>

static constexpr WiiXLaunch::BuildOffset kRoomCap[] = {
    { "1.2.1", 0x01B299EC },
    { "1.2.0", 0x01B28A40 },
};
static const WiiXLaunch::VersionedOffset g_RoomCap("roomCap", kRoomCap);

uintptr_t addr = g_RoomCap.Resolve();   // 0, and one log line, on anything else
```

The names are the ones **the host** enrolled, not names the mod invents — a mod
does not get to decide which build it is on. The host fingerprints the running
game by CRC-ing a slice of its code and matching that against the
`identity.known` list in its target file; see
[Setting Up](setup.md#which-build-of-the-game-is-this).

An unmatched build applies nothing, and the log says which of three things
happened, because they want three different fixes:

```
version: roomCap - this host cannot fingerprint the game ...
version: roomCap - the host does not recognise this build (0x1A2B3C4D) ...
version: roomCap has no row for build "1.2.0" (2 known) ...
```

The first is a target with no identity slice declared; the second is a build
nobody has enrolled yet; the third is an enrolled build *this* mod was never
written for. Only the third is your problem.

**This narrows the window, it does not close it.** A declared patch still names
its origin bytes and the host still reads a patch back after writing it. A
version table is the cheapest check and the coarsest, and it is not a reason to
skip the others.

### Settings, in a file beside your module

A number a user might want to change should not be a constant you compiled in.
`<wiixlaunch/mod_config.h>` reads a `key = value` file out of your own resource
directory:

```cpp
#include <wiixlaunch/mod_config.h>

WiiXLaunch::Config cfg;
cfg.Load("config.txt");                         // scoped to YOUR directory
int rooms = cfg.GetIntClamped("rooms", 45, 16, 128);
bool loud = cfg.GetBool("verbose", false);
```

`Load` goes through the scoped read, so `config.txt` means
`mods/<your id>/config.txt` and nothing else. Put the file in your mod's `data/`
directory and `build_mod.py` stages it next to the `.wxlm`.

Three things about the parser, because settings files are written by hand by
people who have never seen the grammar:

* **It has no allocator**, so the buffer is fixed at 2048 bytes and a file larger
  than that is refused *entirely* rather than truncated. Half a settings file
  that still parses is worse than none.
* **A malformed value is not zero.** `rooms = fourty` does not silently become
  `0`; the getter reports that it could not read a number and you get your
  default. A parser that returns 0 tells the user their setting does not work
  while looking like it worked.
* **`GetIntClamped` states the range**, so an out-of-range value lands inside it
  instead of indexing off the end of something.

CRLF, missing trailing newlines, keys that are prefixes of other keys and
comment lines (`#`, `;`, `//`) are all handled — `tools/config_test` drives 34
cases of exactly these through the real `Load`.

---

## 6. Picking a tick

There are **three** per-frame sources and they do not have the same lifetime.
Choosing wrong is the difference between a mod that works and one that is dead
exactly when you need it.

| source | fires from | stops when |
|---|---|---|
| `wiixl.core:RegisterTick` | the GX2 buffer swap, after host draw callbacks | rendering stops |
| `botw.player:RegisterTick` | the player's own state refresh | **there is no player actor** - title screen, loads |
| `botw.input:RegisterFrame` | the game's input read | basically never |

Rules of thumb:

- Touching **player state** (life, position, this frame's attack)? Use
  `botw.player:RegisterTick`. It runs at the point in the frame where that data
  is coherent, and it not running means there is no player to touch.
- **Drawing**? Use `botw.gfx:RegisterDraw` or `botw.gui:RegisterFrame`.
- Anything that must **survive the title screen and loading** - a server, a menu,
  an input injector, anything holding state it has to release - use
  `botw.input:RegisterFrame`.

That last one is not a hypothetical. The API mod was pumped from the player tick
and was therefore dead on the title screen, which is precisely when you need it
to undo whatever left you there.

All three are attributed multi-slot registries: several mods can register, the
host records which is which, and a callback that hangs is named in the log
rather than being an anonymous freeze.

---

## 7. Arming: the trap

**Several game subsystems answer "no" when the honest answer is "not armed
yet."** They are inert until something installs their hook, and until then the
call succeeds and does nothing, or returns false as if you asked for something
impossible.

If your mod uses any of these, call the initialiser once in `ModEntry`:

```cpp
P::Init();                 // botw.player - EVERY cached player accessor
S::InputInit();            // botw.input  - injection lands nowhere without it
S::InitExtraEffects();     // botw.armour - SetExtraEffect returns false without it
S::InitCompletion();       // botw.gamedata - the display override has no hook
S::InitBeastMarkers();     // botw.map - markers are never collected
```

They are idempotent. Several mods each calling them is fine; the return value
tells you whether *this* call installed it.

Two more are per-frame rather than per-load. If you equip, repair or hold
weather, something has to pump them or the write is re-read away a frame later:

```cpp
S::TickEquipRefresh();     // botw.pouch  - equips and repairs land here
S::TickWeatherHold();      // botw.world  - a hold decays without this
```

This is the single most expensive failure mode in the framework's history and it
is not your fault when you hit it. If a write reports success and nothing
happens, check this list first.

---

## 8. The freestanding rules

A `.wxlm` is compiled `-nostdlib -nostartfiles` and linked with
`--unresolved-symbols=ignore-all`. That last flag is what lets imports be
undefined - and it also means **anything else you forgot links successfully and
branches to address zero.**

- **No libc.** No `printf`, `strlen`, `malloc`, `memcpy`. Nothing.
- **GCC synthesises `memcpy`/`memset`/`memmove`/`memcmp` anyway** for struct
  assignment and array init. You must define them yourself, or you get a jump to
  0 at runtime with no build error. The API mod keeps them in
  `include/wiixlaunch/api/mod_log.hpp`; copy that file into a new mod as a
  starting point.
- **Static constructors do run.** The loader executes `.init_array`. (The *host
  payload* has no crt0 and cannot; a mod is different, because the loader does it
  for you.)
- **`.bss` is zeroed** over a `0xCD` poison fill, so a zeroed global really is
  zero and an uninitialised read is visible as `0xCDCDCDCD`.
- Memory comes from `wiixl.core:Alloc` against your module's arena grant
  (256 KB by default). There is no `free` - the arena is yours for the session.

---

## 9. Versioning

`(major, minor)` per surface. **Major must match exactly; minor must be at least
what you asked for.**

- A symbol was **added** -> minor bumps -> your old mod still resolves.
- A symbol's **meaning or signature changed** -> major bumps -> your mod is
  refused by name at load, with the reason in the log, rather than calling
  something that no longer means what it did.

This is why the API mod requires `botw.map v1.0` and happily runs against a host
publishing v1.1. When you add to a surface, append to the symbol table and bump
the minor - never insert, never reorder.

---

## 10. When it doesn't work

The boot log is the instrument. Read it top-down and find the first line that is
missing or wrong.

| symptom | look for |
|---|---|
| mod not in the list at all | `[loader] N module(s) found` - is the filename `.wxlm` and in `mods/`? |
| `MISSING-SURFACE` | you called a symbol from a surface this host does not publish |
| loads, entry never runs | the `phase 0 reached` line for your id |
| entry runs, nothing happens | section 7 - is the subsystem armed? |
| a write "succeeds" and does nothing | section 7 - is something pumping the tick? |
| game freezes | the in-flight record names the module it was inside, with a sequence number. **A frozen sequence means a hang inside a callback; an advancing one means the game stopped calling us.** Those look identical from outside. |

Load order is **lexical by filename**, and load order is hook install order,
which is call order. `a_first.wxlm` hooks before `b_second.wxlm`, so a_first
wraps b_second. Rename to reorder.

---

## 11. Shipping a mod to other people

Everything above assumes you have this repo. A mod author does not need it.

```
python scripts/make_sdk.py build/sdk
```

cuts a self-contained SDK - no submodules, no game headers, no host source.
`make_sdk` prints how many files it wrote, so that number lives in one place
rather than here as well:

```
sdk/
    scripts/build_mod.py   wxlm.py
            ppc_relocs.py       wxlm_mod.ld           PowerPC
            aarch64_relocs.py   wxlm_mod_aarch64.ld   AArch64
    include/wiixlaunch/imports/*.h        one per surface, generated
    include/wiixlaunch/mod_runtime.h      memcpy and friends
    include/wiixlaunch/mod_log.h          WIIXL_LOG
    include/wiixlaunch/mod_math.h         sqrt, sin, cos - there is no libm
    include/wiixlaunch/mod_config.h       the key = value settings reader
    include/wiixlaunch/format.hpp         the .wxlm layout
    include/wiixlaunch/patch_decl.hpp     WIIXL_DECLARE_PATCH
    sdk.json  README.md                   which host this was cut from
```

Hand that to someone with devkitPro and they build with:

```
python sdk/scripts/build_mod.py --source their_mod
python sdk/scripts/build_mod.py --source their_mod --target switch
```

`build_mod.py` derives its root from its own location, so nothing needs
configuring and the framework tree can be absent entirely.

`--verify` is the part worth knowing about. It compiles a probe module using
**only** the assembled SDK, from a working directory neither tree owns, and
diffs the result against the same probe built from the full checkout. A byte
difference or a build failure fails the gate, so "a mod can be built without the
framework" is something that happened during this build rather than something
the architecture diagram claims. It runs in `test.bat`, and it is how
`ppc_relocs.py` was caught missing from the first SDK - a dependency you forgot
is not one you can notice from inside the tree that has it.

---

## 12. The gates

Run before you trust anything:

```
build_all.bat                 all three hosts
test.bat                      every gate, plus the example modules
python scripts\surface_coverage.py    every public module function is reachable or excused
```

What they *don't* measure, so you know what your own testing is for: whether a
surface's behaviour matches the module's, whether a wrapper drops an argument,
and whether anything actually happens in the game. Every one of those has bitten
this project while the gates read green.

---

## Where to go next

- `docs/loader.md` - the `.wxlm` format, load sequence, phases, refusals
- `docs/modules.md` - the standing rules, and why each one exists
- `docs/hooks.md` - chaining, trampolines, the conflict report
- `docs/net.md` - sockets, the static-import rule
- `examples/` - six working mods, smallest first: `hook_mod_a`, `sample_mod`,
  `patch_mod`, `player_mod`, `net_mod`
- The API mod (`BotW_API_wxlm`) - the largest real one: 201 imports, nine
  surfaces, an HTTP server pumped from the input frame
