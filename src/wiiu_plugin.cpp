// WUPS plugin glue - Wii U (Aroma) only. This is what makes the built .wps a
// loadable plugin: Aroma reads the WUPS_PLUGIN_* metadata sections and calls
// INITIALIZE_PLUGIN() at plugin load, which is our one entry into
// WiiXLaunch_Init().
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
#include <coreinit/cache.h>
#include <coreinit/memdefaultheap.h>
#include <cstdio>

// THE ARENA, and the two things this platform needs that the others state
// differently.
//
// WHERE. Cemu reads the tail of its code cave and Switch reserves space in its
// own .text, because Horizon will not let one address be both writable and
// executable. Wii U needs neither trick: memory from the default heap can be
// written and then executed, which is how WUMS loads and runs the plugins
// themselves. So the reservation is an ordinary allocation and there is no
// write alias - Arena::Writable() stays the identity here.
//
// 2 MB because it costs nothing on a console with MEM2 to spare, and because
// it puts the best-effort grant at the same 256 KB cap Cemu lands on rather
// than at a fraction of it.
constexpr uint32_t kWiiUArenaSize = 2u * 1024u * 1024u;

// CACHE MAINTENANCE, and it is not optional.
//
// The loader has just written instructions through the data cache and is about
// to branch into them through the instruction cache. The Espresso does not
// reconcile those on its own. Until now this platform used the loader's
// DEFAULT flush hook, which is an empty function everywhere except Cemu - so a
// module would have loaded, relocated correctly, and then executed whatever
// happened to be in the instruction cache at that address.
//
// That failure needs hardware to see and says nothing useful when it happens,
// which is the worst combination. Cemu never showed it because the emulator's
// Backend::FlushCache was always wired up there.
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

// The Wii U load point.
//
// Cemu has to nominate one as an ADDRESS INSIDE THE GAME, which is knowledge
// only a game module has - so there it is botw/load_point.hpp that calls the
// loader. Aroma gives us a lifecycle event instead, at a point where the title
// is up and coreinit FS is usable, so the host can drive the loader itself and
// module loading on this platform does not depend on any game module.
//
// INITIALIZE_PLUGIN is too early for it: that runs at plugin load, before a
// title, and WiiXLaunch_Init's job there is registering surfaces. Modules are
// read here, once a title has actually started.
ON_APPLICATION_START() {
    s_NotifyInitStatus = NotificationModule_InitLibrary();

    namespace B = WiiXLaunch::Backend;
    char msg[128];
    snprintf(msg, sizeof(msg), "WiiXLaunch: active (%lu hooks, init %s)",
             (unsigned long)B::g_PatchOkCount, B::g_BackendInitOk ? "ok" : "FAILED");
    NotificationModule_AddInfoNotification(msg);

    // The reservation and the flush, before anything can ask for memory.
    // Both are this platform's answers to questions the other two answer
    // elsewhere; see the comments on kWiiUArenaSize and WiiUFlush.
    void* arena = MEMAllocFromDefaultHeapEx(kWiiUArenaSize, 64);
    if (arena) {
        WiiXLaunch::Arena::SetReservation(reinterpret_cast<uintptr_t>(arena),
                                          kWiiUArenaSize);
        WiiXLaunch::Loader::SetFlushHook(&WiiUFlush);
        WIIXL_LOG("[loader] arena %u B at %p", kWiiUArenaSize, arena);
    } else {
        // Not the same as "no modules", and the loader would otherwise report
        // it as ARENA-NOT-READY without saying who failed to provide one.
        WIIXL_LOG("[loader] could not allocate a %u B arena from the default "
                  "heap - every module will be refused for memory, and that is "
                  "this plugin's fault rather than theirs.", kWiiUArenaSize);
    }

    // What this host is and what it offers, logged before any module is read,
    // so a rejection further down can be read against it.
    WIIXL_LOG("[loader] host ABI v%u, format v%u",
              WiiXLaunch::Core::kAbiVersion, WiiXLaunch::Wxlm::kFormatVersion);
    WiiXLaunch::Surface::LogRegistered();

    // Lexical filename order, which is also hook priority - a specification
    // rather than an enumeration artefact. See docs/loader.md.
    const uint32_t loaded = WiiXLaunch::Loader::LoadAll("WiiXLaunch/mods");

    // Declared patches are applied during the loads above; they are verified
    // and put back HERE, between LoadAll and RunPhase, so no module code runs
    // while the game is modified. Same ordering as the Cemu load point, and for
    // the same reason - it was wrong there once and the boot log said so.
    //
    // A host shipping REAL patch mods must delete the RestoreAll call; a patch
    // is meant to persist.
    WiiXLaunch::Patches::VerifyApplied();
    WiiXLaunch::Patches::RestoreAll();
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
