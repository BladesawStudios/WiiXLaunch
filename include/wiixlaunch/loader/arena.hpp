#pragma once

// WiiXLaunch::Arena - the single memory owner. Everything in this payload that
// allocates does it here, and modules get bounded pieces.
//
// Before this, every allocation came from one bump pointer running from the end
// of the payload to the end of Cemu's code-cave area, and whoever asked first
// got whatever was left. With one module that is merely untidy. With several it
// is a bug reported as somebody else's: a mod that over-allocates starves the
// mods loaded after it, and the failure surfaces in them.
//
// TWO ENDS, NOT A FIXED RESERVE.
//
//   [ host allocations ->                          <- module grants ]
//   ^ Base()                                        Base()+Total() ^
//
// The host allocates upward from the base; module grants are carved downward
// from the top; a grant fails if it would cross the host's high-water mark, and
// a host allocation fails if it would cross into carved territory.
//
// The first version of this file reserved a fixed 64 KB for the host and gave
// modules the rest. That was wrong, and wrong in a way worth recording. The
// HOST is not just the framework: it is the framework plus whatever game
// modules the project installed, and wiixlaunch-botw's GX2 layer allocates font
// sheets and render targets in MEGABYTES. A fixed reserve either starves the
// host or has to be guessed so large it defeats the point. Two ends need no
// guess - each side takes what it takes, and they fail when they meet.
//
// WHICH SIDE SOMETHING IS ON. A game module (vendor/wiixlaunch-*) is compiled
// INTO the payload; its allocations are host allocations and use AllocHost. A
// mod (.wxlm) is loaded at runtime, gets a bounded grant, and reaches memory
// only through wiixl.core's Alloc. That distinction is what "single memory
// owner" actually buys: not that there is one bump pointer, but that a mod
// cannot spend memory the host and the other mods were counting on.
//
// THE SIZE IS NOT A CONSTANT AND MUST NOT BE TREATED AS ONE. On Cemu this is
// the tail of a code cave whose start depends on how many other graphic packs
// the user has enabled - 3959, 3963, 3934 and 3930 KB across four measured
// boots of the same build. Nothing may reserve a fixed number of bytes;
// everything reads the size at runtime and logs what it got.
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
//                     to check for null - and can ask what it got, before
//                     allocating, through wiixl.core's HeapGranted.
//
// The distinction is opt-in on the module's side: stating a number means being
// held to it. Both paths are written out in docs/loader.md, with the log format
// shown literally, and both are exercised by tools/loader_fuzz.
// ---------------------------------------------------------------------------

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>

#include <cstdint>
#include <cstddef>

#if WIIXL_CEMU
#include <wiixl_cemu_backend.hpp>
#endif

