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
#include <wiixlaunch/loader/arena.hpp>
#include <wiixlaunch/hook_manager.hpp>
// For the per-platform installers CoreInstallHook dispatches to. hook.hpp
// pulls the same two in for the host's own hooks; a module's hooks have to
// go through the same doors.
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
// 1.1 appends HeapGranted / HeapUsed / HeapRemaining. Appending bumps the
// MINOR, so every mod built against v1.0 still resolves - which is the whole
// reason the version is two numbers. This is the rule's first real use.
// 1.5 appends RegisterTick. Appending bumps the MINOR, so every mod built
// against v1.0 through v1.4 still resolves.
constexpr uint16_t kVersionMinor = 5;

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
    // Through the arena, so the allocation is charged to - and bounded by - the
    // calling module's own grant. It used to go straight to the code-cave bump
    // allocator, where whoever asked first got whatever was left and an
    // over-allocating mod starved the ones loaded after it.
    return Arena::Alloc(size, align ? align : 64);
}

// --- appended in v1.1 ------------------------------------------------------
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
extern "C" inline uint32_t CoreHeapGranted() {
    return Arena::GrantedTo(Arena::Current());
}

extern "C" inline uint32_t CoreHeapUsed() {
    return Arena::UsedIn(Arena::Current());
}

extern "C" inline uint32_t CoreHeapRemaining() {
    return Arena::RemainingIn(Arena::Current());
}

// --- appended in v1.2 ------------------------------------------------------

// The host's own hook demonstration target - a real function in the payload
// whose first four instructions are known relocatable, written in assembly in
// src/cemu/bootstrap.cpp precisely so that is guaranteed rather than hoped for.
//
// It exists so two mods can collide on ONE address deliberately, and the boot
// can show chain order and the conflict line together, without a wrong chain
// being able to take the game down. Hooking real game functions is what the
// game module's own hooks already do; what needed proving here is the chain.
extern "C" void WiiXLaunch_HookProbe();

extern "C" inline uintptr_t CoreHookProbeTarget() {
#if WIIXL_CEMU
    return reinterpret_cast<uintptr_t>(&WiiXLaunch_HookProbe);
#else
    return 0;
#endif
}

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
    // The platform installs; the manager RECORDS, so the shared-target report
    // is the same on all three even though only one of them chains. Noted
    // first: a Note after a failed install would claim a hook that is not
    // there, and a Note before a successful one costs nothing.
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
    // The host test. Chaining here is the point - tools/hook_test decodes what
    // the manager emitted, and that is the only coverage the encoding has.
    uintptr_t original = 0;
    const WiiXLaunch::Hooks::Install r =
        WiiXLaunch::Hooks::InstallHook(target, callback, owner, &original);
    if (r != WiiXLaunch::Hooks::Install::Ok) return 0;
    return original;
#endif
}

// --- appended in v1.3 ------------------------------------------------------
//
// The marker sequence the host verifies after the chain runs. See
// wiixlaunch/hook_probe.hpp for why a tag is bound at CLAIM time rather than
// trusted at MARK time.

extern "C" inline uint32_t CoreHookProbeClaimTag(uint32_t tag) {
    return WiiXLaunch::HookProbe::ClaimTag(tag);
}

extern "C" inline void CoreHookProbeMark(uint32_t tag) {
    WiiXLaunch::HookProbe::Mark(tag);
}

// --- appended in v1.4 ------------------------------------------------------
//
// TWO READS, and which one a mod meant is legible at the call site rather than
// decided by a resolution order. See wiixlaunch/mod_fs.hpp for why there is no
// single call that tries the mod directory and falls back to game content.

// Negative results, distinct so a mod can tell them apart without a log.
// Numbered from -10 to leave -1 as the existing generic read failure.
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

// This module's own directory, and nothing outside it. The identity comes from
// the host - whichever module it is running - so a mod cannot read another
// mod's files by naming them.
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
// Switch was excluded from this guard while the body it guards - FS::File -
// has worked there all along. Nothing about this code was platform-specific;
// the #if was.
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

// Game content, explicitly. Identical to ReadFile, which is retained only
// because removing a symbol would be a major bump - this is the name to use,
// because "GameReadFile" says at the call site what "ReadFile" left implied.
extern "C" inline int32_t CoreGameReadFile(const char* path, void* buffer,
                                           uint32_t maxSize) {
    size_t read = 0;
    if (!WiiXLaunch::FS::ReadFile(path, buffer, maxSize, &read)) return -1;
    return static_cast<int32_t>(read);
}

// --- appended in v1.5 ------------------------------------------------------

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
extern "C" inline uint32_t CoreRegisterTick(void (*fn)()) {
    return WiiXLaunch::Tick::Add(fn) == WiiXLaunch::Tick::Register::Ok ? 1u : 0u;
}

// Reads a whole file from game content. Returns bytes read, or a negative value
// on failure. `outRead` may be null.
//
// The v1.0 spelling of GameReadFile, kept resolvable for mods built against it.
extern "C" inline int32_t CoreReadFile(const char* path, void* buffer, uint32_t maxSize) {
    size_t read = 0;
    if (!WiiXLaunch::FS::ReadFile(path, buffer, maxSize, &read)) return -1;
    return static_cast<int32_t>(read);
}

// Does a path exist and open? Cheap existence check that does not need a
// buffer, for a mod deciding whether an optional asset is present.
extern "C" inline uint32_t CoreFileExists(const char* path) {
// As above: FS::File opens on Switch, so this always could have.
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
    // v1.1. APPENDED, never inserted: a mod built against v1.0 hashes the same
    // six names and finds them at the same version, so it keeps working.
    WIIXL_SURFACE_SYMBOL("HeapGranted",   &CoreHeapGranted),
    WIIXL_SURFACE_SYMBOL("HeapUsed",      &CoreHeapUsed),
    WIIXL_SURFACE_SYMBOL("HeapRemaining", &CoreHeapRemaining),
    // v1.2. Appended, never inserted.
    WIIXL_SURFACE_SYMBOL("InstallHook",     &CoreInstallHook),
    WIIXL_SURFACE_SYMBOL("HookProbeTarget", &CoreHookProbeTarget),
    // v1.3. Appended, never inserted.
    WIIXL_SURFACE_SYMBOL("HookProbeClaimTag", &CoreHookProbeClaimTag),
    WIIXL_SURFACE_SYMBOL("HookProbeMark",     &CoreHookProbeMark),
    // v1.4. Appended, never inserted.
    WIIXL_SURFACE_SYMBOL("ModReadFile",   &CoreModReadFile),
    WIIXL_SURFACE_SYMBOL("ModFileExists", &CoreModFileExists),
    WIIXL_SURFACE_SYMBOL("GameReadFile",  &CoreGameReadFile),
    // v1.5. Appended, never inserted.
    WIIXL_SURFACE_SYMBOL("RegisterTick",  &CoreRegisterTick),
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
