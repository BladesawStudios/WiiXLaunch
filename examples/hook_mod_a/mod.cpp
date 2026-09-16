// a_first.wxlm - part 1 of hook collision and chaining demonstration.
// Hooks HookProbeTarget before b_second.wxlm based on lexical order.
#include <cstdint>

extern "C" {
    extern void      wiixl_import__wiixl_core__Log(const char* text);
    extern uintptr_t wiixl_import__wiixl_core__InstallHook(uintptr_t target,
                                                           uintptr_t callback);
    extern uintptr_t wiixl_import__wiixl_core__HookProbeTarget(void);
    extern uint32_t  wiixl_import__wiixl_core__HookProbeClaimTag(uint32_t tag);
    extern void      wiixl_import__wiixl_core__HookProbeMark(uint32_t tag);
    extern int32_t   wiixl_import__wiixl_core__ModReadFile(const char* path,
                                                           void* buffer,
                                                           uint32_t maxSize);
    extern uint32_t  wiixl_import__wiixl_core__RegisterTick(void (*fn)());
}

using LogFn    = void (*)(const char*);
using HookFn   = uintptr_t (*)(uintptr_t, uintptr_t);
using TargetFn = uintptr_t (*)(void);
using VoidFn   = void (*)(void);
using ClaimFn  = uint32_t (*)(uint32_t);
using MarkFn   = void (*)(uint32_t);
using ReadFn   = int32_t (*)(const char*, void*, uint32_t);
using TickRegFn = uint32_t (*)(void (*)());

static const uint32_t kTag = 0xA1A1A1A1u;

static LogFn    volatile g_Log    = &wiixl_import__wiixl_core__Log;
static HookFn   volatile g_Hook   = &wiixl_import__wiixl_core__InstallHook;
static TargetFn volatile g_Target = &wiixl_import__wiixl_core__HookProbeTarget;
static ClaimFn  volatile g_Claim = &wiixl_import__wiixl_core__HookProbeClaimTag;
static MarkFn   volatile g_Mark  = &wiixl_import__wiixl_core__HookProbeMark;
static ReadFn   volatile g_Read  = &wiixl_import__wiixl_core__ModReadFile;
static TickRegFn volatile g_RegTick = &wiixl_import__wiixl_core__RegisterTick;

static volatile uint32_t g_Ticks;
static VoidFn volatile g_Original = nullptr;

static char* AppendText(char* out, char* end, const char* text) {
    while (text && *text && out < end - 1) *out++ = *text++;
    return out;
}

extern "C" __attribute__((used)) void WiiXLaunch_ModTick() {
    const uint32_t n = g_Ticks + 1;
    g_Ticks = n;

    // Log the first three ticks to confirm recurring callbacks.
    LogFn log = g_Log;
    if (log && n <= 3) {
        log(n == 1 ? "a_first: tick 1 - my per-frame callback is running"
                   : (n == 2 ? "a_first: tick 2"
                             : "a_first: tick 3 (quiet from here)"));
    }
}

extern "C" __attribute__((used)) void WiiXLaunch_ModHook() {
    LogFn log = g_Log;
    MarkFn mark = g_Mark;

    if (mark) mark(kTag);
    if (log) log("HookProbe: a_first ran (before Original)");

    VoidFn next = g_Original;
    if (next) next();

    if (mark) mark(kTag);
    if (log) log("HookProbe: a_first ran (after Original)");
}

extern "C" __attribute__((used)) void WiiXLaunch_ModEntry() {
    LogFn log = g_Log;
    if (!log) return;

    TargetFn getTarget = g_Target;
    HookFn install = g_Hook;
    if (!getTarget || !install) {
        log("a_first: wiixl.core v1.2 symbols missing - not hooking");
        return;
    }

    ClaimFn claim = g_Claim;
    if (claim && !claim(kTag)) {
        log("a_first: tag claim refused - not hooking, since the host could not "
            "attribute the marks");
        return;
    }

    const uintptr_t target = getTarget();
    if (!target) {
        log("a_first: no hook probe target on this host - not hooking");
        return;
    }

    // Scoped file reading: read own greeting.txt and verify directory traversal is blocked.
    ReadFn read = g_Read;
    if (read) {
        char buf[64];
        const int32_t n = read("greeting.txt", buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = 0;
            char line[128];
            char* o = AppendText(line, line + sizeof(line), "a_first: my greeting.txt says: ");
            o = AppendText(o, line + sizeof(line), buf);
            *o = 0;
            log(line);
        } else {
            log("a_first: could not read my own greeting.txt");
        }

        const int32_t esc = read("../b_second/greeting.txt", buf, sizeof(buf) - 1);
        if (esc < 0) {
            log("a_first: reading ../b_second/greeting.txt was refused, as it should be");
        } else {
            log("a_first: ESCAPED ITS OWN DIRECTORY - containment is broken");
        }
    }

    TickRegFn regTick = g_RegTick;
    if (regTick) {
        if (regTick(&WiiXLaunch_ModTick)) {
            log("a_first: registered a per-frame tick");
        } else {
            log("a_first: RegisterTick was refused - see the Tick: line above");
        }
    }

    const uintptr_t original =
        install(target, reinterpret_cast<uintptr_t>(&WiiXLaunch_ModHook));
    if (!original) {
        log("a_first: InstallHook REFUSED - see the Hook: line above for why");
        return;
    }

    g_Original = reinterpret_cast<VoidFn>(original);
    log("a_first: hooked the probe, Original captured");
}
