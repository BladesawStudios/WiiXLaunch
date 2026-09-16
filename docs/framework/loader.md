# Framework: The module loader

[« Back to overview](../overview.md)

How WiiXLaunch gets compiled mods into memory and running, and what is and
is not initialized when your code first runs. For day-to-day mod work, see
[SDK: Hooks, patches and runtime](../sdk/hooks-patches-and-runtime.md) and
[SDK: Getting started](../sdk/getting-started.md#8-run-it).

## The load point

A mod is a `.wxlm` blob on the filesystem; its callback addresses do not
exist until the bytes are read and relocated. Nothing in a mod can hook
anything that runs before the host loads it. The load point is where that
happens, a per-game, per-platform fact the base framework cannot know.

A game module nominates one at build time with `WIIXL_DECLARE_LOAD_POINT(addr)`
plus a stub named `WiiXLaunch_LoadPointStub`. `scripts/deploy.py` reads both
out of the ELF and emits `.origin = <addr> / b wiixlaunch_loadpoint_stub`
into the host pack. A build declaring neither has no load point, and
deploy.py says so.

### What has been measured

| Platform | Load point | Status |
|---|---|---|
| Cemu | Entry hook `0x03098928` is already late enough | Measured |
| Wii U (Aroma) | `ON_APPLICATION_START` | Implemented, never run |
| Switch | `exl_main`, in the subsdk before the game (or `fs_ready`, per target) | Measured |

| | arena lives in | cache flush |
|---|---|---|
| Cemu | tail of the code cave, read from the backend | `Backend::FlushCache` |
| Wii U | `MEMAllocFromDefaultHeapEx`, one address | `DCFlushRange` + `ICInvalidateRange` |
| Switch | the host's own `.text`, written through a second mapping | `exl::util::Jit::Flush` |

Switch is the only one needing `Arena::SetWriteAlias`; Horizon refuses to
make an address both writable and executable.

Only Cemu needs a game module to nominate its load point, since that load
point is an address inside the game. Wii U gets a lifecycle event, and
Switch runs this subsdk before the game's own main, so on both, the host
drives the loader itself (`src/wiiu_plugin.cpp`, `src/switch_entry.cpp`)
without depending on a game module. Which Switch entry is used is per-target
(`switch.load_point`), depending on the game's SDK version. See
[Setup](setup.md#when-the-loader-runs-switch).

On Cemu, `WiiXLaunch::LoadPoint::Probe` reported `FS-USABLE` at the entry
hook: `FSAddClient`, `FSOpenFile`, `FSReadFile`, `FSReadFileWithPos`, and
`FSOpenDir` all succeed there, before the game has called `FSInit`. This
does not generalize: Cemu HLEs coreinit, so the filesystem is live from
process start. Aroma runs against real IOSU and Switch has its own romfs
mount timing, neither yet probed. Nomination stays the rule.

## Phases

The loader separates ingesting a module from running its code.

### Ingestion (not a phase mods can request)

Reading the blob, relocating it, resolving imports against the surface
registry, running `.init_array`. Touches only the module's own bytes and the
host's tables, so it happens as early as the filesystem is readable.

### `load`

The first phase a mod's code runs in.

You may rely on: host services (code-cave heap, `WIIXL_LOG`, hook manager,
surface registry, every registered surface), `WiiXLaunch::FS` reads
including the pack's `content/` overlay, and installing hooks (in place
before the target function runs).

You may not rely on, on Cemu: the game's own state (the Cemu load point
fires before the game creates its own FS client; `Player`, `Actor`,
`GameData`, `Pouch`, camera, world are reading uninitialized memory),
graphics (GX2/NVN not up), or coreinit base heaps (`Mem::UseCoreinitHeap()`
fails; MEM1/MEM2 do not exist yet).

Rule of thumb: in `load`, install hooks and allocate. Do not read the game.
Hook the function that produces a value instead of reaching for it now.

### `post_gx2` (Cemu, Wii U) / `post_nvn` (Switch)

Deferred until the graphics pipeline is up, via `GX2::OnInitialized` /
`NVN::OnInitialized`. Textures, meshes, and draw callbacks belong here.

### `app_start` (Wii U only)

WUPS's `ON_APPLICATION_START`. `load` and `app_start` coincide on Wii U
until the probe says otherwise.

There is no `early` phase. A mod's code cannot run before its bytes are in
memory.

## The heap is not a fixed size

On Cemu, the host's heap is the tail of its code cave, and Cemu assigns code
caves to graphic packs in load order. Enabling other packs moves
WiiXLaunch's payload later and shrinks the heap:

```
Boot A (7 other packs enabled): payload at 0x01804600, heap 3959 KB
Boot B (1 other pack enabled):  payload at 0x01803500, heap 3963 KB
```

Measured across four boots of the same build: 3959 / 3963 / 3934 / 3930 KB.
A module that loads on a clean setup and is refused on a loaded one is
neither a module bug nor a host bug; the refusal log says so.

## One memory owner, two ends

