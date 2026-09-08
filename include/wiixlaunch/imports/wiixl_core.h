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
#pragma once

#include <cstdint>

extern "C" {
extern void wiixl_import__wiixl_core__Log(const char* text);
extern uint32_t wiixl_import__wiixl_core__AbiVersion(void);
extern void* wiixl_import__wiixl_core__Alloc(uint32_t size, uint32_t align);
extern int32_t wiixl_import__wiixl_core__ReadFile(const char* path, void* buffer, uint32_t maxSize);
extern uint32_t wiixl_import__wiixl_core__FileExists(const char* path);
extern uintptr_t wiixl_import__wiixl_core__ImageBase(void);
extern uint32_t wiixl_import__wiixl_core__HeapGranted(void);
extern uint32_t wiixl_import__wiixl_core__HeapUsed(void);
extern uint32_t wiixl_import__wiixl_core__HeapRemaining(void);
extern uintptr_t wiixl_import__wiixl_core__InstallHook(uintptr_t target, uintptr_t callback);
extern uintptr_t wiixl_import__wiixl_core__HookProbeTarget(void);
extern uint32_t wiixl_import__wiixl_core__HookProbeClaimTag(uint32_t tag);
extern void wiixl_import__wiixl_core__HookProbeMark(uint32_t tag);
extern int32_t wiixl_import__wiixl_core__ModReadFile(const char* path, void* buffer, uint32_t maxSize);
extern uint32_t wiixl_import__wiixl_core__ModFileExists(const char* path);
extern int32_t wiixl_import__wiixl_core__GameReadFile(const char* path, void* buffer, uint32_t maxSize);
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
