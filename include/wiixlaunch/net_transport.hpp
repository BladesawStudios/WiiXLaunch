#pragma once

// WiiXLaunch::Net::Transport - raw, untracked, platform TCP.
//
// This layer knows how to open a socket on each platform and NOTHING about who
// asked. Ownership, quotas, handle validity and attribution all live one level
// up in net.hpp, which is the only thing that should call this.
//
// The split exists because those are genuinely different problems: the platform
// question is "how do I reach nsysnet from here", and it has three completely
// different answers; the ownership question is "which mod holds this and what
// happens when it doesn't close it", and it has ONE answer that must not be
// written three times.
//
//   Wii U (Aroma): wut's socket headers, backed by nsysnet.rpl. The plugin is a
//                  real module with its own import table, so this is ordinary
//                  linking and none of the Cemu problem applies.
//   Cemu:          nsysnet resolved at runtime through coreinit's dynamic
//                  loader - see cemu/cemu_dynload.hpp for why NOT through a
//                  static import shim table like every other backend here.
//   Switch:        no implementation. Supported is false, wiixl.net is never
//                  registered, and a mod that declares it required is refused
//                  BY NAME at load rather than loading and silently doing
//                  nothing.
//   Host test:     an installable fake, so tools/net_test can drive the
//                  ownership logic - including file-descriptor REUSE, which is
//                  the case the generation counter exists for and which no real
//                  platform will reproduce on demand.
//
// Everything here is non-blocking by design: these calls run inside a tick, on
// the game thread, so anything that can block is anything that can freeze the
// game.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>

#include <cstdint>
#include <cstddef>

#if WIIXL_CEMU
#include <wiixlaunch/cemu/cemu_dynload.hpp>
#elif WIIXL_WIIU
#include <sys/socket.h>
#include <netinet/in.h>
// socket_lib_init(), and RPLWRAP(socketclose) - the raw nsysnet export. wut
// steers you at close() instead, but libwut.a does not define close(): that
// path relies on newlib devoptab wiring that is not present here, so closing a
// socket through it links against a stub rather than nsysnet. Calling the
// export directly is both correct and identical to what the Cemu path does.
#include <nsysnet/_socket.h>
#endif

