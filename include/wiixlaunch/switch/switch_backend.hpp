#pragma once

#include "../platform.hpp"

#if WIIXL_SWITCH

#include <lib.hpp>

namespace WiiXLaunch::Backend {

    inline bool InitSwitchBackend() {
        return true;
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
