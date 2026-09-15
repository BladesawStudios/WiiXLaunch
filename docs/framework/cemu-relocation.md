# Framework: How the Cemu payload finds itself

[« Back to overview](../overview.md)

A Cemu build is a flat binary dropped into a graphic-pack code cave. Cemu
assigns code caves sequentially in pack load order, so the address a
payload runs at depends on which packs the user has enabled and the Cemu
version. Two people running the same pack file get different addresses.

The payload links at base 0 and relocates itself on entry.

## The parts

`scripts/deploy.py` reads the relocations the linker kept (`-Wl,-q`) and
emits them as a table after the payload, then patches
`g_CemuRelocTableOffset` and `g_CemuRelocCount`. Each entry is two words:

| Word | Contents |
|---|---|
| 0 | `kind << 24 \| offset` (offset into the payload, capped at 16MB) |
| 1 | the relocation's link-time target |

`kind`: `0` = `R_PPC_ADDR32`, `1` = `ADDR16_HA`, `2` = `ADDR16_HI`, `3` =
`ADDR16_LO`. `ADDR32` sites already hold their own base-0 target, read back
from the payload; the 16-bit halves carry the resolved `S+Addend` instead,
since they can't be recovered from the instruction alone.

The bootstrap (`WiiXLaunch_Cemu_Init`, `src/cemu/bootstrap.cpp`) finds its
own address before touching anything:

```asm
bl __wiixl_here          # LR = this label's *runtime* address
__wiixl_here:
mflr 31
lis  30, __wiixl_here@h  # the same label's *link-time* address
ori  30, 30, __wiixl_here@l
subf 31, 30, 31          # r31 = load address
```

`cemu.ld` brackets this section with
`__wiixl_bootstrap_start`/`__wiixl_bootstrap_end`, and deploy.py excludes
that range from the table since the bootstrap is the one place still
holding base-0 constants at run time. It calls the relocator, stores the
base in `g_CodeCaveBase`, and calls `WiiXLaunch_Init`.

`WiiXLaunch_Cemu_Relocate` applies the table, then flushes the payload from
the data cache and invalidates the instruction cache, since half the writes
edit instruction immediates.

The relocator touches no globals, has no string literals, and calls
nothing; every address arrives in a parameter or is derived from `base`. If
it ever needed relocating itself, it would be reached through a table it
hasn't applied yet. Applying `base + target` rather than adding a delta
also makes it idempotent if the entry hook fires twice.

## What this replaced

`deploy.py` used to relocate the payload at build time against a hardcoded
`codecave_base = 0x01804600`. When wrong, every absolute address in the
payload is off by the same fixed delta: hooks jump past their callbacks,
globals read the wrong memory, and `WIIXL_LOG` resolves a bogus shim table
(so there's no log output either, since the logger is broken by the same
bug). The user sees a crash a second or two after the title screen and an
empty log.

Real case: one machine ran a pack at `0x01804600` and worked; another
placed it at `0x01800000`, and the `0x4600` delta sent the KPAD hook
`0x4600` past `KPADReadExWrapperHook::Callback`. The giveaway, missed at
first, was `r12` holding the same value across two builds whose layouts
had shifted. A symbol address can't do that; a relocation delta can.

## Weak symbols do not work here

An undefined weak symbol is the standard way to call into a component that
might not exist:

```cpp
extern "C" __attribute__((weak)) void MaybeThere();
if (&MaybeThere) MaybeThere();   // WRONG on this platform
```

On the Cemu payload, that null check passes even when the symbol is
absent, then calls into the start of the payload.

An undefined weak symbol links as address 0. Taking its address emits an
ordinary `R_PPC_ADDR32` relocation, and this payload relocates every such
site by adding the runtime code-cave base. So 0 becomes `g_CodeCaveBase`,
the guard passes, and control transfers to `wiixlaunch_codecave_start`,
which is `WiiXLaunch_Cemu_Init`. Nothing warns: the symbol is legitimately
absent and the relocation is legitimately applied.

Use an explicit call instead. See `WiiXLaunch::Core::Register()` and the
game-module registration in `src/main.cpp`. Where a value must come from
the build, use `WIIXL_OFFSET_SYMBOL` / `WIIXL_DECLARE_LOAD_POINT`, where
`deploy.py` reads the real symbol out of the ELF and fails the build if
missing.

Any "is this pointer null" test on a value that passes through relocation
has the same problem. Zero is not preserved.

## If you touch this

* Anything added to the bootstrap section must keep using raw `@h`/`@l`
  constants plus `r31`, never a plain absolute reference.
* Keep the relocator free of globals, literals, and calls.
* `deploy.py` prints the table size every deploy:
  `[Cemu] Payload relocates itself at load (1801 relocations, 14408 bytes of table)`.
  A sudden drop to zero means the relocation extraction broke.
