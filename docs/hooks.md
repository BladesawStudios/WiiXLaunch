# Hooks

[« Back to overview](overview.md)

## Defining a hook

A hook is a struct that intercepts a function at a given address, runs your code, and can call through to the original:

```cpp
#include <wiixlaunch.hpp>

WIIXL_HOOK_DEFINE_TRAMPOLINE(PlayerStaminaHook) {
    static void Callback(float amount, void* player) {
        // Prevent stamina decrease by passing 0.0f instead
        Orig(0.0f, player);
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

`Callback`'s signature must exactly match the target function's (return type, argument types, calling convention). `Orig(...)` calls the original function

(call it, don't call it, or call it with different arguments, depending on what you want the hook to do.)

`Install(switchOffset, wiiuOffset)` takes both offsets and picks the right one for the platform being built. see [Finding offsets](#finding-offsets) below. On Cemu, the Wii U offset is reused obv.

`WIIXL_HOOK_DEFINE_REPLACE` is also available with the same interface, for hooks that don't need `Orig()` at all.

## Several hooks on one function

Any number of hooks may share a target address. They are chained, and the chain
is built by `WiiXLaunch::Hooks` (`include/wiixlaunch/hook_manager.hpp`), which
is the single registry every hook on every platform goes through.

**Call order is first-installed-first.**

```
install A, then B, then C   =>   A -> B -> C -> the game
```

Load order is priority order, which is the only lever a user actually has -
they choose which mods to enable, not how those mods were written.

### Why first-installed-first, and not the reverse

The behaviour matters less than the reason, because the reason is a
correctness property rather than a preference.

Under first-installed-first the chain is laid out like this:

```
target        -> A.callback        written ONCE, on the first install
A.slot        -> B.callback        rewritten when B installs
B.slot        -> C.callback        rewritten when C installs
C.slot        -> prologueTramp     C is the tail
prologueTramp -> saved prologue, then a jump to target+16
```

Appending a hook rewrites the **contents** of the previous tail's trampoline
slot. It never changes that slot's **address**.

That is the whole argument. A mod captures its `Original` pointer at install
time, and a compiled mod may cache it in a global, hand it to another
subsystem, or keep it for the life of the process - the host has no way to
reach into a binary blob and update a pointer it handed out. Under
last-installed-first, `target` would have to be repointed at each new outermost
hook and every earlier `Original` would have to move with it. Every mod already
holding one would be left with a stale pointer into a trampoline that no longer
means what it meant.

**That failure is a use-after-move nobody would ever diagnose.** The pointer is
still readable, the memory is still mapped, the four instructions there are
still a valid jump - to the wrong place. It would present as a mod that works
alone and misbehaves when another mod is enabled, with nothing in any log
connecting the two. First-installed-first makes the situation impossible rather
than rare: the address a mod receives stays valid forever, because only the
jump inside it is ever rewritten.

### The prologue is captured exactly once

When the first hook is installed at an address, the manager saves the four
instructions it is about to displace and builds `prologueTramp` from them. It
never reads those bytes again. Every `Original` after that is **emitted** from
a callback address the manager already knows.

This is what "correct by construction" means here, and it is worth stating what
it replaced. The old mechanism copied the four instructions at the target every
time. When a second hook installed, what it copied was no longer the prologue -
it was the first hook's jump. Chaining worked, but only because a long jump
happens to be exactly four instructions and happens to be position-independent.
Nothing checked either fact, and nothing recorded that it had happened.

`tools/hook_test` asserts the discriminating property directly: **the saved
prologue must not decode as a long jump.** If the manager ever re-read the
target, that assertion fails. It is the one check the old mechanism provably
could not pass.

### Prologues that cannot be relocated

Moving four instructions to a trampoline changes what a PC-relative branch
means - it still executes, it just goes somewhere else. The manager decodes the
displaced instructions and **refuses the hook**, naming the offending
instruction, rather than relocating something it cannot relocate:

```
Hook: modA refused at 0x03A75D48 - PROLOGUE-NOT-RELOCATABLE: instruction 2
(0x48000040) is a PC-relative branch, and moving it to a trampoline would
silently send it somewhere else.
```

The decode is logged on every site, clean or not, because "we refused nothing"
and "the decoder never ran" otherwise read identically:

```
Hook: prologue check at 0x03A75D48: 4 instructions decoded, 0 relative
```

Appends at an address that is already hooked do **not** re-decode - the
prologue was captured before any hook existed - which is why the boot summary
counts sites rather than installs.

### Conflict reporting

Sharing is never refused. The host does not arbitrate between mods it knows
nothing about, and a mod that means to replace a function simply never calls
`Original`, truncating the chain below it. What the host does instead is name
everyone involved, at install and again in the boot summary:

```
Hook: SHARED TARGET 0x03A75D48 is now 2 deep - call order: modA -> modB -> game.
```

That line is the reason the registry is central. It turns "my game crashes with
these two mods enabled" into a one-line diagnosis.

### Who owns a hook

Every install is attributed. Define `WIIXL_HOOK_OWNER` before including
`hook.hpp` to claim a name; the framework's own hooks are `"host"`, a game
module uses its own, and the loader passes a mod id for a `.wxlm`'s hooks. The
name exists for exactly one purpose: so the line above can say who.

### Platform scope, stated honestly

The registry, the ownership record and the conflict report are identical on all
three platforms. **The chaining is not.** On Cemu the manager owns it end to
end. On Switch and Wii U, installation belongs to exlaunch and WUPS, which
build their own trampolines; the manager records the hook so conflict reporting
works there too, but it does not reimplement their mechanism. Chain order on
those platforms is whatever the platform does.

## Patches and hook windows

A hook displaces the first **16 bytes** of its target into a trampoline and
writes a long jump over them. So a raw patch aimed inside that window does not
write into the game at all — the instructions it is aiming at live somewhere
else now, and what it actually corrupts is the branch into the hook chain.

`WiiXLaunch::Patches` checks every patch against every hook site's 16-byte
window and refuses the overlap by name, saying which hook it collided with:

```
Patch: modA REFUSED HOOKED-WINDOW at 0x03A75D4C (4 B) - those bytes are inside
the 16-byte jump the hook manager wrote for host.
```

**This is reachable today, not hypothetically.** The host's own GX2 hook is
installed during `WiiXLaunch_Init`, before any module is loaded, so the very
first patch any mod declares is already able to land in a hooked window.

A hooked window will *also* fail the origin check — the jump is there, not the
prologue — so the order of the two decides which diagnosis a user gets.
`HOOKED-WINDOW` is tested first deliberately: it points at another mod, where
`ORIGIN-MISMATCH` would send someone to check their game version.

The reverse direction needs no check, and the reason is the load order in
[the loader](loader.md#the-load-sequence): declared patches are applied before
any module entry, so a hook installed later captures the patched bytes.

## Finding offsets

Offsets are addresses into the game binary. WiiXLaunch doesn't locate these for you; that's reverse-engineering work done in a disassembler (Ghidra, IDA) against the specific game version you're targeting. WiiXLaunch uses relative offsets, so taking an address from Ghidra using the SwitchLoader plugin, ensure you subtract `0x7100000000`. Using the RPX plugin for Ghidra already yields the proper offsets when reverse engineering Wii U binaries.

Reminder:

* `wiixlaunch.json`'s `switch.title_id_range_min`/`max` and `wiiu.target_title_ids` scope which game versions your hooks are expected to apply to.

## Raw memory patches

For patches that aren't a full function hook (a single instruction, a constant, a NOP): use `WiiXLaunch::CodePatch` directly:

```cpp
// Overwrite raw bytes
WiiXLaunch::CodePatch::Write(offset, data, size);

