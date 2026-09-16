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

// Define before including this header to claim an owner name for attribution
// in the conflict log. "host" is the default for the framework itself.
#ifndef WIIXL_HOOK_OWNER
#define WIIXL_HOOK_OWNER "host"
#endif

#define WIIXL_HOOK_DEFINE_REPLACE(name) \
    struct name : public ::WiiXLaunch::impl::ReplaceHookBase<name>

#define WIIXL_HOOK_DEFINE_TRAMPOLINE(name) \
    struct name : public ::WiiXLaunch::impl::TrampolineHookBase<name>

namespace WiiXLaunch::impl {

    // Every hook goes through here on every platform, so nothing installs
    // without being recorded. Cemu's WiiXLaunch::Hooks owns the chain end to
    // end; on Switch and Wii U, exlaunch and WUPS build their own
    // trampolines and the manager only records ownership for the conflict
    // report.
    //
    // Templated on the callback and Original types rather than void*: the
    // Switch backend deduces its hook signature from the function pointer,
    // so erasing the type here would fail to compile there. Only the Cemu
    // path, which speaks in raw addresses, casts them away.
    template <typename Cb, typename Orig>
    inline void InstallVia(uptr target, Cb callback, Orig* originalOut,
                           const char* fallbackOwner) {
        // A hook installed while a module's entry is running belongs to
        // that module; WIIXL_HOOK_OWNER is a build-time name and can't know
        // which .wxlm is executing.
        const char* owner = ::WiiXLaunch::Hooks::CurrentOwner();
        if (!owner) owner = fallbackOwner;
#if WIIXL_CEMU
        // A payload callback is linked at 0 and lives in the code cave, so
        // its compile-time address needs biasing by where the payload
        // landed.
        uptr cb = reinterpret_cast<uptr>(reinterpret_cast<void*>(callback));
        if (cb < 0x01000000u) cb += ::WiiXLaunch::Backend::g_CodeCaveBase;

        uintptr_t original = 0;
        ::WiiXLaunch::Hooks::InstallHook(target, cb, owner, &original);
        if (originalOut) *originalOut = reinterpret_cast<Orig>(original);
#else
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
