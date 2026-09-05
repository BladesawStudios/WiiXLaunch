#pragma once

// WiiXLaunch::Tick - a per-frame callback for loaded modules.
//
// A .wxlm gets its entry called once, at a phase, and nothing after. That is
// enough for a mod that installs hooks and gets out of the way, and useless for
// one that polls - an HTTP server, a watcher, anything that has to look at the
// world repeatedly. This is the repeating call.
//
// ---------------------------------------------------------------------------
// BASE HAS NO CONCEPT OF A FRAME, so it does not invent one.
//
// A frame is a graphics idea and graphics is game-specific; base does not know
// GX2 exists. So the SOURCE is nominated, exactly as the load point is: a game
// module says "here is where a frame happens" and calls RunAll from it, and
// base owns the registry, the ordering and the attribution.
//
// A host with no game module therefore has no tick. That is a legitimate state
// and it is SAID rather than left to be inferred from silence - LogState
// reports registered ticks with no source as a warning, because a mod that
// registered a callback which will never run is a mod that does nothing, and
// "does nothing quietly" is the hardest kind of broken to report.
//
// ---------------------------------------------------------------------------
// A HANG IN A TICK MUST HAVE AN OWNER.
//
// A tick runs every frame. A mod that crashes or spins in one takes the game
// with it, and the report that reaches you is "my game freezes with these mods
// installed" with nothing narrowing it down. Registration order is not enough:
// the log's last line is whatever was printed before the freeze, which is
// usually from something else entirely.
//
// So the dispatcher writes who it is about to call into g_InFlight BEFORE
// calling, and clears it after. If the game stops, that field still names the
// module that was running. It carries a magic word so a memory dump or
// tools/ring_log_reader can find it without a symbol table, and a sequence
// counter that stops advancing - which distinguishes a hang inside a tick from
// a game that stopped calling RunAll at all. Those are different problems and
// they look identical from outside.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/mod_context.hpp>
#include <wiixlaunch/loader/arena.hpp>

#include <cstdint>

