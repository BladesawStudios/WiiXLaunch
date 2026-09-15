#pragma once

// WiiXLaunch::Arena - the single memory owner. Everything in this payload
// that allocates does it here, and modules get bounded pieces so an
// over-allocating mod starves only itself.
//
// Two ends, not a fixed reserve:
//
//   [ host allocations ->                          <- module grants ]
//   ^ Base()                                        Base()+Total() ^
//
// The host allocates upward from the base; module grants are carved
// downward from the top. A grant fails if it would cross the host's
// high-water mark; a host allocation fails if it would cross into carved
// territory. A fixed split doesn't work here: the host is the framework
// plus whatever game modules are installed, and a GX2 layer can allocate
// render targets in megabytes.
//
// A game module (vendor/wiixlaunch-*) is compiled into the payload, so its
// allocations use AllocHost. A mod (.wxlm) is loaded at runtime, gets a
// bounded grant, and reaches memory only through wiixl.core's Alloc.
//
// The total size is not a constant: on Cemu it's the tail of a code cave
// whose start depends on how many other graphic packs are enabled (measured
// 3930-3963 KB across four boots of the same build). Nothing may reserve a
// fixed number of bytes; everything reads the size at runtime.
//
// The heapRequest contract is two contracts:
//
//   heapRequest > 0   a stated requirement. Granted exactly, or the module
//                     is refused at load time, by name, before relocating.
//   heapRequest == 0  best effort. Granted whatever is sensible; Alloc
//                     returns null once that runs out.
//
// Both paths and their exact log wording are in docs/framework/loader.md,
// and both are exercised by tools/loader_fuzz.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>

#include <cstdint>
#include <cstddef>

#if WIIXL_CEMU
#include <wiixl_cemu_backend.hpp>
#endif

