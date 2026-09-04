#pragma once

// WiiXLaunch::Arena - the host owns all of memory, and hands modules bounded
// pieces of it.
//
// Before this, every allocation came from one bump pointer running from the end
// of the payload to the end of Cemu's code-cave area, and whoever asked first
// got whatever was left. With one module that is merely untidy. With several it
// is a bug waiting to be reported as somebody else's: a mod that over-allocates
// starves the mods loaded after it, and the failure surfaces in them.
//
// So the reservation is carved:
//
//   [ host reserve ][ module A ][ module B ] ... [ unassigned ]
//
// The host reserve is what the framework itself needs and is taken first,
// because a module exhausting the arena must not be able to stop the host
// logging why. Each module then gets a sub-arena it cannot allocate past. A
// module that runs out gets null from its own allocation and nothing else in
// the process is affected.
//
// THE SIZE IS NOT A CONSTANT AND MUST NOT BE TREATED AS ONE. On Cemu the
// reservation is the tail of a code cave whose start depends on how many other
// graphic packs the user has enabled - 3959, 3963, 3934 and 3930 KB across four
// measured boots of the same build. Nothing may reserve a fixed number of
// bytes; everything reads the size at runtime and logs what it got.
//
// ---------------------------------------------------------------------------
// THE heapRequest CONTRACT, and it is deliberately two contracts.
//
//   heapRequest > 0   A STATED REQUIREMENT. The host either grants exactly that
//                     much or refuses the module at load time, by name, before
//                     relocating it. A mod that knows what it needs gets a hard,
//                     early, diagnosable failure instead of a mysterious null
//                     halfway through a frame.
//
//   heapRequest == 0  BEST EFFORT. The module is granted whatever is sensible
//                     and Alloc returns null when that runs out. A mod that
//                     cannot predict its usage stays loadable, and is expected
//                     to check for null.
//
// The distinction is opt-in on the module's side: stating a number means being
// held to it. Both paths are documented in docs/loader.md and both are tested.
// ---------------------------------------------------------------------------

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>

#include <cstdint>
#include <cstddef>

#if WIIXL_CEMU
#include <wiixl_cemu_backend.hpp>
#endif