namespace WiiXLaunch::Tick {

constexpr uint32_t kMaxTicks = 16;
constexpr uint32_t kOwnerLen = 17;

// How many times each module's first ticks are logged. Enough to prove in a
// boot that the callback is really running, few enough not to flood a log that
// something else needs to be readable in.
constexpr uint32_t kAnnounceFirst = 3;

using TickFn = void (*)();

enum class Register : uint32_t {
    Ok = 0,
    NoModule,           // not inside a module entry - nothing to attribute it to
    NullCallback,
    NoSlots,            // kMaxTicks already registered
    AlreadyRegistered,  // this module already has a tick
};

inline const char* RegisterName(Register r) {
    switch (r) {
        case Register::Ok:                return "OK";
        case Register::NoModule:          return "NO-MODULE";
        case Register::NullCallback:      return "NULL-CALLBACK";
        case Register::NoSlots:           return "NO-SLOTS";
        case Register::AlreadyRegistered: return "ALREADY-REGISTERED";
    }
    return "?";
}

struct Entry {
    TickFn   fn;
    char     owner[kOwnerLen];
    uint32_t calls;
    Arena::SubArena* arena;   // charged while this tick runs
    bool     inUse;
};

// What is running right now, findable without symbols.
//
// Deliberately a plain struct with a magic word rather than a set of separate
// globals: a hang is diagnosed from a memory dump or a log reader, and one
// contiguous, greppable record is worth more than tidiness.
struct InFlightRecord {
    uint32_t magic;              // kInFlightMagic
    uint32_t sequence;           // ++ on every dispatch; frozen means frozen
    uint32_t depth;              // 1 while a tick runs; >1 means re-entered
    char     owner[kOwnerLen];   // who is running, or "" between ticks
    char     pad[3];
};

constexpr uint32_t kInFlightMagic = 0x5758544Bu;   // 'WXTK'

namespace impl {

inline Entry g_Ticks[kMaxTicks];
inline uint32_t g_Count = 0;

inline InFlightRecord g_InFlight = { kInFlightMagic, 0, 0, {0}, {0} };

// Who supplies the frame, and whether anyone does.
inline const char* g_SourceName = nullptr;
inline uint32_t g_Dispatches = 0;
inline bool g_WarnedNoSource = false;

inline void CopyOwner(char* dst, const char* src) {
    uint32_t i = 0;
    for (; i + 1 < kOwnerLen && src && src[i]; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

inline bool SameOwner(const char* a, const char* b) {
    for (uint32_t i = 0; i < kOwnerLen; ++i) {
        if (a[i] != b[i]) return false;
        if (a[i] == '\0') return true;
    }
    return true;
}

} // namespace impl

inline void ResetForTest() {
    impl::g_Count = 0;
    impl::g_Dispatches = 0;
    impl::g_SourceName = nullptr;
    impl::g_WarnedNoSource = false;
    impl::g_InFlight.sequence = 0;
    impl::g_InFlight.depth = 0;
    impl::g_InFlight.owner[0] = '\0';
}

// A game module says where a frame happens. Base cannot know this and does not
// guess - see the header comment.
inline void NominateSource(const char* what) {
    impl::g_SourceName = what;
    WIIXL_LOG("Tick: frame source nominated by the game module: %s",
              what ? what : "(unnamed)");
}

inline const char* SourceName() { return impl::g_SourceName; }
inline uint32_t Count() { return impl::g_Count; }
inline uint32_t Dispatches() { return impl::g_Dispatches; }
inline const InFlightRecord& InFlight() { return impl::g_InFlight; }

inline const Entry* At(uint32_t i) {
    return i < impl::g_Count ? &impl::g_Ticks[i] : nullptr;
}

// Registers the CURRENT module's tick.
//
// The owner is not a parameter. It comes from ModContext - whichever module the
// host is running - so a mod cannot register a callback under another mod's
// name and make a hang report point at the wrong one. Same rule as hook
// attribution and probe tags; see the ABI discipline in loader/surface.hpp.
inline Register Add(TickFn fn) {
    const char* owner = ModContext::Current();
    if (!owner || owner[0] == '\0') {
        WIIXL_LOG("Tick: refused %s - a tick is registered by a module, and none "
                  "is running", RegisterName(Register::NoModule));
        return Register::NoModule;
    }
    if (!fn) {
        WIIXL_LOG("Tick: %s refused %s", owner, RegisterName(Register::NullCallback));
        return Register::NullCallback;
    }
    for (uint32_t i = 0; i < impl::g_Count; ++i) {
        if (impl::SameOwner(impl::g_Ticks[i].owner, owner)) {
            WIIXL_LOG("Tick: %s refused %s - one tick per module; call what you need "
                      "from the one you have", owner,
                      RegisterName(Register::AlreadyRegistered));
            return Register::AlreadyRegistered;
        }
    }
    if (impl::g_Count >= kMaxTicks) {
        WIIXL_LOG("Tick: %s refused %s - all %u slots are taken", owner,
                  RegisterName(Register::NoSlots), kMaxTicks);
        return Register::NoSlots;
    }

    Entry& e = impl::g_Ticks[impl::g_Count++];
    e.fn = fn;
    e.calls = 0;
    e.arena = Arena::Current();
    e.inUse = true;
    impl::CopyOwner(e.owner, owner);

    WIIXL_LOG("Tick: %s registered a per-frame callback (%u of %u slots used)",
              e.owner, impl::g_Count, kMaxTicks);
    return Register::Ok;
}

// Calls every registered tick once, in registration order.
//
// Called by whatever the game module nominated. Registration order is load
// order, which is lexical filename order - the same lever the user already has
// over hook priority, so there is one answer to "which mod goes first" rather
// than two.
inline void RunAll() {
    impl::g_Dispatches++;

    for (uint32_t i = 0; i < impl::g_Count; ++i) {
        Entry& e = impl::g_Ticks[i];
        if (!e.inUse || !e.fn) continue;

        // BEFORE the call, so a hang leaves this behind pointing at the culprit.
        impl::g_InFlight.sequence++;
        impl::g_InFlight.depth++;
        impl::CopyOwner(impl::g_InFlight.owner, e.owner);

        // A tick is this module running, so its identity and its arena apply -
        // otherwise a file read or an allocation from inside a tick would be
        // attributed to whoever ran last, or to nobody.
        Arena::SetCurrent(e.arena);
        ModContext::SetCurrent(e.owner);

        e.fn();

        ModContext::SetCurrent(nullptr);
        Arena::SetCurrent(nullptr);

        impl::g_InFlight.depth--;
        impl::g_InFlight.owner[0] = '\0';

        e.calls++;
        if (e.calls <= kAnnounceFirst) {
            WIIXL_LOG("Tick: %s ran (call %u)", e.owner, e.calls);
        }
    }
}

// Reported at the load point, after modules have registered.
inline void LogState() {
    if (impl::g_Count == 0) {
        WIIXL_LOG("Tick: no module registered a per-frame callback");
        return;
    }

    WIIXL_LOG("Tick: %u module(s) registered a per-frame callback, source: %s",
              impl::g_Count, impl::g_SourceName ? impl::g_SourceName : "NONE");

    for (uint32_t i = 0; i < impl::g_Count; ++i) {
        WIIXL_LOG("Tick:   %u. %s", i + 1, impl::g_Ticks[i].owner);
    }

    // A registered tick with nothing to drive it is a mod that will silently do
    // nothing at all, which is the hardest kind of broken to report. Said here
    // rather than left to be noticed.
    if (!impl::g_SourceName) {
        WIIXL_LOG("Tick: NO FRAME SOURCE - these callbacks will never run. Base has "
                  "no concept of a frame; a game module has to nominate one.");
        WIIXL_LOG("Tick:   a host with no game module installed is expected to look "
                  "exactly like this");
    }
}

} // namespace WiiXLaunch::Tick
