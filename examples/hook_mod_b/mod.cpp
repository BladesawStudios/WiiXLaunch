// b_second.wxlm - part 2 of hook collision and chaining demonstration.
// Hooks HookProbeTarget after a_first.wxlm based on lexical order.
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
}

using LogFn    = void (*)(const char*);
using HookFn   = uintptr_t (*)(uintptr_t, uintptr_t);
using TargetFn = uintptr_t (*)(void);
using VoidFn   = void (*)(void);
using ClaimFn  = uint32_t (*)(uint32_t);
using MarkFn   = void (*)(uint32_t);
using ReadFn   = int32_t (*)(const char*, void*, uint32_t);

static const uint32_t kTag = 0xB2B2B2B2u;

static LogFn    volatile g_Log    = &wiixl_import__wiixl_core__Log;
static HookFn   volatile g_Hook   = &wiixl_import__wiixl_core__InstallHook;
static TargetFn volatile g_Target = &wiixl_import__wiixl_core__HookProbeTarget;
static ClaimFn  volatile g_Claim = &wiixl_import__wiixl_core__HookProbeClaimTag;
static MarkFn   volatile g_Mark  = &wiixl_import__wiixl_core__HookProbeMark;
static ReadFn   volatile g_Read  = &wiixl_import__wiixl_core__ModReadFile;

static VoidFn volatile g_Original = nullptr;

static char* AppendText(char* out, char* end, const char* text) {
    while (text && *text && out < end - 1) *out++ = *text++;
    return out;
}

extern "C" __attribute__((used)) void WiiXLaunch_ModHook() {
    LogFn log = g_Log;
    MarkFn mark = g_Mark;

    if (mark) mark(kTag);
    if (log) log("HookProbe: b_second ran (before Original)");

    VoidFn next = g_Original;
    if (next) next();

    if (mark) mark(kTag);
    if (log) log("HookProbe: b_second ran (after Original)");
}

extern "C" __attribute__((used)) void WiiXLaunch_ModEntry() {
    LogFn log = g_Log;
    if (!log) return;

    TargetFn getTarget = g_Target;
    HookFn install = g_Hook;
    if (!getTarget || !install) {
        log("b_second: wiixl.core v1.2 symbols missing - not hooking");
        return;
    }

    ClaimFn claim = g_Claim;
    if (claim && !claim(kTag)) {
        log("b_second: tag claim refused - not hooking, since the host could not "
            "attribute the marks");
        return;
    }

    const uintptr_t target = getTarget();
    if (!target) {
        log("b_second: no hook probe target on this host - not hooking");
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
            char* o = AppendText(line, line + sizeof(line), "b_second: my greeting.txt says: ");
            o = AppendText(o, line + sizeof(line), buf);
            *o = 0;
            log(line);
        } else {
            log("b_second: could not read my own greeting.txt");
        }

        const int32_t esc = read("../a_first/greeting.txt", buf, sizeof(buf) - 1);
        if (esc < 0) {
            log("b_second: reading ../a_first/greeting.txt was refused, as it should be");
        } else {
            log("b_second: ESCAPED ITS OWN DIRECTORY - containment is broken");
        }
    }

    const uintptr_t original =
        install(target, reinterpret_cast<uintptr_t>(&WiiXLaunch_ModHook));
    if (!original) {
        log("b_second: InstallHook REFUSED - see the Hook: line above for why");
        return;
    }

    g_Original = reinterpret_cast<VoidFn>(original);
    log("b_second: hooked the probe, Original captured");
}
