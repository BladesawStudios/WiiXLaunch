// d_net.wxlm - the wiixl.net demonstration.
//
// It opens a real listener on a real port and answers a real request, so a boot
// proves the surface rather than the build proving it compiles. Point a browser
// or curl at the address this logs and you get a line back.
//
// It also does ONE THING WRONG ON PURPOSE: it closes a socket and then uses the
// closed handle. That is the case host handles exist for - with raw descriptors
// the stale send would have landed on whatever socket inherited the number, so
// the boot log carrying "STALE-HANDLE" is the property demonstrating itself
// rather than a comment claiming it.
//
// Everything the loader writes at runtime is volatile, for the reason in
// docs/modules.md: without it the compiler folds the import pointers into
// direct branches and the demonstration stops demonstrating anything.

#include <cstdint>

extern "C" {
    extern void     wiixl_import__wiixl_core__Log(const char* text);
    extern uint32_t wiixl_import__wiixl_core__RegisterTick(void (*fn)());

    // wiixl.net v1.0. Importing ANY of these makes wiixl.net a required
    // surface, so on a host that does not register it - Switch, which has no
    // socket implementation - this module is refused by name at load and never
    // runs at all. That refusal is the feature.
    extern uint32_t wiixl_import__wiixl_net__Available(void);
    extern uint32_t wiixl_import__wiixl_net__Open(uint32_t* outHandle);
    extern uint32_t wiixl_import__wiixl_net__SetNonBlocking(uint32_t handle);
    extern uint32_t wiixl_import__wiixl_net__SetReuseAddr(uint32_t handle);
    extern uint32_t wiixl_import__wiixl_net__Bind(uint32_t handle, uint32_t port);
    extern uint32_t wiixl_import__wiixl_net__Listen(uint32_t handle, uint32_t backlog);
    extern uint32_t wiixl_import__wiixl_net__Accept(uint32_t listener, uint32_t* outHandle);
    extern int32_t  wiixl_import__wiixl_net__Send(uint32_t handle, const void* buf, uint32_t len);
    extern int32_t  wiixl_import__wiixl_net__Recv(uint32_t handle, void* buf, uint32_t maxSize);
    // v1.1. Half-close. Sending a reply and closing while the request is still
    // unread makes TCP send an RST instead of a FIN, and the client loses the
    // reply - see the state machine below.
    extern uint32_t wiixl_import__wiixl_net__Shutdown(uint32_t handle, uint32_t how);
    extern uint32_t wiixl_import__wiixl_net__Close(uint32_t handle);
    extern uint32_t wiixl_import__wiixl_net__LocalIp(uint32_t handle);
    extern uint32_t wiixl_import__wiixl_net__Held(void);
    extern uint32_t wiixl_import__wiixl_net__Quota(void);
    // The host's own name for a result code. Imported rather than copied, so
    // this mod's log cannot drift from the host's enum the day a value is added.
    extern const char* wiixl_import__wiixl_net__ResultName(uint32_t result);
}

using LogFn     = void (*)(const char*);
using TickRegFn = uint32_t (*)(void (*)());
using U32Fn     = uint32_t (*)(void);
using OpenFn    = uint32_t (*)(uint32_t*);
using HandleFn  = uint32_t (*)(uint32_t);
using BindFn    = uint32_t (*)(uint32_t, uint32_t);
using AcceptFn  = uint32_t (*)(uint32_t, uint32_t*);
using SendFn    = int32_t  (*)(uint32_t, const void*, uint32_t);
using RecvFn    = int32_t  (*)(uint32_t, void*, uint32_t);
using NameFn    = const char* (*)(uint32_t);