// Overwrite a typed value
WiiXLaunch::CodePatch::WriteValue<uint32_t>(offset, 0x60000000);

// NOP out an instruction
WiiXLaunch::CodePatch::Nop(offset);
```

This writes directly to the target's memory/code cave rather than installing a trampoline (use it when you don't need to run any C++ logic at that address, just change what's there.)

## Platform differences you don't have to think about

`WIIXL_HOOK_DEFINE_TRAMPOLINE` and `CodePatch` cover the same three backends as everything else in the framework:

* **Switch**: exlaunch's inline hooking engine (`vendor/exlaunch`), applied against the loaded NSO.
* **Wii U**: libfunctionpatcher, matching by title ID and RPX text offset (`AddPPCExecutablePatch` in [`wiiu_backend.hpp`](../include/wiixlaunch/wiiu/wiiu_backend.hpp)).
* **Cemu**: a hand-rolled PowerPC trampoline pool + long-jump patcher (`InstallHook` in [`wiixl_cemu_backend.hpp`](../include/wiixl_cemu_backend.hpp)), since there's no plugin API to call into on a bare code cave.

You write the hook once; `Install()` dispatches to whichever of these applies at compile time via `#if WIIXL_SWITCH` / `WIIXL_WIIU` / `WIIXL_CEMU`.

