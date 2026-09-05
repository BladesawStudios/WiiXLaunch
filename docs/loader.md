# The module loader

[« Back to overview](overview.md)

How WiiXLaunch gets compiled mods into memory and running, and — more
importantly — **what is and is not initialised when your code first runs**.

## The load point

A mod is a `.wxlm` blob on the filesystem. Its callback addresses do not exist
until its bytes have been read and relocated, so nothing in a mod can hook
anything that runs before the host has loaded it. The **load point** is where
that happens, and it is a **per-game, per-platform fact** that the base
framework has no way to know.

A game module nominates one at build time with `WIIXL_DECLARE_LOAD_POINT(addr)`
plus a stub named `WiiXLaunch_LoadPointStub`; `scripts/deploy.py` reads both out
of the ELF and emits `.origin = <addr> / b wiixlaunch_loadpoint_stub` into the
host pack, next to the entry hook. A build that declares neither has no load
point, and deploy.py says so rather than producing a host that boots and
silently does nothing.

### What has actually been measured

| Platform | Load point | Status |
|---|---|---|
| Cemu | Entry hook `0x03098928` is already late enough | **Measured** |
| Wii U (Aroma) | `ON_APPLICATION_START` | **Not yet probed** |
| Switch | `subsdk9`, before the game | **Not yet probed** |

On Cemu, `WiiXLaunch::LoadPoint::Probe` reported `FS-USABLE` at the entry hook
itself: `FSAddClient`, `FSOpenFile`, `FSReadFile`, `FSReadFileWithPos` and
`FSOpenDir` all succeed there, against both stock title content and
graphic-pack-injected content, *before* the game has called `FSInit`.

**That result does not generalise, and must not be treated as if it does.** It
holds because Cemu HLEs coreinit — the filesystem is live from process start, so
the game's own `FSInit`/`FSAddClient` pair concerns the game's client, not the
subsystem. That is a property of the emulator. Aroma runs against real IOSU, and
Switch has its own romfs mount timing. Nomination therefore stays the rule, not
the exception, until the equivalent probe has run on the other two.

## Phases

The loader deliberately separates **ingesting a module** from **running its
code**. They have different safety requirements and they do not have to happen
at the same moment.

### Ingestion (not a phase mods can request)

Reading the blob, relocating it, resolving its imports against the surface
registry, and running its `.init_array`. This touches only the module's own
bytes and the host's own tables — no game state is involved — so it can happen
at the earliest point the filesystem is readable.

### `load`

The first phase a mod's own code runs in, immediately after ingestion.

**What you may rely on:**

- Host services: the code-cave heap, `WIIXL_LOG`, the hook manager, the surface
  registry, and every surface the host registered.
- `WiiXLaunch::FS` — reading files, including from the pack's `content/`
  overlay.
- Installing hooks. A hook installed here is in place before the function it
  targets runs, which is the entire point of loading this early.

**What you may NOT rely on, on Cemu:**

- **The game's own state.** The Cemu load point is *inside* the construction of
  the game's first singleton — it fires before the game has created its own FS
  client. Anything a game module exposes (`Player`, `Actor`, `GameData`,
  `Pouch`, camera, world) is reading uninitialised memory at this point.
- **Graphics.** GX2/NVN are not up. Nothing may be drawn and no texture may be
  created.
- **coreinit base heaps.** `Mem::UseCoreinitHeap()` will fail here; MEM1/MEM2
  base heaps do not exist yet. The code-cave heap is all there is.

The rule of thumb: in `load`, **install hooks and allocate; do not read the
game.** If you want to know a game value, hook the function that produces it
rather than reaching for it now.

### `post_gx2` (Cemu, Wii U) / `post_nvn` (Switch)

Deferred until the graphics pipeline is initialised, via
`GX2::OnInitialized` / `NVN::OnInitialized`. Textures, meshes and draw
callbacks belong here. The game is up by this point, so game state is readable.

### `app_start` (Wii U only)

WUPS's `ON_APPLICATION_START`. This is where the Wii U host loads modules at
all, so on that platform `load` and `app_start` coincide until the Wii U probe
says otherwise.

There is deliberately **no `early` phase**. A mod's code cannot run before its
bytes are in memory, and moving the load point earlier is a host and game-module
decision, not something an individual mod can ask for.