`WiiXLaunch::Arena` (`include/wiixlaunch/loader/arena.hpp`) owns the whole
reservation. There is no second allocator.

```
[ host allocations ->                          <- module grants ]
^ Base()                                        Base()+Total() ^
```

The host (framework plus every compiled-in game module) allocates upward
with `Arena::AllocHost`. A mod gets a bounded piece carved downward, reached
only through `wiixl.core`'s `Alloc`. A grant fails if it would cross the
host's high-water mark; a host allocation fails if it would cross into
carved territory.

A game module is compiled into the payload, so its memory is the host's.
When `wiixlaunch-botw`'s GUI allocates a font sheet, it spends the host's
memory, bounded only by where module grants begin.

`Mem::UseCoreinitHeap()` installs a host provider
(`Arena::SetHostProvider`) to move host allocation onto a coreinit base
heap. Module grants are never redirected: a grant holds relocated code that
gets executed, and the code cave is the only region established as
executable.

### What stays a host static

The trampoline pool and log ring stay plain host statics rather than moving
into the arena. They are per-payload state, and once `main.cpp` is the only
payload, there is exactly one of each already. Moving the log ring would
also break `ring_log_reader`, which finds it by scanning for a magic cookie:
a static has a stable address across boots, while an arena allocation moves
with pack count.

## The heapRequest contract

`heapRequest` in the `.wxlm` header selects one of two contracts.

### `heapRequest > 0`: a stated requirement

The host grants exactly that much or refuses the module by name at load
time, before relocating it. The image itself is charged to the grant, so the
loader reserves `heapRequest + payloadSize + bssSize` and rejects the module
if that sum overflows 32 bits.

On success:

```
Arena: <mod_id> granted=<N> (<N/1024> KB) requested=<N> (<N/1024> KB) at <addr> - stated requirement, met exactly; <F> KB free
```

On refusal:

```
Arena: <mod_id> REFUSED granted=0 requested=<N> (<N/1024> KB), free=<F> (<F/1024> KB). A stated heapRequest is a requirement, so the module is refused rather than given less than it asked for.
Arena: The arena is the tail of a 4 MB code cave shared with every enabled graphic pack, so it shrinks as more are enabled (measured 3930-3963 KB across four boots). The same module may load on a cleaner setup.
```

Exceeding a stated grant later:

```
Arena: <mod_id> wanted <N> bytes and has <U> of <G> used - refused. It stated a heapRequest and has now exceeded it.
```

### `heapRequest == 0`: best effort

