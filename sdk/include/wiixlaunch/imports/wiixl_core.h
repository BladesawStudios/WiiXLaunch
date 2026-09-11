// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// wiixl.core v1.5, 17 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_wiixl_core(Log); }
//     S::Log(...);
//
// so a mod that uses two symbols imports two, not all 17.
//
// The comments are the SURFACE's own, carried across - they say why a
// symbol behaves as it does, which is the half a signature cannot.
#pragma once

#include <cstdint>

extern "C" {

//
// Deliberately NOT varargs. A mod formats its own text and hands over a
// finished string; the host's formatter and the mod's need not agree, and a
// vararg mismatch across this boundary is not diagnosable.
extern void wiixl_import__wiixl_core__Log(const char* text);
extern uint32_t wiixl_import__wiixl_core__AbiVersion(void);

// Bytes from the host heap. Never freed, matching every other allocator in
// this project. Stage 5 replaces the implementation with a per-module
// sub-arena; the signature does not change, which is the point of routing mod
// allocation through a surface now rather than letting mods call the backend.
extern void* wiixl_import__wiixl_core__Alloc(uint32_t size, uint32_t align);

// Reads a whole file from game content. Returns bytes read, or a negative value
// on failure. `outRead` may be null.
//
// The v1.0 spelling of GameReadFile, kept resolvable for mods built against it.
extern int32_t wiixl_import__wiixl_core__ReadFile(const char* path, void* buffer, uint32_t maxSize);

// Does a path exist and open? Cheap existence check that does not need a
// buffer, for a mod deciding whether an optional asset is present.
extern uint32_t wiixl_import__wiixl_core__FileExists(const char* path);

// The code cave base, so a mod can reason about where it is. Opaque to the mod
// beyond being an address.
extern uintptr_t wiixl_import__wiixl_core__ImageBase(void);

//
// How much this module was actually granted, how much it has spent, and how
// much is left. Callable DURING the load phase, before allocating.
//
// A module on the best-effort path (heapRequest == 0) is told what it got
// rather than having to discover it by allocating until null. "Allocate until
// null" is not a design; it is finding out by failing, in a place where failing
// means a half-initialised mod running in someone's game.
//
// A module that stated a heapRequest already knows its size, but not what it
// has spent, so all three are useful to both.
extern uint32_t wiixl_import__wiixl_core__HeapGranted(void);
extern uint32_t wiixl_import__wiixl_core__HeapUsed(void);
extern uint32_t wiixl_import__wiixl_core__HeapRemaining(void);

// Installs a hook on behalf of the calling module.
//
// The owner is NOT a parameter, and that is deliberate: a mod could then name
// itself anything, and the conflict report - the entire reason the registry is
// central - would be worth nothing. The loader sets the current owner around a
// module's entry, so attribution comes from who the host is running, not from
// what the module claims.
//
// Returns the address to call to continue the chain, or 0 if the hook was
// refused. A mod that ignores the return value and never calls it has replaced
// the function, which is legal and reported.
//
// THE SAME PLATFORM DISPATCH THE HOST USES, which this did not do.
//
// hook.hpp's InstallVia has always chosen per platform: the chain manager on
// Cemu, exlaunch on Switch, WUPS on Wii U - because Hooks::InstallHook emits
// PowerPC, and because its trampoline pool and cache flush are #if WIIXL_CEMU
// with the host-TEST fallback underneath. This function called the chain
// manager directly on every platform, so a module got the one path that cannot
// work anywhere but Cemu while the host beside it took the right one.
//
// On Switch that wrote `lis/ori/mtctr/bctr` into aarch64 code and the game died
// on 0x618C64B4. On Wii U it would have built trampolines in a non-executable
// static array and flushed nothing - the same bug, wearing the right ISA.
extern uintptr_t wiixl_import__wiixl_core__InstallHook(uintptr_t target, uintptr_t callback);
extern uintptr_t wiixl_import__wiixl_core__HookProbeTarget(void);
extern uint32_t wiixl_import__wiixl_core__HookProbeClaimTag(uint32_t tag);
extern void wiixl_import__wiixl_core__HookProbeMark(uint32_t tag);

// This module's own directory, and nothing outside it. The identity comes from
// the host - whichever module it is running - so a mod cannot read another
// mod's files by naming them.
extern int32_t wiixl_import__wiixl_core__ModReadFile(const char* path, void* buffer, uint32_t maxSize);
extern uint32_t wiixl_import__wiixl_core__ModFileExists(const char* path);

// Game content, explicitly. Identical to ReadFile, which is retained only
// because removing a symbol would be a major bump - this is the name to use,
// because "GameReadFile" says at the call site what "ReadFile" left implied.
extern int32_t wiixl_import__wiixl_core__GameReadFile(const char* path, void* buffer, uint32_t maxSize);

// Asks to be called once a frame.
//
// Returns 1 on success, 0 if refused - and the log names which of the four
// refusals it was. The callback is attributed to whichever module the host is
// running, never to anything passed here, so a hang inside a tick names the
// module that actually registered it.
//
// A host with no game module has no frame source and these never run; that is
// reported at the load point rather than left to be inferred from a mod that
// quietly does nothing.
extern uint32_t wiixl_import__wiixl_core__RegisterTick(void (*fn)());
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require wiixl.core@1.5 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_wiixl_core {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 5;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/modules.md.
#define WXL_USE_wiixl_core(sym) \
    inline decltype(&wiixl_import__wiixl_core__##sym) volatile sym = \
        &wiixl_import__wiixl_core__##sym
