// WUPS plugin glue for Wii U (Aroma).
#include <wiixlaunch/platform.hpp>

#if WIIXL_WIIU

#include <wups.h>
#include <wups/config_api.h>
#include <wups/config/WUPSConfigItemStub.h>
#include <notifications/notifications.h>
#include <wiixlaunch/generated_wiiu_config.hpp>
#include <wiixlaunch/wiiu/wiiu_backend.hpp>
#include <wiixlaunch/time.hpp>
#include <wiixlaunch/loader/loader.hpp>
#include <wiixlaunch/loader/core_surface.hpp>
#include <wiixlaunch/loader/arena.hpp>
#include <wiixlaunch/patches.hpp>
// Host::PatchesPersist - whether declared patches outlive the load.
#include <wiixlaunch/generated_host.hpp>
#include <coreinit/cache.h>
#include <coreinit/memdefaultheap.h>
#include <cstdio>

// Heap-allocated arena for loaded modules.
constexpr uint32_t kWiiUArenaSize = 2u * 1024u * 1024u;

// Flush data cache and invalidate instruction cache after loading modules.
static void WiiUFlush(uintptr_t addr, uint32_t size) {
    DCFlushRange(reinterpret_cast<void*>(addr), size);
    ICInvalidateRange(reinterpret_cast<void*>(addr), size);
}

WUPS_PLUGIN_NAME(WUPS_PLUGIN_NAME_STR);
WUPS_PLUGIN_DESCRIPTION(WUPS_PLUGIN_DESCRIPTION_STR);
WUPS_PLUGIN_VERSION(WUPS_PLUGIN_VERSION_STR);
WUPS_PLUGIN_AUTHOR(WUPS_PLUGIN_AUTHOR_STR);
WUPS_PLUGIN_LICENSE("GPLv3");

extern "C" void WiiXLaunch_Init();

static NotificationModuleStatus s_NotifyInitStatus = NOTIFICATION_MODULE_RESULT_LIB_UNINITIALIZED;
static WUPSConfigAPIStatus s_ConfigInitStatus = WUPSCONFIG_API_RESULT_LIB_UNINITIALIZED;

static WUPSConfigAPICallbackStatus ConfigMenuOpened(WUPSConfigCategoryHandle root) {
    namespace B = WiiXLaunch::Backend;

    // Static buffers: the config item may keep the pointer until the menu
    // closes, so these must outlive this callback.
    static char lines[4][96];
    snprintf(lines[0], sizeof(lines[0]), "FunctionPatcher init: %s",
             B::g_BackendInitOk ? "OK" : "FAILED");
    snprintf(lines[1], sizeof(lines[1]), "Hook patches: %lu ok / %lu failed%s%s",
             (unsigned long)B::g_PatchOkCount, (unsigned long)B::g_PatchFailCount,
             B::g_PatchFailCount ? " - " : "",
             B::g_PatchFailCount ? FunctionPatcher_GetStatusStr(B::g_LastPatchStatus) : "");
    snprintf(lines[2], sizeof(lines[2]), "Notification lib status: %d",
             (int)s_NotifyInitStatus);
    // Re-read on every menu open, so this doubles as a liveness check on
    // the console RTC rather than a value frozen at plugin load.
    char clock[20];
    WiiXLaunch::Time::FormatNow(clock, sizeof(clock));
    snprintf(lines[3], sizeof(lines[3]), "System clock: %s", clock);

    for (auto& line : lines) {
        WUPSConfigItemStub_AddToCategory(root, line);
    }
    return WUPSCONFIG_API_CALLBACK_RESULT_SUCCESS;
}

static void ConfigMenuClosed() {
}

INITIALIZE_PLUGIN() {
    s_NotifyInitStatus = NotificationModule_InitLibrary();

    WUPSConfigAPIOptionsV1 configOptions = {.name = WUPS_PLUGIN_NAME_STR};
    s_ConfigInitStatus = WUPSConfigAPI_Init(configOptions, ConfigMenuOpened, ConfigMenuClosed);

    WiiXLaunch_Init();

    namespace B = WiiXLaunch::Backend;
    char msg[128];
    if (!B::g_BackendInitOk) {
        NotificationModule_AddErrorNotification("WiiXLaunch: FunctionPatcher init FAILED");
    } else if (B::g_PatchFailCount > 0) {
        snprintf(msg, sizeof(msg), "WiiXLaunch: %lu hooks ok, %lu FAILED (%s)",
                 (unsigned long)B::g_PatchOkCount, (unsigned long)B::g_PatchFailCount,
                 FunctionPatcher_GetStatusStr(B::g_LastPatchStatus));
        NotificationModule_AddErrorNotification(msg);
    } else {
        snprintf(msg, sizeof(msg), "WiiXLaunch: %lu hooks registered",
                 (unsigned long)B::g_PatchOkCount);
        NotificationModule_AddInfoNotification(msg);
    }
}

// Wii U load point: runs when title starts and filesystem is ready.
ON_APPLICATION_START() {
    s_NotifyInitStatus = NotificationModule_InitLibrary();

    namespace B = WiiXLaunch::Backend;
    char msg[128];
    snprintf(msg, sizeof(msg), "WiiXLaunch: active (%lu hooks, init %s)",
             (unsigned long)B::g_PatchOkCount, B::g_BackendInitOk ? "ok" : "FAILED");
    NotificationModule_AddInfoNotification(msg);

    // Allocate module arena from default heap and configure flush hook.
    void* arena = MEMAllocFromDefaultHeapEx(kWiiUArenaSize, 64);
    if (arena) {
        WiiXLaunch::Arena::SetReservation(reinterpret_cast<uintptr_t>(arena),
                                          kWiiUArenaSize);
        WiiXLaunch::Loader::SetFlushHook(&WiiUFlush);
        WIIXL_LOG("[loader] arena %u B at %p", kWiiUArenaSize, arena);
    } else {
        WIIXL_LOG("[loader] could not allocate a %u B arena from the default "
                  "heap - every module will be refused for memory, and that is "
                  "this plugin's fault rather than theirs.", kWiiUArenaSize);
    }

    WIIXL_LOG("[loader] host ABI v%u, format v%u",
              WiiXLaunch::Core::kAbiVersion, WiiXLaunch::Wxlm::kFormatVersion);
    WiiXLaunch::Surface::LogRegistered();

    const uint32_t loaded = WiiXLaunch::Loader::LoadAll("WiiXLaunch/mods");

    // Verify declared patches and restore them unless configured to persist.
    WiiXLaunch::Patches::VerifyApplied();
    if constexpr (!WiiXLaunch::Host::PatchesPersist) {
        WiiXLaunch::Patches::RestoreAll();
    } else {
        WIIXL_LOG("Patch: this host keeps declared patches (patches.persist), so "
                  "they are verified and LEFT IN PLACE");
    }
    WiiXLaunch::Patches::LogState();

    if (loaded != 0) {
        WiiXLaunch::Loader::RunPhase(WiiXLaunch::Wxlm::Phase::Load);
        snprintf(msg, sizeof(msg), "WiiXLaunch: %lu module(s) loaded",
                 (unsigned long)loaded);
        NotificationModule_AddInfoNotification(msg);
    } else {
        // A module that was found and REJECTED must not report as an absent
        // one. Only the loader knows which happened, and the lines above say.
        WIIXL_LOG("[loader] no modules loaded. The game boots normally either way; "
                  "if the directory is simply empty that is the default state of a "
                  "fresh host, and the lines above say which it was.");
    }
}

#endif