namespace WiiXLaunch::Net::Transport {

// Can this build open a socket AT ALL? Compile-time, and it is what decides
// whether wiixl.net is registered - so a Switch mod's dependency on it fails at
// load, by name, rather than at the first send.
constexpr bool Supported = !WIIXL_SWITCH;

// nsysnet socket constants. Values taken from wut's include/sys/socket.h and
// include/netinet/in.h; spelled out rather than #if'd so all platforms provably
// agree on them, and because the Cemu build is bare metal with no SDK headers.
constexpr int32_t kAfInet      = 2;
constexpr int32_t kSockStream  = 1;
constexpr int32_t kIpProtoTcp  = 6;
constexpr int32_t kSolSocket   = -1;
constexpr int32_t kSoReuseAddr = 0x0004;
constexpr int32_t kSoNonBlock  = 0x1016;  // set/get blocking mode via optval
constexpr int32_t kSoMyAddr    = 0x1013;  // get this interface's IPv4 address
constexpr uint32_t kInAddrAny  = 0x00000000;

constexpr int kInvalidFd = -1;

// The Wii U and Cemu are both big-endian PowerPC, so host order already IS
// network order and this is the identity. Written out anyway so the intent
// survives on the little-endian host test, where it is NOT the identity and
// where getting it wrong would silently pass.
inline uint16_t Htons(uint16_t v) {
    if constexpr (IsBigEndian) return v;
    else return static_cast<uint16_t>((v << 8) | (v >> 8));
}

// Byte-identical to wut's struct sockaddr_in (no BSD sin_len byte), so the
// Wii U path can cast this straight to struct sockaddr* and every platform
// shares one definition.
struct SockAddrIn {
    uint16_t family;
    uint16_t port;
    uint32_t addr;
    uint8_t  zero[8];
};
static_assert(sizeof(SockAddrIn) == 16, "must match nsysnet's 16-byte sockaddr_in");

// ---------------------------------------------------------------------------
// The host-test seam.
//
// net.hpp's whole job is ownership, and ownership bugs are only observable when
// descriptors are REUSED - mod A closes fd 5, mod B opens and receives fd 5,
// and A's stale handle must not reach B's socket. No real platform will do that
// on cue, so the host test installs a fake that does it deliberately.
//
// This is a seam, not a mock of convenience: on every real target these
// function pointers do not exist and the calls below compile straight to the
// platform's own.
// ---------------------------------------------------------------------------
#if WIIXL_HOST

struct HostOps {
    bool     (*init)();
    int      (*open)();
    bool     (*setOptInt)(int fd, int32_t level, int32_t option, int32_t value);
    bool     (*bind)(int fd, uint16_t port);
    bool     (*listen)(int fd, int32_t backlog);
    int      (*accept)(int fd);
    int      (*recv)(int fd, void* buf, uint32_t len);
    int      (*send)(int fd, const void* buf, uint32_t len);
    void     (*close)(int fd);
    uint32_t (*localIp)(int fd);
    bool     (*available)();
};

namespace impl { inline const HostOps* g_HostOps = nullptr; }

inline void SetHostOps(const HostOps* ops) { impl::g_HostOps = ops; }
inline const HostOps* CurrentHostOps() { return impl::g_HostOps; }

#endif // WIIXL_HOST

namespace impl {

inline bool g_LibInitialized = false;

#if WIIXL_CEMU

using FnSocketLibInit = void    (*)();
using FnSocket        = int32_t (*)(int32_t domain, int32_t type, int32_t protocol);
using FnBind          = int32_t (*)(int32_t fd, const void* addr, int32_t addrlen);
using FnListen        = int32_t (*)(int32_t fd, int32_t backlog);
using FnAccept        = int32_t (*)(int32_t fd, void* addr, int32_t* addrlen);
using FnRecv          = int32_t (*)(int32_t fd, void* buf, uint32_t len, int32_t flags);
using FnSend          = int32_t (*)(int32_t fd, const void* buf, uint32_t len, int32_t flags);
using FnSetSockOpt    = int32_t (*)(int32_t fd, int32_t level, int32_t opt, const void* val, int32_t len);
using FnGetSockOpt    = int32_t (*)(int32_t fd, int32_t level, int32_t opt, void* val, int32_t* len);
using FnSocketClose   = int32_t (*)(int32_t fd);

// The whole nsysnet entry table, resolved once through the dynamic loader.
//
// One struct rather than ten globals so "did resolution happen" is a single
// question with a single answer. g_Resolved is set even on FAILURE, so a host
// with no nsysnet does not re-attempt an OSDynLoad_Acquire every frame - and
// g_Acquired records which of the two failures it was, because "the RPL is not
// there" and "the RPL is there but an export is missing" are different problems
// with the same symptom.
struct NsysnetTable {
    FnSocketLibInit socket_lib_init;
    FnSocket        socket;
    FnBind          bind;
    FnListen        listen;
    FnAccept        accept;
    FnRecv          recv;
    FnSend          send;
    FnSetSockOpt    setsockopt;
    FnGetSockOpt    getsockopt;
    FnSocketClose   socketclose;
};

inline NsysnetTable g_Nsysnet = {};
inline bool g_Resolved = false;
inline bool g_Acquired = false;
inline bool g_Complete = false;

inline const NsysnetTable* Nsysnet() {
    if (g_Resolved) return g_Complete ? &g_Nsysnet : nullptr;
    g_Resolved = true;

    if (!Backend::CemuDynLoadAvailable()) {
        WIIXL_LOG("Net: the coreinit dynamic-loader shims are not in this payload, "
                  "so nsysnet cannot be reached. Sockets are unavailable.");
        return nullptr;
    }

    Backend::OSDynLoadModule mod = Backend::CemuAcquireRpl("nsysnet.rpl");
    if (!mod) {
        WIIXL_LOG("Net: OSDynLoad_Acquire(nsysnet.rpl) failed - this title's process "
                  "has no network stack and would not load one. Sockets unavailable.");
        return nullptr;
    }
    g_Acquired = true;

    // Each export by name, and ALL of them must resolve. A partially resolved
    // table is worse than none: it would open sockets it cannot close.
    void** slots[] = {
        reinterpret_cast<void**>(&g_Nsysnet.socket_lib_init),
        reinterpret_cast<void**>(&g_Nsysnet.socket),
        reinterpret_cast<void**>(&g_Nsysnet.bind),
        reinterpret_cast<void**>(&g_Nsysnet.listen),
        reinterpret_cast<void**>(&g_Nsysnet.accept),
        reinterpret_cast<void**>(&g_Nsysnet.recv),
        reinterpret_cast<void**>(&g_Nsysnet.send),
        reinterpret_cast<void**>(&g_Nsysnet.setsockopt),
        reinterpret_cast<void**>(&g_Nsysnet.getsockopt),
        reinterpret_cast<void**>(&g_Nsysnet.socketclose),
    };
    const char* names[] = {
        "socket_lib_init", "socket", "bind", "listen", "accept",
        "recv", "send", "setsockopt", "getsockopt", "socketclose",
    };
    constexpr uint32_t kCount = sizeof(names) / sizeof(names[0]);

    for (uint32_t i = 0; i < kCount; ++i) {
        void* addr = Backend::CemuFindRplExport(mod, names[i]);
        if (!addr) {
            WIIXL_LOG("Net: nsysnet.rpl is loaded but has no export '%s'. Sockets "
                      "unavailable - a partial table would open what it cannot close.",
                      names[i]);
            return nullptr;
        }
        *slots[i] = addr;
    }

    g_Complete = true;
    WIIXL_LOG("Net: nsysnet.rpl resolved through the dynamic loader, %u exports",
              kCount);
    return &g_Nsysnet;
}

#endif // WIIXL_CEMU

} // namespace impl

// True when this build can reach a socket implementation RIGHT NOW.
//
// Distinct from Supported: Supported is "this platform has sockets at all" and
// is a compile-time fact; Available is "they are reachable here" and on Cemu
// depends on whether the title's process can load nsysnet. A mod sees the
// difference as a missing surface versus an Unavailable result.
inline bool Available() {
#if WIIXL_CEMU
    return impl::Nsysnet() != nullptr;
#elif WIIXL_WIIU
    return true;
#elif WIIXL_HOST
    return impl::g_HostOps && impl::g_HostOps->available && impl::g_HostOps->available();
#else
    return false;
#endif
}

// Brings the socket library up. Idempotent.
//
// Deliberately not called at host init: on Wii U the plugin loads before the
// title's network stack is up, so this happens lazily on first use.
inline bool Init() {
    if (impl::g_LibInitialized) return true;
    if (!Available()) return false;

#if WIIXL_CEMU
    const impl::NsysnetTable* net = impl::Nsysnet();
    if (!net) return false;
    net->socket_lib_init();
#elif WIIXL_WIIU
    socket_lib_init();
#elif WIIXL_HOST
    if (!impl::g_HostOps->init || !impl::g_HostOps->init()) return false;
#else
    return false;
#endif

    impl::g_LibInitialized = true;
    return true;
}

// Only for the host test, which runs many independent cases in one process and
// must not carry "the library is already up" from one fake backend to the next.
inline void ResetForTest() {
    impl::g_LibInitialized = false;
}

inline int Open() {
    if (!Available()) return kInvalidFd;
#if WIIXL_CEMU
    return impl::Nsysnet()->socket(kAfInet, kSockStream, kIpProtoTcp);
#elif WIIXL_WIIU
    return ::socket(kAfInet, kSockStream, kIpProtoTcp);
#elif WIIXL_HOST
    return impl::g_HostOps->open ? impl::g_HostOps->open() : kInvalidFd;
#else
    return kInvalidFd;
#endif
}

inline bool SetOptInt(int fd, int32_t level, int32_t option, int32_t value) {
    if (fd < 0 || !Available()) return false;
#if WIIXL_CEMU
    return impl::Nsysnet()->setsockopt(fd, level, option, &value, sizeof(value)) == 0;
#elif WIIXL_WIIU
    return ::setsockopt(fd, level, option, &value, sizeof(value)) == 0;
#elif WIIXL_HOST
    return impl::g_HostOps->setOptInt &&
           impl::g_HostOps->setOptInt(fd, level, option, value);
#else
    (void)level; (void)option; (void)value;
    return false;
#endif
}

// SO_NONBLOCK is the whole reason this layer is usable from a tick: nsysnet has
// no fcntl(), so this option is the only way to stop accept()/recv()/send()
// from parking the game thread on a slow or idle client.
inline bool SetNonBlocking(int fd) {
    return SetOptInt(fd, kSolSocket, kSoNonBlock, 1);
}

inline bool SetReuseAddr(int fd) {
    return SetOptInt(fd, kSolSocket, kSoReuseAddr, 1);
}

inline bool Bind(int fd, uint16_t port) {
    if (fd < 0 || !Available()) return false;

// The address is built only where it is passed to something. Hoisting it above
// the #if left it unused on Switch and on the host, and devkitA64 builds this
// tree with -Werror, so "harmless" was a build failure on one target out of
// three - which is the whole reason every change here builds all three.
#if WIIXL_CEMU || WIIXL_WIIU
    SockAddrIn addr = {};
    addr.family = kAfInet;
    addr.port = Htons(port);
    addr.addr = kInAddrAny;
#endif

#if WIIXL_CEMU
    return impl::Nsysnet()->bind(fd, &addr, sizeof(addr)) == 0;
#elif WIIXL_WIIU
    return ::bind(fd, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)) == 0;
#elif WIIXL_HOST
    return impl::g_HostOps->bind && impl::g_HostOps->bind(fd, port);
#else
    (void)port;
    return false;
#endif
}

