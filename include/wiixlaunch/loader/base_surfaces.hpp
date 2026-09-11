#pragma once

// The rest of BASE WiiXLaunch, reachable by mods.
//
// wiixl.core carries what every mod needs: logging, arena memory, files, hooks,
// ticks. These are the framework's other services, each its own surface so a
// mod declares what it actually uses and a host missing one refuses it by name
// rather than at the first call.
//
//   wiixl.time   the clock, monotonic and wall
//   wiixl.mem    the coreinit heaps - NOT the module arena
//   wiixl.call   resolving a game function's address
//   wiixl.patch  writing to game code at runtime
//
// wiixl.net lives in its own header because it is far larger than these four
// together and has a platform story of its own.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/mod_context.hpp>
#include <wiixlaunch/time.hpp>
#include <wiixlaunch/mem.hpp>
#include <wiixlaunch/call.hpp>
#include <wiixlaunch/patch.hpp>
#include <wiixlaunch/patches.hpp>

#include <cstdint>

// ===========================================================================
// wiixl.time
//
// TWO CLOCKS, and they are not interchangeable. The monotonic tick counter
// always moves forward and is what a mod should measure elapsed time with. The
// wall clock is the console's RTC (or the host PC's, under Cemu) and can be
// wrong, can jump, and on some hosts is not there at all - IsWallClockAvailable
// says which, rather than leaving a mod to infer it from an implausible date.
// ===========================================================================
namespace WiiXLaunch::TimeSurface {

constexpr const char* kName = "wiixl.time";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

namespace impl {

// int64 through two uint32s. A 64-bit value crosses this boundary perfectly
// well by ABI, but every other entry in every surface is 32-bit, and one call
// with a different width is one call whose calling convention has to be right
// in a place nobody would think to check.
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

// The calendar, flattened. CalendarTime is a struct of ten ints and must not
// cross; the caller passes an int32[10] and the order is fixed here forever:
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

// "2026-09-05 12:39:29" into a caller-owned buffer, because a mod has no
// formatter of its own and this is the one string every mod wants.
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


// ===========================================================================
// wiixl.mem - the coreinit heaps.
//
// NOT THE MODULE ARENA, and the difference matters enough to say twice.
// wiixl.core's Alloc hands out bytes from this module's own grant: bounded,
// attributed, never freed, and invisible to the game. This is the console's own
// expanded heap - unbounded, unattributed, freeable, and visible to everything.
//
// A mod wants this only when something OUTSIDE the mod has to read the memory:
// a buffer handed to a game function, a texture the GPU reads. For anything the
// mod merely uses itself, the arena is the right answer and this is a way to
// leak memory nothing will reclaim.
// ===========================================================================
namespace WiiXLaunch::MemSurface {

constexpr const char* kName = "wiixl.mem";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

namespace impl {

extern "C" inline uint32_t MemShimsAvailable() {
    return Mem::ShimsAvailable() ? 1u : 0u;
}

// heap: 0 MEM1, 1 MEM2, 2 FG - matching Mem::BaseHeap. Returned as an opaque
// address; a mod passes it back and never dereferences it.
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


// ===========================================================================
// wiixl.call - where a game function lives.
//
// The module's GetTargetFunction is a template that returns a typed pointer; a
// template cannot cross, so this returns the ADDRESS and the mod supplies the
// type at its own call site. That is the honest split anyway - the host cannot
// check a signature it was never told, and pretending to would be worse than
// not offering.
//
// A mod calling a game function through this has taken on exactly what the
// declared-patch origin check exists to prevent: an address baked into a binary
// that nobody can rebuild. It is still much better than the alternative, since
// the SWITCH/WII U SPLIT is resolved here rather than in the mod.
// ===========================================================================
namespace WiiXLaunch::CallSurface {

constexpr const char* kName = "wiixl.call";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

namespace impl {

// One offset per platform, and the host picks. A mod that hard-coded the Wii U
// address would be silently wrong on Switch; this way the wrong-platform case
// cannot arise at the call site at all.
extern "C" inline uintptr_t ClResolveTarget(uintptr_t switchOffset, uintptr_t wiiuOffset) {
#if WIIXL_SWITCH
    (void)wiiuOffset;
    return ResolveTarget(switchOffset);
#else
    (void)switchOffset;
    return ResolveTarget(wiiuOffset);
#endif
}

// Where the image starts, for a mod doing its own arithmetic. Same value
// wiixl.core's ImageBase reports; here so a mod using wiixl.call does not have
// to declare wiixl.core as well for one number.
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


// ===========================================================================
// wiixl.patch - writing to game code at runtime.
//
// A .wxlm can already DECLARE patches, which the loader applies before any
// entry runs, with an origin check and a conflict report. That is the right way
// and it stays the right way. This is for what declared patches cannot express:
// a patch applied and reverted while the game runs - a toggle.
//
// THE ORIGIN CHECK COMES WITH IT. Write takes the bytes it expects to find and
// refuses if they are not there, exactly as a declared patch does, because the
// reason is the same: a mod built against another version of the game would
// otherwise corrupt a function it has never seen. The refusal is a value and
// the log names the mod.
//
// WriteUnchecked exists because some patches genuinely have no stable origin -
// a site already patched by another mod, most obviously. It is named Unchecked
// at every call site and the host logs the module that used it, the same
// treatment as botw.player's raw-pointer hatch: from outside, memory corruption
// caused by an unchecked write is indistinguishable from a host bug, and the
// log is the only place that difference can be recorded.
// ===========================================================================
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

// 1 on success. 0 when the bytes at the address are not what the caller said
// they would be - which is the version guard doing its job, not a failure of
// this call.
// THROUGH THE PATCH REGISTRY, not past it.
//
// This used to verify the origin bytes and then write, recording nothing. So a
// runtime patch got one of the four checks a DECLARED patch gets: it could land
// inside the 16 bytes a hook displaced, or on top of another module's patch, or
// into the module arena, and none of those were looked at - while the surface
// sat next to Patches, which does all of them and names the other party.
//
// That gap is the reason a mod wanting a CONFIGURABLE patch had to choose
// between the collision report and the config: a declared patch cannot depend on
// a number read at runtime, and a runtime patch was unregistered. It no longer
// is - Patches::ApplyAt checks and records, so two mods rewriting the same
// instruction collide by name whichever way either of them wrote it.
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

// Reads bytes back out of game memory, which is how a mod verifies its own
// write rather than trusting the return value. The sixth rule in
// docs/modules.md, made available to mods.
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

// Everything in this header, registered together. Called by the host right
// after wiixl.core, before any game module - these are base services and a game
// module may perfectly well want them itself.
inline void RegisterAll() {
    TimeSurface::Register();
    MemSurface::Register();
    CallSurface::Register();
    PatchSurface::Register();
}

} // namespace WiiXLaunch::BaseSurfaces
