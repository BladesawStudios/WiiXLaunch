#pragma once

// The rest of base WiiXLaunch, reachable by mods. Each is its own surface
// so a mod declares what it actually uses and a host missing one refuses
// it by name rather than at the first call.
//
//   wiixl.time     the clock, monotonic and wall
//   wiixl.mem      the coreinit heaps, not the module arena
//   wiixl.call     resolving a game function's address
//   wiixl.version  which build of the game is underneath
//   wiixl.patch    writing to game code at runtime
//
// wiixl.net has its own header; it's larger and has its own platform story.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/mod_context.hpp>
#include <wiixlaunch/time.hpp>
#include <wiixlaunch/mem.hpp>
#include <wiixlaunch/call.hpp>
#include <wiixlaunch/patch.hpp>
#include <wiixlaunch/patches.hpp>
#include <wiixlaunch/game_version.hpp>

#include <cstdint>

// wiixl.time: two clocks, not interchangeable. The monotonic tick counter
// always moves forward and is what a mod should measure elapsed time with.
// The wall clock is the console's RTC (or the host PC's, under Cemu), can
// jump, and isn't available everywhere - IsWallClockAvailable says which.
namespace WiiXLaunch::TimeSurface {

constexpr const char* kName = "wiixl.time";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

namespace impl {

// int64 through two uint32s, matching every other 32-bit surface entry
// rather than being the one call with a different calling-convention width.
extern "C" inline void TmGetMonotonicTicks(uint32_t* hi, uint32_t* lo) {
    const uint64_t t = static_cast<uint64_t>(Time::GetMonotonicTicks());
    if (hi) *hi = static_cast<uint32_t>(t >> 32);
    if (lo) *lo = static_cast<uint32_t>(t & 0xFFFFFFFFu);
}

extern "C" inline uint32_t TmTicksPerSecond() {
    return static_cast<uint32_t>(Time::kTicksPerSecond);
}

extern "C" inline uint32_t TmIsWallClockAvailable() {
    return Time::IsWallClockAvailable() ? 1u : 0u;
}

// The calendar, flattened: a struct can't cross the surface boundary, so
// the caller passes an int32[10] in this fixed order:
// sec, min, hour, mday, mon, year, wday, yday, msec, usec.
extern "C" inline uint32_t TmGetCalendarTime(int32_t* out10) {
    if (!out10) return 0;
    Time::CalendarTime t{};
    if (!Time::GetCalendarTime(&t)) return 0;
    out10[0] = t.sec;  out10[1] = t.min;  out10[2] = t.hour; out10[3] = t.mday;
    out10[4] = t.mon;  out10[5] = t.year; out10[6] = t.wday; out10[7] = t.yday;
    out10[8] = t.msec; out10[9] = t.usec;
    return 1;
}

// "2026-09-05 12:39:29" into a caller-owned buffer.
extern "C" inline uint32_t TmFormatNow(char* out, uint32_t cap) {
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    return Time::FormatNow(out, cap) ? 1u : 0u;
}

inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("GetMonotonicTicks",    &TmGetMonotonicTicks),
    WIIXL_SURFACE_SYMBOL("TicksPerSecond",       &TmTicksPerSecond),
    WIIXL_SURFACE_SYMBOL("IsWallClockAvailable", &TmIsWallClockAvailable),
    WIIXL_SURFACE_SYMBOL("GetCalendarTime",      &TmGetCalendarTime),
    WIIXL_SURFACE_SYMBOL("FormatNow",            &TmFormatNow),
};

} // namespace impl

inline bool Register() {
    Surface::Registration reg{};
    reg.name = kName;
    reg.versionMajor = kVersionMajor;
    reg.versionMinor = kVersionMinor;
    reg.symbols = impl::kSymbols;
    reg.symbolCount = static_cast<uint32_t>(sizeof(impl::kSymbols) / sizeof(impl::kSymbols[0]));
    return Surface::Register(reg);
}

} // namespace WiiXLaunch::TimeSurface