static LogFn     volatile g_Log       = &wiixl_import__wiixl_core__Log;
static TickRegFn volatile g_RegTick   = &wiixl_import__wiixl_core__RegisterTick;
static U32Fn     volatile g_Available = &wiixl_import__wiixl_net__Available;
static OpenFn    volatile g_Open      = &wiixl_import__wiixl_net__Open;
static HandleFn  volatile g_NonBlock  = &wiixl_import__wiixl_net__SetNonBlocking;
static HandleFn  volatile g_ReuseAddr = &wiixl_import__wiixl_net__SetReuseAddr;
static BindFn    volatile g_Bind      = &wiixl_import__wiixl_net__Bind;
static BindFn    volatile g_Listen    = &wiixl_import__wiixl_net__Listen;
static AcceptFn  volatile g_Accept    = &wiixl_import__wiixl_net__Accept;
static SendFn    volatile g_Send      = &wiixl_import__wiixl_net__Send;
static RecvFn    volatile g_Recv      = &wiixl_import__wiixl_net__Recv;
static BindFn    volatile g_Shutdown  = &wiixl_import__wiixl_net__Shutdown;
static HandleFn  volatile g_Close     = &wiixl_import__wiixl_net__Close;
static HandleFn  volatile g_LocalIp   = &wiixl_import__wiixl_net__LocalIp;
static U32Fn     volatile g_Held      = &wiixl_import__wiixl_net__Held;
static U32Fn     volatile g_Quota     = &wiixl_import__wiixl_net__Quota;
static NameFn    volatile g_ResultName = &wiixl_import__wiixl_net__ResultName;

// SEVERAL PORTS, TRIED IN ORDER.
//
// The first boot of this mod failed at bind, and the reason was not the socket
// layer: something else on the machine already held 8080. A demonstration that
// only works when a popular port happens to be free proves nothing on the
// machines where it matters most, so it tries a few and says which it got.
//
// A real server should take its port from configuration rather than guessing;
// this is a sample, and guessing quietly is the thing worth avoiding.
static const uint32_t kPorts[] = { 8080, 8099, 9080, 51080 };
static const uint32_t kPortCount = sizeof(kPorts) / sizeof(kPorts[0]);
static const uint32_t kResultOk = 0;

// .bss, so the loader has to have zeroed it. volatile so the compiler cannot
// fold state it can see only this file writing.
static volatile uint32_t g_Listener;
static volatile uint32_t g_Served;

// --- one connection at a time, carried ACROSS TICKS -------------------------
//
// The first working boot of this mod served three requests and curl got
// "connection reset by peer" every time. Accept, send, close in a single tick
// looks right and is not:
//
//   - the client has usually not even SENT its request when accept() returns,
//     so closing immediately meets the request with a closed socket, and
//   - closing a socket with unread bytes still in its receive buffer makes TCP
//     send an RST rather than a FIN, which tells the client to DISCARD anything
//     it has not read yet - including the reply.
//
// So a connection lives across ticks: read until the request's headers are
// complete, then reply, then half-close, then close. That is the minimum an
// HTTP server can do and still be one.
constexpr uint32_t kNoConn = 0;
constexpr uint32_t kMaxConnTicks = 600;   // ~10s at 60fps, then give up

static volatile uint32_t g_Conn;          // handle, or kNoConn
static volatile uint32_t g_ConnTicks;
static volatile uint32_t g_ConnSent;      // bytes of the reply written so far
static volatile uint32_t g_ConnGotRequest;
static volatile uint32_t g_Match;         // how much of "\r\n\r\n" we have seen
// Whether anything has been read on THIS connection yet. The not-HTTP check
// below must look at the connection's genuinely first byte and no other: a
// request split across reads can easily resume mid-line, and "the chunk starts
// with a lowercase letter" would then reject a perfectly good request.
static volatile uint32_t g_ConnRead;
static volatile uint32_t g_Timeouts;      // connections that never finished a request
static volatile uint32_t g_NotHttp;       // connections that were not HTTP at all

// No libc here, so this module builds its own strings.
static char* AppendText(char* out, char* end, const char* text) {
    while (text && *text && out < end - 1) *out++ = *text++;
    return out;
}

static char* AppendU32(char* out, char* end, uint32_t v) {
    char digits[12];
    uint32_t n = 0;
    do { digits[n++] = static_cast<char>('0' + (v % 10u)); v /= 10u; } while (v);
    while (n && out < end - 1) *out++ = digits[--n];
    return out;
}

