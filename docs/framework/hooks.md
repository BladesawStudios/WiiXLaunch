# Framework: Hooks

[« Back to overview](../overview.md)

This page describes host-built hooks: code in `src/main.cpp` or a game
module, compiled into the payload, using compile-time offset pairs. For a
`.wxlm` mod, see
[SDK: Hooks, patches and runtime](../sdk/hooks-patches-and-runtime.md), which
reaches the same registry through a runtime import instead.

## Defining a hook

```cpp
#include <wiixlaunch.hpp>

WIIXL_HOOK_DEFINE_TRAMPOLINE(PlayerStaminaHook) {
    static void Callback(float amount, void* player) {
        Orig(0.0f, player);   // never let stamina decrease
    }
};

extern "C" void WiiXLaunch_Init() {
#if WIIXL_WIIU
    if (!WiiXLaunch::Backend::InitWiiUBackend()) return;
#elif WIIXL_CEMU
    if (!WiiXLaunch::Backend::InitCemuBackend()) return;
#endif

    PlayerStaminaHook::Install(0x00885bd0, 0x02d908b4);
}
```

`Callback`'s signature must match the target function's exactly. `Orig(...)`
calls the original function.

`Install(switchOffset, wiiuOffset)` picks the right offset for the platform
via `WIIXL_OFFSET`. Cemu runs the Wii U binary and reuses the Wii U offset.

`WIIXL_HOOK_DEFINE_REPLACE` has the same interface, for hooks that never call
`Orig()`.

## Several hooks on one function

Any number of hooks may share a target address, chained by
`WiiXLaunch::Hooks` (`include/wiixlaunch/hook_manager.hpp`), the single
registry every hook goes through regardless of platform or call path.

Call order is first-installed-first:

```
install A, then B, then C   =>   A -> B -> C -> the game
```

Load order is the user's only lever over priority.

### Why first-installed-first

```
target        -> A.callback        written ONCE, on the first install
A.slot        -> B.callback        rewritten when B installs
B.slot        -> C.callback        rewritten when C installs
C.slot        -> prologueTramp     C is the tail
prologueTramp -> saved prologue, then a jump to target+16
```

Appending a hook rewrites the contents of the previous tail's trampoline
slot. It never changes that slot's address.

A hook captures its `Original` pointer at install time and may cache it
indefinitely. Under last-installed-first, `target` would have to be
repointed at each new outermost hook, and every earlier `Original` would
have to move with it. Anything already holding one would end up with a
stale pointer into a trampoline that no longer means what it meant: still
readable, still mapped, still a valid jump, just to the wrong place. It
presents as a mod that works alone and misbehaves with another mod enabled.
First-installed-first avoids this: the address a mod receives stays valid
forever, because only the jump inside it is ever rewritten.

### The prologue is captured once

The manager saves the four displaced instructions on the first install at an
address and never reads them again. Every later `Original` is emitted from a
callback address the manager already knows.

`tools/hook_test` asserts the saved prologue never decodes as a long jump. If
the manager re-read the target, that assertion fails.

### Prologues that cannot be relocated

Moving four instructions to a trampoline breaks a PC-relative branch among
them; it still executes, just somewhere else. The manager decodes the
displaced instructions and refuses the hook rather than relocate something
it cannot:

```
Hook: modA refused at 0x03A75D48 - PROLOGUE-NOT-RELOCATABLE: instruction 2
(0x48000040) is a PC-relative branch, and moving it to a trampoline would
silently send it somewhere else.
```

The decode is logged on every site, clean or not:

```
Hook: prologue check at 0x03A75D48: 4 instructions decoded, 0 relative
```

Appends at an already-hooked address do not re-decode.

### Conflict reporting

Sharing is never refused. A mod that means to replace a function simply
never calls `Original`, truncating the chain below it. Every hook is named,
at install and again in the boot summary:

```
Hook: SHARED TARGET 0x03A75D48 is now 2 deep - call order: modA -> modB -> game.
```

### Who owns a hook

Host code defines `WIIXL_HOOK_OWNER` before including `hook.hpp` to claim a
name; the framework's own hooks are `"host"`. A `.wxlm`'s hooks are
attributed by the loader from the current module identity.

### Platform scope

The registry, ownership record, and conflict report are identical on all
three platforms. Chaining is not: Cemu's manager owns it end to end; on
Switch and Wii U, exlaunch and WUPS build their own trampolines and the
manager records the hook for conflict reporting only.

## Patches and hook windows

A hook displaces the first 16 bytes of its target into a trampoline and
writes a long jump over them. A raw patch aimed inside that window writes
into the trampoline's jump, not the game.

`WiiXLaunch::Patches` checks every patch against every hook site's 16-byte
window:

