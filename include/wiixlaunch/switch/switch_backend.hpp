#pragma once

#include "../platform.hpp"

#if WIIXL_SWITCH

#include <lib.hpp>

namespace WiiXLaunch::Backend {

    inline bool InitSwitchBackend() {
        return true;
    }

    // An ABSOLUTE target, and untyped, for wiixl.core:InstallHook.
    //
    // The template below takes an OFFSET and adds the module base, because the
    // host's own hooks are written against Ghidra offsets. A module's target
    // has already been through wiixl.call and is a real address, so adding the
    // base again would aim it into nothing. Two functions rather than one flag:
    // the difference is which of two things the caller has, and a bool at the
    // call site says neither.
    //
    // Returns the trampoline to call to continue into the game, or 0.
    inline uintptr_t InstallHookAbsolute(uintptr_t targetAddr, uintptr_t callback) {
        if (targetAddr == 0 || callback == 0) return 0;
        void* orig = exl::hook::Hook(reinterpret_cast<void*>(targetAddr),
                                     reinterpret_cast<void*>(callback), true);
        return reinterpret_cast<uintptr_t>(orig);
    }

    template<typename Ret, typename... Args>
    inline void InstallHook(uptr offset, Ret(*callback)(Args...), Ret(**outOriginal)(Args...)) {
        uptr targetAddr = exl::util::modules::GetTargetStart() + offset;
        auto orig = exl::hook::Hook(reinterpret_cast<void*>(targetAddr), reinterpret_cast<void*>(callback), outOriginal != nullptr);
        if (outOriginal) {
            *outOriginal = reinterpret_cast<Ret(*)(Args...)>(orig);
        }
    }

}

#endif

#if WIIXL_SWITCH

extern "C" void WiiXLaunch_Init();

// Weak: this header is included from every game TU (it rides in via the
// umbrella wiixlaunch.hpp), so a multi-file project would otherwise hit
// "multiple definition of exl_main" at link. All copies are identical;
// weak linkage lets the linker keep one.
// Defined in src/switch_entry.cpp: reads and starts the .wxlm modules. Declared
// rather than inlined here because it needs loader.hpp, and this header rides
// in through the umbrella on every translation unit.
extern "C" void WiiXLaunch_SwitchLoadPoint();

extern "C" __attribute__((weak)) void exl_main(void* x0, void* x1) {
    exl::hook::Initialize();
    WiiXLaunch_Init();
    WiiXLaunch_SwitchLoadPoint();
}

extern "C" __attribute__((weak)) NORETURN void exl_exception_entry() {
    EXL_ABORT("Default exception handler called!");
}
#endif
