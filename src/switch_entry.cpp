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
// rtld::lookup_global_auto - the game's allocator, by name. See RomCacheAlloc.
#include <rtld.hpp>

// Not declared by vendor/exlaunch (upstream, not patched here). All three are
// in BotW 1.5.0's sdk dynamic symbol table - checked by decompressing
// exefs/sdk, not assumed - and test_switch_module.py vets them by name.
namespace nn::fs {
Result QueryMountRomCacheSize(unsigned long* outSize);
Result MountRom(const char* name, void* cache, unsigned long cacheSize);
Result Unmount(const char* name);
}

// On Horizon, code memory cannot be simultaneously writable and executable.
// exl::util::Jit reserves a block in .text (RX) and maps a secondary writable
// view (RW) so the loader can write and relocate modules.
constexpr size_t kSwitchArenaSize = 0x100000;
JIT_CREATE(g_WiiXLaunchArena, kSwitchArenaSize)

// Flush data cache and invalidate instruction cache across RX/RW views.
static void SwitchFlush(uintptr_t, uint32_t) {
    g_WiiXLaunchArena.Flush();
}

// ---------------------------------------------------------------------------
// Romfs for the load.
//
// The cache MountRom needs is the whole romfs's metadata, ~2.5 MB on BotW and
// more with LayeredFS mods, so it cannot come from the host's 320 KB fake heap
// and must not be held for the session. It comes from the game's own libc
// allocator, looked up by name because this module links newlib's malloc/free
// locally and would otherwise call those. That allocator is live wherever this
// runs: at exl_main the SD mount already allocates through it inside
// nn::fs::fsa::Register (the call that crashed on TotK, which is why TotK
// loads at fs_ready instead).
// ---------------------------------------------------------------------------
using FnAlignedAlloc = void* (*)(unsigned long, unsigned long);
using FnFree         = void  (*)(void*);

static void*         s_RomCache     = nullptr;
static FnFree        s_RomCacheFree = nullptr;

// Mounts romfs for the load if the game has not mounted it already. Every
// failure leaves the SD card as the only source and says why.
static void MountRomForLoad() {
    namespace FSI = WiiXLaunch::FS::impl;
    if (FSI::g_GameRomMounted) {
        WIIXL_LOG("[romfs] the game has already mounted romfs as '%s:' - reading "
                  "through that, no mount of our own", FSI::g_GameRomMount);
        return;
    }

    unsigned long size = 0;
    const auto q = nn::fs::QueryMountRomCacheSize(&size);
    if (q != 0 || size == 0) {
        WIIXL_LOG("[romfs] QueryMountRomCacheSize failed (result 0x%X, size %lu) - "
                  "romfs NOT read, SD card only", static_cast<unsigned>(q), size);
        return;
    }

    const auto alloc = reinterpret_cast<FnAlignedAlloc>(rtld::lookup_global_auto("aligned_alloc"));
    const auto freeFn = reinterpret_cast<FnFree>(rtld::lookup_global_auto("free"));
    if (!alloc || !freeFn) {
        // Both or neither: a buffer that cannot be given back is 2.5 MB the
        // game never sees again.
        WIIXL_LOG("[romfs] the game's allocator was not found (aligned_alloc %p, "
                  "free %p) - romfs NOT read, SD card only",
                  reinterpret_cast<void*>(alloc), reinterpret_cast<void*>(freeFn));
        return;
    }

    s_RomCache = alloc(0x10, size);
    if (!s_RomCache) {
        WIIXL_LOG("[romfs] the game's allocator refused %lu B for the romfs cache - "
                  "romfs NOT read, SD card only", size);
        return;
    }
    s_RomCacheFree = freeFn;

    const auto r = nn::fs::MountRom(FSI::kRomLoadMount, s_RomCache, size);
    if (r != 0) {
        WIIXL_LOG("[romfs] MountRom('%s') failed (result 0x%X) - romfs NOT read, "
                  "SD card only", FSI::kRomLoadMount, static_cast<unsigned>(r));
        s_RomCacheFree(s_RomCache);
        s_RomCache = nullptr;
        return;
    }
    FSI::g_RomLoadMounted = true;
    WIIXL_LOG("[romfs] mounted as '%s:' for the load, %lu B cache borrowed from "
              "the game's heap", FSI::kRomLoadMount, size);
}

static void UnmountRomAfterLoad() {
    namespace FSI = WiiXLaunch::FS::impl;
    if (!FSI::g_RomLoadMounted) return;
    // Cleared FIRST: from here on nothing may be offered this mount name.
    FSI::g_RomLoadMounted = false;
    nn::fs::Unmount(FSI::kRomLoadMount);
    s_RomCacheFree(s_RomCache);
    s_RomCache = nullptr;
    WIIXL_LOG("[romfs] '%s:' unmounted and its cache returned. Later reads go "
              "through %s%s%s, then the SD card.", FSI::kRomLoadMount,
              FSI::g_GameRomMounted ? "'" : "",
              FSI::g_GameRomMounted ? FSI::g_GameRomMount : "the game's romfs mount once it makes one",
              FSI::g_GameRomMounted ? ":'" : "");
}

// The game's own MountRom, observed so reads after the load can go through
// the mount the game keeps for itself. Whatever it is called; the host never
// assumes a name.
static uint32_t (*volatile s_OrigMountRom)(const char*, void*, unsigned long) = nullptr;