The module is granted whatever is sensible (`kDefaultGrantCap` 256 KB, or an
equal share of the arena, or less if that's all there is), and `Alloc`
returns null past it. `wiixl.core` v1.1 adds `HeapGranted`, `HeapUsed`, and
`HeapRemaining`, callable during `load` before allocating.

On success:

```
Arena: <mod_id> granted=<G> (<G/1024> KB) requested=unspecified at <addr> - best effort, Alloc returns null past this; <F> KB free
```

Refused only when nothing is free:

```
Arena: <mod_id> REFUSED granted=0 requested=unspecified - nothing free to assign
```

On exhaustion:

```
Arena: <mod_id> wanted <N> bytes and has <U> of <G> used - refused. It stated no heapRequest, so this is a best-effort grant and null is the documented answer.
```

Granted-vs-requested is logged for every module, always, in this exact
wording.

## Load order

`Loader::LoadAll(dir)` loads every `.wxlm` in a directory. Load order is
lexical by filename, ascending, byte-wise on the raw name. `FSReadDir`'s
order is not used; names are collected and sorted.

Byte-wise means uppercase sorts before lowercase (`Zebra.wxlm` before
`apple.wxlm`), and digits sort before letters, so a `10_`/`20_` prefix
scheme works but `10_` sorts before `9_`. Pad to a fixed width.

A rejected module does not stop the others; the log names which failed.
`scripts/deploy.py` prints modules in the same sorted order.

## The load sequence

```
1. host hooks         installed by WiiXLaunch_Init, before any module exists
2. declared patches   every module's, at load, in lexical load order
3. module entries     called by RunPhase; these may install more hooks
```

`LoadAll` loads every module before `RunPhase` calls a single entry, so
patch conflicts are known before any mod code runs. Patches before later
hooks matters too: the hook manager captures a target's prologue once, on
first install, so a module hooking an address another module patched
captures the patched bytes. Reversed, the manager would capture the
original prologue and the patch would overwrite the jump it had just
written.

The other direction is checked, not merely ordered around: see
[Hooks](hooks.md#patches-and-hook-windows).

## Declared patches

Raw bytes written to an absolute address, declared in the `.wxlm` header as
data. `PatchEntry` is 40 bytes:

```
targetAddr   absolute address in the game
size         1..16
origin[16]   what must be there now
data[16]     what to write
```

Every patch carries the bytes it expects to find, and the host refuses to
write if the target doesn't hold them. Refusals are values
(`Patches::Result`), not log strings:

| Result | Means | Whose problem |
|---|---|---|
| `BAD-SIZE` | size 0 or above 16 | the mod's build |
| `BAD-TARGET` | address 0 | the mod's build |
| `INTO-ARENA` | aimed at module memory, which moves per boot | the mod's build |
| `ORIGIN-MISMATCH` | target does not hold what was expected | game version |
| `HOOKED-WINDOW` | inside 16 bytes a hook displaced | another mod |
| `PATCH-OVERLAP` | bytes another module already patched | another mod |

A refused patch does not fail the module; it is named and skipped, and
other patches still apply.

## Module resources

```
content/WiiXLaunch/mods/
    a_first.wxlm            the modules themselves
    b_second.wxlm
    _host/logo.bin          the host's own resources
    a_first/greeting.txt    a_first's files
    b_second/greeting.txt   b_second's files (a DIFFERENT file)
```

### Per platform

Wii U and Cemu: inside the game's own content, the only storage those hosts
have. Switch: on the SD card, per title:

```
sd:/WiiXLaunch/mods/0100F2C0115B6000/        one game's modules
    houselimit.wxlm
    houselimit/config.ini
sd:/WiiXLaunch/mods/01007EF00011E000/        another game's
```

One SD card serves every game, so a flat directory would offer every game's
modules to every host. Most crossovers are already refused by name (a
missing game surface, a mismatched patch origin), but a module needing only
base surfaces and raw offsets is refused by nothing. That's exactly the
shape of a mod for a game with no module yet.

The loader falls back to the flat `WiiXLaunch/mods/` if no per-title
directory exists, and logs which it used.

### Two reads, not one with a fallback

| Call | Reads | Escapes? |
|---|---|---|
| `ModReadFile` | `mods/<this mod's id>/…` | no |
| `GameReadFile` | game content, host path candidates | n/a |

No single call tries the mod directory and falls back. The scoped call
refuses by value: `NO-MODULE`, `EMPTY`, `ABSOLUTE`, `PARENT-ESCAPE`,
`BAD-CHAR`, `TOO-LONG`. A `..` is refused only as a whole path component;
`version..txt` and `..hidden` are ordinary filenames.

The directory comes from which module the host is running
(`WiiXLaunch::ModContext`), never from anything the mod passes.

### The reserved id space

Ids beginning with `_` belong to the host. The loader refuses a module whose
id starts with one (`RESERVED-MOD-ID`); `mods/_host/` holds the host's own
resources.

## Per-frame ticks

A `.wxlm` entry runs once, at a phase. `wiixl.core`'s `RegisterTick` is the
repeating call; see
[SDK: Picking a tick](../sdk/hooks-patches-and-runtime.md#picking-a-tick).

Base has no concept of a frame; the source is nominated by whichever game
module is installed. `wiixlaunch-botw` calls `Tick::RunAll()` from the GX2
swap, after the host's own draw callbacks. No game module means no tick:

```
Tick: NO FRAME SOURCE - these callbacks will never run.
```

Call order is registration order, which is load order, which is lexical
filename order. Refusals: `NO-MODULE`, `NULL-CALLBACK`, `NO-SLOTS`,
`ALREADY-REGISTERED`. One tick per module.

### A hang in a tick has an owner

The dispatcher writes who it is about to call into a record before calling,
and clears it after:

```c
struct InFlightRecord {
    uint32_t magic;      // 'WXTK', findable in a dump without symbols
    uint32_t sequence;   // ++ per dispatch; frozen means frozen
    uint32_t depth;      // 1 while a tick runs
    char     owner[17];  // who is running, or "" between ticks
};
```

If the game freezes, `owner` still names the module. The sequence counter
distinguishes a hang inside a tick from the game no longer calling `RunAll`
at all.

A tick also runs with its module's identity and arena current, so a file
read or allocation from inside one is attributed to the right mod.

## Verifying the loader

`tools/loader_fuzz` builds a valid module, produces corrupted variants (bit
flips, truncation at every section boundary, extreme counts/offsets,
overlapping sections), and asserts each is rejected by name with nothing
allocated and nothing relocated. It runs as an ordinary program: parsing is
ordinary logic, and a boot proves less at higher cost.

The important class is corruption with a correct checksum: a CRC only
proves the bytes match what the writer produced, not that their structure
is sane. The fuzzer recomputes the CRC after corrupting, so the integrity
check cannot short-circuit the structural ones.

Anything the loader writes at runtime (imports, relocated pointers, zeroed
`.bss`) must be `volatile` in a module that checks it, or the compiler folds
the check to a constant. The header sweep is judged against an independent
oracle that re-derives well-formedness from the mutated bytes, not a list
of which fields matter (that list would just restate the loader's own
model). See [Modules](modules.md) for both rules in full.

## Failure modes the loader must handle

* No modules present: clean, logged no-op.
* A failed or truncated read: skip that module, log it, keep going.
* ABI mismatch: the host logs its version and surfaces at the load point,
  and rejects mismatched modules by name.
* A missing surface: a module requiring `botw.gfx v1` on a host with no
  BotW module fails cleanly with that message.