namespace WiiXLaunch::Arena {

// What a module gets when it states no requirement: whatever can be
// spared, capped. The cap is a fraction of the arena rather than an
// absolute number, so it still constrains a single-module grant on a small
// arena (an absolute 256 KB cap on a 256 KB Switch arena granted one
// module everything).
constexpr uint32_t kDefaultGrantCap = 256 * 1024;

// A module cannot have more slots than the host can load modules. Table
// size and a divisor, nothing more.
constexpr uint32_t kMaxModules = 16;

// Per-platform reason a module can be refused memory on one machine and
// load on another, for the boot log.
#if WIIXL_CEMU
constexpr const char* kSharedArenaNote =
    "The arena is the tail of a 4 MB code cave shared with every enabled graphic "
    "pack, so it shrinks as more are enabled (measured 3930-3963 KB across four "
    "boots). The same module may load on a cleaner setup.";
#elif WIIXL_SWITCH
constexpr const char* kSharedArenaNote =
    "The arena is a fixed block reserved in the host's own .text (see "
    "kSwitchArenaSize in src/switch_entry.cpp). It does not vary between boots, "
    "so a module refused here will be refused every time until that size is "
    "raised or another module asks for less.";
#else
constexpr const char* kSharedArenaNote =
    "The arena is whatever this host reserved through Arena::SetReservation.";
#endif

enum class Grant : uint32_t {
    Ok = 0,
    ArenaNotReady,     // the base is unknown or already past the wall
    NoSlots,           // kMaxModules already handed out
    RequestUnmeetable, // a STATED heapRequest larger than what is free
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

// One module's bounded piece. Never freed; a mod is loaded once and lives
// for the session.
struct SubArena {
    uintptr_t base;
    uint32_t  size;      // granted, not requested
    uint32_t  used;
    bool      stated;    // did the module state a requirement?
    char      owner[17];
    bool      inUse;
};

// Where the host's own allocations go when not coming from the arena
// directly. Installed by wiixlaunch/mem.hpp to move host allocation onto a
// coreinit or game heap. Module grants are never redirected: a grant holds
// relocated code that gets executed, and the code cave is the only region
// established as executable.
using HostProvider = void* (*)(size_t size, size_t align);

namespace impl {

inline SubArena g_Subs[kMaxModules];
inline uint32_t g_SubCount = 0;

// The two ends. g_HostUsed grows up from Base(); g_ModuleCarved grows down
// from Base() + Total(). They may never cross.
inline uint32_t g_HostUsed = 0;
inline uint32_t g_ModuleCarved = 0;

// The sub-arena allocations are currently charged to. Set around a
// module's load and its entry, then cleared, so wiixl.core's Alloc (which
// has no place to carry a module identity) charges the right one.
inline SubArena* g_Current = nullptr;

inline HostProvider g_HostProvider = nullptr;

inline uint32_t g_HostRefusedBytes = 0;
inline uint32_t g_HostRefusedCount = 0;

// An explicitly supplied reservation, for a host that isn't the Cemu code
// cave (a host test, for instance).
inline uintptr_t g_ExplicitBase = 0;
inline uint32_t g_ExplicitTotal = 0;
inline bool g_HasExplicit = false;

// Where to write, when that's not where the arena executes from. Horizon
// won't make one address both writable and executable, so on Switch a
// module's image lives in the host's own .text (not writable) and
// exl::util::Jit maps a second, writable view of the same pages. A delta
// rather than a second base, so it's zero (free, invisible) everywhere
// else.
inline uintptr_t g_WriteDelta = 0;

inline void CopyOwner(char* dst, const char* src) {
    uint32_t i = 0;
    for (; i < 16 && src && src[i]; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

} // namespace impl

// Supplies the reservation directly, and resets everything taken from it.
// Cemu reads the code cave instead; a host test needs this.
inline void SetReservation(uintptr_t base, uint32_t size) {
    impl::g_ExplicitBase = base;
    impl::g_ExplicitTotal = size;
    impl::g_HasExplicit = true;
    impl::g_HostUsed = 0;
    impl::g_ModuleCarved = 0;
    impl::g_SubCount = 0;
    impl::g_Current = nullptr;
    impl::g_HostRefusedBytes = 0;
    impl::g_HostRefusedCount = 0;
}

// Forgets every grant and host allocation, keeping the reservation. For a
// test that runs many loads.
inline void ResetGrants() {
    impl::g_HostUsed = 0;
    impl::g_ModuleCarved = 0;
    impl::g_SubCount = 0;
    impl::g_Current = nullptr;
    impl::g_HostRefusedBytes = 0;
    impl::g_HostRefusedCount = 0;
}

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
    const uintptr_t base = Backend::CemuHeapBase();
    if (base == 0 || base >= Backend::kCemuCodeCaveEnd) return 0;
    return static_cast<uint32_t>(Backend::kCemuCodeCaveEnd - base);
#else
    return 0;
#endif
}

inline uint32_t HostUsed()     { return impl::g_HostUsed; }
inline uint32_t ModuleCarved() { return impl::g_ModuleCarved; }

inline uint32_t Free() {
    const uint32_t total = Total();
    const uint64_t taken =
        static_cast<uint64_t>(impl::g_HostUsed) + impl::g_ModuleCarved;
    return taken < total ? static_cast<uint32_t>(total - taken) : 0u;
}

inline bool Ready() { return Base() != 0 && Total() != 0; }

// Call after SetReservation; the delta is computed against Base().
inline void SetWriteAlias(uintptr_t writableBase) {
    impl::g_WriteDelta = writableBase - Base();
}

// The address to write `p` through. Identity unless SetWriteAlias said
// otherwise.
inline void* Writable(void* p) {
    if (!p || impl::g_WriteDelta == 0) return p;
    return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(p) +
                                   impl::g_WriteDelta);
}

inline bool HostExhausted() { return impl::g_HostRefusedCount != 0; }
inline uint32_t HostRefusedBytes() { return impl::g_HostRefusedBytes; }
inline uint32_t HostRefusedCount() { return impl::g_HostRefusedCount; }

// --- host allocation -------------------------------------------------------

inline void SetHostProvider(HostProvider p) { impl::g_HostProvider = p; }
inline HostProvider GetHostProvider() { return impl::g_HostProvider; }

// The host's own allocator: the framework and every compiled-in game
// module. A bump pointer, bounded by where module grants begin (not the
// end of the reservation), never freed.
inline void* AllocHost(size_t size, size_t align) {
    if (size == 0) return nullptr;
    if (align == 0) align = 256;

    if (impl::g_HostProvider) return impl::g_HostProvider(size, align);

    const uintptr_t base = Base();
    const uint32_t total = Total();
    if (base == 0 || total == 0) {
        impl::g_HostRefusedBytes += static_cast<uint32_t>(size);
        impl::g_HostRefusedCount++;
        return nullptr;
    }

    const uintptr_t cur = base + impl::g_HostUsed;
    const uintptr_t aligned = (cur + (align - 1)) & ~static_cast<uintptr_t>(align - 1);
    // 64-bit throughout so the sum can't wrap back under the wall.
    const uint64_t end = static_cast<uint64_t>(aligned - base) + size;

    const uint64_t wall = static_cast<uint64_t>(total) - impl::g_ModuleCarved;
    if (end > wall) {
        impl::g_HostRefusedBytes += static_cast<uint32_t>(size);
        impl::g_HostRefusedCount++;
        return nullptr;
    }

    impl::g_HostUsed = static_cast<uint32_t>(end);
    return reinterpret_cast<void*>(aligned);
}

// --- module grants ---------------------------------------------------------

// Grants a module its piece, carved down from the top. `request` is the
// module's heapRequest (0 = best effort). `floor` is what the module needs
// merely to exist (its image); best effort still must clear this even if
// it's more than a fair per-module share, or a large-but-otherwise-fine
// module could never load on an emptier-than-fair arena.
inline Grant Acquire(const char* owner, uint32_t request, uint32_t floor,
                     SubArena** out) {
    *out = nullptr;

    if (!Ready()) {
        WIIXL_LOG("Arena: %s refused - arena not ready (base %p, total %u). No module "
                  "can be granted memory until the payload knows where it is.",
                  owner, reinterpret_cast<void*>(Base()), Total());
        return Grant::ArenaNotReady;
    }
    if (impl::g_SubCount >= kMaxModules) {
        WIIXL_LOG("Arena: %s refused - all %u module slots are taken",
                  owner, kMaxModules);
        return Grant::NoSlots;
    }

    const uint32_t free = Free();

    uint32_t grant;
    if (request != 0) {
        if (request > free) {
            WIIXL_LOG("Arena: %s REFUSED granted=0 requested=%u (%u KB), free=%u (%u KB). "
                      "A stated heapRequest is a requirement, so the module is refused "
                      "rather than given less than it asked for.",
                      owner, request, request / 1024u, free, free / 1024u);
            WIIXL_LOG("Arena: %s", kSharedArenaNote);
            return Grant::RequestUnmeetable;
        }
        grant = request;
    } else {
        const uint32_t share = Total() / kMaxModules;
        uint32_t cap = share < kDefaultGrantCap ? share : kDefaultGrantCap;
        if (cap == 0) cap = free;          // an arena smaller than kMaxModules
        if (cap < floor) cap = floor;      // it has to fit before it can be fair
        grant = free < cap ? free : cap;
        if (grant == 0) {
            WIIXL_LOG("Arena: %s REFUSED granted=0 requested=unspecified - nothing free "
                      "to assign", owner);
            WIIXL_LOG("Arena: %s", kSharedArenaNote);
            return Grant::NothingLeft;
        }
    }

    impl::g_ModuleCarved += grant;

    SubArena& s = impl::g_Subs[impl::g_SubCount++];
    s.base = Base() + (Total() - impl::g_ModuleCarved);
    s.size = grant;
    s.used = 0;
    s.stated = (request != 0);
    s.inUse = true;
    impl::CopyOwner(s.owner, owner);

    if (s.stated) {
        WIIXL_LOG("Arena: %s granted=%u (%u KB) requested=%u (%u KB) at %p - stated "
                  "requirement, met exactly; %u KB free",
                  s.owner, s.size, s.size / 1024u, request, request / 1024u,
                  reinterpret_cast<void*>(s.base), Free() / 1024u);
    } else {
        WIIXL_LOG("Arena: %s granted=%u (%u KB) requested=unspecified at %p - best "
                  "effort, Alloc returns null past this; %u KB free",
                  s.owner, s.size, s.size / 1024u,
                  reinterpret_cast<void*>(s.base), Free() / 1024u);
    }

    *out = &s;
    return Grant::Ok;
}

// Allocates within one module's piece. Bounded by that piece, so a
// module that over-allocates starves only itself.
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

// Which sub-arena a mod's allocation is charged to. The loader sets this
// around a module's load and entry; nothing else may.
inline void SetCurrent(SubArena* s) { impl::g_Current = s; }
inline SubArena* Current() { return impl::g_Current; }

// The hook behind wiixl.core's Alloc. Refuses rather than falling through
// to AllocHost when there's no current sub-arena, since that would let a
// mod escape its bound.
inline void* Alloc(uint32_t size, uint32_t align) {
    SubArena* s = impl::g_Current;
    if (!s) {
        WIIXL_LOG("Arena: allocation of %u bytes with no module arena current - "
                  "refused. Only a loaded module allocates here; the host and its "
                  "game modules use AllocHost.", size);
        return nullptr;
    }
    return AllocIn(*s, size, align);
}

// What a module got and what's left, callable during load before
// allocating, so a best-effort module can size a buffer instead of
// allocating until null.
inline uint32_t GrantedTo(const SubArena* s) { return s ? s->size : 0u; }
inline uint32_t UsedIn(const SubArena* s)    { return s ? s->used : 0u; }
inline uint32_t RemainingIn(const SubArena* s) {
    if (!s) return 0;
    return s->size > s->used ? s->size - s->used : 0u;
}

inline void LogState() {
    if (!Ready()) {
        WIIXL_LOG("Arena: not ready - base %p, total %u",
                  reinterpret_cast<void*>(Base()), Total());
        return;
    }
    WIIXL_LOG("Arena: %u KB at %p - host %u KB up, modules %u KB down, %u KB free",
              Total() / 1024u, reinterpret_cast<void*>(Base()),
              impl::g_HostUsed / 1024u, impl::g_ModuleCarved / 1024u, Free() / 1024u);
    if (impl::g_HostProvider) {
        WIIXL_LOG("Arena:   host allocations are redirected to a provider, so the host "
                  "figure above is only the arena's own share");
    }
    for (uint32_t i = 0; i < impl::g_SubCount; ++i) {
        const SubArena& s = impl::g_Subs[i];
        WIIXL_LOG("Arena:   %-16s %u of %u used at %p (%s)",
                  s.owner, s.used, s.size, reinterpret_cast<void*>(s.base),
                  s.stated ? "stated" : "best effort");
    }
}

} // namespace WiiXLaunch::Arena
