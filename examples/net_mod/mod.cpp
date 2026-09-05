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
static HandleFn  volatile g_Close     = &wiixl_import__wiixl_net__Close;
static HandleFn  volatile g_LocalIp   = &wiixl_import__wiixl_net__LocalIp;
static U32Fn     volatile g_Held      = &wiixl_import__wiixl_net__Held;
static U32Fn     volatile g_Quota     = &wiixl_import__wiixl_net__Quota;
static NameFn    volatile g_ResultName = &wiixl_import__wiixl_net__ResultName;

static const uint32_t kPort = 8080;
static const uint32_t kResultOk = 0;

// .bss, so the loader has to have zeroed it. volatile so the compiler cannot
// fold state it can see only this file writing.
static volatile uint32_t g_Listener;
static volatile uint32_t g_Served;

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

extern "C" __attribute__((used)) void WiiXLaunch_ModTick() {
    const uint32_t listener = g_Listener;
    if (!listener) return;

    AcceptFn accept = g_Accept;
    SendFn send = g_Send;
    HandleFn close = g_Close;
    if (!accept || !send || !close) return;

    // Non-blocking, so "nothing pending" is the ordinary answer every frame and
    // costs one syscall. Anything that can block here can freeze the game.
    uint32_t conn = 0;
    if (accept(listener, &conn) != kResultOk) return;

    // One request, one canned reply, one close. A real server belongs in a mod
    // of its own; what this proves is that the surface carries a connection end
    // to end and that the socket is given back.
    send(conn, kReply, sizeof(kReply) - 1);
    close(conn);

    const uint32_t n = g_Served + 1;
    g_Served = n;

    LogFn log = g_Log;
    if (log && n <= 3) {
        char line[96];
        char* o = AppendText(line, line + sizeof(line), "d_net: served request ");
        o = AppendU32(o, line + sizeof(line), n);
        if (n == 3) o = AppendText(o, line + sizeof(line), " (quiet from here)");
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

    r = bind(listener, kPort);
    LogResult("bind", r);
    if (r != kResultOk) { close(listener); return; }

    r = listen(listener, 4);
    LogResult("listen", r);
    if (r != kResultOk) { close(listener); return; }

    // The address you actually have to type. The console does not tell you.
    HandleFn localIp = g_LocalIp;
    if (localIp) {
        char line[96];
        char* o = AppendText(line, line + sizeof(line), "d_net: listening on http://");
        o = AppendIp(o, line + sizeof(line), localIp(listener));
        o = AppendText(o, line + sizeof(line), ":");
        o = AppendU32(o, line + sizeof(line), kPort);
        o = AppendText(o, line + sizeof(line), "/");
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