## The heap is not a fixed size

On Cemu the host's heap is the tail of its code cave, and Cemu assigns code
caves to graphic packs in load order. Enabling other packs moves WiiXLaunch's
payload later and **shrinks the heap**:

```
Boot A (7 other packs enabled): payload at 0x01804600, heap 3959 KB
Boot B (1 other pack enabled):  payload at 0x01803500, heap 3963 KB
```

Small in that example, but it is a function of what the user has enabled, not a
constant. Anything that subdivides this space must read the size at runtime and
log what it got. Nothing may reserve a fixed amount.

Measured across four boots of the same build: **3959 / 3963 / 3934 / 3930 KB**.
A module that loads on a clean setup and is refused on a loaded one is neither a
module bug nor a host bug, and the refusal log says so in as many words.

## One memory owner, two ends

`WiiXLaunch::Arena` (`include/wiixlaunch/loader/arena.hpp`) owns the whole
reservation. There is no second allocator; `Backend::CemuHeapLimit`,
`AllocCemuHeap`, `CemuHeapUsed`, `CemuHeapRemaining`, `CemuHeapExhausted`,
`SetHeapProvider` and `SetHeapLimit` were **deleted, not wrapped**. Every one of
those callers read the distance to the wall and then did its own bookkeeping
against it, each believing it had the whole distance to itself — which is how
two allocators hand out the same bytes. A compatibility shim would have kept
every caller compiling and kept the overlap reachable, just harder to see.

The arena is carved from both ends:

```
[ host allocations ->                          <- module grants ]
^ Base()                                        Base()+Total() ^
```

- The **host** — the framework plus every game module compiled into the payload
  — allocates upward with `Arena::AllocHost`.
- A **mod** (`.wxlm`) is granted a bounded piece carved downward, and reaches
  memory only through `wiixl.core`'s `Alloc`, which can never step outside it.

A grant fails if it would cross the host's high-water mark; a host allocation
fails if it would cross into carved territory.

**A game module is compiled into the payload, so its memory is the host's.**
Mods get carved grants; the host and its game modules share one growing region.
That is the line, and everything downstream inherits it: when
`wiixlaunch-botw`'s GUI allocates a font sheet it is spending the host's memory,
not a mod's, and it is bounded only by where module grants begin.

That line is also the reason the fixed reserve was wrong. An earlier version
reserved a flat 64 KB for the host and gave modules the rest — which assumed the
host is the framework. It is not: it is the framework **plus whatever game
module is installed**, and that is unbounded by nature. `wiixlaunch-botw`'s GX2
layer allocates font sheets and render targets in megabytes. A fixed reserve
either starves the host or has to be guessed so large it defeats the point. Two
ends need no guess, because neither side has to be sized in advance.

`Mem::UseCoreinitHeap()` installs a **host** provider (`Arena::SetHostProvider`)
to move host allocation onto a coreinit base heap. Module grants are never
redirected: a grant holds relocated code that gets executed, and the code cave is
the only region this project has established is executable.

### What stays a host static, and why not to "finish the job"

The trampoline pool and the log ring stay plain host statics. They are not
moving into the arena, and a later change that moves them for the sake of
uniformity would make things worse.

They were never a sharing problem. They are **per-payload** state, and the
original diagnosis was about two payloads each having their own. Once
`main.cpp` becomes a `.wxlm` there is exactly one payload — the host — so that
is solved by the architecture, not by the arena. "Single memory owner" means
modules no longer share an unbounded heap, and that is now true.

Moving the log ring would also break a working tool. `ring_log_reader` finds it
by scanning for a magic cookie, and **a static has a stable address across boots
while an arena allocation moves with pack count** — the four measurements above
are the same variance the reader would then have to chase. Do not trade a
working debugging tool for uniformity.

## The `heapRequest` contract — both paths

`heapRequest` in the `.wxlm` header is a **request, not a grant**. It selects
between two contracts, and stating a number means being held to it.

### `heapRequest > 0` — a stated requirement

The host grants exactly that much or refuses the module by name, at load time,
before relocating it. It is never rounded down to what could be spared: handing
over less would be a promise the host did not keep, which is the failure this
contract exists to prevent.

