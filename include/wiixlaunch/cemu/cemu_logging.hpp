#pragma once

// Resolving coreinit's OSReport from the Cemu raw code-cave build, via the
// "import.<lib>.<Name> shim table" mechanism - the payload is a raw codecave
// blob, never a real RPL module, so there is no OS import resolution for
// anything it calls, and every coreinit entry point has to be reached through
// a Cemu-assembled tail call in src/cemu/cemu_logging.asm.
//
// This is base-framework plumbing, not game-specific: OSReport and the
// MEM2-arena allocators are coreinit exports present in every Wii U title and
// need no game address to reach. WiiXLaunch::Debug::DebugPrint (debug_log.hpp)
// relays through here, so it must be available whether or not a game module
// like vendor/wiixlaunch-botw is installed.
//
// Callers must append a trailing '\n' - Cemu's own OSReport implementation
// (WriteCafeConsole, coreinit_Misc.cpp) line-buffers and only flushes to the
// log on '\n', which was the real reason earlier calls here never showed up
// anywhere despite not crashing and being genuinely reached. The PowerPC EABI
// varargs CR-bit-6 fix that the same calls also need lives on the asm side;
// see cemu_logging.asm.

#include <wiixlaunch/platform.hpp>

#if WIIXL_CEMU

#include <cstdint>

extern "C" {
    // Patched by scripts/deploy.py at deploy time - left at 0 in the actual
    // compiled ELF, never meant to be read before that patch has happened.
    __attribute__((section(".data"))) inline uint32_t g_CemuLoggingShimTableOffset = 0;
}

namespace WiiXLaunch::Backend {

// Keep this enum's order EXACTLY in sync with wiixlaunch_cemu_logging_shim_table
// in src/cemu/cemu_logging.asm - each entry here is that table's Nth slot.
enum class CemuLogImport : uint32_t {
    OSReport = 0,
    MEMAllocFromDefaultHeapEx,
    MEMFreeToDefaultHeap,
    Count
};

extern "C" uintptr_t g_CodeCaveBase;

// True once deploy.py has patched the table offset in AND the bootstrap has
// computed the code-cave base. Both are zero in the compiled ELF, so
// resolving before that reads through a null pointer. Check this first -
// ResolveCemuLogging does not.
inline bool CemuLoggingAvailable() {
    return g_CodeCaveBase != 0 && g_CemuLoggingShimTableOffset != 0;
}

inline uintptr_t* CemuLoggingShimTable() {
    return reinterpret_cast<uintptr_t*>(g_CodeCaveBase + g_CemuLoggingShimTableOffset);
}

// Usage: auto osReport = WiiXLaunch::Backend::ResolveCemuLogging<void(*)(const char*, ...)>(CemuLogImport::OSReport);
template <typename FnPtr>
inline FnPtr ResolveCemuLogging(CemuLogImport fn) {
    return reinterpret_cast<FnPtr>(CemuLoggingShimTable()[static_cast<uint32_t>(fn)]);
}

} // namespace WiiXLaunch::Backend

#endif
