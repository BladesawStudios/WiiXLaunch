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