The image itself is charged to the grant as well — a module's footprint is its
code plus whatever it allocates — so the loader reserves `heapRequest +
payloadSize + bssSize` and rejects the module if that sum does not fit a 32-bit
size.

On success, verbatim:

```
Arena: <mod_id> granted=<N> (<N/1024> KB) requested=<N> (<N/1024> KB) at <addr> - stated requirement, met exactly; <F> KB free
```

On refusal, verbatim, followed by the shared-arena note:

```
Arena: <mod_id> REFUSED granted=0 requested=<N> (<N/1024> KB), free=<F> (<F/1024> KB). A stated heapRequest is a requirement, so the module is refused rather than given less than it asked for.
Arena: The arena is the tail of a 4 MB code cave shared with every enabled graphic pack, so it shrinks as more are enabled (measured 3930-3963 KB across four boots). The same module may load on a cleaner setup.
```

Exceeding a stated grant later is still refused, and says which contract was in
force:

```
Arena: <mod_id> wanted <N> bytes and has <U> of <G> used - refused. It stated a heapRequest and has now exceeded it.
```

### `heapRequest == 0` — best effort

The module is granted whatever is sensible (`kDefaultGrant`, 256 KB, or less if
that is all there is) and `Alloc` returns null past it. A mod that cannot
predict its usage stays loadable and is expected to check for null.

It does **not** have to find its size out by allocating until null. `wiixl.core`
v1.1 appends `HeapGranted`, `HeapUsed` and `HeapRemaining`, callable during the
`load` phase before allocating anything.

On success, verbatim:

```
Arena: <mod_id> granted=<G> (<G/1024> KB) requested=unspecified at <addr> - best effort, Alloc returns null past this; <F> KB free
```

Refused only when there is genuinely nothing:

```
Arena: <mod_id> REFUSED granted=0 requested=unspecified - nothing free to assign
```

And on exhaustion:

```
Arena: <mod_id> wanted <N> bytes and has <U> of <G> used - refused. It stated no heapRequest, so this is a best-effort grant and null is the documented answer.
```

**granted-vs-requested is logged for every module, always** — including the
best-effort path, where "requested" is the interesting half of the answer. When
a mod misbehaves in-game this is the first line worth having, and the wording
above is reproduced literally so a bug report can be matched against it.

## Load order

`Loader::LoadAll(dir)` loads every `.wxlm` in a directory. **Load order is
lexical by filename, ascending, byte-wise on the raw name.**

That is a specification, and it has to be one. Load order determines the order
module entry points run, which determines the order they install hooks, which
determines the order they see a hooked call (see [Hooks](hooks.md)). It is the
user's only lever over which mod acts first, so it has to be predictable from
the filenames they can see.

**`FSReadDir`'s order is not used.** coreinit does not specify it and it is not
stable across filesystems or hosts. The names are collected and then sorted, so
the order is a property of the filenames rather than of the volume they happen
to sit on.

Byte-wise means:

- uppercase sorts before lowercase — `Zebra.wxlm` loads before `apple.wxlm`
- digits sort before letters, so a `10_` / `20_` prefix scheme works, but
  `10_` sorts before `9_` — pad to a fixed width if you use numbers
- no locale, no case folding, no natural-number handling

A module that is found and rejected does not stop the others: the remaining
modules still load, and the log says which failed and how many were left. One
bad file must not cost the user their boot, and it must not silently cost them
the rest of their mods either.

`scripts/deploy.py` prints the modules it packs in the same sorted order, so
what the build shows and what the loader will do are the same list.

## The load sequence

Fixed, and it is a specification rather than an implementation detail:

```
1. host hooks         installed by WiiXLaunch_Init, before any module exists
2. declared patches   every module's, at load, in lexical load order
3. module entries     called by RunPhase; these may install more hooks
```

`LoadAll` loads every module before `RunPhase` calls a single entry, so by the
time any mod code runs the host has already applied every patch every mod
declared.

**Patches before entries** is what makes patch conflicts detectable at all. The
host sees the whole set up front, so patch-vs-patch and patch-vs-hook overlaps
are known before anything executes rather than discovered when someone's game
misbehaves. Applying patches on request from inside a module's entry would give
that up: the host would learn about the second patch only after the first had
been written.

