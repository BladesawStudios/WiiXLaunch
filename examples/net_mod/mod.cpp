// d_net.wxlm - wiixl.net demonstration module.
// Listens on a port and serves a small HTTP response across ticks.
#include <cstdint>

extern "C" {
    extern void     wiixl_import__wiixl_core__Log(const char* text);
    extern uint32_t wiixl_import__wiixl_core__RegisterTick(void (*fn)());

    // wiixl.net imports.
    extern uint32_t wiixl_import__wiixl_net__Available(void);
    extern uint32_t wiixl_import__wiixl_net__Open(uint32_t* outHandle);
    extern uint32_t wiixl_import__wiixl_net__SetNonBlocking(uint32_t handle);
    extern uint32_t wiixl_import__wiixl_net__SetReuseAddr(uint32_t handle);
    extern uint32_t wiixl_import__wiixl_net__Bind(uint32_t handle, uint32_t port);
    extern uint32_t wiixl_import__wiixl_net__Listen(uint32_t handle, uint32_t backlog);
    extern uint32_t wiixl_import__wiixl_net__Accept(uint32_t listener, uint32_t* outHandle);
    extern int32_t  wiixl_import__wiixl_net__Send(uint32_t handle, const void* buf, uint32_t len);
    extern int32_t  wiixl_import__wiixl_net__Recv(uint32_t handle, void* buf, uint32_t maxSize);
    extern uint32_t wiixl_import__wiixl_net__Shutdown(uint32_t handle, uint32_t how);
    extern uint32_t wiixl_import__wiixl_net__Close(uint32_t handle);
    extern uint32_t wiixl_import__wiixl_net__LocalIp(uint32_t handle);
    extern uint32_t wiixl_import__wiixl_net__Held(void);
    extern uint32_t wiixl_import__wiixl_net__Quota(void);
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

// Candidate ports to try in order.
static const uint32_t kPorts[] = { 8080, 8099, 9080, 51080 };
static const uint32_t kPortCount = sizeof(kPorts) / sizeof(kPorts[0]);
static const uint32_t kResultOk = 0;

static volatile uint32_t g_Listener;
static volatile uint32_t g_Served;

// Non-blocking connection state machine across ticks.
constexpr uint32_t kNoConn = 0;
constexpr uint32_t kMaxConnTicks = 600;   // ~10s at 60fps

static volatile uint32_t g_Conn;          // handle, or kNoConn
static volatile uint32_t g_ConnTicks;
static volatile uint32_t g_ConnSent;      // bytes of the reply written so far
static volatile uint32_t g_ConnGotRequest;
static volatile uint32_t g_Match;         // how much of "\r\n\r\n" we have seen
static volatile uint32_t g_ConnRead;
static volatile uint32_t g_Timeouts;
static volatile uint32_t g_NotHttp;

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

    // Accept incoming connection non-blockingly if free.
    if (g_Conn == kNoConn) {
        uint32_t conn = 0;
        if (accept(listener, &conn) != kResultOk) return;
        g_Conn = conn;
        g_ConnTicks = 0;
        g_ConnSent = 0;
        g_ConnGotRequest = 0;
        g_ConnRead = 0;
        g_Match = 0;
        return;
    }

    const uint32_t conn = g_Conn;

    // Drop connection if request is not received within ~10s.
    g_ConnTicks = g_ConnTicks + 1;
    if (g_ConnTicks > kMaxConnTicks) {
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

    // Drain request headers.
    if (!g_ConnGotRequest) {
        char in[128];
        const int32_t n = recv(conn, in, sizeof(in));
        if (n == 0) { FinishConn(); return; }
        if (n > 0) {
            // Reject non-HTTP traffic early (e.g. TLS handshake).
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
                                   "). Plain HTTP only, not HTTPS.");
                    *o = 0;
                    log(line);
                }
                FinishConn();
                return;
            }

            // Look for end of HTTP headers (\r\n\r\n).
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
        if (!g_ConnGotRequest) return;
    }

    // Send HTTP reply across ticks.
    const uint32_t total = sizeof(kReply) - 1;
    uint32_t sent = g_ConnSent;
    if (sent < total) {
        const int32_t n = send(conn, kReply + sent, total - sent);
        if (n < 0) return;
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
        o = AppendText(o, line + sizeof(line), ")");
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

    HandleFn localIp = g_LocalIp;
    if (localIp) {
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

    // Verify stale handle detection: using a closed handle must fail.
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
            o = AppendText(o, line + sizeof(line), " - as expected");
            *o = 0;
            log(line);
        } else {
            log("d_net: A CLOSED HANDLE STILL SENT DATA - generation check failed");
        }
    }

    TickRegFn regTick = g_RegTick;
    if (regTick && regTick(&WiiXLaunch_ModTick)) {
        log("d_net: registered a per-frame tick to accept connections");
    } else {
        log("d_net: RegisterTick was refused - nothing will be served.");
    }
}