```
Patch: modA REFUSED HOOKED-WINDOW at 0x03A75D4C (4 B) - those bytes are inside
the 16-byte jump the hook manager wrote for host.
```

`HOOKED-WINDOW` is checked before `ORIGIN-MISMATCH` (a hooked window also
fails the origin check, since the jump is there, not the prologue).
`HOOKED-WINDOW` points at another mod; `ORIGIN-MISMATCH` points at a game
version.

The reverse case needs no check: declared patches apply before any module
entry (see [the loader](loader.md#the-load-sequence)), so a hook installed
later captures the patched bytes.

## Finding offsets

Reverse-engineering work, done in Ghidra or IDA against the specific game
version you're targeting. Using the SwitchLoader plugin, subtract
`0x7100000000` from the address. The RPX plugin already yields correct Wii U
offsets.

The active target's `switch.title_id_range_min`/`max` and
`wiiu.target_title_ids` (`targets/<game>.json`) scope which game versions
your hooks apply to.

## Raw memory patches

For a single instruction, constant, or NOP, use `WiiXLaunch::CodePatch`
directly:

```cpp
WiiXLaunch::CodePatch::Write(offset, data, size);
WiiXLaunch::CodePatch::WriteValue<uint32_t>(offset, 0x60000000);
WiiXLaunch::CodePatch::Nop(offset);
```

Writes directly to memory/code cave, no trampoline installed.

### CodePatch is unchecked

`CodePatch` writes what you tell it, records nothing. On Switch it goes
through exlaunch's `StreamPatcher`, writing via a writable alias of the
game's pages. On Wii U it flushes data cache and invalidates instruction
cache for the range. Fine for host and game-module code, rebuilt with the
host. Not fine for anything shipped separately: a `.wxlm` uses a checked
patch instead, see
[SDK: Hooks, patches and runtime](../sdk/hooks-patches-and-runtime.md#declaring-a-patch).

### The checked path

Host code uses `WiiXLaunch::Patches::ApplyAt` directly, passing both the
expected bytes and the new bytes:

```cpp
// mov w1,#15  ->  mov w1,#45      (0x52800001 | imm << 5, Rd = w1)
const uint32_t expected = 0x528001E1;
const uint32_t wanted   = 0x528005A1;
Patches::ApplyAt(addr,
                 reinterpret_cast<const uint8_t*>(&wanted),
                 reinterpret_cast<const uint8_t*>(&expected),
                 sizeof(wanted), "mymod");
```

This adds four checks `CodePatch` skips:

* A different game build is refused by name (`ORIGIN-MISMATCH`).
* Two mods aiming at the same address collide visibly (`PATCH-OVERLAP`).
* A patch landing inside a hook's 16-byte window is refused
  (`HOOKED-WINDOW`).
* The write is verified after the fact, read back through the executable
  address; a mismatch is `WRITE-FAILED`.

`WRITE-FAILED` exists because a plain store to Switch's read-execute game
memory either faults or, under a recompiler that doesn't enforce the
permission, lands in memory while the original instruction's translation
keeps running. The bytes read back correctly and the game behaves as if
nothing happened; this is the only check that verifies the write actually
occurred.

For a patch applied before any module runs and restored or kept per
`patches.persist`, declare it instead: see
[Declared patches](loader.md#declared-patches).

## Platform backends

* Switch: exlaunch's inline hooking engine (`vendor/exlaunch`), against the
  loaded NSO.
* Wii U: libfunctionpatcher, matching by title ID and RPX text offset
  (`AddPPCExecutablePatch` in
  [`wiiu_backend.hpp`](../../include/wiixlaunch/wiiu/wiiu_backend.hpp)).
* Cemu: a hand-rolled PowerPC trampoline pool and long-jump patcher
  (`InstallHook` in
  [`wiixl_cemu_backend.hpp`](../../include/wiixl_cemu_backend.hpp)).

`Install()` dispatches via `#if WIIXL_SWITCH` / `WIIXL_WIIU` / `WIIXL_CEMU`.

## The "same" function is not always the same

Switch and Wii U/Cemu builds of "the same game" are different compiler
output: different ABI, different struct packing. Field offsets and argument
order can differ even for the same game object. Don't reuse a `Callback`
written for one platform's layout on the other.

When a hook target differs enough between platforms, split it: either
`#if WIIXL_SWITCH` / `#elif WIIXL_WIIU || WIIXL_CEMU` inside one `Callback`,
or pull the shared logic into a base struct and give each platform its own
thin `Callback`. The hook macros expand to `struct name : public
<HookBase>`, so add your own base with multiple inheritance:

```cpp
struct MyCameraLogic {
    static void Apply(float* pos, float* at, float* up) {
        // shared logic
    }
};

#if WIIXL_SWITCH
WIIXL_HOOK_DEFINE_TRAMPOLINE(MyCameraHook), public MyCameraLogic {
    static void Callback(void* camera, float* matrix) {
        auto* p = static_cast<uint8_t*>(camera);
        Apply(reinterpret_cast<float*>(p + 0x38), reinterpret_cast<float*>(p + 0x44), reinterpret_cast<float*>(p + 0x50));
        Orig(camera, matrix);
    }
};
#else
WIIXL_HOOK_DEFINE_TRAMPOLINE(MyCameraHook), public MyCameraLogic {
    static void Callback(void* camera, float* matrix) {
        auto* p = static_cast<uint8_t*>(camera);
        Apply(reinterpret_cast<float*>(p + 0x34), reinterpret_cast<float*>(p + 0x40), reinterpret_cast<float*>(p + 0x4C));
        Orig(camera, matrix);
    }
};
#endif
```

## Reference

### WIIXL_HOOK_DEFINE_TRAMPOLINE(name)

```cpp
WIIXL_HOOK_DEFINE_TRAMPOLINE(MyHook) {
    static RetType Callback(ArgTypes... args) {
        return Orig(args...); // optional
    }
};

MyHook::Install(switchOffset, wiiuOffset);
```

`Orig(args...)` calls the original function; valid only after `Install()`.
`Install(switchOffset, wiiuOffset)` installs the hook. On Wii U this always
patches against the target's `wiiu.target_title_ids`.

### WIIXL_HOOK_DEFINE_REPLACE(name)

Same mechanics as `WIIXL_HOOK_DEFINE_TRAMPOLINE`. Use it to name intent: a
hook that replaces behavior rather than wrapping it.

### WIIXL_HOOK_REPLACE(HookName, RetType, SwitchOffset, WiiUOffset, Args...)

Offsets baked in at the macro invocation; the original is exposed as
`HookName::Original`, a plain function pointer.

```cpp
WIIXL_HOOK_REPLACE(MyHook, void, 0x1234, 0x5678, int a, float b) {
    MyHook::Original(a, b);
}

// Switch / Cemu:
MyHook::Install();

// Wii U, defaults to the target's wiiu.target_title_ids:
MyHook::Install();
// or override per-hook:
MyHook::Install(myTitleIds, myTitleIdCount);
```

Only this form lets you target different title IDs per hook on Wii U.

### WIIXL_OFFSET(switchOffset, wiiuOffset)

```cpp
constexpr auto offset = WIIXL_OFFSET(0x00885bd0, 0x02d908b4);
```

`switchOffset` on Switch, `wiiuOffset` everywhere else.

### WiiXLaunch::GetTargetFunction<FnPtr>(switchOffset, wiiuOffset)

Calls a game function directly instead of hooking it.

```cpp
using GetUniqueNameFn = const char* (*)(void* actor);
auto getUniqueName = WiiXLaunch::GetTargetFunction<GetUniqueNameFn>(0x11c9bfc, 0x0);
const char* name = getUniqueName(actor);
```

Signature must match the real function; it's a raw pointer cast, unchecked.

Switch: NSOs relocate on every launch, so the base address gets added back at
runtime via `exl::util::modules::GetTargetStart()`. Wii U/Cemu: the RPX's
`.text` loads at a fixed address (`kRpxTextBase` in
[`wiiu_backend.hpp`](../../include/wiixlaunch/wiiu/wiiu_backend.hpp)), so the
Ghidra offset is already the final address.

`WiiXLaunch::ResolveTarget(offset)`
([`call.hpp`](../../include/wiixlaunch/call.hpp)) does the same resolution
without the function-pointer cast. This is the same call
`wiixl.call:ResolveTarget` publishes to mods.

### CodePatch::Write(targetOffset, data, size)

Copies raw bytes over the target address. On Wii U also flushes/invalidates
caches for the range.

```cpp
const uint8_t bytes[] = { 0x60, 0x00, 0x00, 0x00 };
WiiXLaunch::CodePatch::Write(offset, bytes, sizeof(bytes));
```

### CodePatch::WriteValue<T>(targetOffset, value)

Equivalent to `Write(targetOffset, &value, sizeof(T))`.

```cpp
WiiXLaunch::CodePatch::WriteValue<float>(offset, 0.0f);
WiiXLaunch::CodePatch::WriteValue<uint32_t>(offset, 0x60000000);
```

### CodePatch::Nop(targetOffset)

Writes a platform-correct no-op (`0xD503201F` AArch64, `0x60000000` PowerPC).

```cpp
WiiXLaunch::CodePatch::Nop(offset);
```

### CpuContext

[`context.hpp`](../../include/wiixlaunch/context.hpp) defines
`WiiXLaunch::CpuContext` for raw register access. Not wired into any hook
path on any platform; reserved for a register-level hook form not needed so
far.