## A note on the "same" function not always being the same

Even when Switch and Wii U/Cemu are running "the same game," the two builds aren't the same machine code (different compiler, different ABI, different struct packing.) A struct that's genuinely the same game object can still end up with its fields at different byte offsets, or a function's arguments shuffled differently, on one platform versus the other. Don't assume a `Callback` written against one platform's layout is safe to reuse verbatim on the other.

When a hook's target differs enough between platforms, split it: either `#if WIIXL_SWITCH` / `#elif WIIXL_WIIU || WIIXL_CEMU` branches inside one `Callback`, or, once the platform-specific part is more than a couple of lines: pull the actual game logic out into a shared base struct, and give each platform its own thin `Callback` that extracts that platform's fields and calls into it. The hook macros expand to `struct name : public <HookBase>`, so you can add your own base with the usual multiple-inheritance syntax:

```cpp
struct MyCameraLogic {
    static void Apply(float* pos, float* at, float* up) {
        // the actual logic, written once, shared by every platform
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

That keeps the part you actually care about (what the hook does) written once, while the part that has to differ (how you reach into each platform's version of the struct to get there), stays isolated and easy to audit per platform.

## List of Hooks and CodePatch types:

### WIIXL_HOOK_DEFINE_TRAMPOLINE(name)

Wraps a function: your `Callback` runs instead of it, and can call `Orig(...)` to run the real thing.

```cpp
WIIXL_HOOK_DEFINE_TRAMPOLINE(MyHook) {
    static RetType Callback(ArgTypes... args) {
        // your code 
        return Orig(args...); // optional: call through to the original
    }
};

MyHook::Install(switchOffset, wiiuOffset);
```

* `Orig(args...)` - static member, calls the original function. Only valid to call after `Install()` has run.
* `Install(switchOffset, wiiuOffset)` - installs the hook. On Wii U this always patches against `wiixlaunch.json`'s `wiiu.target_title_ids`.

### WIIXL_HOOK_DEFINE_REPLACE(name)

Identical mechanics to `WIIXL_HOOK_DEFINE_TRAMPOLINE` (same `Orig()` + `Install(switchOffset, wiiuOffset)` shape). It's a separate macro purely so the call site can say what it means: a hook that fully replaces a function's behavior rather than wrapping around it. Use whichever name documents your intent; there's no behavioral difference.

### WIIXL_HOOK_REPLACE(HookName, RetType, SwitchOffset, WiiUOffset, Args...)

A macro-only alternative: offsets are baked in at the macro invocation instead of chosen later at an `Install()` call, and the original function is exposed as a plain function pointer (`HookName::Original`) instead of an `Orig(...)` call.

```cpp
WIIXL_HOOK_REPLACE(MyHook, void, 0x1234, 0x5678, int a, float b) {
    // call through if you want to
    MyHook::Original(a, b);
}

// Switch / Cemu:
MyHook::Install();

