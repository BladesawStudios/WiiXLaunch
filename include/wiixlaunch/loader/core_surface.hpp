#pragma once

// wiixl.core v1 - the base framework's own export surface. What a mod can
// rely on with no game module installed: logging, memory, files, hooks, and
// the host's own version.
//
// ABI rules (see surface.hpp): no structs by value, no varargs, opaque
// handles and primitives only. Entries are append-only within v1; a new
// entry bumps the minor, a changed or removed one bumps the major.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/fs.hpp>
#include <wiixlaunch/loader/arena.hpp>
#include <wiixlaunch/hook_manager.hpp>
#if WIIXL_SWITCH
#include <wiixlaunch/switch/switch_backend.hpp>
#elif WIIXL_WIIU
#include <wiixlaunch/wiiu/wiiu_backend.hpp>
#endif
#include <wiixlaunch/hook_probe.hpp>
#include <wiixlaunch/mod_fs.hpp>
#include <wiixlaunch/tick.hpp>

#include <cstdint>
#include <cstddef>

#if WIIXL_CEMU
#include <wiixl_cemu_backend.hpp>
#endif

namespace WiiXLaunch::Core {

constexpr const char* kSurfaceName = "wiixl.core";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 5;

// Bumped when a mod built against an older host would misbehave rather than
// just miss a symbol. Reported at the load point.
constexpr uint32_t kAbiVersion = 1;

namespace impl {

// Not varargs: a mod formats its own text and hands over a finished string,
// so a vararg mismatch across the host/mod boundary can't happen.
extern "C" inline void CoreLog(const char* text) {
    if (!text) return;
    WIIXL_LOG("%s", text);
}

extern "C" inline uint32_t CoreAbiVersion() {
    return kAbiVersion;
}

// Through the arena, so the allocation is charged to and bounded by the
// calling module's own grant.
extern "C" inline void* CoreAlloc(uint32_t size, uint32_t align) {
    return Arena::Alloc(size, align ? align : 64);
}

// Granted/used/remaining for the calling module's arena. Callable during
// load, before allocating, so a best-effort module can size a buffer instead
// of discovering its limit by allocating until null.
extern "C" inline uint32_t CoreHeapGranted() {
    return Arena::GrantedTo(Arena::Current());
}

extern "C" inline uint32_t CoreHeapUsed() {
    return Arena::UsedIn(Arena::Current());
}

extern "C" inline uint32_t CoreHeapRemaining() {
    return Arena::RemainingIn(Arena::Current());
}

// A real payload function with known-relocatable first instructions
// (src/cemu/bootstrap.cpp), used to demonstrate two mods hooking one address.
extern "C" void WiiXLaunch_HookProbe();

extern "C" inline uintptr_t CoreHookProbeTarget() {
#if WIIXL_CEMU
    return reinterpret_cast<uintptr_t>(&WiiXLaunch_HookProbe);
#else
    return 0;
#endif
}

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
extern "C" inline uintptr_t CoreInstallHook(uintptr_t target, uintptr_t callback) {
    const char* owner = WiiXLaunch::Hooks::CurrentOwner();
    if (!owner) owner = "unattributed";

#if WIIXL_CEMU
    uintptr_t original = 0;
    const WiiXLaunch::Hooks::Install r =
        WiiXLaunch::Hooks::InstallHook(target, callback, owner, &original);
    if (r != WiiXLaunch::Hooks::Install::Ok) return 0;
    return original;
#elif WIIXL_SWITCH
    // Noted before installing: recording after a failed install would claim a
    // hook that isn't there.
    WiiXLaunch::Hooks::Note(target, callback, owner);
    return WiiXLaunch::Backend::InstallHookAbsolute(target, callback);
#elif WIIXL_WIIU
    WiiXLaunch::Hooks::Note(target, callback, owner);
    void* original = nullptr;
    if (!WiiXLaunch::Backend::AddPPCExecutablePatch(
            reinterpret_cast<void*>(callback), &original, target, nullptr, 0)) {
        return 0;
    }
    return reinterpret_cast<uintptr_t>(original);
#else
    uintptr_t original = 0;
    const WiiXLaunch::Hooks::Install r =
        WiiXLaunch::Hooks::InstallHook(target, callback, owner, &original);
    if (r != WiiXLaunch::Hooks::Install::Ok) return 0;
    return original;
#endif
}

extern "C" inline uint32_t CoreHookProbeClaimTag(uint32_t tag) {
    return WiiXLaunch::HookProbe::ClaimTag(tag);
}

extern "C" inline void CoreHookProbeMark(uint32_t tag) {
    WiiXLaunch::HookProbe::Mark(tag);
}

// Distinct refusal values, not a shared negative, so a mod can branch without
// a log. Numbered from -10 to leave -1 as the generic read failure.
constexpr int32_t kModReadNoModule     = -10;
constexpr int32_t kModReadEmpty        = -11;
constexpr int32_t kModReadAbsolute     = -12;
constexpr int32_t kModReadParentEscape = -13;
constexpr int32_t kModReadBadChar      = -14;
constexpr int32_t kModReadTooLong      = -15;

inline int32_t ModPathError(WiiXLaunch::ModFS::PathResult r) {
    switch (r) {
        case WiiXLaunch::ModFS::PathResult::NoModule:     return kModReadNoModule;
        case WiiXLaunch::ModFS::PathResult::Empty:        return kModReadEmpty;
        case WiiXLaunch::ModFS::PathResult::Absolute:     return kModReadAbsolute;
        case WiiXLaunch::ModFS::PathResult::ParentEscape: return kModReadParentEscape;
        case WiiXLaunch::ModFS::PathResult::BadChar:      return kModReadBadChar;
        case WiiXLaunch::ModFS::PathResult::TooLong:      return kModReadTooLong;
        case WiiXLaunch::ModFS::PathResult::Ok:           return 0;
    }
    return -1;
}

// Scoped to the calling module's own directory; identity comes from the
// host, not a mod-supplied name, so a mod can't read another mod's files.
extern "C" inline int32_t CoreModReadFile(const char* path, void* buffer,
                                          uint32_t maxSize) {
    char full[WiiXLaunch::ModFS::kMaxScopedPath];
    const WiiXLaunch::ModFS::PathResult r = WiiXLaunch::ModFS::Resolve(path, full);
    if (r != WiiXLaunch::ModFS::PathResult::Ok) {
        WIIXL_LOG("ModFS: %s refused '%s' - %s",
                  WiiXLaunch::ModContext::Current()
                      ? WiiXLaunch::ModContext::Current() : "<host>",
                  path ? path : "(null)", WiiXLaunch::ModFS::PathResultName(r));
        return ModPathError(r);
    }

    size_t read = 0;
    if (!WiiXLaunch::FS::ReadFile(full, buffer, maxSize, &read)) return -1;
    return static_cast<int32_t>(read);
}

extern "C" inline uint32_t CoreModFileExists(const char* path) {
#if WIIXL_CEMU || WIIXL_WIIU || WIIXL_SWITCH
    char full[WiiXLaunch::ModFS::kMaxScopedPath];
    if (WiiXLaunch::ModFS::Resolve(path, full) != WiiXLaunch::ModFS::PathResult::Ok) {
        return 0;
    }
    WiiXLaunch::FS::File f;
    if (!f.Open(full)) return 0;
    const uint32_t size = f.Size();
    f.Close();
    return size ? size : 1;
#else
    (void)path;
    return 0;
#endif
}

// Game content, explicitly. Identical to ReadFile, kept as a separate name
// because removing ReadFile would be a major bump.
extern "C" inline int32_t CoreGameReadFile(const char* path, void* buffer,
                                           uint32_t maxSize) {
    size_t read = 0;
    if (!WiiXLaunch::FS::ReadFile(path, buffer, maxSize, &read)) return -1;
    return static_cast<int32_t>(read);
}

// Registers a per-frame callback. Returns 1 on success, 0 if refused (the
// log names which of four reasons). Attributed to whichever module the host
// is running, never to anything passed here.
extern "C" inline uint32_t CoreRegisterTick(void (*fn)()) {
    return WiiXLaunch::Tick::Add(fn) == WiiXLaunch::Tick::Register::Ok ? 1u : 0u;
}

// The v1.0 spelling of GameReadFile, kept resolvable for old mods.
extern "C" inline int32_t CoreReadFile(const char* path, void* buffer, uint32_t maxSize) {
    size_t read = 0;
    if (!WiiXLaunch::FS::ReadFile(path, buffer, maxSize, &read)) return -1;
    return static_cast<int32_t>(read);
}

extern "C" inline uint32_t CoreFileExists(const char* path) {
#if WIIXL_CEMU || WIIXL_WIIU || WIIXL_SWITCH
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

extern "C" inline uintptr_t CoreImageBase() {
#if WIIXL_CEMU
    return WiiXLaunch::Backend::g_CodeCaveBase;
#else
    return 0;
#endif
}

// Append only. The names here are what a mod's import table hashes.
inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("Log",         &CoreLog),
    WIIXL_SURFACE_SYMBOL("AbiVersion",  &CoreAbiVersion),
    WIIXL_SURFACE_SYMBOL("Alloc",       &CoreAlloc),
    WIIXL_SURFACE_SYMBOL("ReadFile",    &CoreReadFile),
    WIIXL_SURFACE_SYMBOL("FileExists",  &CoreFileExists),
    WIIXL_SURFACE_SYMBOL("ImageBase",   &CoreImageBase),
    WIIXL_SURFACE_SYMBOL("HeapGranted",   &CoreHeapGranted),
    WIIXL_SURFACE_SYMBOL("HeapUsed",      &CoreHeapUsed),
    WIIXL_SURFACE_SYMBOL("HeapRemaining", &CoreHeapRemaining),
    WIIXL_SURFACE_SYMBOL("InstallHook",     &CoreInstallHook),
    WIIXL_SURFACE_SYMBOL("HookProbeTarget", &CoreHookProbeTarget),
    WIIXL_SURFACE_SYMBOL("HookProbeClaimTag", &CoreHookProbeClaimTag),
    WIIXL_SURFACE_SYMBOL("HookProbeMark",     &CoreHookProbeMark),
    WIIXL_SURFACE_SYMBOL("ModReadFile",   &CoreModReadFile),
    WIIXL_SURFACE_SYMBOL("ModFileExists", &CoreModFileExists),
    WIIXL_SURFACE_SYMBOL("GameReadFile",  &CoreGameReadFile),
    WIIXL_SURFACE_SYMBOL("RegisterTick",  &CoreRegisterTick),
};

} // namespace impl

// The host calls this first, before any game module or mod.
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