inline bool Listen(int fd, int32_t backlog) {
    if (fd < 0 || !Available()) return false;
#if WIIXL_CEMU
    return impl::Nsysnet()->listen(fd, backlog) == 0;
#elif WIIXL_WIIU
    return ::listen(fd, backlog) == 0;
#elif WIIXL_HOST
    return impl::g_HostOps->listen && impl::g_HostOps->listen(fd, backlog);
#else
    (void)backlog;
    return false;
#endif
}

// A connected descriptor, or negative when nothing is pending. On a
// non-blocking listener "nothing pending" and "real error" both surface as -1;
// callers treat both the same way (try again next frame), so no errno plumbing
// is needed here.
inline int Accept(int fd) {
    if (fd < 0 || !Available()) return kInvalidFd;
#if WIIXL_CEMU
    SockAddrIn peer = {};
    int32_t peerLen = sizeof(peer);
    return impl::Nsysnet()->accept(fd, &peer, &peerLen);
#elif WIIXL_WIIU
    SockAddrIn peer = {};
    socklen_t peerLen = sizeof(peer);
    return ::accept(fd, reinterpret_cast<struct sockaddr*>(&peer), &peerLen);
#elif WIIXL_HOST
    return impl::g_HostOps->accept ? impl::g_HostOps->accept(fd) : kInvalidFd;
#else
    return kInvalidFd;
#endif
}

