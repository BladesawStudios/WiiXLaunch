// net_test - the ownership half of wiixl.net, on the host.
//
// WHAT THIS CAN AND CANNOT TEST, up front. It cannot open a socket: there is no
// nsysnet here and the point is not to test nsysnet. What it tests is the layer
// that decides WHO holds a socket, what happens when a module leaks them, and
// whether a stale handle can reach a live socket - all of which are pure logic
// over a table, and all of which are where the bugs that hurt actually live.
//
// The transport underneath is a fake installed through Transport::SetHostOps,
// and it is a fake with one specific job: IT REUSES FILE DESCRIPTORS, lowest
// free first, exactly like every real OS. That is the behaviour that turns a
// use-after-close into cross-mod corruption, and no real platform will produce
// it on demand. The whole generation-counter design is answerable only against
// a backend that recycles, so the fake recycles.
//
// Every refusal below is paired with the same call SUCCEEDING. A check that can
// only pass is not a check: if Open started returning NoModule unconditionally,
// half these assertions would still be green without the positive controls.

#include <wiixlaunch/net.hpp>
#include <wiixlaunch/net_transport.hpp>
#include <wiixlaunch/mod_context.hpp>

#include <cstdio>
#include <cstring>
#include <cstdint>

namespace N = WiiXLaunch::Net;
namespace T = WiiXLaunch::Net::Transport;
namespace MC = WiiXLaunch::ModContext;

static int g_checks = 0;
static int g_failures = 0;
static int g_sectionStart = 0;
static const char* g_section = "";

static void ok(const char* what, bool cond) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::printf("  FAIL  %s\n", what);
    } else {
        std::printf("  ok    %s\n", what);
    }
}

static void BeginSection(const char* name) {
    g_section = name;
    g_sectionStart = g_checks;
    std::printf("\n%s:\n", name);
}

// A section that ran zero checks is a section that silently did nothing, which
// is the failure mode this project keeps finding. Every section declares how
// many it must have run.
static void EndSection(int expected) {
    const int ran = g_checks - g_sectionStart;
    if (ran != expected) {
        ++g_failures;
        std::printf("  FAIL  section '%s' ran %d checks, expected %d\n",
                    g_section, ran, expected);
    }
}

// ---------------------------------------------------------------------------
// The fake transport.
// ---------------------------------------------------------------------------

static constexpr int kFakeFds = 64;

struct FakeSocket {
    bool     open;
    bool     nonBlocking;
    bool     reuseAddr;
    uint16_t port;
    bool     listening;
    uint32_t bytesSent;      // what THIS descriptor received from Send
    uint32_t bytesRecvd;
    char     lastSent[64];
    int32_t  shutdownHow;    // -1 until shutdown() was called on this descriptor
};

static FakeSocket g_Fake[kFakeFds];
static bool g_FakeAvailable = true;
static bool g_FakeOpenFails = false;
static int  g_FakePendingAccepts = 0;
static int  g_FakeCloseCount = 0;
static int  g_FakeLastClosed = -1;
static int  g_FakeRecvBytes = 0;      // what the next Recv should deliver
static bool g_FakeBindFails = false;
static int  g_FakeLastError = 0;      // what the platform would say went wrong

static void FakeReset() {
    for (int i = 0; i < kFakeFds; ++i) {
        g_Fake[i] = FakeSocket{};
        g_Fake[i].shutdownHow = -1;
    }
    g_FakeAvailable = true;
    g_FakeOpenFails = false;
    g_FakePendingAccepts = 0;
    g_FakeCloseCount = 0;
    g_FakeLastClosed = -1;
    g_FakeRecvBytes = 0;
    g_FakeBindFails = false;
    g_FakeLastError = 0;
}

static bool FakeInit() { return g_FakeAvailable; }
static bool FakeAvailable() { return g_FakeAvailable; }

