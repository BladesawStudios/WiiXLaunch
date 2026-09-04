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