// > 0: bytes read. 0: peer closed cleanly. < 0: nothing available right now.
inline int Recv(int fd, void* buf, uint32_t len) {
    if (fd < 0 || !buf || len == 0 || !Available()) return -1;
#if WIIXL_CEMU
    return impl::Nsysnet()->recv(fd, buf, len, 0);
#elif WIIXL_WIIU
    return static_cast<int>(::recv(fd, buf, len, 0));
#elif WIIXL_HOST
    return impl::g_HostOps->recv ? impl::g_HostOps->recv(fd, buf, len) : -1;
#else
    return -1;
#endif
}

// >= 0: bytes written, possibly a partial write. < 0: would block, try again.
inline int Send(int fd, const void* buf, uint32_t len) {
    if (fd < 0 || !buf || len == 0 || !Available()) return -1;
#if WIIXL_CEMU
    return impl::Nsysnet()->send(fd, buf, len, 0);
#elif WIIXL_WIIU
    return static_cast<int>(::send(fd, buf, len, 0));
#elif WIIXL_HOST
    return impl::g_HostOps->send ? impl::g_HostOps->send(fd, buf, len) : -1;
#else
    return -1;
#endif
}

inline void Close(int fd) {
    if (fd < 0 || !Available()) return;
#if WIIXL_CEMU
    impl::Nsysnet()->socketclose(fd);
#elif WIIXL_WIIU
    RPLWRAP(socketclose)(fd);
#elif WIIXL_HOST
    if (impl::g_HostOps->close) impl::g_HostOps->close(fd);
#endif
}

// This interface's IPv4 address in host order, or 0 if it could not be read.
// Worth logging on real hardware - it is the address you have to curl, and the
// console does not otherwise tell you what it is.
inline uint32_t LocalIp(int fd) {
    if (fd < 0 || !Available()) return 0;
#if WIIXL_CEMU
    int32_t ip = 0;
    int32_t len = sizeof(ip);
    if (impl::Nsysnet()->getsockopt(fd, kSolSocket, kSoMyAddr, &ip, &len) != 0) return 0;
    return static_cast<uint32_t>(ip);
#elif WIIXL_WIIU
    int32_t ip = 0;
    socklen_t len = sizeof(ip);
    if (::getsockopt(fd, kSolSocket, kSoMyAddr, &ip, &len) != 0) return 0;
    return static_cast<uint32_t>(ip);
#elif WIIXL_HOST
    return impl::g_HostOps->localIp ? impl::g_HostOps->localIp(fd) : 0;
#else
    return 0;
#endif
}

} // namespace WiiXLaunch::Net::Transport