**Patches before later hooks** is what makes patching a to-be-hooked function
safe. The hook manager captures a target's prologue exactly once, when the first
hook on that address is installed — so a module hooking an address another
module patched captures the **patched** bytes, which is correct. That is the
only reason that direction needs no check. Reverse the order and the manager
would capture the original prologue, the patch would then overwrite the jump the
manager had just written, and the trampoline would hold bytes matching nothing.

**The other direction is not safe and is checked** — see
[Hooks](hooks.md#patches-and-hook-windows).

## Declared patches

A patch is raw bytes written to an absolute address, declared in the `.wxlm`
header as data rather than performed by code. `PatchEntry` is 40 bytes:

```
targetAddr   absolute address in the game
size         1..16
origin[16]   what must be there now
data[16]     what to write
```

**Every patch carries the bytes it expects to find, and the host refuses to
write if the target does not hold them.** A patch is built against one build of
one game and written by absolute address; applied to a different build the
address means something else and the write *succeeds*, silently, into a function
the mod has never heard of. Nothing crashes at the write — something unrelated
misbehaves later. Cemu graphic packs have carried `.origin` for exactly this
reason, and moving the mechanism into the header is not a licence to drop the
property that made it usable.

Refusals are values, not log strings (`Patches::Result`), so a test can assert
which one happened:

| Result | Means | Whose problem |
|---|---|---|
| `BAD-SIZE` | size 0 or above 16 | the mod's build |
| `BAD-TARGET` | address 0 | the mod's build |
| `INTO-ARENA` | aimed at module memory, which moves per boot | the mod's build |
| `ORIGIN-MISMATCH` | target does not hold what was expected | game version |
| `HOOKED-WINDOW` | inside 16 bytes a hook displaced | another mod |
| `PATCH-OVERLAP` | bytes another module already patched | another mod |

A refused patch never fails the module. It is named and skipped, the module
still loads, and its other patches are still tried — one bad address must not
cost a user the mod, and must not silently cost them its other patches either.

## Module resources

Every module gets a directory named for its id:

```
content/WiiXLaunch/mods/
    a_first.wxlm            the modules themselves
    b_second.wxlm
    _host/logo.bin          the host's own resources
    a_first/greeting.txt    a_first's files
    b_second/greeting.txt   b_second's files - a DIFFERENT file
```

Two mods shipping a file of the same name is now a non-event. Before, it was a
collision resolved by whichever the filesystem answered first: silent and
order-dependent.

### Two reads, not one with a fallback

`wiixl.core` gives a mod two distinct calls, and the choice is made at the call
site:

| Call | Reads | Escapes? |
|---|---|---|
| `ModReadFile` | `mods/<this mod's id>/…` | **no** |
| `GameReadFile` | game content, host path candidates | n/a — that is its job |

There is deliberately no single call that tries the mod directory and falls
back. "Whichever resolves first wins" is the same ambiguity as an FS status of
`-6` meaning two things, as two path resolvers disagreeing, and as three refusal
reasons collapsed into one `bool`. Each of those cost a debugging round.

Equally deliberately, there is no full containment. Reading game content is a
large part of what modding *is*, and a mod that cannot open a game pack is
crippled. The containment lives on the scoped call, where it is a guarantee
worth having.

**The scoped call refuses by value**, so a test can assert which rule fired:
`NO-MODULE`, `EMPTY`, `ABSOLUTE`, `PARENT-ESCAPE`, `BAD-CHAR`, `TOO-LONG`. A
`..` is refused only as a whole path *component* — `version..txt` and
`..hidden` are ordinary filenames and stay readable.

The directory comes from **which module the host is running**
(`WiiXLaunch::ModContext`), never from anything the mod passes — the same
attribution rule as hook ownership and probe tags. A mod cannot reach another
mod's files by naming them.

### The reserved id space

**Ids beginning with `_` belong to the host.** The loader refuses a module whose
id starts with one (`RESERVED-MOD-ID`), and `mods/_host/` holds the host's own
resources.

The host's `logo.bin` moved there rather than staying at
`content/WiiXLaunch/logo.bin`. It should not be the one thing exempt from the
scheme — that is how someone later concludes the scheme is optional. A whole
prefix is reserved rather than one name, so a future reserved id needs no new
check and no new refusal path.

## Per-frame ticks

A `.wxlm` entry is called once, at a phase, and nothing after. That is enough
for a mod that installs hooks and gets out of the way, and useless for one that
polls. `wiixl.core`'s `RegisterTick` is the repeating call.

**Base has no concept of a frame and does not invent one.** A frame is a
graphics idea and graphics is game-specific, so the source is *nominated* -
exactly as the load point is. `wiixlaunch-botw` calls `Tick::RunAll()` from the
GX2 swap, after the host's own draw callbacks; base owns the registry, the
ordering and the attribution.

A host with no game module therefore has no tick, and `Tick::LogState` **says
so** rather than leaving a mod that registered a callback to fail silently:

```
Tick: NO FRAME SOURCE - these callbacks will never run.
```

Call order is registration order, which is load order, which is lexical
filename order — the same lever the user already has over hook priority, so
there is one answer to "which mod goes first" rather than two.

Refusals are values: `NO-MODULE`, `NULL-CALLBACK`, `NO-SLOTS`,
`ALREADY-REGISTERED`. One tick per module.

### A hang in a tick has an owner

A tick runs every frame, so a mod that crashes or spins in one takes the game
down — and the report that reaches you is *"my game freezes with these mods
installed"* with nothing narrowing it. The log's last line is whatever printed
before the freeze, usually from something else entirely.

So the dispatcher writes who it is **about to** call into a record before
calling, and clears it after:

```c
struct InFlightRecord {
    uint32_t magic;      // 'WXTK' - findable in a dump without symbols
    uint32_t sequence;   // ++ per dispatch; frozen means frozen
    uint32_t depth;      // 1 while a tick runs
    char     owner[17];  // who is running, or "" between ticks
};
```

If the game stops, `owner` still names the module. The magic makes it findable
in a memory dump or by `tools/ring_log_reader` without a symbol table, and the
sequence counter distinguishes **a hang inside a tick** from **the game no
longer calling `RunAll` at all** — different problems that look identical from
outside.

A tick also runs with its module's identity and arena current, so a file read
or an allocation from inside one is attributed and charged to the right mod
rather than to whoever ran last.

## Verifying the loader

Two properties make the loader the component most worth testing hard: it reads
data it did not produce, and it writes to memory it then executes. A bad file
must be rejected, not partially applied.

**Structure is fuzzed on the host, not in-game.** `tools/loader_fuzz` builds a
valid module, produces corrupted variants of it - bit flips, truncation at every
section boundary, counts and offsets set to 0, 1, 0xFFFFFFFF and just-past-
bounds, overlapping sections, an entry outside the payload, an `.init_array`
range crossing the end - and asserts each is rejected by name with nothing
allocated and nothing relocated. It runs as an ordinary program because parsing
is ordinary logic; a boot proves less and costs more.

The important class is **corruption with a correct checksum**. A CRC only proves
the bytes are the ones the writer produced; it says nothing about whether their
structure is sane. A module built by a slightly-wrong future writer has a valid
checksum over invalid structure, and that is exactly the file that must not
relocate. So the fuzzer recomputes the CRC after corrupting, which stops the
integrity check short-circuiting the structural ones.

**A self-check the optimizer can answer is not a test.** See the rule in
[Modules](modules.md): a probe the compiler can fold to a constant reports
success without the mechanism running. Anything the loader writes at runtime -
imports, relocated pointers, zeroed `.bss` - must be `volatile` in a module that
checks it.

**The header sweep is judged by an independent oracle**, not by a list of which
fields matter. That list would be the loader's own model of the format restated,
and a test built from it can only confirm what the loader already believes. The
oracle re-derives well-formedness from the mutated bytes using the format rules
alone, and a flip is a failure only when the two disagree. The first version did
use a field list, reported 12 failures, and was mostly wrong - most of what it
flagged were flips producing a different but still valid module. See the rule in
[Modules](modules.md).

## Failure modes the loader must handle

- **No modules present** — clean, logged no-op. The game boots normally. This is
  a fresh host's default state.
- **A failed or truncated read** — skip that module, log it, keep going. One
  corrupt file must not cost the user their boot.
- **ABI mismatch** — the host logs its own version and registered surfaces at
  the load point, and rejects mismatched modules by name.
- **A missing surface** — a module requiring `botw.gx2 v1` on a host that has no
  BotW module must fail cleanly with that message, never jump into nothing.
