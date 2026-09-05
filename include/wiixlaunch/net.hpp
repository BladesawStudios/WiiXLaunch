#pragma once

// WiiXLaunch::Net - sockets that belong to somebody.
//
// This is the first surface where a mod holds a resource with a LIFETIME. Hooks
// and patches are install-once and the host owns the result forever; a socket is
// different. A mod that opens one and never closes it holds a coreinit handle
// for the whole session, and "the game runs out of sockets after an hour" is
// exactly the kind of report with no owner attached that everything else in this
// framework exists to prevent.
//
// So the host tracks them, and hands out its own handles rather than raw
// descriptors.
//
// ---------------------------------------------------------------------------
// WHY NOT RAW DESCRIPTORS.
//
// Not for tidiness. A raw descriptor makes USE-AFTER-CLOSE into silent
// cross-mod corruption:
//
//   mod A closes fd 5
//   mod B opens a socket and the OS hands it fd 5 - descriptors are reused,
//     that is what they do
//   mod A, holding a stale 5, sends its response down mod B's connection
//
// Nothing crashes. B's client gets A's data, intermittently, and the bug looks
// like it lives in the host. A handle carries a GENERATION alongside the slot
// index, the generation moves every time the slot is reused, and A's stale
// handle comes back StaleHandle instead of reaching B's socket. The same
// argument as generation-counted actor handles, for the same reason.
//
// ---------------------------------------------------------------------------
// WHY PER-MODULE QUOTAS. This is fault isolation, not accounting.
//
// Without per-module attribution the only cap that can exist is a global one,
// and a global cap means the mod that leaks exhausts the pool while the mods
// that get REFUSED are whichever ones happened to ask next. The failure lands
// on innocent modules and names none of them.
//
// With a per-module cap the leak is contained to its owner: the leaking mod hits
// kMaxPerModule, is refused BY NAME, and every other mod keeps working.
//
// ---------------------------------------------------------------------------
// WHAT THIS DOES NOT BUY, said plainly rather than left to be discovered.
//
// Tracking buys attribution and containment. It does NOT buy recovery, and
// there are three cases that have to be told apart:
//
//   A leak within a session - CONTAINED. The quota stops it at kMaxPerModule
//   and LogState names who is holding what.
//
//   A hang inside a tick - ATTRIBUTION ONLY. Nothing reclaims anything, because
//   nothing runs; the game is frozen. What you get is Tick's in-flight record
//   naming the module, and this module's socket count next to it. That is the
//   honest limit, and no amount of tracking changes it.
//
//   Shutdown - THE PROCESS OWNS THE HANDLES. There is no module-unload path in
//   this system, so a host-side "close everything on the way out" would be a
//   function with no caller, which this project treats as worse than nothing.
//   When unload or a tick watchdog exists, CloseAllFor is what it will call;
//   until then the only caller is a module closing its own sockets.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/mod_context.hpp>
#include <wiixlaunch/net_transport.hpp>

#include <cstdint>

