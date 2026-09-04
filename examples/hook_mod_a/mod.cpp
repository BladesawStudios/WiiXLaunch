// a_first.wxlm - one half of the two-module collision demonstration.
//
// This mod and examples/hook_mod_b hook THE SAME ADDRESS on purpose. That is
// the point: two mods sharing one function is the situation the whole central
// registry exists for, and a pair of mods that did not interact would prove
// directory enumeration and nothing else.
//
// The filenames decide the order. Load order is lexical by filename
// (docs/loader.md), load order is hook install order, and hook install order is
// call order (docs/hooks.md). "a_first.wxlm" sorts before "b_second.wxlm", so
// this one runs first, and the boot log should read:
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
// docs/modules.md: without it the compiler folds the import pointers into
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
}

using LogFn    = void (*)(const char*);
using HookFn   = uintptr_t (*)(uintptr_t, uintptr_t);
using TargetFn = uintptr_t (*)(void);
using VoidFn   = void (*)(void);

static LogFn    volatile g_Log    = &wiixl_import__wiixl_core__Log;
static HookFn   volatile g_Hook   = &wiixl_import__wiixl_core__InstallHook;
static TargetFn volatile g_Target = &wiixl_import__wiixl_core__HookProbeTarget;

// The next link in the chain. Written by the host at install time, so volatile
// for the same reason the imports are - and read back through the pointer
// rather than through whatever the compiler thinks it knows.
static VoidFn volatile g_Original = nullptr;

extern "C" __attribute__((used)) void WiiXLaunch_ModHook() {
    LogFn log = g_Log;
    if (log) log("HookProbe: a_first ran (before Original)");

    // Calling Original is what continues the chain. A mod that returns here
    // instead has REPLACED the function - legal, reported, and it would show up
    // as b_second and the host body never running.
    VoidFn next = g_Original;
    if (next) next();

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

    const uintptr_t target = getTarget();
    if (!target) {
        log("a_first: no hook probe target on this host - not hooking");
        return;
    }

    const uintptr_t original =
        install(target, reinterpret_cast<uintptr_t>(&WiiXLaunch_ModHook));
    if (!original) {
        // Refused. Says so rather than leaving a mod that looks installed and
        // silently never runs.
        log("a_first: InstallHook REFUSED - see the Hook: line above for why");
        return;
    }

    g_Original = reinterpret_cast<VoidFn>(original);
    log("a_first: hooked the probe, Original captured");
}