// LOWEST FREE DESCRIPTOR FIRST. This is the entire reason the fake exists.
static int FakeOpen() {
    if (g_FakeOpenFails) return -1;
    for (int i = 3; i < kFakeFds; ++i) {          // 0-2 reserved, like a real OS
        if (!g_Fake[i].open) {
            g_Fake[i] = FakeSocket{};
            g_Fake[i].shutdownHow = -1;
            g_Fake[i].open = true;
            return i;
        }
    }
    return -1;
}

static bool FakeSetOptInt(int fd, int32_t level, int32_t option, int32_t value) {
    if (fd < 0 || fd >= kFakeFds || !g_Fake[fd].open) return false;
    (void)level;
    if (option == T::kSoNonBlock) g_Fake[fd].nonBlocking = (value != 0);
    else if (option == T::kSoReuseAddr) g_Fake[fd].reuseAddr = (value != 0);
    return true;
}

static bool FakeBind(int fd, uint16_t port) {
    if (fd < 0 || fd >= kFakeFds || !g_Fake[fd].open) return false;
    if (g_FakeBindFails) { g_FakeLastError = 48; return false; }   // EADDRINUSE
    g_Fake[fd].port = port;
    return true;
}

static bool FakeListen(int fd, int32_t backlog) {
    (void)backlog;
    if (fd < 0 || fd >= kFakeFds || !g_Fake[fd].open) return false;
    g_Fake[fd].listening = true;
    return true;
}

static int FakeAccept(int fd) {
    if (fd < 0 || fd >= kFakeFds || !g_Fake[fd].open || !g_Fake[fd].listening) return -1;
    if (g_FakePendingAccepts <= 0) return -1;
    --g_FakePendingAccepts;
    return FakeOpen();
}

static int FakeRecv(int fd, void* buf, uint32_t len) {
    if (fd < 0 || fd >= kFakeFds || !g_Fake[fd].open) return -1;
    if (g_FakeRecvBytes <= 0) return -1;
    uint32_t n = static_cast<uint32_t>(g_FakeRecvBytes);
    if (n > len) n = len;
    std::memset(buf, 'r', n);
    g_FakeRecvBytes = 0;
    g_Fake[fd].bytesRecvd += n;
    return static_cast<int>(n);
}

// Records WHICH descriptor the bytes landed on. That record is what proves a
// stale handle did not reach somebody else's socket - the assertion cannot be
// "it returned an error", because a wrong implementation could return an error
// AND still have written.
static int FakeSend(int fd, const void* buf, uint32_t len) {
    if (fd < 0 || fd >= kFakeFds || !g_Fake[fd].open) return -1;
    uint32_t n = len;
    if (n > sizeof(g_Fake[fd].lastSent) - 1) n = sizeof(g_Fake[fd].lastSent) - 1;
    std::memcpy(g_Fake[fd].lastSent, buf, n);
    g_Fake[fd].lastSent[n] = 0;
    g_Fake[fd].bytesSent += len;
    return static_cast<int>(len);
}

static void FakeClose(int fd) {
    if (fd < 0 || fd >= kFakeFds) return;
    if (g_Fake[fd].open) {
        g_Fake[fd].open = false;
        g_Fake[fd].listening = false;
        ++g_FakeCloseCount;
        g_FakeLastClosed = fd;
    }
}

static int32_t FakeLastError() { return g_FakeLastError; }

static bool FakeShutdown(int fd, int32_t how) {
    if (fd < 0 || fd >= kFakeFds || !g_Fake[fd].open) return false;
    g_Fake[fd].shutdownHow = how;
    return true;
}

static uint32_t FakeLocalIp(int fd) {
    if (fd < 0 || fd >= kFakeFds || !g_Fake[fd].open) return 0;
    return 0xC0A80164u;   // 192.168.1.100
}

static const T::HostOps kFakeOps = {
    &FakeInit, &FakeOpen, &FakeSetOptInt, &FakeBind, &FakeListen,
    &FakeAccept, &FakeRecv, &FakeSend, &FakeClose, &FakeLocalIp, &FakeAvailable,
    &FakeLastError, &FakeShutdown,
};

