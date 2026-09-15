// b_second.wxlm - the other half of the two-module collision demonstration.
//
// This mod and examples/hook_mod_a hook THE SAME ADDRESS on purpose. That is
// the point: two mods sharing one function is the situation the whole central
// registry exists for, and a pair of mods that did not interact would prove
// directory enumeration and nothing else.
//
// The filenames decide the order. Load order is lexical by filename
// (docs/framework/loader.md), load order is hook install order, and hook install order is
// call order (docs/framework/hooks.md). "a_first.wxlm" sorts before "b_second.wxlm", so
// this one runs SECOND, and the boot log should read:
//
//   HookProbe: a_first ran (before Original)
//   HookProbe: b_second ran (before Original)
//   HookProbe: host body ran (this is the end of the chain)
//   HookProbe: b_second ran (after Original)
//   HookProbe: a_first ran (after Original)
//
// The nesting is the assertion. Anything else - a different order, a missing
// line, one mod's "after" without its "before" - means the chain is wrong, and
// each line names which mod it came from so it is obvious which.
//
// Everything the loader writes at runtime is volatile, for the reason in
// docs/framework/modules.md: without it the compiler folds the import pointers into
// direct branches and the test stops testing anything.

#include <cstdint>

extern "C" {
    extern void      wiixl_import__wiixl_core__Log(const char* text);
    // v1.2. A mod does NOT pass its own name here - the loader attributes the
    // hook to whichever module it is currently running, so a mod cannot claim
    // to be someone else in the conflict report.
    extern uintptr_t wiixl_import__wiixl_core__InstallHook(uintptr_t target,
                                                           uintptr_t callback);
    extern uintptr_t wiixl_import__wiixl_core__HookProbeTarget(void);
    // v1.3. The tag is bound to THIS module at claim time, by the host, from
    // whichever module the loader is running - not from anything passed here.
    // See wiixlaunch/hook_probe.hpp.
    extern uint32_t  wiixl_import__wiixl_core__HookProbeClaimTag(uint32_t tag);
    extern void      wiixl_import__wiixl_core__HookProbeMark(uint32_t tag);
    // v1.4. THIS module's directory and nothing outside it. There is a separate
    // GameReadFile for game content - two calls rather than one that falls back,
    // so which was meant is legible here rather than decided by resolution
    // order. See wiixlaunch/mod_fs.hpp.
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

// This module's marker. Self-chosen, but worthless on its own: the host refuses
// a tag another module already claimed, and records the binding itself, so the
// ordering assertion is against the host's record rather than this number.
static const uint32_t kTag = 0xB2B2B2B2u;

static LogFn    volatile g_Log    = &wiixl_import__wiixl_core__Log;
static HookFn   volatile g_Hook   = &wiixl_import__wiixl_core__InstallHook;
static TargetFn volatile g_Target = &wiixl_import__wiixl_core__HookProbeTarget;
static ClaimFn  volatile g_Claim = &wiixl_import__wiixl_core__HookProbeClaimTag;
static MarkFn   volatile g_Mark  = &wiixl_import__wiixl_core__HookProbeMark;
static ReadFn   volatile g_Read  = &wiixl_import__wiixl_core__ModReadFile;

// The next link in the chain. Written by the host at install time, so volatile
// for the same reason the imports are - and read back through the pointer
// rather than through whatever the compiler thinks it knows.
static VoidFn volatile g_Original = nullptr;

// wiixl.core's Log is not varargs on purpose, so a module that wants to put a
// value in a line builds the line itself. No libc here.
static char* AppendText(char* out, char* end, const char* text) {
    while (text && *text && out < end - 1) *out++ = *text++;
    return out;
}

extern "C" __attribute__((used)) void WiiXLaunch_ModHook() {
    LogFn log = g_Log;
    MarkFn mark = g_Mark;

    // The mark is what the host verifies; the log line is for a human reading
    // the boot. Both, because a failing run wants the narrative and the verdict.
    if (mark) mark(kTag);
    if (log) log("HookProbe: b_second ran (before Original)");

    // Calling Original is what continues the chain. A mod that returns here
    // instead has REPLACED the function - legal, reported, and it would show up
    // as b_second and the host body never running.
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

    // Claim the tag BEFORE hooking. The host binds it to this module because
    // this is the module it is currently running - the binding is the host's
    // observation, not this module's assertion.
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

    // --- this module's own directory ---------------------------------------
    //
    // Both demonstration mods ship a file called greeting.txt. Before
    // namespacing that was a collision decided by whichever resolved first;
    // now they are two different files and neither mod had to know the other
    // existed. The host decides which directory this reads from, from the
    // module it is running - so this cannot read a_first's copy by asking nicely.
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

        // And the containment, demonstrated rather than asserted in a comment.
        // A negative result is a NAMED refusal - see the kModRead* constants in
        // wiixl.core - not a generic failure.
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
        // Refused. Says so rather than leaving a mod that looks installed and
        // silently never runs.
        log("b_second: InstallHook REFUSED - see the Hook: line above for why");
        return;
    }

    g_Original = reinterpret_cast<VoidFn>(original);
    log("b_second: hooked the probe, Original captured");
}