namespace WiiXLaunch::Arena {

// What a module gets when it states no requirement. Not a promise - it is
// whatever can be spared, capped at this - but it keeps one silent mod from
// taking everything on a first-come basis.
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

// One module's bounded piece. Never freed, like everything else here - a mod is
// loaded once and lives for the session.
struct SubArena {
    uintptr_t base;
    uint32_t  size;      // granted, not requested
    uint32_t  used;
    bool      stated;    // did the module state a requirement?
    char      owner[17];
    bool      inUse;
};

// Where the HOST's own allocations go when they are not coming from the arena.
// This is what wiixlaunch/mem.hpp installs to move host allocation onto a
// coreinit or game heap, which can be far larger than the code cave.
//
// MODULE GRANTS ARE NEVER REDIRECTED, and that is deliberate. A module's grant
// holds its relocated image, which gets executed; the code cave is
// known-executable because Cemu emits code into it, and no other heap in this
// project has had that established. A provider therefore moves the host's own
// allocations and leaves module grants exactly where they are.
using HostProvider = void* (*)(size_t size, size_t align);

namespace impl {

inline SubArena g_Subs[kMaxModules];
inline uint32_t g_SubCount = 0;

// The two ends. g_HostUsed grows up from Base(); g_ModuleCarved grows down from
// Base() + Total(). They may never cross.
inline uint32_t g_HostUsed = 0;
inline uint32_t g_ModuleCarved = 0;

// The sub-arena allocations are currently charged to. Set around a module's
// load and around its entry, then cleared - so wiixl.core's Alloc, which has no
// place to carry a module identity, charges the right one.
inline SubArena* g_Current = nullptr;

inline HostProvider g_HostProvider = nullptr;

// Refusals, so a caller that got null can report that the arena is the reason.
// The backend used to own these; it cannot log, and every caller had to
// remember to ask. They live here now, next to the thing that sets them.
inline uint32_t g_HostRefusedBytes = 0;
inline uint32_t g_HostRefusedCount = 0;

// An explicitly supplied reservation, for a host that is not the Cemu code
// cave. Same reasoning as the loader's memory hooks: the carving logic is
// ordinary arithmetic and testing it should not need a console. Without this
// the arena could only exist on Cemu, and tools/loader_fuzz would be exercising
// a loader whose allocator always fails.
inline uintptr_t g_ExplicitBase = 0;
inline uint32_t g_ExplicitTotal = 0;
inline bool g_HasExplicit = false;

// THE WRITE ALIAS, and it exists because Horizon will not let one address be
// both writable and executable.
//
// On Cemu and Wii U the arena is ordinary memory: the address a module is
// placed at is the address it is written through and the address it executes
// from. A module image is CODE, so on Switch it has to live somewhere
// executable, and the only executable region a subsdk has is its own .text -
// which is not writable. exl::util::Jit maps a second, writable view of the
// same pages; the loader writes through that view and the module runs from the
// first one.
//
// A delta rather than a second base, so it is zero - and therefore free and
// invisible - on every platform that does not need it.
inline uintptr_t g_WriteDelta = 0;

inline void CopyOwner(char* dst, const char* src) {
    uint32_t i = 0;
    for (; i < 16 && src && src[i]; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

} // namespace impl

// Supplies the reservation directly, and resets everything taken from it.
// Cemu does not need this - it reads the code cave - but a host test does.
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

// Forgets every grant and every host allocation, keeping the reservation. For a
// test that runs many loads; nothing in a real host calls it, because nothing
// here is ever freed.
inline void ResetGrants() {
    impl::g_HostUsed = 0;
    impl::g_ModuleCarved = 0;
    impl::g_SubCount = 0;
    impl::g_Current = nullptr;
    impl::g_HostRefusedBytes = 0;
    impl::g_HostRefusedCount = 0;
}

// The reservation, read at runtime because it is not a constant.
//
// This arithmetic lives HERE, not in the backend. The backend used to publish a
// CemuHeapLimit() that the arena, the loader, the GX2 layer and the BotW heap
// shim each consulted independently, and each then did its own bookkeeping
// against it. That is precisely how two things end up believing different
// amounts of memory are available - which was the original overlap bug. There
// is one owner now, and this is it.
inline uintptr_t Base() {
    if (impl::g_HasExplicit) return impl::g_ExplicitBase;
#if WIIXL_CEMU
    return Backend::CemuHeapBase();
#else
    // Switch and Wii U supply theirs through SetReservation - there is no
    // region to read here the way the Cemu code cave can be read.
    return 0;
#endif
}

inline uint32_t Total() {
    if (impl::g_HasExplicit) return impl::g_ExplicitTotal;
#if WIIXL_CEMU
    const uintptr_t base = Backend::CemuHeapBase();
    // Past the wall, or the base is not known yet: no memory, rather than a
    // guess. 0x01C00000 is the end of the cave AREA and the gap above it is
    // unmapped - see the memory map in wiixl_cemu_backend.hpp.
    if (base == 0 || base >= Backend::kCemuCodeCaveEnd) return 0;
    return static_cast<uint32_t>(Backend::kCemuCodeCaveEnd - base);
#else
    return 0;
#endif
}

inline uint32_t HostUsed()     { return impl::g_HostUsed; }
inline uint32_t ModuleCarved() { return impl::g_ModuleCarved; }

// What is still unclaimed between the two ends. Either side may take it.
inline uint32_t Free() {
    const uint32_t total = Total();
    const uint64_t taken =
        static_cast<uint64_t>(impl::g_HostUsed) + impl::g_ModuleCarved;
    return taken < total ? static_cast<uint32_t>(total - taken) : 0u;
}

inline bool Ready() { return Base() != 0 && Total() != 0; }

// Where to WRITE the reservation, when that is not where it lives.
//
// Call after SetReservation - the delta is computed against Base(). Passing the
// base itself, or never calling this at all, means "written where it lives",
// which is what Cemu and Wii U do.
inline void SetWriteAlias(uintptr_t writableBase) {
    impl::g_WriteDelta = writableBase - Base();
}

// The address to write `p` through. Identity unless SetWriteAlias said
// otherwise. A pointer the loader is about to STORE must still be the
// executable one - only the store itself is redirected.
inline void* Writable(void* p) {
    if (!p || impl::g_WriteDelta == 0) return p;
    return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(p) +
                                   impl::g_WriteDelta);
}

// True once anything has been refused, host side. A caller that got null can
// say why without having to reason about it.
inline bool HostExhausted() { return impl::g_HostRefusedCount != 0; }
inline uint32_t HostRefusedBytes() { return impl::g_HostRefusedBytes; }
inline uint32_t HostRefusedCount() { return impl::g_HostRefusedCount; }

// --- host allocation -------------------------------------------------------

// Moves the host's own allocations elsewhere - see HostProvider. Null (the
// default) means the arena itself. Module grants are unaffected either way.
//
// Set before the first allocation. Allocations already handed out by the
// previous provider stay valid - nothing here is ever freed - but mixing the
// two mid-run means HostUsed() only describes the arena's share.
inline void SetHostProvider(HostProvider p) { impl::g_HostProvider = p; }
inline HostProvider GetHostProvider() { return impl::g_HostProvider; }

// The host's own allocator: the framework and every game module compiled into
// this payload. A bump pointer, bounded, never freed.
//
// The bound is where module grants begin, NOT the end of the reservation, so a
// host that over-allocates cannot silently eat memory a module was promised. It
// gets null instead, which is the same answer a module gets, for the same
// reason.
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
    // 64-bit throughout: size is a size_t and the sum must not be able to wrap
    // back under the wall. Same shape as the bssSize wrap the fuzzer found.
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

// Grants a module its piece, carved down from the top.
//
// `request` is the module's heapRequest: non-zero means a stated requirement
// the host must meet exactly or refuse; zero means best effort.
inline Grant Acquire(const char* owner, uint32_t request, SubArena** out) {
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
        // Stated requirement: meet it exactly or refuse. Rounding down to "what
        // we could spare" would hand the module a promise the host did not
        // keep, which is the failure this contract exists to prevent.
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
        // Best effort: whatever is sensible, and the module must handle null.
        grant = free < kDefaultGrant ? free : kDefaultGrant;
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

    // granted-vs-requested on EVERY module, always, including the best-effort
    // path where "requested" is the interesting half of the answer. When a mod
    // misbehaves in-game this is the first line worth having, and it costs
    // nothing to print. The exact wording is reproduced in docs/loader.md so a
    // bug report can be matched against it.
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

// Which sub-arena a mod's allocation is charged to. The loader sets this around
// a module's load and around its entry; nothing else may.
inline void SetCurrent(SubArena* s) { impl::g_Current = s; }
inline SubArena* Current() { return impl::g_Current; }

// The hook behind wiixl.core's Alloc - a MOD's allocation.
//
// With no current sub-arena this refuses rather than falling through to
// AllocHost. Falling through is how a mod would quietly escape its bound and
// take memory the host and every later module were counting on, which is the
// entire thing this file exists to stop.
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

// The whole picture, for the log at the load point.
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