// Host order, most significant octet first - the way you would type it.
static char* AppendIp(char* out, char* end, uint32_t ip) {
    for (int i = 3; i >= 0; --i) {
        out = AppendU32(out, end, (ip >> (i * 8)) & 0xFFu);
        if (i) out = AppendText(out, end, ".");
    }
    return out;
}

static void LogResult(const char* what, uint32_t result) {
    LogFn log = g_Log;
    NameFn name = g_ResultName;
    if (!log) return;

    char line[128];
    char* o = AppendText(line, line + sizeof(line), "d_net: ");
    o = AppendText(o, line + sizeof(line), what);
    o = AppendText(o, line + sizeof(line), " -> ");
    o = AppendText(o, line + sizeof(line), name ? name(result) : "?");
    *o = 0;
    log(line);
}

// The reply. Deliberately a complete, tiny HTTP response: a browser shows it,
// and curl shows it, so "did the surface work" needs no special client.
static const char kReply[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 35\r\n"
    "Connection: close\r\n"
    "\r\n"
    "d_net.wxlm answering from wiixl.net";

// Closes the current connection politely: half-close first so the reply is not
// discarded, then release the socket back to the host.
static void FinishConn() {
    const uint32_t conn = g_Conn;
    if (conn == kNoConn) return;

    BindFn shutdown = g_Shutdown;
    HandleFn close = g_Close;
    if (shutdown) shutdown(conn, 1);   // 1 = stop sending; FIN, not RST
    if (close) close(conn);

    g_Conn = kNoConn;
    g_ConnTicks = 0;
    g_ConnSent = 0;
    g_ConnGotRequest = 0;
    g_ConnRead = 0;
    g_Match = 0;
}

extern "C" __attribute__((used)) void WiiXLaunch_ModTick() {
    const uint32_t listener = g_Listener;
    if (!listener) return;

    AcceptFn accept = g_Accept;
    SendFn send = g_Send;
    RecvFn recv = g_Recv;
    LogFn log = g_Log;
    if (!accept || !send || !recv) return;

    // --- take a connection if we are free ----------------------------------
    //
    // Non-blocking, so "nothing pending" is the ordinary answer every frame and
    // costs one call. Anything that can block here can freeze the game.
    if (g_Conn == kNoConn) {
        uint32_t conn = 0;
        if (accept(listener, &conn) != kResultOk) return;
        g_Conn = conn;
        g_ConnTicks = 0;
        g_ConnSent = 0;
        g_ConnGotRequest = 0;
        g_ConnRead = 0;
        g_Match = 0;
        return;   // the client has not sent anything yet; read next frame
    }

    const uint32_t conn = g_Conn;

    // A client that connects and says nothing must not hold the only slot for
    // the rest of the session.
    g_ConnTicks = g_ConnTicks + 1;
    if (g_ConnTicks > kMaxConnTicks) {
        // Silence is not a diagnosis. Before this line the connection was just
        // dropped, and a client that connected and never sent a valid request
        // looked exactly like a server that was not listening.
        const uint32_t t = g_Timeouts + 1;
        g_Timeouts = t;
        LogFn tlog = g_Log;
        if (tlog && t <= 3) {
            tlog("d_net: dropped a connection that never sent a complete HTTP "
                 "request within ~10s");
        }
        FinishConn();
        return;
    }

    // --- drain the request --------------------------------------------------
    //
    // This is the part whose absence caused the reset. The bytes are not needed
    // - the reply is the same either way - but they have to be TAKEN, or the
    // close below turns into an RST and the client discards the reply.
    if (!g_ConnGotRequest) {
        char in[128];
        const int32_t n = recv(conn, in, sizeof(in));
        if (n == 0) { FinishConn(); return; }        // peer went away
        if (n > 0) {
            // A request that is not HTTP at all. Every HTTP method starts with
            // an uppercase letter; a TLS ClientHello starts with 0x16, and
            // waiting ten seconds to time out on one tells the person at the
            // other end nothing. This is the exact case that cost a boot:
            // `curl https://...` against a plain-HTTP server.
            const bool firstRead = (g_ConnRead == 0);
            g_ConnRead = 1;
            if (firstRead && (in[0] < 'A' || in[0] > 'Z')) {
                const uint32_t k = g_NotHttp + 1;
                g_NotHttp = k;
                if (log && k <= 3) {
                    char line[144];
                    char* o = AppendText(line, line + sizeof(line),
                                         "d_net: that was not an HTTP request (first byte 0x");
                    const char* hex = "0123456789ABCDEF";
                    const uint8_t b = static_cast<uint8_t>(in[0]);
                    if (o < line + sizeof(line) - 1) *o++ = hex[(b >> 4) & 0xF];
                    if (o < line + sizeof(line) - 1) *o++ = hex[b & 0xF];
                    o = AppendText(o, line + sizeof(line),
                                   "). 0x16 means TLS - this server speaks plain "
                                   "http://, not https://");
                    *o = 0;
                    log(line);
                }
                FinishConn();
                return;
            }

            // Look for the blank line ending the headers, across reads.
            uint32_t m = g_Match;
            for (int32_t i = 0; i < n; ++i) {
                const char c = in[i];
                if ((m == 0 || m == 2) && c == '\r') ++m;
                else if ((m == 1 || m == 3) && c == '\n') ++m;
                else m = (c == '\r') ? 1u : 0u;
                if (m == 4) break;
            }
            g_Match = m;
            if (m == 4) g_ConnGotRequest = 1;
        }
        // n < 0 is "nothing yet" - try again next frame.
        if (!g_ConnGotRequest) return;
    }

    // --- write the reply, across as many ticks as it takes ------------------
    const uint32_t total = sizeof(kReply) - 1;
    uint32_t sent = g_ConnSent;
    if (sent < total) {
        const int32_t n = send(conn, kReply + sent, total - sent);
        if (n < 0) return;                            // would block; next frame
        sent += static_cast<uint32_t>(n);
        g_ConnSent = sent;
        if (sent < total) return;
    }

    FinishConn();

    const uint32_t served = g_Served + 1;
    g_Served = served;

    if (log && served <= 3) {
        char line[96];
        char* o = AppendText(line, line + sizeof(line), "d_net: served request ");
        o = AppendU32(o, line + sizeof(line), served);
        if (served == 3) o = AppendText(o, line + sizeof(line), " (quiet from here)");
        *o = 0;
        log(line);
    }
}

extern "C" __attribute__((used)) void WiiXLaunch_ModEntry() {
    LogFn log = g_Log;
    if (!log) return;

    U32Fn available = g_Available;
    OpenFn open = g_Open;
    if (!available || !open) {
        log("d_net: wiixl.net symbols missing - nothing to do");
        return;
    }

    // The surface is registered, which is why this module loaded at all. Whether
    // a socket can be opened RIGHT NOW is a separate question with its own
    // answer - on Cemu it depends on the title's process having a network stack.
    if (!available()) {
        log("d_net: wiixl.net is registered but no socket library is reachable "
            "here - see the Net: line above. Not listening.");
        return;
    }

    uint32_t listener = 0;
    uint32_t r = open(&listener);
    LogResult("open listener", r);
    if (r != kResultOk) return;

    HandleFn reuse = g_ReuseAddr;
    HandleFn nonBlock = g_NonBlock;
    BindFn bind = g_Bind;
    BindFn listen = g_Listen;
    HandleFn close = g_Close;
    if (!reuse || !nonBlock || !bind || !listen || !close) return;

    reuse(listener);

    // The one option this cannot do without. There is no fcntl in nsysnet, so
    // this is the only way to keep accept() off the game thread's neck.
    r = nonBlock(listener);
    LogResult("set non-blocking", r);
    if (r != kResultOk) { close(listener); return; }

    uint32_t port = 0;
    for (uint32_t i = 0; i < kPortCount; ++i) {
        r = bind(listener, kPorts[i]);
        if (r == kResultOk) { port = kPorts[i]; break; }

        char line[112];
        char* o = AppendText(line, line + sizeof(line), "d_net: port ");
        o = AppendU32(o, line + sizeof(line), kPorts[i]);
        o = AppendText(o, line + sizeof(line), " refused (");
        NameFn name = g_ResultName;
        o = AppendText(o, line + sizeof(line), name ? name(r) : "?");
        o = AppendText(o, line + sizeof(line), ") - see the Net: line for the "
                                               "platform's own reason");
        *o = 0;
        log(line);
    }
    if (!port) {
        log("d_net: no port in my list was free - not listening");
        close(listener);
        return;
    }

    r = listen(listener, 4);
    LogResult("listen", r);
    if (r != kResultOk) { close(listener); return; }

    // The address you actually have to type. The console does not tell you.
    HandleFn localIp = g_LocalIp;
    if (localIp) {
        // A socket bound to INADDR_ANY has no single local address, and under
        // Cemu SO_MYADDR comes back 0. Printing "http://0.0.0.0:8099/" as if it
        // were an address to type is a URL that cannot work - so when there is
        // no address to give, say what is actually true instead.
        const uint32_t ip = localIp(listener);
        char line[128];
        char* o = AppendText(line, line + sizeof(line), "d_net: listening on ");
        if (ip) {
            o = AppendText(o, line + sizeof(line), "http://");
            o = AppendIp(o, line + sizeof(line), ip);
            o = AppendText(o, line + sizeof(line), ":");
            o = AppendU32(o, line + sizeof(line), port);
            o = AppendText(o, line + sizeof(line), "/");
        } else {
            // The exact command, because "curl localhost" left the scheme to
            // be guessed and https:// was guessed twice. A plain-HTTP server
            // meeting a TLS handshake says nothing useful on its own.
            o = AppendText(o, line + sizeof(line), "all interfaces, port ");
            o = AppendU32(o, line + sizeof(line), port);
            o = AppendText(o, line + sizeof(line), " - try:  curl http://localhost:");
            o = AppendU32(o, line + sizeof(line), port);
            o = AppendText(o, line + sizeof(line), "/");
        }
        *o = 0;
        log(line);
    }

    g_Listener = listener;

    // --- what this module is allowed to hold --------------------------------
    U32Fn held = g_Held;
    U32Fn quota = g_Quota;
    if (held && quota) {
        char line[96];
        char* o = AppendText(line, line + sizeof(line), "d_net: holding ");
        o = AppendU32(o, line + sizeof(line), held());
        o = AppendText(o, line + sizeof(line), " of my ");
        o = AppendU32(o, line + sizeof(line), quota());
        o = AppendText(o, line + sizeof(line), " sockets");
        *o = 0;
        log(line);
    }

    // --- the deliberate mistake ---------------------------------------------
    //
    // Open a socket, close it, then use the closed handle. With raw descriptors
    // this is the send that lands on somebody else's connection; here it comes
    // back named, and the boot log shows the refusal happening rather than a
    // comment asserting that it would.
    SendFn send = g_Send;
    uint32_t doomed = 0;
    if (send && open(&doomed) == kResultOk) {
        close(doomed);
        const int32_t n = send(doomed, "this should not go anywhere", 27);
        if (n < 0) {
            char line[112];
            char* o = AppendText(line, line + sizeof(line),
                                 "d_net: using a closed handle was refused, code ");
            o = AppendU32(o, line + sizeof(line), static_cast<uint32_t>(-n));
            o = AppendText(o, line + sizeof(line), " - as it should be");
            *o = 0;
            log(line);
        } else {
            log("d_net: A CLOSED HANDLE STILL SENT DATA - the generation check "
                "is not working");
        }
    }

    // Accepting has to happen repeatedly, which is what a tick is for.
    TickRegFn regTick = g_RegTick;
    if (regTick && regTick(&WiiXLaunch_ModTick)) {
        log("d_net: registered a per-frame tick to accept connections");
    } else {
        log("d_net: RegisterTick was refused - nothing will be served. See the "
            "Tick: line above.");
    }
}