// wiixl.mem: the coreinit heaps, not the module arena. wiixl.core's Alloc
// hands out bytes from this module's own grant (bounded, attributed, never
// freed, invisible to the game); this is the console's expanded heap
// (unbounded, unattributed, freeable, visible to everything). Want this
// only when something outside the mod has to read the memory - a buffer
// handed to a game function, a GPU-read texture. Otherwise the arena is
// the right answer and this leaks memory nothing reclaims.
namespace WiiXLaunch::MemSurface {

constexpr const char* kName = "wiixl.mem";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

namespace impl {

extern "C" inline uint32_t MemShimsAvailable() {
    return Mem::ShimsAvailable() ? 1u : 0u;
}

// heap: 0 MEM1, 1 MEM2, 2 FG (matching Mem::BaseHeap). Returned as an
// opaque address a mod passes back and never dereferences.
extern "C" inline uintptr_t MemGetBaseHeapHandle(uint32_t heap) {
    return reinterpret_cast<uintptr_t>(
        Mem::GetBaseHeapHandle(static_cast<Mem::BaseHeap>(heap)));
}

extern "C" inline uint32_t MemGetAllocatableSize(uintptr_t heap, int32_t align) {
    if (!heap) return 0;
    return Mem::GetAllocatableSize(reinterpret_cast<void*>(heap), align ? align : 256);
}

extern "C" inline uintptr_t MemAllocFromExpHeap(uintptr_t heap, uint32_t size, int32_t align) {
    if (!heap || size == 0) return 0;
    return reinterpret_cast<uintptr_t>(
        Mem::AllocFromExpHeap(reinterpret_cast<void*>(heap), size, align ? align : 256));
}

extern "C" inline void MemFreeToExpHeap(uintptr_t heap, uintptr_t block) {
    if (!heap || !block) return;
    Mem::FreeToExpHeap(reinterpret_cast<void*>(heap), reinterpret_cast<void*>(block));
}

inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("ShimsAvailable",     &MemShimsAvailable),
    WIIXL_SURFACE_SYMBOL("GetBaseHeapHandle",  &MemGetBaseHeapHandle),
    WIIXL_SURFACE_SYMBOL("GetAllocatableSize", &MemGetAllocatableSize),
    WIIXL_SURFACE_SYMBOL("AllocFromExpHeap",   &MemAllocFromExpHeap),
    WIIXL_SURFACE_SYMBOL("FreeToExpHeap",      &MemFreeToExpHeap),
};

} // namespace impl

inline bool Register() {
    Surface::Registration reg{};
    reg.name = kName;
    reg.versionMajor = kVersionMajor;
    reg.versionMinor = kVersionMinor;
    reg.symbols = impl::kSymbols;
    reg.symbolCount = static_cast<uint32_t>(sizeof(impl::kSymbols) / sizeof(impl::kSymbols[0]));
    return Surface::Register(reg);
}

} // namespace WiiXLaunch::MemSurface


// wiixl.version: which build of the game is underneath. A mod carrying raw
// offsets carries them for one build; Name() is what the host enrolled
// this build as, null when unrecognised - the useful part, since it means
// a mod holding raw offsets should refuse rather than apply them (see
// include/wiixlaunch/mod_version.h). Fingerprint() is always available
// where the target declares an identity slice, recognised or not.
namespace WiiXLaunch::VersionSurface {

constexpr const char* kName = "wiixl.version";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

namespace impl {

extern "C" inline uint32_t VrFingerprint() {
    return GameVersion::Fingerprint();
}

// Null when unrecognised, not the string "unknown" - a literal name would
// let a mod match against every unenrolled build.
extern "C" inline const char* VrName() {
    return GameVersion::Name();
}

// Whether this host can fingerprint at all, distinct from "it can, and
// this build isn't one it knows."
extern "C" inline uint32_t VrConfigured() {
    return GameVersion::Configured() ? 1u : 0u;
}

inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("Fingerprint", &VrFingerprint),
    WIIXL_SURFACE_SYMBOL("Name",        &VrName),
    WIIXL_SURFACE_SYMBOL("Configured",  &VrConfigured),
};

} // namespace impl

inline bool Register() {
    Surface::Registration reg{};
    reg.name = kName;
    reg.versionMajor = kVersionMajor;
    reg.versionMinor = kVersionMinor;
    reg.symbols = impl::kSymbols;
    reg.symbolCount = static_cast<uint32_t>(sizeof(impl::kSymbols) / sizeof(impl::kSymbols[0]));
    return Surface::Register(reg);
}

} // namespace WiiXLaunch::VersionSurface


// wiixl.call: where a game function lives. GetTargetFunction is a template
// returning a typed pointer, which can't cross the surface boundary, so
// this returns the address and the mod supplies the type at its own call
// site - the host can't check a signature it was never told.
namespace WiiXLaunch::CallSurface {

constexpr const char* kName = "wiixl.call";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

namespace impl {

// One offset per platform, and the host picks, so the wrong-platform case
// can't arise at the call site.
extern "C" inline uintptr_t ClResolveTarget(uintptr_t switchOffset, uintptr_t wiiuOffset) {
#if WIIXL_SWITCH
    (void)wiiuOffset;
    return ResolveTarget(switchOffset);
#else
    (void)switchOffset;
    return ResolveTarget(wiiuOffset);
#endif
}

// Same value wiixl.core's ImageBase reports; here so a mod using
// wiixl.call doesn't have to declare wiixl.core too for one number.
extern "C" inline uintptr_t ClImageBase() {
    return ResolveTarget(0);
}

inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("ResolveTarget", &ClResolveTarget),
    WIIXL_SURFACE_SYMBOL("ImageBase",     &ClImageBase),
};

} // namespace impl