namespace WiiXLaunch::Arena {

// What the framework keeps for itself, taken off the top before any module is
// given anything. Deliberately generous relative to what the host actually
// uses: the cost of over-reserving is a module getting less, and the cost of
// under-reserving is the host failing to log why a module was refused.
constexpr uint32_t kHostReserve = 64 * 1024;

// What a module gets when it states no requirement. Not a promise - it is
// whatever remains, capped at this - but it keeps one silent mod from taking
// the whole arena on a first-come basis.
constexpr uint32_t kDefaultGrant = 256 * 1024;

// A module cannot have more slots than the host can load modules.
constexpr uint32_t kMaxModules = 8;

// Why a module can be refused on one machine and load on another, said in the
// log rather than left for a bug report nobody can reproduce.
//
// The arena is the TAIL of a 4 MB code cave that every enabled graphic pack is
// carved out of, in load order. Enabling more packs pushes WiiXLaunch later and
// leaves it less. Measured across four boots of the same build: 3959, 3963,
// 3934 and 3930 KB. A mod that loads on a clean setup and is refused on a
// loaded one is not a mod bug and not a host bug.
constexpr const char* kSharedArenaNote =
    "The arena is the tail of a 4 MB code cave shared with every enabled graphic "
    "pack, so it shrinks as more are enabled (measured 3930-3963 KB across four "
    "boots). The same module may load on a cleaner setup.";

enum class Grant : uint32_t {
    Ok = 0,
    ArenaNotReady,     // the base is unknown or already past the wall
    NoSlots,           // kMaxModules already handed out
    RequestUnmeetable, // a STATED heapRequest larger than what is left
    NothingLeft,       // best-effort, and there is genuinely nothing
};

inline const char* GrantName(Grant g) {
    switch (g) {
        case Grant::Ok:                return "OK";
        case Grant::ArenaNotReady:     return "ARENA-NOT-READY";
        case Grant::NoSlots:           return "NO-SLOTS";
        case Grant::RequestUnmeetable: return "REQUEST-UNMEETABLE";
        case Grant::NothingLeft:       return "NOTHING-LEFT";
    }
    return "?";
}

// One module's bounded piece. Never freed, like everything else here - a mod
// is loaded once and lives for the session.
struct SubArena {
    uintptr_t base;
    uint32_t  size;      // granted, not requested
    uint32_t  used;
    bool      stated;    // did the module state a requirement?
    char      owner[17];
    bool      inUse;
};

namespace impl {

inline SubArena g_Subs[kMaxModules];
inline uint32_t g_SubCount = 0;

// How far into the reservation the host has handed out, host reserve included.
inline uint32_t g_Carved = 0;

// The sub-arena allocations are currently charged to. Set around a module's
// load and cleared afterwards, so the loader's allocation hook - which has no
// place to carry a module identity - charges the right one.
inline SubArena* g_Current = nullptr;

// An explicitly supplied reservation, for a host that is not the Cemu code
// cave. Same reasoning as the loader's memory hooks: the carving logic is
// ordinary arithmetic and testing it should not need a console. Without this
// the arena can only ever exist on Cemu, and tools/loader_fuzz would be
// exercising a loader whose allocator always fails.
inline uintptr_t g_ExplicitBase = 0;
inline uint32_t g_ExplicitTotal = 0;
inline bool g_HasExplicit = false;

inline void CopyOwner(char* dst, const char* src) {
    uint32_t i = 0;
    for (; i < 16 && src && src[i]; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

} // namespace impl

// Supplies the reservation directly, and resets everything carved from it.
// Cemu does not need this - it reads the code cave - but a host test does.
inline void SetReservation(uintptr_t base, uint32_t size) {
    impl::g_ExplicitBase = base;
    impl::g_ExplicitTotal = size;
    impl::g_HasExplicit = true;
    impl::g_Carved = 0;
    impl::g_SubCount = 0;
    impl::g_Current = nullptr;
}

// Forgets every grant, keeping the reservation. For a test that runs many
// loads; nothing in a real host calls it, because nothing is ever freed.
inline void ResetGrants() {
    impl::g_Carved = 0;
    impl::g_SubCount = 0;
    impl::g_Current = nullptr;
}

// The whole reservation, read at runtime because it is not a constant.
inline uintptr_t Base() {
    if (impl::g_HasExplicit) return impl::g_ExplicitBase;
#if WIIXL_CEMU
    return Backend::CemuHeapBase();
#else
    return 0;
#endif
}

inline uint32_t Total() {
    if (impl::g_HasExplicit) return impl::g_ExplicitTotal;
#if WIIXL_CEMU
    return static_cast<uint32_t>(Backend::CemuHeapLimit());
#else
    return 0;
#endif
}

inline uint32_t Carved() { return impl::g_Carved; }

inline uint32_t Unassigned() {
    const uint32_t total = Total();
    return total > impl::g_Carved ? total - impl::g_Carved : 0;
}

// Takes the host reserve off the top. Idempotent, and safe to call before the
// base is known - it simply reports that and changes nothing.
inline bool Init() {
    if (impl::g_Carved != 0) return true;

    const uint32_t total = Total();
    if (Base() == 0 || total == 0) {
        WIIXL_LOG("Arena: not ready - base %p, total %u. No module can be granted "
                  "memory until the payload knows where it is.",
                  reinterpret_cast<void*>(Base()), total);
        return false;
    }
    if (total <= kHostReserve) {
        WIIXL_LOG("Arena: only %u bytes total, less than the %u-byte host reserve. "
                  "This host cannot load modules at all.", total, kHostReserve);
        return false;
    }

    impl::g_Carved = kHostReserve;
    WIIXL_LOG("Arena: %u bytes at %p; %u reserved for the host, %u assignable",
              total, reinterpret_cast<void*>(Base()), kHostReserve, Unassigned());
    return true;
}

// Grants a module its piece.
//
// `request` is the module's heapRequest: non-zero means a stated requirement
// the host must meet exactly or refuse; zero means best effort.
inline Grant Acquire(const char* owner, uint32_t request, SubArena** out) {
    *out = nullptr;
    if (!Init()) return Grant::ArenaNotReady;

    if (impl::g_SubCount >= kMaxModules) {
        WIIXL_LOG("Arena: %s refused - all %u module slots are taken",
                  owner, kMaxModules);
        return Grant::NoSlots;
    }

    const uint32_t left = Unassigned();

    uint32_t grant;
    if (request != 0) {
        // Stated requirement: meet it exactly or refuse. Rounding down to
        // "what we could spare" would hand the module a promise the host did
        // not keep, which is the failure this contract exists to prevent.
        if (request > left) {
            WIIXL_LOG("Arena: %s REFUSED granted=0 requested=%u (%u KB), unassigned=%u "
                      "(%u KB). A stated heapRequest is a requirement, so the module is "
                      "refused rather than given less than it asked for.",
                      owner, request, request / 1024u, left, left / 1024u);
            WIIXL_LOG("Arena: %s", kSharedArenaNote);
            return Grant::RequestUnmeetable;
        }
        grant = request;
    } else {
        // Best effort: whatever is sensible, and the module must handle null.
        grant = left < kDefaultGrant ? left : kDefaultGrant;
        if (grant == 0) {
            WIIXL_LOG("Arena: %s REFUSED granted=0 requested=unspecified - nothing left "
                      "to assign", owner);
            WIIXL_LOG("Arena: %s", kSharedArenaNote);
            return Grant::NothingLeft;
        }
    }

    SubArena& s = impl::g_Subs[impl::g_SubCount++];
    s.base = Base() + impl::g_Carved;
    s.size = grant;
    s.used = 0;
    s.stated = (request != 0);
    s.inUse = true;
    impl::CopyOwner(s.owner, owner);
    impl::g_Carved += grant;

    // granted-vs-requested on EVERY module, always, including the best-effort
    // path where "requested" is the interesting half of the answer. When a mod
    // misbehaves in-game this is the first line worth having, and it costs
    // nothing to print.
    if (s.stated) {
        WIIXL_LOG("Arena: %s granted=%u (%u KB) requested=%u (%u KB) at %p - stated "
                  "requirement, met exactly; %u KB unassigned",
                  s.owner, s.size, s.size / 1024u, request, request / 1024u,
                  reinterpret_cast<void*>(s.base), Unassigned() / 1024u);
    } else {
        WIIXL_LOG("Arena: %s granted=%u (%u KB) requested=unspecified at %p - best "
                  "effort, Alloc returns null past this; %u KB unassigned",
                  s.owner, s.size, s.size / 1024u,
                  reinterpret_cast<void*>(s.base), Unassigned() / 1024u);
    }
    *out = &s;
    return Grant::Ok;
}

// Allocates within one module's piece. Bounded by that piece, so a module that
// over-allocates starves only itself.
inline void* AllocIn(SubArena& s, uint32_t size, uint32_t align) {
    if (align == 0) align = 64;
    if (size == 0) return nullptr;

    const uintptr_t cur = s.base + s.used;
    const uintptr_t aligned = (cur + (align - 1)) & ~static_cast<uintptr_t>(align - 1);
    const uint64_t end = static_cast<uint64_t>(aligned - s.base) + size;

    if (end > s.size) {
        WIIXL_LOG("Arena: %s wanted %u bytes and has %u of %u used - refused. %s",
                  s.owner, size, s.used, s.size,
                  s.stated ? "It stated a heapRequest and has now exceeded it."
                           : "It stated no heapRequest, so this is a best-effort "
                             "grant and null is the documented answer.");
        return nullptr;
    }

    s.used = static_cast<uint32_t>(end);
    return reinterpret_cast<void*>(aligned);
}

// Which sub-arena the allocation hook charges. The loader sets this around a
// module's load; nothing else may.
inline void SetCurrent(SubArena* s) { impl::g_Current = s; }
inline SubArena* Current() { return impl::g_Current; }

// The hook handed to Loader::SetMemoryHooks and to wiixl.core's Alloc.
//
// With no current sub-arena this refuses rather than falling back to the old
// unbounded heap. Falling back is how a module would quietly escape its bound
// and take memory from the host and from every module after it - which is the
// entire thing this file exists to stop.
inline void* Alloc(uint32_t size, uint32_t align) {
    SubArena* s = impl::g_Current;
    if (!s) {
        WIIXL_LOG("Arena: allocation of %u bytes with no module arena current - "
                  "refused. Nothing outside a module's own piece may allocate here.",
                  size);
        return nullptr;
    }
    return AllocIn(*s, size, align);
}

// What a module got, and what is left of it. A module on the best-effort path
// needs this DURING load, before it allocates, to size a buffer sensibly -
// "allocate until null" is not a design, it is a way of finding out by failing.
// Exposed through wiixl.core so a compiled mod can call it.
inline uint32_t GrantedTo(const SubArena* s) { return s ? s->size : 0u; }
inline uint32_t UsedIn(const SubArena* s)    { return s ? s->used : 0u; }
inline uint32_t RemainingIn(const SubArena* s) {
    if (!s) return 0;
    return s->size > s->used ? s->size - s->used : 0u;
}

// One line per module, for the log at the load point.
inline void LogState() {
    WIIXL_LOG("Arena: %u of %u bytes carved, %u unassigned, %u module(s):",
              impl::g_Carved, Total(), Unassigned(), impl::g_SubCount);
    for (uint32_t i = 0; i < impl::g_SubCount; ++i) {
        const SubArena& s = impl::g_Subs[i];
        WIIXL_LOG("Arena:   %-16s %u of %u used at %p (%s)",
                  s.owner, s.used, s.size, reinterpret_cast<void*>(s.base),
                  s.stated ? "stated" : "best effort");
    }
}

} // namespace WiiXLaunch::Arena