namespace WiiXLaunch::Net {

// The host's whole budget, and one module's share of it.
//
// 24 and 8 are chosen against the only real consumer: an HTTP server needs one
// listener plus its concurrent connections, which is 5 for the API server. 8
// leaves a module room to be a server and something else; 24 lets three such
// modules coexist and still refuses a runaway before nsysnet's own limits turn
// into game-wide failures.
constexpr uint32_t kMaxSockets    = 24;
constexpr uint32_t kMaxPerModule  = 8;
constexpr uint32_t kOwnerLen      = 17;

// A host handle, never a descriptor. 0 is never valid, so a zeroed struct in a
// mod's .bss cannot accidentally name a socket.
//
//   bits  0..15   slot index + 1
//   bits 16..31   generation, incremented every time the slot is reused
//
// The generation is 16 bits, so a slot would have to be recycled 65536 times
// before a stale handle could alias a live one. That is a BOUND, not an
// impossibility - worth saying, because a comment claiming impossibility here
// would be false.
using Handle = uint32_t;
constexpr Handle kInvalidHandle = 0;

enum class Result : uint32_t {
    Ok = 0,
    Unsupported,    // this platform has no sockets at all - Switch
    Unavailable,    // sockets exist here but could not be reached (no nsysnet)
    NoModule,       // not inside a module; nothing to attribute the socket to
    NoSlots,        // the host's whole table is full
    ModuleQuota,    // this module is at kMaxPerModule - ITS problem, not others'
    BadHandle,      // never named a slot
    StaleHandle,    // named a slot whose socket has since been closed
    NotOwner,       // a live socket, held by a different module
    BadArgument,
    PlatformError,  // the transport refused
};

inline const char* ResultName(Result r) {
    switch (r) {
        case Result::Ok:            return "OK";
        case Result::Unsupported:   return "UNSUPPORTED";
        case Result::Unavailable:   return "UNAVAILABLE";
        case Result::NoModule:      return "NO-MODULE";
        case Result::NoSlots:       return "NO-SLOTS";
        case Result::ModuleQuota:   return "MODULE-QUOTA";
        case Result::BadHandle:     return "BAD-HANDLE";
        case Result::StaleHandle:   return "STALE-HANDLE";
        case Result::NotOwner:      return "NOT-OWNER";
        case Result::BadArgument:   return "BAD-ARGUMENT";
        case Result::PlatformError: return "PLATFORM-ERROR";
    }
    return "?";
}

// Byte results for Recv/Send, which have to return a COUNT and still be able to
// say what went wrong. Non-negative is a byte count; every negative value is a
// distinct named reason, so a mod can tell "try again next frame" from "your
// handle is stale" without reading the host log.
constexpr int32_t kIoWouldBlock   = -1;   // or a transient error; retry
constexpr int32_t kIoBadHandle    = -2;
constexpr int32_t kIoStaleHandle  = -3;
constexpr int32_t kIoNotOwner     = -4;
constexpr int32_t kIoNoModule     = -5;
constexpr int32_t kIoUnavailable  = -6;
constexpr int32_t kIoBadArgument  = -7;

struct Slot {
    int      fd;
    uint16_t generation;
    bool     inUse;
    bool     listener;          // opened a listen() - only for the state report
    char     owner[kOwnerLen];
    uint32_t bytesIn;
    uint32_t bytesOut;
};

namespace impl {

inline Slot g_Slots[kMaxSockets];
inline uint32_t g_Opened = 0;      // lifetime totals, for the state report
inline uint32_t g_Closed = 0;

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

inline Handle MakeHandle(uint32_t index, uint16_t generation) {
    return (static_cast<uint32_t>(generation) << 16) | (index + 1u);
}

inline bool SplitHandle(Handle h, uint32_t* index, uint16_t* generation) {
    const uint32_t low = h & 0xFFFFu;
    if (low == 0 || low > kMaxSockets) return false;
    *index = low - 1u;
    *generation = static_cast<uint16_t>(h >> 16);
    return true;
}

} // namespace impl

inline void ResetForTest() {
    for (uint32_t i = 0; i < kMaxSockets; ++i) {
        impl::g_Slots[i].fd = Transport::kInvalidFd;
        impl::g_Slots[i].generation = 0;
        impl::g_Slots[i].inUse = false;
        impl::g_Slots[i].listener = false;
        impl::g_Slots[i].owner[0] = '\0';
        impl::g_Slots[i].bytesIn = 0;
        impl::g_Slots[i].bytesOut = 0;
    }
    impl::g_Opened = 0;
    impl::g_Closed = 0;
    Transport::ResetForTest();
}

// How many sockets are open in total, and how many one module holds.
inline uint32_t Count() {
    uint32_t n = 0;
    for (uint32_t i = 0; i < kMaxSockets; ++i) if (impl::g_Slots[i].inUse) ++n;
    return n;
}

inline uint32_t CountFor(const char* owner) {
    if (!owner) return 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < kMaxSockets; ++i) {
        if (impl::g_Slots[i].inUse && impl::SameOwner(impl::g_Slots[i].owner, owner)) ++n;
    }
    return n;
}

