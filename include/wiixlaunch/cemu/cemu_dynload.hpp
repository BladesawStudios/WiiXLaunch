#pragma once

// Reaching an RPL the GAME does not statically import, from the Cemu code cave.
//
// Every other shim table in src/cemu/ resolves `import.coreinit.<Name>`, and
// that works because coreinit is imported by every title that exists. nsysnet
// is not. BotW v208's import table has 445 entries and not one of them is a
// socket call - checked, not assumed.
//
// AND AN UNRESOLVED IMPORT IS NOT A LOCAL FAILURE. Cemu fails the ENTIRE
// graphic pack when a patch import cannot be resolved. A static
// `import.nsysnet.socket` in base would therefore mean that on any setup where
// nsysnet.rpl is not in the title's process, WiiXLaunch does not load at all -
// not the network surface, the whole framework, every mod, silently, with the
// pack simply not applying. That is the worst failure shape in this project:
// no log, no owner, no partial function.
//
// So nsysnet is resolved at RUNTIME instead, through the dynamic loader, which
// BotW does import (OSDynLoad_Acquire / OSDynLoad_FindExport / OSDynLoad_Release
// are EXTERNAL:12a-12c in its import table). The three shims here are coreinit
// calls like every other table's, so they always resolve, and "nsysnet is not
// there" becomes a value that Net::Available() reports and the log names -
// instead of the pack failing to apply.
//
// This mechanism is not specific to sockets. It is how base reaches ANY library
// the game does not itself import, and the next surface that needs one should
// use it rather than adding a static import and hoping.

#include <wiixlaunch/platform.hpp>

#if WIIXL_CEMU

#include <cstdint>

extern "C" {
    // Patched by scripts/deploy.py with src/cemu/cemu_dynload.asm's table
    // offset. `used` for the reason every other shim offset is: the only
    // writer is deploy.py and the only reader is the .asm table, neither of
    // which the compiler can see, so without it the inline variable is never
    // emitted, deploy.py has nothing to patch, and the table ships unreachable.
    __attribute__((section(".data"), used)) inline uint32_t g_CemuDynLoadShimTableOffset = 0;
}

namespace WiiXLaunch::Backend {

// Keep this enum's order EXACTLY in sync with
// wiixlaunch_cemu_dynload_shim_table in src/cemu/cemu_dynload.asm.
enum class CemuDynLoadImport : uint32_t {
    OSDynLoad_Acquire = 0,
    OSDynLoad_FindExport,
    OSDynLoad_Release,
    Count
};

extern "C" uintptr_t g_CodeCaveBase;

inline bool CemuDynLoadAvailable() {
    return g_CemuDynLoadShimTableOffset != 0 && g_CodeCaveBase != 0;
}

inline uintptr_t* CemuDynLoadShimTable() {
    return reinterpret_cast<uintptr_t*>(g_CodeCaveBase + g_CemuDynLoadShimTableOffset);
}

template <typename FnPtr>
inline FnPtr ResolveCemuDynLoad(CemuDynLoadImport fn) {
    if (!CemuDynLoadAvailable()) return nullptr;
    return reinterpret_cast<FnPtr>(CemuDynLoadShimTable()[static_cast<uint32_t>(fn)]);
}

// coreinit's dynamic loader. Signatures from wut's <coreinit/dynload.h>.
// OSDynLoad_Error is 0 (OS_DYNLOAD_OK) on success; the module handle is opaque.
using OSDynLoadModule = void*;
using FnDynLoadAcquire    = int32_t (*)(const char* name, OSDynLoadModule* outModule);
using FnDynLoadFindExport = int32_t (*)(OSDynLoadModule module, int32_t isData,
                                        const char* name, void** outAddr);
using FnDynLoadRelease    = void    (*)(OSDynLoadModule module);

constexpr int32_t kDynLoadOk       = 0;
constexpr int32_t kDynLoadFunction = 0;   // OS_DYNLOAD_EXPORT_FUNC

// Loads (or finds already-loaded) `rplName` and returns its handle, or null.
//
// Acquire on an RPL the process has not loaded will LOAD it, which is the
// point: nsysnet is a system library that any title may bring up on demand.
// If it cannot be loaded, this returns null and the caller reports Unavailable
// - a value, on a host that is otherwise completely functional.
inline OSDynLoadModule CemuAcquireRpl(const char* rplName) {
    auto acquire = ResolveCemuDynLoad<FnDynLoadAcquire>(CemuDynLoadImport::OSDynLoad_Acquire);
    if (!acquire || !rplName) return nullptr;

    OSDynLoadModule module = nullptr;
    if (acquire(rplName, &module) != kDynLoadOk) return nullptr;
    return module;
}

// One export from an acquired module, or null.
inline void* CemuFindRplExport(OSDynLoadModule module, const char* name) {
    auto find = ResolveCemuDynLoad<FnDynLoadFindExport>(CemuDynLoadImport::OSDynLoad_FindExport);
    if (!find || !module || !name) return nullptr;

    void* addr = nullptr;
    if (find(module, kDynLoadFunction, name, &addr) != kDynLoadOk) return nullptr;
    return addr;
}

} // namespace WiiXLaunch::Backend

#endif // WIIXL_CEMU