inline bool Register() {
    Surface::Registration reg{};
    reg.name = kName;
    reg.versionMajor = kVersionMajor;
    reg.versionMinor = kVersionMinor;
    reg.symbols = impl::kSymbols;
    reg.symbolCount = static_cast<uint32_t>(sizeof(impl::kSymbols) / sizeof(impl::kSymbols[0]));
    return Surface::Register(reg);
}

} // namespace WiiXLaunch::CallSurface


// wiixl.patch: writing to game code at runtime, for what a declared patch
// can't express - a patch applied and reverted while the game runs.
//
// Write takes the origin bytes it expects and refuses if they aren't
// there, same as a declared patch and for the same reason. WriteUnchecked
// exists for the rare site with no stable origin (already patched by
// another mod), and is named Unchecked and logged by mod so a resulting
// memory corruption isn't indistinguishable from a host bug.
namespace WiiXLaunch::PatchSurface {

constexpr const char* kName = "wiixl.patch";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

namespace impl {

constexpr uint32_t kMaxNoted = 8;
inline char g_Noted[kMaxNoted][17];
inline uint32_t g_NotedCount = 0;

inline void NoteUncheckedOnce() {
    const char* owner = ModContext::Current();
    if (!owner || owner[0] == '\0') owner = "<host>";

    for (uint32_t i = 0; i < g_NotedCount; ++i) {
        bool same = true;
        for (uint32_t c = 0; c < 17; ++c) {
            if (g_Noted[i][c] != owner[c]) { same = false; break; }
            if (owner[c] == '\0') break;
        }
        if (same) return;
    }
    if (g_NotedCount < kMaxNoted) {
        uint32_t i = 0;
        for (; i + 1 < 17 && owner[i]; ++i) g_Noted[g_NotedCount][i] = owner[i];
        g_Noted[g_NotedCount][i] = '\0';
        ++g_NotedCount;
    }
    WIIXL_LOG("wiixl.patch: %s wrote to game code WITHOUT an origin check - it "
              "has opted out of the version guard every declared patch gets", owner);
}

inline bool OriginMatches(uintptr_t addr, const uint8_t* origin, uint32_t len) {
    const uint8_t* live = reinterpret_cast<const uint8_t*>(addr);
    for (uint32_t i = 0; i < len; ++i) {
        if (live[i] != origin[i]) return false;
    }
    return true;
}

// 1 on success. 0 when the bytes at the address aren't what the caller
// said - the version guard doing its job. Goes through Patches::ApplyAt
// (not around it), so a runtime patch gets the same hooked-window and
// patch-overlap checks a declared patch gets, and two mods rewriting the
// same instruction collide by name either way.
extern "C" inline uint32_t PtWrite(uintptr_t addr, const void* data, uint32_t size,
                                   const void* origin, uint32_t originSize) {
    if (!addr || !data || size == 0 || !origin || originSize != size) return 0;

    const char* owner = ModContext::Current();
    return Patches::ApplyAt(addr, static_cast<const uint8_t*>(data),
                            static_cast<const uint8_t*>(origin), size,
                            owner ? owner : "<host>") == Patches::Result::Ok;
}

extern "C" inline uint32_t PtWriteUnchecked(uintptr_t addr, const void* data, uint32_t size) {
    if (!addr || !data || size == 0) return 0;
    NoteUncheckedOnce();
    CodePatch::Write(addr, data, size);
    return 1;
}

// Reads bytes back out of game memory, so a mod can verify its own write
// rather than trust the return value.
extern "C" inline uint32_t PtRead(uintptr_t addr, void* out, uint32_t size) {
    if (!addr || !out || size == 0) return 0;
    const uint8_t* src = reinterpret_cast<const uint8_t*>(addr);
    uint8_t* dst = static_cast<uint8_t*>(out);
    for (uint32_t i = 0; i < size; ++i) dst[i] = src[i];
    return 1;
}

inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("Write",          &PtWrite),
    WIIXL_SURFACE_SYMBOL("WriteUnchecked", &PtWriteUnchecked),
    WIIXL_SURFACE_SYMBOL("Read",           &PtRead),
};

} // namespace impl

inline bool Register() {
    Surface::Registration reg{};
    reg.name = kName;
    reg.versionMajor = kVersionMajor;
    reg.versionMinor = kVersionMinor;
    reg.symbols = impl::kSymbols;
    reg.symbolCount = static_cast<uint32_t>(sizeof(impl::kSymbols) / sizeof(impl::kSymbols[0]));
    return Surface::Register(reg);
}

} // namespace WiiXLaunch::PatchSurface


namespace WiiXLaunch::BaseSurfaces {

// Everything in this header, registered together. Called by the host
// right after wiixl.core, before any game module.
inline void RegisterAll() {
    TimeSurface::Register();
    MemSurface::Register();
    CallSurface::Register();
    PatchSurface::Register();
    VersionSurface::Register();
}

} // namespace WiiXLaunch::BaseSurfaces