// Fresh table AND fresh fake, so no case can pass on state another left behind.
static void FreshWorld() {
    FakeReset();
    N::ResetForTest();
    T::SetHostOps(&kFakeOps);
    MC::SetCurrent(nullptr);
}

// The descriptor behind a handle, for assertions the surface deliberately does
// not expose to mods.
static int FdOf(N::Handle h) {
    const uint32_t low = h & 0xFFFFu;
    if (low == 0 || low > N::kMaxSockets) return -1;
    return N::impl::g_Slots[low - 1].fd;
}

int main() {
    // Unbuffered, because a crash discards a buffer and this gate has to say
    // what it got as far as it got. Learned the hard way from hook_test.
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    std::printf("net_test - ownership, quotas and stale handles\n");

    // -----------------------------------------------------------------------
    BeginSection("attribution");
    {
        FreshWorld();

        N::Handle h = N::kInvalidHandle;

        MC::SetCurrent(nullptr);
        ok("opening outside a module is refused",
           N::Open(&h) == N::Result::NoModule);
        ok("and hands back no handle", h == N::kInvalidHandle);

        // The positive control. Without it, an Open that always returned
        // NoModule would pass the check above.
        MC::SetCurrent("modA");
        ok("opening inside a module succeeds", N::Open(&h) == N::Result::Ok);
        ok("and hands back a non-zero handle", h != N::kInvalidHandle);
        ok("the socket is charged to modA", N::CountFor("modA") == 1);
        ok("and to nobody else", N::CountFor("modB") == 0);

        ok("a null out-pointer is refused",
           N::Open(nullptr) == N::Result::BadArgument);

        MC::SetCurrent(nullptr);
    }
    EndSection(7);

    // -----------------------------------------------------------------------
    BeginSection("handle validity");
    {
        FreshWorld();
        MC::SetCurrent("modA");

        N::Handle good = N::kInvalidHandle;
        ok("a socket opens", N::Open(&good) == N::Result::Ok);

        N::Slot* slot = nullptr;
        ok("zero is not a handle", N::Resolve(0, &slot) == N::Result::BadHandle);
        ok("an out-of-range index is not a handle",
           N::Resolve(N::kMaxSockets + 1, &slot) == N::Result::BadHandle);
        ok("a live handle resolves", N::Resolve(good, &slot) == N::Result::Ok);

        // Same slot, wrong generation - the shape a stale handle has.
        const N::Handle wrongGen = (good & 0xFFFFu) | 0x00990000u;
        ok("the right slot with the wrong generation is stale",
           N::Resolve(wrongGen, &slot) == N::Result::StaleHandle);

        MC::SetCurrent("modB");
        ok("another module cannot use it", N::Resolve(good, &slot) == N::Result::NotOwner);
        MC::SetCurrent("modA");
        ok("and the owner still can", N::Resolve(good, &slot) == N::Result::Ok);

        MC::SetCurrent(nullptr);
        ok("outside a module, nothing resolves",
           N::Resolve(good, &slot) == N::Result::NoModule);
    }
    EndSection(8);

    // -----------------------------------------------------------------------
    // THE HEADLINE CASE. A closed socket's descriptor is handed to another
    // module, and the first module's stale handle must not reach it.
    BeginSection("use after close, with the descriptor reused");
    {
        FreshWorld();

        MC::SetCurrent("modA");
        N::Handle a = N::kInvalidHandle;
        ok("modA opens", N::Open(&a) == N::Result::Ok);
        const int fdA = FdOf(a);
        ok("modA got a real descriptor", fdA >= 3);
        ok("modA closes", N::Close(a) == N::Result::Ok);

        MC::SetCurrent("modB");
        N::Handle b = N::kInvalidHandle;
        ok("modB opens", N::Open(&b) == N::Result::Ok);
        const int fdB = FdOf(b);

        // LIVENESS. If the fake stopped recycling, everything below would pass
        // for the wrong reason - the stale handle would be refused because it
        // named a dead descriptor, not because the generation caught it.
        ok("the fake really did recycle the descriptor", fdB == fdA);
        ok("and really did reuse the slot", (a & 0xFFFFu) == (b & 0xFFFFu));
        ok("so the two handles differ ONLY in generation", a != b);

        MC::SetCurrent("modA");
        const char* payload = "this must never reach modB";
        const int32_t sent = N::Send(a, payload, static_cast<uint32_t>(std::strlen(payload)));
        ok("modA's stale handle is refused by name", sent == N::kIoStaleHandle);

        // The assertion that matters. An implementation that returned an error
        // AND wrote anyway would pass the line above and fail this one.
        ok("and nothing was written to the recycled descriptor",
           g_Fake[fdB].bytesSent == 0);

        // Positive control: the same descriptor IS writable by its real owner,
        // so "nothing was written" above is not just a dead fake.
        MC::SetCurrent("modB");
        const int32_t okSent = N::Send(b, "mine", 4);
        ok("modB can write to its own socket", okSent == 4);
        ok("and the bytes landed on that descriptor", g_Fake[fdB].bytesSent == 4);
        ok("with modB's content, not modA's",
           std::strcmp(g_Fake[fdB].lastSent, "mine") == 0);

        // Reading is the same story in the other direction.
        MC::SetCurrent("modA");
        char buf[16];
        ok("modA's stale handle cannot read either",
           N::Recv(a, buf, sizeof(buf)) == N::kIoStaleHandle);

        MC::SetCurrent(nullptr);
    }
    EndSection(13);

    // -----------------------------------------------------------------------
    // The property that decides the whole design: a leak is contained TO THE
    // LEAKING MODULE. A global cap could not do this.
    BeginSection("quota is fault isolation");
    {
        FreshWorld();

        MC::SetCurrent("greedy");
        uint32_t accepted = 0;
        for (uint32_t i = 0; i < N::kMaxPerModule + 4u; ++i) {
            N::Handle h = N::kInvalidHandle;
            if (N::Open(&h) == N::Result::Ok) ++accepted;
        }
        ok("a module is stopped at its own quota", accepted == N::kMaxPerModule);

        N::Handle over = N::kInvalidHandle;
        ok("and the refusal names the quota, not the host table",
           N::Open(&over) == N::Result::ModuleQuota);

        // THE ISOLATION CHECK. Without it, "the cap works" would be equally
        // true of a global cap that punishes everyone.
        MC::SetCurrent("innocent");
        N::Handle other = N::kInvalidHandle;
        ok("another module is completely unaffected",
           N::Open(&other) == N::Result::Ok);
        ok("and holds its own socket", N::CountFor("innocent") == 1);
        ok("while the greedy one still holds its share",
           N::CountFor("greedy") == N::kMaxPerModule);
        ok("the host's total is the sum of the two",
           N::Count() == N::kMaxPerModule + 1u);

        MC::SetCurrent(nullptr);
    }
    EndSection(6);

    // -----------------------------------------------------------------------
    BeginSection("the host table has its own limit");
    {
        FreshWorld();

        // kMaxSockets / kMaxPerModule modules, each taking its full share.
        const uint32_t modules = N::kMaxSockets / N::kMaxPerModule;
        char id[8];
        uint32_t total = 0;
        for (uint32_t m = 0; m < modules; ++m) {
            id[0] = 'm'; id[1] = static_cast<char>('0' + m); id[2] = 0;
            MC::SetCurrent(id);
            for (uint32_t i = 0; i < N::kMaxPerModule; ++i) {
                N::Handle h = N::kInvalidHandle;
                if (N::Open(&h) == N::Result::Ok) ++total;
            }
        }
        ok("the table fills exactly", total == N::kMaxSockets);
        ok("and reports itself full", N::Count() == N::kMaxSockets);

        // A module that has taken NOTHING still cannot open - and the reason it
        // is given is the host's limit, not its own. Those are different
        // problems for the mod author and must not share a value.
        MC::SetCurrent("newcomer");
        N::Handle h = N::kInvalidHandle;
        const N::Result r = N::Open(&h);
        ok("a module under its quota is refused for NoSlots", r == N::Result::NoSlots);
        ok("which is a different value from ModuleQuota", r != N::Result::ModuleQuota);

        MC::SetCurrent(nullptr);
    }
    EndSection(4);

    // -----------------------------------------------------------------------
    BeginSection("closing, and closing everything");
    {
        FreshWorld();

        MC::SetCurrent("modA");
        N::Handle a1 = 0, a2 = 0, a3 = 0;
        N::Open(&a1); N::Open(&a2); N::Open(&a3);
        MC::SetCurrent("modB");
        N::Handle b1 = 0, b2 = 0;
        N::Open(&b1); N::Open(&b2);

        ok("modA holds three", N::CountFor("modA") == 3);
        ok("modB holds two", N::CountFor("modB") == 2);

        const int closesBefore = g_FakeCloseCount;
        ok("CloseAllFor closes exactly one module's sockets",
           N::CloseAllFor("modA") == 3);
        ok("and really closed them underneath",
           g_FakeCloseCount == closesBefore + 3);
        ok("modA now holds none", N::CountFor("modA") == 0);

        // The positive control for CloseAll: a version that closed the whole
        // table would pass every line above.
        ok("modB is untouched", N::CountFor("modB") == 2);

        MC::SetCurrent("modA");
        ok("modA's old handle is now stale", N::Close(a1) == N::Result::StaleHandle);

        MC::SetCurrent("modB");
        ok("modB can still close its own", N::Close(b1) == N::Result::Ok);
        ok("and closing it twice is stale, not a double close",
           N::Close(b1) == N::Result::StaleHandle);
        ok("modB holds one", N::CountFor("modB") == 1);

        MC::SetCurrent(nullptr);
    }
    EndSection(10);

    // -----------------------------------------------------------------------
    BeginSection("accept is charged to the listener's owner");
    {
        FreshWorld();

        MC::SetCurrent("server");
        N::Handle listener = 0;
        ok("the listener opens", N::Open(&listener) == N::Result::Ok);
        ok("reuseaddr is set", N::SetReuseAddr(listener) == N::Result::Ok);
        ok("non-blocking is set", N::SetNonBlocking(listener) == N::Result::Ok);
        ok("bind succeeds", N::Bind(listener, 8080) == N::Result::Ok);
        ok("listen succeeds", N::Listen(listener, 4) == N::Result::Ok);
        ok("the port reached the transport", g_Fake[FdOf(listener)].port == 8080);
        ok("and the descriptor is non-blocking",
           g_Fake[FdOf(listener)].nonBlocking);
        ok("a bound socket reports a local address", N::LocalIp(listener) != 0);

        N::Handle conn = 0;
        ok("an idle listener accepts nothing",
           N::Accept(listener, &conn) == N::Result::PlatformError);

        g_FakePendingAccepts = 1;
        ok("a pending connection is accepted", N::Accept(listener, &conn) == N::Result::Ok);
        ok("the accepted socket is charged to the same module",
           N::CountFor("server") == 2);

        MC::SetCurrent("other");
        ok("and belongs to it, not to whoever asks",
           N::Close(conn) == N::Result::NotOwner);
        MC::SetCurrent("server");
        ok("its owner can close it", N::Close(conn) == N::Result::Ok);

        MC::SetCurrent(nullptr);
    }
    EndSection(13);

    // -----------------------------------------------------------------------
    // A server that accepts every frame and never closes is the real leak
    // vector, so an accepted socket goes through the quota too - and when it is
    // refused, the descriptor must be CLOSED rather than leaked untracked.
    BeginSection("an accept the host cannot track is closed, not leaked");
    {
        FreshWorld();

        MC::SetCurrent("server");
        N::Handle listener = 0;
        N::Open(&listener);
        N::Listen(listener, 4);

        // Fill the rest of this module's share.
        for (uint32_t i = 1; i < N::kMaxPerModule; ++i) {
            N::Handle h = 0;
            N::Open(&h);
        }
        ok("the module is at its quota", N::CountFor("server") == N::kMaxPerModule);

        g_FakePendingAccepts = 1;
        const int closesBefore = g_FakeCloseCount;
        N::Handle conn = 0;
        ok("the accept is refused for quota",
           N::Accept(listener, &conn) == N::Result::ModuleQuota);
        ok("no handle is handed out", conn == N::kInvalidHandle);
        ok("the module still holds exactly its share",
           N::CountFor("server") == N::kMaxPerModule);

        // The point: the platform DID hand over a descriptor, and the host must
        // not simply forget about it.
        ok("the descriptor the platform gave us was closed",
           g_FakeCloseCount == closesBefore + 1);
        ok("and it is not left open in the transport",
           g_FakeLastClosed >= 0 && !g_Fake[g_FakeLastClosed].open);

        MC::SetCurrent(nullptr);
    }
    EndSection(6);

    // -----------------------------------------------------------------------
    BeginSection("the two ways there is no socket");
    {
        FreshWorld();
        MC::SetCurrent("modA");

        // Positive control first: with the fake healthy, this works.
        N::Handle h = 0;
        ok("a healthy transport opens", N::Open(&h) == N::Result::Ok);

        // "The library is not reachable" - Cemu with no nsysnet.
        FreshWorld();
        MC::SetCurrent("modA");
        g_FakeAvailable = false;
        ok("an unreachable socket library reports Unavailable",
           N::Open(&h) == N::Result::Unavailable);

        // "The library is there and refused" - a real error from the platform.
        FreshWorld();
        MC::SetCurrent("modA");
        g_FakeOpenFails = true;
        ok("a transport that refuses reports PlatformError",
           N::Open(&h) == N::Result::PlatformError);

        ok("and those are different values",
           N::Result::Unavailable != N::Result::PlatformError);

        MC::SetCurrent(nullptr);
    }
    EndSection(4);

    // -----------------------------------------------------------------------
    // A bind that fails is the ordinary case, not a bug in the mod - a port
    // already held by another process. The first real boot returned
    // PLATFORM-ERROR and nothing else, which sent the search at the socket
    // layer when the answer was "something else has 8080".
    BeginSection("a refusal carries the platform's own reason");
    {
        FreshWorld();
        MC::SetCurrent("modA");

        N::Handle h = 0;
        ok("a socket opens", N::Open(&h) == N::Result::Ok);
        ok("and binds when the port is free", N::Bind(h, 8080) == N::Result::Ok);
        ok("with nothing to report", T::LastError() == 0);

        // Now the same call, refused by the platform.
        g_FakeBindFails = true;
        N::Handle h2 = 0;
        N::Open(&h2);
        ok("a taken port is a platform error",
           N::Bind(h2, 8080) == N::Result::PlatformError);

        // The assertion that matters: the reason SURVIVED to where a caller can
        // read it. A Result alone cannot distinguish EADDRINUSE from EINVAL,
        // and those want completely different responses.
        ok("and the platform's reason is readable afterwards", T::LastError() == 48);

        MC::SetCurrent(nullptr);
    }
    EndSection(5);

    // -----------------------------------------------------------------------
    // A reply that never arrives. Send-then-Close with the peer's request still
    // unread makes TCP answer with an RST, and the client discards the reply it
    // was about to read - which is exactly what the d_net sample did on its
    // first working boot: three requests served, and curl reporting
    // "connection reset by peer" for every one of them.
    BeginSection("half-close");
    {
        FreshWorld();
        MC::SetCurrent("modA");

        N::Handle h = 0;
        ok("a socket opens", N::Open(&h) == N::Result::Ok);
        const int fd = FdOf(h);

        ok("nothing has been shut down yet", g_Fake[fd].shutdownHow == -1);
        ok("shutting down the write side succeeds",
           N::Shutdown(h, 1) == N::Result::Ok);

        // The invisible half: the call has to have REACHED the transport with
        // the direction the caller asked for. "It returned Ok" would pass on an
        // implementation that did nothing at all.
        ok("and the transport was told to stop sending", g_Fake[fd].shutdownHow == 1);

        ok("an out-of-range direction is refused",
           N::Shutdown(h, 7) == N::Result::BadArgument);
        ok("and did not reach the transport", g_Fake[fd].shutdownHow == 1);

        N::Close(h);
        ok("a stale handle cannot be shut down",
           N::Shutdown(h, 1) == N::Result::StaleHandle);

        MC::SetCurrent(nullptr);
    }
    EndSection(7);

    // -----------------------------------------------------------------------
    BeginSection("byte accounting");
    {
        FreshWorld();
        MC::SetCurrent("modA");

        N::Handle h = 0;
        N::Open(&h);

        ok("a send is counted", N::Send(h, "hello", 5) == 5);

        g_FakeRecvBytes = 3;
        char buf[8];
        ok("a recv returns what the transport had", N::Recv(h, buf, sizeof(buf)) == 3);
        ok("an empty transport is a retry, not an error",
           N::Recv(h, buf, sizeof(buf)) == N::kIoWouldBlock);

        N::Slot* s = nullptr;
        N::Resolve(h, &s);
        ok("the slot counted the bytes out", s->bytesOut == 5);
        ok("and the bytes in", s->bytesIn == 3);

        ok("a null buffer is refused", N::Send(h, nullptr, 4) == N::kIoBadArgument);
        ok("and a zero length", N::Recv(h, buf, 0) == N::kIoBadArgument);

        MC::SetCurrent(nullptr);
    }
    EndSection(7);

    // -----------------------------------------------------------------------
    BeginSection("every result has a name");
    {
        // Rule 5, applied to this enum: if two outcomes are supposed to be
        // distinguishable, something must be able to tell them apart. Names are
        // what a mod logs, so a duplicate name would make two different results
        // read identically in a bug report.
        const N::Result all[] = {
            N::Result::Ok, N::Result::Unsupported, N::Result::Unavailable,
            N::Result::NoModule, N::Result::NoSlots, N::Result::ModuleQuota,
            N::Result::BadHandle, N::Result::StaleHandle, N::Result::NotOwner,
            N::Result::BadArgument, N::Result::PlatformError,
        };
        constexpr int kCount = static_cast<int>(sizeof(all) / sizeof(all[0]));

        bool named = true, unique = true;
        for (int i = 0; i < kCount; ++i) {
            const char* n = N::ResultName(all[i]);
            if (!n || n[0] == '\0' || std::strcmp(n, "?") == 0) named = false;
            for (int j = i + 1; j < kCount; ++j) {
                if (std::strcmp(n, N::ResultName(all[j])) == 0) unique = false;
            }
        }
        ok("every result has a name", named);
        ok("and no two share one", unique);
        ok("the list covers the whole enum",
           kCount == static_cast<int>(N::Result::PlatformError) + 1);
    }
    EndSection(3);

    // -----------------------------------------------------------------------
    std::printf("\n");
    if (g_failures) {
        std::printf("NET TESTS FAILED: %d of %d checks\n", g_failures, g_checks);
        return 1;
    }

    // A floor, so a build that compiled away half the file cannot report
    // success. Raise it deliberately when checks are added.
    static const int kExpectedChecks = 93;
    if (g_checks < kExpectedChecks) {
        std::printf("NET TESTS INCOMPLETE: ran %d checks, expected at least %d\n",
                    g_checks, kExpectedChecks);
        return 1;
    }

    std::printf("ALL NET TESTS PASS (%d checks: attribution, handle validity, "
                "use-after-close, quotas, host limit, close-all, accept, "
                "untracked-accept, availability, platform reasons, half-close, "
                "accounting, result names)\n",
                g_checks);
    return 0;
}