extern "C" uint32_t WiiXLaunch_MountRomHook(const char* name, void* cache, unsigned long size) {
    const auto orig = s_OrigMountRom;
    const uint32_t r = orig ? orig(name, cache, size) : 1;
    namespace FSI = WiiXLaunch::FS::impl;
    const bool ours = name && std::strcmp(name, FSI::kRomLoadMount) == 0;
    if (r == 0 && !ours && !FSI::g_GameRomMounted) {
        FSI::NoteGameRomMount(name);
        WIIXL_LOG("[romfs] the game mounted romfs as '%s:'%s", name ? name : "?",
                  FSI::g_GameRomMounted ? " - mod reads go through it from now on"
                                        : ", a name too long to route - NOT used");
    }
    return r;
}

// Is `target` this module's own call stub rather than the nnSdk function?
static bool InsideSelf(uintptr_t target) {
    const auto& self = exl::util::GetSelfModuleInfo().m_Total;
    return target >= self.m_Start && target < self.GetEnd();
}

static void HookGameMountRom() {
    const uintptr_t target = reinterpret_cast<uintptr_t>(
        static_cast<Result (*)(const char*, void*, unsigned long)>(&nn::fs::MountRom));
    if (InsideSelf(target)) {
        WIIXL_LOG("[romfs] nn::fs::MountRom resolved to %p inside THIS module - not "
                  "hooked. After the load, mod reads fall back to the SD card only.",
                  reinterpret_cast<void*>(target));
        return;
    }
    s_OrigMountRom = reinterpret_cast<uint32_t (*)(const char*, void*, unsigned long)>(
        exl::hook::Hook(reinterpret_cast<void*>(target),
                        reinterpret_cast<void*>(&WiiXLaunch_MountRomHook), true));
    if (!s_OrigMountRom) {
        WIIXL_LOG("[romfs] the MountRom hook returned no original - every romfs "
                  "mount the game makes will FAIL. This is not recoverable here.");
    }
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

    MountRomForLoad();

    // Romfs first: it is per-title by construction, so it needs no title-id
    // subdirectory. Checked through the romfs mount EXPLICITLY - the relative
    // form would also find the SD card's shared directory. Then romfslite,
    // then the SD card: a title-scoped directory, then the shared one.
    //
    // modsDir stays RELATIVE for romfs and the SD card. It is resolved per
    // call, romfs before SD, so the same root keeps working for ModReadFile
    // after the load mount is gone and reads move to the game's own romfs
    // mount. Romfslite is the exception - see below.
    const char* modsDir = WiiXLaunch::ModFS::kModsRoot;
    char romMods[64] = {};
    if (const char* rom = WiiXLaunch::FS::impl::RomMount()) {
        std::snprintf(romMods, sizeof(romMods), "%s:/%s", rom, modsDir);
    }

    // ROMFSLITE is not romfs. It is a folder on the SD card that TKMM writes
    // and an UltraCam-based subsdk serves by redirecting the game's file
    // opens - nothing is added to romfs, and directory listings are not
    // redirected, so the romfs check above can never see it. It is read here
    // directly off the SD card, which needs nothing of UltraCam. Absolute,
    // because it is on the SD card for the whole session. Lowercase first:
    // that is how Ryujinx and TKMM spell the title id; FAT does not care.
    // Static: ModFS keeps the pointer.
    static char liteMods[96];
    liteMods[0] = '\0';
    {
        char tidLower[sizeof(WiiXLaunch::Host::TitleId)];
        for (uint32_t i = 0; i < sizeof(tidLower); ++i) {
            const char c = WiiXLaunch::Host::TitleId[i];
            tidLower[i] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }
        const char* const spellings[2] = {tidLower, WiiXLaunch::Host::TitleId};
        for (const char* tid : spellings) {
            char probe[sizeof(liteMods)];
            std::snprintf(probe, sizeof(probe),
                          "%s:/atmosphere/contents/%s/romfslite/%s",
                          WiiXLaunch::FS::impl::kSwitchMount, tid,
                          WiiXLaunch::ModFS::kModsRoot);
            if (WiiXLaunch::Loader::impl::DirectoryExists(probe)) {
                std::snprintf(liteMods, sizeof(liteMods), "%s", probe);
                break;
            }
        }
    }

    if (romMods[0] && WiiXLaunch::Loader::impl::DirectoryExists(romMods)) {
        WIIXL_LOG("[loader] mods directory: %s (romfs, this title only). The SD "
                  "card's mods directories are NOT read this boot.", romMods);
    } else if (liteMods[0]) {
        modsDir = liteMods;
        WIIXL_LOG("[loader] mods directory: %s (romfslite on the SD card, this "
                  "title only)%s", modsDir,
                  romMods[0] ? " - romfs has no WiiXLaunch/mods" : "");
    } else if (modsDir = WiiXLaunch::Host::ModsDir;
               WiiXLaunch::Loader::impl::DirectoryExists(modsDir)) {
        WIIXL_LOG("[loader] mods directory: %s (SD card, this title only)%s", modsDir,
                  romMods[0] ? " - romfs has no WiiXLaunch/mods" : "");
    } else {
        modsDir = WiiXLaunch::ModFS::kModsRoot;
        WIIXL_LOG("[loader] mods directory: %s - SHARED BY EVERY GAME on this "
                  "card, because none of romfs, romfslite or %s has one. A module built for "
                  "another game will be offered to this host.",
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

    // AFTER the Load phase, so a module reading its own files while it
    // loads still reaches the load mount.
    UnmountRomAfterLoad();
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
    // Before either load point, so a game that mounts romfs before its first
    // file open (the fs_ready case) is seen doing it.
    HookGameMountRom();

    if constexpr (WiiXLaunch::Host::SwitchLoadPoint == 0) {
        RunLoader();
        return;
    }

    // Resolve imported nn::fs::OpenFile from nnSdk.
    const uintptr_t target =
        reinterpret_cast<uintptr_t>(&nn::fs::OpenFile);

    if (InsideSelf(target)) {
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
