#pragma once

// Resolving coreinit's filesystem APIs from the Cemu raw code-cave build, via
// the same import shim table mechanism as cemu_logging.hpp / cemu_mem.hpp -
// the payload is a raw codecave blob, never a real RPL module, so there is no
// OS import resolution for anything it calls, and every coreinit entry point
// has to be reached through a Cemu-assembled tail call in
// src/cemu/cemu_fs.asm.
//
// This is base-framework plumbing, not game-specific: these are coreinit
// exports present in every Wii U title and need no game address to reach. The
// module loader reads .wxlm blobs off the title's filesystem, so base
// WiiXLaunch depends on these directly and they cannot be behind "did someone
// install a game module".

#include <wiixlaunch/platform.hpp>

#if WIIXL_CEMU

#include <cstdint>

extern "C" {
    // Patched by scripts/deploy.py at deploy time - left at 0 in the actual
    // compiled ELF, never meant to be read before that patch has happened.
    // `used` because nothing in C++ may reference this: deploy.py writes it
    // and the paired src/cemu/*.asm table reads through it, neither of which
    // the compiler can see. Without it an inline variable no translation unit
    // odr-uses is never emitted, the symbol is absent from the ELF, deploy.py
    // has nothing to patch, and the shim table ships unreachable.
    __attribute__((section(".data"), used)) inline uint32_t g_CemuFsShimTableOffset = 0;
}

namespace WiiXLaunch::Backend {

// Keep this enum's order EXACTLY in sync with wiixlaunch_cemu_fs_shim_table
// in src/cemu/cemu_fs.asm.
enum class CemuFsImport : uint32_t {
    FSAddClient = 0,
    FSDelClient,
    FSInitCmdBlock,
    FSOpenFile,
    FSGetStatFile,
    FSReadFile,
    FSWriteFile,
    FSCloseFile,
    // Appended for FS::File::ReadAt (positioned reads of large archives
    // without loading them whole). Keep new entries at the END.
    FSReadFileWithPos,
    Count
};

extern "C" uintptr_t g_CodeCaveBase;

// True once deploy.py has patched the table offset in AND the bootstrap has
// computed the code-cave base. Both are zero in the compiled ELF, so
// resolving before that reads through a null pointer. Check this first -
// ResolveCemuFs does not.
inline bool CemuFsAvailable() {
    return g_CodeCaveBase != 0 && g_CemuFsShimTableOffset != 0;
}

inline uintptr_t* CemuFsShimTable() {
    return reinterpret_cast<uintptr_t*>(g_CodeCaveBase + g_CemuFsShimTableOffset);
}

template <typename FnPtr>
inline FnPtr ResolveCemuFs(CemuFsImport fn) {
    return reinterpret_cast<FnPtr>(CemuFsShimTable()[static_cast<uint32_t>(fn)]);
}

} // namespace WiiXLaunch::Backend

#endif
