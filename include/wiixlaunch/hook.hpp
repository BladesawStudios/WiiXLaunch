#pragma once

#include <utility>
#include "platform.hpp"
#include "offsets.hpp"
#include "context.hpp"
#include "hook_manager.hpp"

#if WIIXL_SWITCH
    #include "switch/switch_backend.hpp"
#elif WIIXL_WIIU
    #include "wiiu/wiiu_backend.hpp"
#elif WIIXL_CEMU
    #include "wiixl_cemu_backend.hpp"
#endif

#if WIIXL_SWITCH

#define WIIXL_HOOK_REPLACE(HookName, RetType, SwitchOffset, WiiUOffset, ...) \
    struct HookName { \
        static constexpr ::WiiXLaunch::uptr TargetOffset = WIIXL_OFFSET(SwitchOffset, WiiUOffset); \
        static RetType (*Original)(__VA_ARGS__); \
        static RetType Callback(__VA_ARGS__); \
        static void Install() { \
            ::WiiXLaunch::Backend::InstallHook(TargetOffset, &Callback, &Original); \
        } \
    }; \
    RetType (*HookName::Original)(__VA_ARGS__) = nullptr; \
    RetType HookName::Callback(__VA_ARGS__)

#elif WIIXL_WIIU

#define WIIXL_HOOK_REPLACE(HookName, RetType, SwitchOffset, WiiUOffset, ...) \
    struct HookName { \
        static constexpr ::WiiXLaunch::uptr TargetOffset = WIIXL_OFFSET(SwitchOffset, WiiUOffset); \
        static RetType (*Original)(__VA_ARGS__); \
        static RetType Callback(__VA_ARGS__); \
        static void Install(const uint64_t* titleIds = nullptr, uint32_t count = 0) { \
            ::WiiXLaunch::Backend::AddPPCExecutablePatch( \
                reinterpret_cast<void*>(&Callback), \
                reinterpret_cast<void**>(&Original), \
                TargetOffset, \
                titleIds, \
                count \
            ); \
        } \
    }; \
    RetType (*HookName::Original)(__VA_ARGS__) = nullptr; \
    RetType HookName::Callback(__VA_ARGS__)

#elif WIIXL_CEMU

#define WIIXL_HOOK_REPLACE(HookName, RetType, SwitchOffset, WiiUOffset, ...) \
    struct HookName { \
        static constexpr ::WiiXLaunch::uptr TargetOffset = WIIXL_OFFSET(SwitchOffset, WiiUOffset); \
        static RetType (*Original)(__VA_ARGS__); \
        static RetType Callback(__VA_ARGS__); \
        static void Install() { \
            ::WiiXLaunch::impl::InstallVia(TargetOffset, \
                reinterpret_cast<void*>(&Callback), \
                reinterpret_cast<void**>(&Original), WIIXL_HOOK_OWNER); \
        } \
    }; \
    RetType (*HookName::Original)(__VA_ARGS__) = nullptr; \
    RetType HookName::Callback(__VA_ARGS__)

#endif

// WHO OWNS A HOOK. Every install is attributed, because the entire point of a
// central registry is that when two things hook one address the log can name
// both. Define this before including the header to claim a name - a game module
// uses its own, and the loader passes a mod id when it installs a .wxlm's
// declared hooks. "host" is the honest default for the framework itself.
#ifndef WIIXL_HOOK_OWNER
#define WIIXL_HOOK_OWNER "host"
#endif

#define WIIXL_HOOK_DEFINE_REPLACE(name) \
    struct name : public ::WiiXLaunch::impl::ReplaceHookBase<name>

#define WIIXL_HOOK_DEFINE_TRAMPOLINE(name) \
    struct name : public ::WiiXLaunch::impl::TrampolineHookBase<name>

namespace WiiXLaunch::impl {

    // EVERY hook goes through here, on every platform. That is what makes the
    // registry central rather than advisory: there is no second path that
    // installs a hook without being recorded.
    //
    // The chain itself is built differently per platform, and honestly so:
    //
    //   Cemu   WiiXLaunch::Hooks owns it end to end - it captures the prologue
    //          once, emits every jump, and chains by construction.
    //   Switch exlaunch installs and builds its own trampoline. Wii U, WUPS.
    //          Those are not ours to reimplement, so the manager records the
    //          ownership and reports conflicts, and the platform does the
    //          patching. Conflict reporting is identical everywhere; only the
    //          chaining mechanism differs.
    //
    // Recording on all three is the point. "Two mods hooked this address" is a
    // diagnosis a user needs whatever they are playing on.
    // Templated on the callback and Original types rather than taking void*.
    // The Switch backend deduces its hook signature from the function pointer
    // it is handed, so erasing the types here fails to compile there - the
    // typed pointers have to survive all the way to the platform call. Only
    // the Cemu path, which speaks in raw addresses, casts them away.
    template <typename Cb, typename Orig>
    inline void InstallVia(uptr target, Cb callback, Orig* originalOut,
                           const char* owner) {
#if WIIXL_CEMU
        // A payload callback is linked at 0 and lives in the code cave, so its
        // compile-time address has to be biased by where the payload landed.
        // This was in Backend::InstallHook; it belongs wherever the payload's
        // own addresses are turned into real ones.
        uptr cb = reinterpret_cast<uptr>(reinterpret_cast<void*>(callback));
        if (cb < 0x01000000u) cb += ::WiiXLaunch::Backend::g_CodeCaveBase;

        uintptr_t original = 0;
        ::WiiXLaunch::Hooks::InstallHook(target, cb, owner, &original);
        if (originalOut) *originalOut = reinterpret_cast<Orig>(original);
#else
        // The platform installs; the manager records, so the conflict report
        // exists here too.
        ::WiiXLaunch::Hooks::Note(
            target, reinterpret_cast<uintptr_t>(reinterpret_cast<void*>(callback)), owner);
#if WIIXL_SWITCH
        ::WiiXLaunch::Backend::InstallHook(target, callback, originalOut);
#elif WIIXL_WIIU
        ::WiiXLaunch::Backend::AddPPCExecutablePatch(
            reinterpret_cast<void*>(callback),
            reinterpret_cast<void**>(originalOut), target, nullptr, 0);
#else
        (void)target; (void)callback; (void)originalOut;
#endif
#endif
    }

    template<typename Derived>
    class ReplaceHookBase {
    public:
        template<typename T = Derived>
        using CallbackFuncPtr = decltype(&T::Callback);

        static auto& OrigRef() {
            static CallbackFuncPtr<> s_FnPtr = nullptr;
            return s_FnPtr;
        }

        template<typename... Args>
        static decltype(auto) Orig(Args&&... args) {
            return OrigRef()(std::forward<Args>(args)...);
        }

        static void Install(uptr switchOffset, uptr wiiuOffset) {
            uptr targetOffset = WIIXL_OFFSET(switchOffset, wiiuOffset);
            InstallVia(targetOffset, &Derived::Callback, &OrigRef(), WIIXL_HOOK_OWNER);
        }
    };

    template<typename Derived>
    class TrampolineHookBase : public ReplaceHookBase<Derived> {};

}
