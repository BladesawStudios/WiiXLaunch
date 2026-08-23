# How the Cemu payload finds itself

[« Back to overview](overview.md)

A Cemu build is a flat binary dropped into a graphic-pack code cave. Cemu
assigns code caves **sequentially in pack load order**, so the address a payload
runs at depends on which graphic packs the user has enabled and on the Cemu
version. Two people running the same pack file get different addresses.

The payload is therefore linked at base 0 and **relocates itself on entry**.
Nothing in the build has to know where it will land.

## The parts

**`scripts/deploy.py`** reads the relocations the linker kept (`-Wl,-q`) and
emits them as a table immediately after the payload, then patches
`g_CemuRelocTableOffset` and `g_CemuRelocCount` so the bootstrap can find it.
Each entry is two words:

| Word | Contents |
|---|---|
| 0 | `kind << 24 \| offset` - offset is into the payload, so it is capped at 16MB (checked at deploy time) |
| 1 | the relocation's link-time target |

`kind` is `0` for `R_PPC_ADDR32`, `1` for `ADDR16_HA`, `2` for `ADDR16_HI`,
`3` for `ADDR16_LO`. `ADDR32` sites already hold their own base-0 target, so
that value is read back out of the payload; the 16-bit halves cannot be
recovered from the instruction (each is half an address, and `HA` folds in a
sign-extension carry) so those entries carry the relocation's resolved
`S+Addend` instead.

**The bootstrap** (`WiiXLaunch_Cemu_Init`, in `src/main.cpp`) works out where it
is before touching anything:

```asm
bl __wiixl_here          # LR = this label's *runtime* address
__wiixl_here:
mflr 31
lis  30, __wiixl_here@h  # the same label's *link-time* address
ori  30, 30, __wiixl_here@l
subf 31, 30, 31          # r31 = load address
```

That works because `cemu.ld` brackets this section with
`__wiixl_bootstrap_start`/`__wiixl_bootstrap_end` and deploy.py excludes that
range from the table - the bootstrap is the one place in the payload still
holding base-0 constants at run time. It then calls the relocator, stores the
base in `g_CodeCaveBase`, and calls `WiiXLaunch_Init`.

**`WiiXLaunch_Cemu_Relocate`** applies the table and then flushes the payload
out of the data cache and invalidates it in the instruction cache - half the
writes edited instruction immediates, and the code is about to be executed.

Three properties make it safe to run before relocation: it touches no globals,
has no string literals, and calls nothing. Every address it uses arrives in a
parameter or is derived from `base`. The build checks this holds - if the
relocator ever needed relocating itself it would be reached through a table it
has not applied yet.

Applying `base + target` from the table, rather than adding a delta to whatever
is stored, also makes it idempotent: if the entry hook ever fires twice, the
second pass writes the same values.

## What this replaced, and why it matters

`deploy.py` used to relocate the payload at build time against a hardcoded
`codecave_base = 0x01804600`. That is a guess, and when it is wrong it is wrong
by a fixed delta on **every absolute address in the payload** - hooks jump that
far past their callbacks into unrelated code, globals read the wrong memory, and
`WIIXL_LOG` resolves a bogus shim table.

That last one is what makes it so unpleasant to diagnose: there is no log
output, because the logger is broken by the same bug. What the user sees is a
crash a second or two after the title screen and an empty log. And the crash
addresses in the report are all `real + delta`, so every symbol lookup against
them lands in a plausible-looking but entirely innocent function.

A real case: one machine ran a pack at `0x01804600` and worked; another placed
it at `0x01800000`, and the `0x4600` delta sent the KPAD hook `0x4600` past
`KPADReadExWrapperHook::Callback`. Three rounds of debugging went into the actor
traversal that the faulting addresses appeared to point at. The giveaway, missed
at the time, was `r12` holding the *same* value across two builds whose layouts
had shifted - a symbol address cannot do that, a relocation delta can.

## If you touch this

* Anything added to the bootstrap section must keep using raw `@h`/`@l`
  constants plus `r31`, never a plain absolute reference.
* Keep the relocator free of globals, literals and calls.
* `deploy.py` prints the table size every deploy:
  `[Cemu] Payload relocates itself at load (1801 relocations, 14408 bytes of table)`.
  A sudden drop to zero means the relocation extraction broke, and the payload
  will be silently unrelocated.