// Resolves a handle for the CURRENT module.
//
// Three distinct refusals, and they must stay distinct: BadHandle is "that was
// never a socket", StaleHandle is "it was yours and you closed it", NotOwner is
// "it is live and it is somebody else's". Collapsing them into one failure would
// make the use-after-close case indistinguishable from a typo.
inline Result Resolve(Handle h, Slot** out) {
    const char* owner = ModContext::Current();
    if (!owner || owner[0] == '\0') return Result::NoModule;

    uint32_t index = 0;
    uint16_t generation = 0;
    if (!impl::SplitHandle(h, &index, &generation)) return Result::BadHandle;

    Slot& s = impl::g_Slots[index];
    if (!s.inUse || s.generation != generation) return Result::StaleHandle;
    if (!impl::SameOwner(s.owner, owner)) return Result::NotOwner;

    *out = &s;
    return Result::Ok;
}

// Opens a TCP socket, charged to the calling module.
//
// The owner is not a parameter, for the same reason it is not one in Hooks or
// Tick: identity is what the host observes, never what a module claims.
inline Result Open(Handle* outHandle) {
    if (!outHandle) return Result::BadArgument;
    *outHandle = kInvalidHandle;

    if constexpr (!Transport::Supported) return Result::Unsupported;

    const char* owner = ModContext::Current();
    if (!owner || owner[0] == '\0') {
        WIIXL_LOG("Net: refused %s - a socket is held by a module, and none is running",
                  ResultName(Result::NoModule));
        return Result::NoModule;
    }

    if (!Transport::Available() || !Transport::Init()) {
        WIIXL_LOG("Net: %s got %s - the socket library is not reachable on this host",
                  owner, ResultName(Result::Unavailable));
        return Result::Unavailable;
    }

    if (CountFor(owner) >= kMaxPerModule) {
        WIIXL_LOG("Net: %s refused %s - it already holds %u sockets, its whole share. "
                  "Other modules are unaffected.",
                  owner, ResultName(Result::ModuleQuota), kMaxPerModule);
        return Result::ModuleQuota;
    }

    uint32_t index = kMaxSockets;
    for (uint32_t i = 0; i < kMaxSockets; ++i) {
        if (!impl::g_Slots[i].inUse) { index = i; break; }
    }
    if (index == kMaxSockets) {
        WIIXL_LOG("Net: %s refused %s - all %u host slots are held",
                  owner, ResultName(Result::NoSlots), kMaxSockets);
        return Result::NoSlots;
    }

    const int fd = Transport::Open();
    if (fd < 0) {
        WIIXL_LOG("Net: %s got %s - the platform would not open a socket, error %d",
                  owner, ResultName(Result::PlatformError), Transport::LastError());
        return Result::PlatformError;
    }

    Slot& s = impl::g_Slots[index];
    s.fd = fd;
    s.inUse = true;
    s.listener = false;
    s.bytesIn = 0;
    s.bytesOut = 0;
    // Moved on every ACQUISITION, so a handle from the previous life of this
    // slot can never match the current one.
    ++s.generation;
    impl::CopyOwner(s.owner, owner);
    ++impl::g_Opened;

    *outHandle = impl::MakeHandle(index, s.generation);
    return Result::Ok;
}

