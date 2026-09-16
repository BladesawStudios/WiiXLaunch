#pragma once

// WiiXLaunch::Net - sockets that belong to somebody. A mod holding a socket
// holds a resource with a lifetime, unlike a hook or patch, so the host
// tracks sockets and hands out its own generation-counted handles rather
// than raw descriptors (a raw descriptor would let a use-after-close on a
// recycled fd land on another mod's live connection).
//
// Every socket is forced non-blocking, since mods run on the game's own
// thread and a blocking call would freeze the game; a socket that won't go
// non-blocking is closed rather than returned. Per-module quotas
// (kMaxPerModule of kMaxSockets) contain a leak to its own owner instead of
// starving whichever mod asks next.
//
// Tracking buys attribution and containment, not recovery: a leak within a
// session is contained by the quota, a hang inside a tick can only be
// attributed (nothing runs to reclaim it), and at shutdown the process
// owns the handles (there is no module-unload path). See
// docs/framework/net.md for the full reasoning.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/mod_context.hpp>
#include <wiixlaunch/net_transport.hpp>

#include <cstdint>

namespace WiiXLaunch::Net {

// The host's whole budget, and one module's share of it. 8 per module
// leaves room to be a server and something else; 24 total lets three such
// modules coexist.
constexpr uint32_t kMaxSockets    = 24;
constexpr uint32_t kMaxPerModule  = 8;
constexpr uint32_t kOwnerLen      = 17;

// A host handle, never a descriptor. 0 is never valid, so a zeroed struct
// in a mod's .bss can't accidentally name a socket.
//
//   bits  0..15   slot index + 1
//   bits 16..31   generation, incremented every time the slot is reused
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

// Byte results for Recv/Send: non-negative is a byte count, every negative
// value is a distinct named reason, so a mod can tell "try again next
// frame" from "your handle is stale" without reading the host log.
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

// Sets non-blocking and checks it took: setsockopt returning 0 is the
// report, the socket still being blocking is the damage. The readback is
// best-effort - a platform that can't answer tells us nothing - so only a
// readback that succeeds and says "blocking" is treated as a failure.
inline bool ForceNonBlocking(int fd) {
    if (!Transport::SetNonBlocking(fd)) return false;

    int32_t value = 0;
    if (Transport::GetOptInt(fd, Transport::kSolSocket, Transport::kSoNonBlock, &value)) {
        if (value == 0) {
            WIIXL_LOG("Net: the platform accepted SO_NONBLOCK and did not apply it - "
                      "refusing this socket rather than handing over one that can "
                      "freeze the game");
            return false;
        }
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

// Resolves a handle for the current module. BadHandle ("never a socket"),
// StaleHandle ("yours, and closed"), and NotOwner ("live, and somebody
// else's") stay distinct so use-after-close isn't indistinguishable from a
// typo.
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

// Opens a TCP socket, charged to the calling module. No owner parameter,
// same as Hooks and Tick: identity is what the host observes.
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

    // Before the mod ever sees it: a socket that won't go non-blocking is
    // closed rather than handed over.
    if (!impl::ForceNonBlocking(fd)) {
        Transport::Close(fd);
        WIIXL_LOG("Net: %s got %s - the socket would not go non-blocking (error %d), "
                  "so it was closed. A blocking socket on the game thread is a hang.",
                  owner, ResultName(Result::PlatformError), Transport::LastError());
        return Result::PlatformError;
    }

    Slot& s = impl::g_Slots[index];
    s.fd = fd;
    s.inUse = true;
    s.listener = false;
    s.bytesIn = 0;
    s.bytesOut = 0;
    // Moved on every acquisition, so a handle from this slot's previous
    // life can never match the current one.
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

// Already true of every socket this surface hands out (Open and Accept
// both set it first); kept as a no-op since removing a symbol is a major
// bump. There is no way to ask for a blocking socket.
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

// Takes a pending connection, charged to the same module as the listener,
// through the same quota an Open goes through. Returns PlatformError when
// nothing is pending, the ordinary case every frame on a non-blocking
// listener.
inline Result Accept(Handle listener, Handle* outHandle) {
    if (!outHandle) return Result::BadArgument;
    *outHandle = kInvalidHandle;

    Slot* s = nullptr;
    const Result r = Resolve(listener, &s);
    if (r != Result::Ok) return r;

    const int fd = Transport::Accept(s->fd);
    if (fd < 0) return Result::PlatformError;

    // Accepted sockets do not inherit SO_NONBLOCK from the listener.
    if (!impl::ForceNonBlocking(fd)) {
        Transport::Close(fd);
        WIIXL_LOG("Net: %s accepted a connection that would not go non-blocking "
                  "(error %d) - closed rather than handed over, since a blocking "
                  "socket on the game thread is a hang.",
                  s->owner, Transport::LastError());
        return Result::PlatformError;
    }

    const Result adopted = impl::Adopt(fd, s->owner, outHandle);
    if (adopted != Result::Ok) {
        // Refused for a quota or slot reason: close what was just accepted
        // rather than leaving it untracked.
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

// Half-close, so a reply isn't thrown away by the close that follows it: a
// close with unread bytes still in the receive buffer sends an RST, not a
// FIN, and the client loses the reply.
inline Result Shutdown(Handle h, uint32_t how) {
    if (how > static_cast<uint32_t>(Transport::kShutReadWrite)) return Result::BadArgument;

    Slot* s = nullptr;
    const Result r = Resolve(h, &s);
    if (r != Result::Ok) return r;
    return Transport::Shutdown(s->fd, static_cast<int32_t>(how))
               ? Result::Ok : Result::PlatformError;
}

inline uint32_t LocalIp(Handle h) {
    Slot* s = nullptr;
    if (Resolve(h, &s) != Result::Ok) return 0;
    return Transport::LocalIp(s->fd);
}

// Releases the slot. The generation has already moved by the time anything
// can ask again, so the caller's own handle is StaleHandle from here on.
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

// Everything one module holds. Returns how many were closed. The only
// caller today is a module resetting itself through wiixl.net.
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

// Who holds what.
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