// Wii U - defaults to wiixlaunch.json's target title IDs:
MyHook::Install();
// or override per-hook:
MyHook::Install(myTitleIds, myTitleIdCount);
```

This is the only hook form that lets you target different title IDs than the project defaults on a per-hook basis on Wii U.

### WIIXL_OFFSET(switchOffset, wiiuOffset)

The plain macro all three hook forms use internally to pick the right offset for the platform being compiled: `switchOffset` on Switch, `wiiuOffset` everywhere else (Wii U and Cemu share the same binary offsets obv). Usable standalone if you need a platform-correct address outside of a hook:

```cpp
constexpr auto offset = WIIXL_OFFSET(0x00885bd0, 0x02d908b4);
```

### WiiXLaunch::GetTargetFunction<FnPtr>(switchOffset, wiiuOffset)

For calling a game function directly instead of hooking it (e.g. a getter you want the answer from, not behavior you want to change.) Same `(switchOffset, wiiuOffset)` shape as `Install()`, but resolves to a callable function pointer of the type you ask for:

```cpp
using GetUniqueNameFn = const char* (*)(void* actor);
auto getUniqueName = WiiXLaunch::GetTargetFunction<GetUniqueNameFn>(0x11c9bfc, 0x0);
const char* name = getUniqueName(actor);
```

The signature you give it must match the target function's real signature (return type, argument types, calling convention) the same way a hook `Callback` does. It's a raw function pointer cast, nothing checks this for you.

This exists because the two platforms resolve a Ghidra offset into a real address differently, and getting that wrong is a silent crash, not a compile error:

* **Switch**: NSOs are relocated to a random base every launch, so a Switch offset (a Ghidra address minus `0x7100000000`) needs that base added back at runtime. `GetTargetFunction` does this via `exl::util::modules::GetTargetStart()`, the same call every hook `Install()` already makes internally to find its target.
* **Wii U / Cemu**: the RPX's `.text` loads at a fixed, known address (`kRpxTextBase` in [`wiiu_backend.hpp`](../include/wiixlaunch/wiiu/wiiu_backend.hpp)), so a Wii U offset from Ghidra's RPX loader plugin is already the final, absolute, callable address. Cemu reuses the Wii U offset for the same reason hook `Install()` does.

`WiiXLaunch::ResolveTarget(offset)` (in [`call.hpp`](../include/wiixlaunch/call.hpp)) does the same platform-correct resolution without the function-pointer cast, if you need a raw address instead (to read/write memory at a computed location, say).

### CodePatch::Write(targetOffset, data, size)

Copies raw bytes over the target address. On Wii U this also flushes the data cache and invalidates the instruction cache at that range (`DCFlushRange`/`ICInvalidateRange`), since PowerPC doesn't keep them coherent for you.

```cpp
const uint8_t bytes[] = { 0x60, 0x00, 0x00, 0x00 };
WiiXLaunch::CodePatch::Write(offset, bytes, sizeof(bytes));
```

### CodePatch::WriteValue<T>(targetOffset, value)

`Write`, but for a single typed value instead of a raw byte buffer. Equivalent to `Write(targetOffset, &value, sizeof(T))`.

```cpp
WiiXLaunch::CodePatch::WriteValue<float>(offset, 0.0f);
WiiXLaunch::CodePatch::WriteValue<uint32_t>(offset, 0x60000000);
```

### CodePatch::Nop(targetOffset)

Overwrites the instruction at `targetOffset` with a platform-correct no-op (`0xD503201F` on Switch's AArch64, `0x60000000` on Wii U/Cemu's PowerPC).

```cpp
WiiXLaunch::CodePatch::Nop(offset);
```

### CpuContext

[`context.hpp`](../include/wiixlaunch/context.hpp) defines a `WiiXLaunch::CpuContext` struct for raw register access (`GetArg`/`SetArg` per platform). It's not wired into any hook path yet on Switch, Wii U, or Cemu (nothing constructs one or passes it to a callback.) I haven't found a reason to implement it yet, if I do, I will, or maybe you could make a PR lol.