// Adopts an already-open descriptor into a slot for `owner`. Internal: the only
// caller is Accept, whose descriptor comes from the platform, not from a mod.
namespace impl {

inline Result Adopt(int fd, const char* owner, Handle* outHandle) {
    if (CountFor(owner) >= kMaxPerModule) return Result::ModuleQuota;

    uint32_t index = kMaxSockets;
    for (uint32_t i = 0; i < kMaxSockets; ++i) {
        if (!g_Slots[i].inUse) { index = i; break; }
    }
    if (index == kMaxSockets) return Result::NoSlots;

    Slot& s = g_Slots[index];
    s.fd = fd;
    s.inUse = true;
    s.listener = false;
    s.bytesIn = 0;
    s.bytesOut = 0;
    ++s.generation;
    CopyOwner(s.owner, owner);
    ++g_Opened;

    *outHandle = MakeHandle(index, s.generation);
    return Result::Ok;
}

} // namespace impl

inline Result SetNonBlocking(Handle h) {
    Slot* s = nullptr;
    const Result r = Resolve(h, &s);
    if (r != Result::Ok) return r;
    return Transport::SetNonBlocking(s->fd) ? Result::Ok : Result::PlatformError;
}

inline Result SetReuseAddr(Handle h) {
    Slot* s = nullptr;
    const Result r = Resolve(h, &s);
    if (r != Result::Ok) return r;
    return Transport::SetReuseAddr(s->fd) ? Result::Ok : Result::PlatformError;
}

inline Result Bind(Handle h, uint16_t port) {
    Slot* s = nullptr;
    const Result r = Resolve(h, &s);
    if (r != Result::Ok) return r;
    if (Transport::Bind(s->fd, port)) return Result::Ok;

    // The one refusal that is almost never a bug in the mod. A port already in
    // use is the ordinary case, and PLATFORM-ERROR on its own sent the reader
    // looking at the wrong thing entirely.
    WIIXL_LOG("Net: %s could not bind port %u - platform error %d. A port already "
              "held by another process is the usual cause.",
              s->owner, port, Transport::LastError());
    return Result::PlatformError;
}

inline Result Listen(Handle h, uint32_t backlog) {
    Slot* s = nullptr;
    const Result r = Resolve(h, &s);
    if (r != Result::Ok) return r;
    if (!Transport::Listen(s->fd, static_cast<int32_t>(backlog))) {
        WIIXL_LOG("Net: %s could not listen - platform error %d",
                  s->owner, Transport::LastError());
        return Result::PlatformError;
    }
    s->listener = true;
    return Result::Ok;
}

// Takes a pending connection, charged to the SAME module as the listener.
//
// This is the real leak vector - a server that accepts every frame and forgets
// to close - so an accepted socket goes through the quota exactly like an opened
// one. Returns Ok with *outHandle set, or PlatformError when nothing is pending,
// which on a non-blocking listener is the ordinary case every frame.
inline Result Accept(Handle listener, Handle* outHandle) {
    if (!outHandle) return Result::BadArgument;
    *outHandle = kInvalidHandle;

    Slot* s = nullptr;
    const Result r = Resolve(listener, &s);
    if (r != Result::Ok) return r;

    const int fd = Transport::Accept(s->fd);
    if (fd < 0) return Result::PlatformError;

    const Result adopted = impl::Adopt(fd, s->owner, outHandle);
    if (adopted != Result::Ok) {
        // Refused for a quota or slot reason, so the host closes what it just
        // accepted rather than leaking a descriptor it declined to track. The
        // alternative - hand it over untracked - is precisely the failure this
        // whole layer exists to prevent.
        Transport::Close(fd);
        WIIXL_LOG("Net: %s accepted a connection and then dropped it - %s. The "
                  "descriptor was closed rather than left untracked.",
                  s->owner, ResultName(adopted));
    }
    return adopted;
}

inline int32_t IoError(Result r) {
    switch (r) {
        case Result::BadHandle:   return kIoBadHandle;
        case Result::StaleHandle: return kIoStaleHandle;
        case Result::NotOwner:    return kIoNotOwner;
        case Result::NoModule:    return kIoNoModule;
        case Result::BadArgument: return kIoBadArgument;
        default:                  return kIoUnavailable;
    }
}

