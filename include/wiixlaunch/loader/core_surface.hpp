#pragma once

// wiixl.core v1 - the base framework's own export surface.
//
// This is what a mod can rely on with no game module installed at all: logging,
// memory, files, hooks, and the host's own version. Everything game-specific
// lives in a surface some module registered.
//
// Every entry obeys the ABI rules in surface.hpp: no structs by value, no
// varargs, opaque handles and primitives only. These signatures are frozen for
// the life of v1 - new entries are appended and bump the minor, changes bump
// the major.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/fs.hpp>

#include <cstdint>
#include <cstddef>

#if WIIXL_CEMU
#include <wiixl_cemu_backend.hpp>
#endif

namespace WiiXLaunch::Core {

constexpr const char* kSurfaceName = "wiixl.core";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

// The ABI version of the .wxlm format and this whole boundary. Bumped when a
// mod built against an older host would misbehave rather than merely miss a
// symbol. Reported in the log at the load point so a rejection is diagnosable.
constexpr uint32_t kAbiVersion = 1;

namespace impl {

// --- wiixl.core v1 entry points -------------------------------------------
//
// Deliberately NOT varargs. A mod formats its own text and hands over a
// finished string; the host's formatter and the mod's need not agree, and a
// vararg mismatch across this boundary is not diagnosable.
extern "C" inline void CoreLog(const char* text) {
    if (!text) return;
    WIIXL_LOG("%s", text);
}

extern "C" inline uint32_t CoreAbiVersion() {
    return kAbiVersion;
}

// Bytes from the host heap. Never freed, matching every other allocator in
// this project. Stage 5 replaces the implementation with a per-module
// sub-arena; the signature does not change, which is the point of routing mod
// allocation through a surface now rather than letting mods call the backend.
extern "C" inline void* CoreAlloc(uint32_t size, uint32_t align) {
#if WIIXL_CEMU
    return WiiXLaunch::Backend::AllocCemuHeap(size, align ? align : 256);
#else
    (void)size; (void)align;
    return nullptr;
#endif
}

// Reads a whole file. Returns bytes read, or a negative value on failure.
// `outRead` may be null.
extern "C" inline int32_t CoreReadFile(const char* path, void* buffer, uint32_t maxSize) {
    size_t read = 0;
    if (!WiiXLaunch::FS::ReadFile(path, buffer, maxSize, &read)) return -1;
    return static_cast<int32_t>(read);
}

// Does a path exist and open? Cheap existence check that does not need a
// buffer, for a mod deciding whether an optional asset is present.
extern "C" inline uint32_t CoreFileExists(const char* path) {
#if WIIXL_CEMU || WIIXL_WIIU
    WiiXLaunch::FS::File f;
    if (!f.Open(path)) return 0;
    const uint32_t size = f.Size();
    f.Close();
    return size ? size : 1;
#else
    (void)path;
    return 0;
#endif
}

// The code cave base, so a mod can reason about where it is. Opaque to the mod
// beyond being an address.
extern "C" inline uintptr_t CoreImageBase() {
#if WIIXL_CEMU
    return WiiXLaunch::Backend::g_CodeCaveBase;
#else
    return 0;
#endif
}

// --- the table -------------------------------------------------------------
//
// APPEND ONLY. Adding an entry bumps kVersionMinor; changing or removing one
// bumps kVersionMajor. The names here are what a mod's import table hashes.
inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("Log",         &CoreLog),
    WIIXL_SURFACE_SYMBOL("AbiVersion",  &CoreAbiVersion),
    WIIXL_SURFACE_SYMBOL("Alloc",       &CoreAlloc),
    WIIXL_SURFACE_SYMBOL("ReadFile",    &CoreReadFile),
    WIIXL_SURFACE_SYMBOL("FileExists",  &CoreFileExists),
    WIIXL_SURFACE_SYMBOL("ImageBase",   &CoreImageBase),
};

} // namespace impl

// Registers wiixl.core. The host calls this first, before any game module's
// registration and long before any mod is loaded.
//
// Idempotent: Surface::Register refuses a duplicate name and says so, so
// calling this twice logs once and changes nothing.
inline bool Register() {
    Surface::Registration reg{};
    reg.name = kSurfaceName;
    reg.versionMajor = kVersionMajor;
    reg.versionMinor = kVersionMinor;
    reg.symbols = impl::kSymbols;
    reg.symbolCount = static_cast<uint32_t>(sizeof(impl::kSymbols) / sizeof(impl::kSymbols[0]));
    return Surface::Register(reg);
}

} // namespace WiiXLaunch::Core
