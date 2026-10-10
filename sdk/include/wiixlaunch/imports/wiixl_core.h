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

// Not varargs: a mod formats its own text and hands over a finished string,
// so a vararg mismatch across the host/mod boundary can't happen.
extern void wiixl_import__wiixl_core__Log(const char* text);
extern uint32_t wiixl_import__wiixl_core__AbiVersion(void);

// Through the arena, so the allocation is charged to and bounded by the
// calling module's own grant.
extern void* wiixl_import__wiixl_core__Alloc(uint32_t size, uint32_t align);

// The v1.0 spelling of GameReadFile, kept resolvable for old mods.
extern int32_t wiixl_import__wiixl_core__ReadFile(const char* path, void* buffer, uint32_t maxSize);
extern uint32_t wiixl_import__wiixl_core__FileExists(const char* path);
extern uintptr_t wiixl_import__wiixl_core__ImageBase(void);

// Granted/used/remaining for the calling module's arena. Callable during
// load, before allocating, so a best-effort module can size a buffer instead
// of discovering its limit by allocating until null.
extern uint32_t wiixl_import__wiixl_core__HeapGranted(void);
extern uint32_t wiixl_import__wiixl_core__HeapUsed(void);
extern uint32_t wiixl_import__wiixl_core__HeapRemaining(void);

// Installs a hook on behalf of the calling module. No owner parameter: a mod
// could otherwise name itself anything, and the conflict report exists
// precisely so that can't happen. The loader sets the current owner around a
// module's entry.
//
// Returns the address to call to continue the chain, or 0 if refused.
// Ignoring the return value replaces the function; that's legal and
// reported.
//
// Dispatches per platform the same way hook.hpp's InstallVia does for host
// hooks (Cemu's chain manager, exlaunch, WUPS) rather than calling the chain
// manager unconditionally, which corrupted non-Cemu builds.
extern uintptr_t wiixl_import__wiixl_core__InstallHook(uintptr_t target, uintptr_t callback);
extern uintptr_t wiixl_import__wiixl_core__HookProbeTarget(void);
extern uint32_t wiixl_import__wiixl_core__HookProbeClaimTag(uint32_t tag);
extern void wiixl_import__wiixl_core__HookProbeMark(uint32_t tag);

// Scoped to the calling module's own directory; identity comes from the
// host, not a mod-supplied name, so a mod can't read another mod's files.
extern int32_t wiixl_import__wiixl_core__ModReadFile(const char* path, void* buffer, uint32_t maxSize);
extern uint32_t wiixl_import__wiixl_core__ModFileExists(const char* path);

// Game content, explicitly. Identical to ReadFile, kept as a separate name
// because removing ReadFile would be a major bump.
extern int32_t wiixl_import__wiixl_core__GameReadFile(const char* path, void* buffer, uint32_t maxSize);

// Registers a per-frame callback. Returns 1 on success, 0 if refused (the
// log names which of four reasons). Attributed to whichever module the host
// is running, never to anything passed here.
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
// address - the module fails to relocate. See docs/framework/modules.md.
#define WXL_USE_wiixl_core(sym) \
    inline decltype(&wiixl_import__wiixl_core__##sym) volatile sym = \
        &wiixl_import__wiixl_core__##sym