// > 0 bytes, 0 peer closed cleanly, < 0 one of the named kIo* reasons.
inline int32_t Recv(Handle h, void* buffer, uint32_t maxSize) {
    if (!buffer || maxSize == 0) return kIoBadArgument;

    Slot* s = nullptr;
    const Result r = Resolve(h, &s);
    if (r != Result::Ok) return IoError(r);

    const int n = Transport::Recv(s->fd, buffer, maxSize);
    if (n < 0) return kIoWouldBlock;
    s->bytesIn += static_cast<uint32_t>(n);
    return static_cast<int32_t>(n);
}

// >= 0 bytes written, possibly partial. < 0 one of the named kIo* reasons.
inline int32_t Send(Handle h, const void* buffer, uint32_t size) {
    if (!buffer || size == 0) return kIoBadArgument;

    Slot* s = nullptr;
    const Result r = Resolve(h, &s);
    if (r != Result::Ok) return IoError(r);

    const int n = Transport::Send(s->fd, buffer, size);
    if (n < 0) return kIoWouldBlock;
    s->bytesOut += static_cast<uint32_t>(n);
    return static_cast<int32_t>(n);
}

inline uint32_t LocalIp(Handle h) {
    Slot* s = nullptr;
    if (Resolve(h, &s) != Result::Ok) return 0;
    return Transport::LocalIp(s->fd);
}

// Releases the slot. The generation has already moved by the time anything can
// ask again, so the caller's handle is StaleHandle from here on - including for
// the module that owned it, which is the point.
inline Result Close(Handle h) {
    Slot* s = nullptr;
    const Result r = Resolve(h, &s);
    if (r != Result::Ok) return r;

    Transport::Close(s->fd);
    s->fd = Transport::kInvalidFd;
    s->inUse = false;
    s->listener = false;
    s->owner[0] = '\0';
    ++impl::g_Closed;
    return Result::Ok;
}

// Everything one module holds. Returns how many were closed.
//
// The only caller today is a module resetting itself through wiixl.net. When
// module unload or a tick watchdog exists, this is what they will call - and
// until one of them does, this file does not pretend the host reclaims anything
// on its own.
inline uint32_t CloseAllFor(const char* owner) {
    if (!owner || owner[0] == '\0') return 0;

    uint32_t closed = 0;
    for (uint32_t i = 0; i < kMaxSockets; ++i) {
        Slot& s = impl::g_Slots[i];
        if (!s.inUse || !impl::SameOwner(s.owner, owner)) continue;
        Transport::Close(s.fd);
        s.fd = Transport::kInvalidFd;
        s.inUse = false;
        s.listener = false;
        s.owner[0] = '\0';
        ++impl::g_Closed;
        ++closed;
    }
    return closed;
}

// Who holds what. Printed at the load point and worth printing again next to a
// hang report: "module X is in flight" plus "module X holds 8 sockets" is a much
// more specific starting point than either line alone.
inline void LogState() {
    if constexpr (!Transport::Supported) {
        WIIXL_LOG("Net: not supported on this platform - wiixl.net is not "
                  "registered, so a mod requiring it is refused by name at load");
        return;
    }

    const uint32_t open = Count();
    if (open == 0 && impl::g_Opened == 0) {
        WIIXL_LOG("Net: no module has opened a socket");
        return;
    }

    WIIXL_LOG("Net: %u socket(s) open of %u, %u opened and %u closed this session",
              open, kMaxSockets, impl::g_Opened, impl::g_Closed);

    for (uint32_t i = 0; i < kMaxSockets; ++i) {
        const Slot& s = impl::g_Slots[i];
        if (!s.inUse) continue;
        WIIXL_LOG("Net:   slot %u gen %u: %s%s, %u in / %u out",
                  i, s.generation, s.owner, s.listener ? " (listener)" : "",
                  s.bytesIn, s.bytesOut);
    }
}

} // namespace WiiXLaunch::Net
