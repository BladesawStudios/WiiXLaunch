// Switch module load point.
#include <wiixlaunch/platform.hpp>

#if WIIXL_SWITCH

#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/loader/loader.hpp>
// Host::PatchesPersist - whether declared patches outlive the load.
#include <wiixlaunch/generated_host.hpp>
#include <wiixlaunch/mod_fs.hpp>
#include <wiixlaunch/loader/core_surface.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/loader/arena.hpp>
#include <wiixlaunch/patches.hpp>

#include <lib/util/sys/jit.hpp>
#include <lib.hpp>

// On Horizon, code memory cannot be simultaneously writable and executable.
// exl::util::Jit reserves a block in .text (RX) and maps a secondary writable
// view (RW) so the loader can write and relocate modules.
constexpr size_t kSwitchArenaSize = 0x100000;
JIT_CREATE(g_WiiXLaunchArena, kSwitchArenaSize)

// Flush data cache and invalidate instruction cache across RX/RW views.
static void SwitchFlush(uintptr_t, uint32_t) {
    g_WiiXLaunchArena.Flush();
}

static void RunLoader() {
    g_WiiXLaunchArena.Initialize();
    const uintptr_t rx = g_WiiXLaunchArena.GetRo();
    const uintptr_t rw = g_WiiXLaunchArena.GetRw();
    WiiXLaunch::Arena::SetReservation(rx, static_cast<uint32_t>(kSwitchArenaSize));
    WiiXLaunch::Arena::SetWriteAlias(rw);
    WiiXLaunch::Loader::SetFlushHook(&SwitchFlush);
    WIIXL_LOG("[loader] arena %u B at %p, written through %p",
              static_cast<uint32_t>(kSwitchArenaSize),
              reinterpret_cast<void*>(rx), reinterpret_cast<void*>(rw));

    WIIXL_LOG("[loader] host ABI v%u, format v%u",
              WiiXLaunch::Core::kAbiVersion, WiiXLaunch::Wxlm::kFormatVersion);
    WiiXLaunch::Surface::LogRegistered();

    // Check for a title-scoped mod directory before falling back to shared.
    const char* modsDir = WiiXLaunch::Host::ModsDir;
    if (WiiXLaunch::Loader::impl::DirectoryExists(modsDir)) {
        WIIXL_LOG("[loader] mods directory: %s (this title only)", modsDir);
    } else {
        modsDir = "WiiXLaunch/mods";
        WIIXL_LOG("[loader] mods directory: %s - SHARED BY EVERY GAME on this "
                  "card, because %s does not exist. A module built for another "
                  "game will be offered to this host.",
                  modsDir, WiiXLaunch::Host::ModsDir);
    }

    WiiXLaunch::ModFS::SetRoot(modsDir);

    const uint32_t loaded = WiiXLaunch::Loader::LoadAll(modsDir);

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
    } else {
        // A module that was found and REJECTED must not report as an absent
        // one. Only the loader knows which happened; the lines above say.
        WIIXL_LOG("[loader] no modules loaded. The game boots normally either way; "
                  "if the directory is simply empty that is the default state of a "
                  "fresh host, and the lines above say which it was.");
    }
}

// On SDKs where the filesystem is not ready at exl_main (e.g. TOTK), defer
// loading until the game's first file open.
static void (*volatile s_OrigOpenFile)(void*, const char*, int) = nullptr;
static bool s_LoadDone = false;
static bool s_InLoader = false;

extern "C" uint32_t WiiXLaunch_OpenFileHook(void* handle, const char* path, int mode) {
    // Guard against recursion when loading modules.
    if (!s_LoadDone && !s_InLoader) {
        s_LoadDone = true;
        s_InLoader = true;
        WIIXL_LOG("[loader] filesystem is up (the game opened '%s') - loading now",
                  path ? path : "?");
        RunLoader();
        s_InLoader = false;
    }

    auto orig = reinterpret_cast<uint32_t (*)(void*, const char*, int)>(s_OrigOpenFile);
    return orig ? orig(handle, path, mode) : 1;
}

extern "C" void WiiXLaunch_SwitchLoadPoint() {
    if constexpr (WiiXLaunch::Host::SwitchLoadPoint == 0) {
        RunLoader();
        return;
    }

    // Resolve imported nn::fs::OpenFile from nnSdk.
    const uintptr_t target =
        reinterpret_cast<uintptr_t>(&nn::fs::OpenFile);

    const auto& self = exl::util::GetSelfModuleInfo().m_Total;
    if (target >= self.m_Start && target < self.GetEnd()) {
        WIIXL_LOG("[loader] nn::fs::OpenFile resolved to %p, which is inside THIS "
                  "module - that is a call stub, not nnSdk. Loading now and "
                  "accepting the risk rather than hooking something inert.",
                  reinterpret_cast<void*>(target));
        RunLoader();
        return;
    }

    s_OrigOpenFile = reinterpret_cast<void (*)(void*, const char*, int)>(
        exl::hook::Hook(reinterpret_cast<void*>(target),
                        reinterpret_cast<void*>(&WiiXLaunch_OpenFileHook), true));

    WIIXL_LOG("[loader] deferred (%s): modules load at the first file the game "
              "opens. nn::fs::OpenFile is at %p; this SDK has no usable "
              "filesystem before then.",
              WiiXLaunch::Host::SwitchLoadPointName,
              reinterpret_cast<void*>(target));

    if (!s_OrigOpenFile) {
        WIIXL_LOG("[loader] the hook returned no original - the game would lose "
                  "every file it opens, so this host will NOT load modules.");
        s_LoadDone = true;
    }
}

#endif
