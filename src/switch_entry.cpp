// The Switch load point - where modules are read and started on this platform.
//
// Each platform reaches this moment differently, and the difference is real:
//
//   Cemu    an address inside the game, which only a game module can know, so
//           the module nominates it (botw/load_point.hpp).
//   Wii U   WUPS hands the plugin ON_APPLICATION_START, so the host drives the
//           loader itself (src/wiiu_plugin.cpp).
//   Switch  exlaunch runs this subsdk before the game's own main, and calls
//           exl_main. That is early, unconditional, and needs no game
//           knowledge - so the host drives the loader here too.
//
// Which means module loading depends on a game module being installed only on
// Cemu, and only because of where the code cave has to go.
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
// exl::hook::Hook and util::modules::GetTargetOffset, for a target whose
// filesystem is not usable at exl_main. See WiiXLaunch_SwitchLoadPoint.
#include <lib.hpp>

// THE ARENA HAS TO BE EXECUTABLE, and on this platform that is the whole
// problem.
//
// A module image is code. Cemu's arena is the tail of the code cave, which the
// graphic pack carved out of the game and which is executable; the address a
// module is written to is the address it runs from. Horizon does not permit
// that: a subsdk's only executable region is its own .text, and .text is not
// writable.
//
// exl::util::Jit is exlaunch's answer, and it is the same mechanism its own
// hook trampolines use. JIT_CREATE reserves a page-aligned block inside
// .text - executable, read-only - and Initialize() maps a SECOND, writable
// view of those same pages. The loader writes through the writable view and
// the module executes from the .text one, which is what Arena::SetWriteAlias
// exists to express.
//
// The size is its own number rather than wiixlaunch.json's memory.heap_size,
// and deliberately: that value describes the Cemu code cave, which is a region
// carved out of the game. This is .text in our own NSO - it costs exactly this
// many zero bytes in the file, and nothing else competes for it.
//
// 256 KB was "generous for modules that run a couple of kilobytes each", and
// that assumption lasted exactly until a real mod arrived. AIPuppet is an
// 83 KB image - two thousand lines and a 64 KB texture - and botw_api is 66 KB,
// so two modules were most of the arena and the rest had nowhere to go. A
// megabyte of zero bytes in our own NSO is not worth being clever about.
//
// Page-aligned by JIT_CREATE, which matters: an aarch64 image must be
// page-aligned or its adrp pairs land one page out, and starting the
// reservation on a page boundary means the first grant is aligned for free.
constexpr size_t kSwitchArenaSize = 0x100000;
JIT_CREATE(g_WiiXLaunchArena, kSwitchArenaSize)

// The pages were just written to through the RW view and are about to be
// executed through the RX one. On aarch64 those are separate caches and the
// instruction side has no idea; without this the module runs whatever was
// there before.
static void SwitchFlush(uintptr_t, uint32_t) {
    g_WiiXLaunchArena.Flush();
}

// Called from the weak exl_main in wiixlaunch/switch/switch_backend.hpp, after
// exl::hook::Initialize() and WiiXLaunch_Init().
//
// It lives in a .cpp rather than in that header because loader.hpp is a large
// header and switch_backend.hpp rides in through the umbrella on every single
// translation unit. Same split as the Wii U side, for the same reason.
// Everything the old load point did, now callable at two different moments.
static void RunLoader() {
    // The reservation, before anything can ask for memory. Base is the .text
    // address a module will RUN from; the alias is where it is written.
    g_WiiXLaunchArena.Initialize();
    const uintptr_t rx = g_WiiXLaunchArena.GetRo();
    const uintptr_t rw = g_WiiXLaunchArena.GetRw();
    WiiXLaunch::Arena::SetReservation(rx, static_cast<uint32_t>(kSwitchArenaSize));
    WiiXLaunch::Arena::SetWriteAlias(rw);
    WiiXLaunch::Loader::SetFlushHook(&SwitchFlush);
    WIIXL_LOG("[loader] arena %u B at %p, written through %p",
              static_cast<uint32_t>(kSwitchArenaSize),
              reinterpret_cast<void*>(rx), reinterpret_cast<void*>(rw));

    // What this host is and what it offers, logged before any module is read,
    // so a rejection further down can be read against it.
    WIIXL_LOG("[loader] host ABI v%u, format v%u",
              WiiXLaunch::Core::kAbiVersion, WiiXLaunch::Wxlm::kFormatVersion);
    WiiXLaunch::Surface::LogRegistered();

    // Lexical filename order, which is also hook priority - a specification
    // rather than an enumeration artefact. See docs/framework/loader.md.
    // PER-TITLE FIRST, SHARED AS A FALLBACK.
    //
    // sd:/WiiXLaunch/mods is one directory for every game on the card. Cemu's
    // equivalent lives inside a graphic pack that names its titleIds and Wii U's
    // lives in the game's own content, so both are already scoped; an SD card
    // is not. Two games' modules therefore land in the same folder and each
    // game's host tries to load both.
    //
    // Most crossovers are already refused by name: a module needing a game
    // surface this host does not publish, a declared patch whose origin bytes
    // are not there, a runtime patch likewise. A module that needs only the
    // base surfaces and HOOKS RAW OFFSETS is refused by nothing - and that is
    // exactly the shape of a mod for a game with no module yet.
    //
    // The fallback is deliberate rather than tidy: an existing card keeps
    // working untouched, and creating the per-title directory is how you opt
    // in. Which one was used is logged either way, because a host silently
    // reading a different directory than you think is worse than either.
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

    // ModFS follows, so a module's own files are found beside the module that
    // was actually loaded rather than wherever a constant said.
    WiiXLaunch::ModFS::SetRoot(modsDir);

    const uint32_t loaded = WiiXLaunch::Loader::LoadAll(modsDir);

    // Declared patches are applied during the loads above; they are verified
    // and put back HERE, between LoadAll and RunPhase, so no module code runs
    // while the game is modified. Same ordering as the other two platforms, and
    // it was wrong on Cemu once with the boot log to prove it.
    //
    // WHETHER THEY STAY IS A SETTING NOW - patches.persist in the target's
    // config, which is how a host says it ships real patch mods rather than the
    // sample. This used to be a comment asking you to delete the call, and a
    // comment telling you to edit code is a setting nobody has written down.
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

// --- when RunLoader is called ----------------------------------------------
//
// exl_main is the earliest moment there is, and for a long time it was the only
// one: the host runs before the game's own main, so hooks are installed and
// declared patches applied before a single game instruction executes. That is
// worth keeping wherever it works.
//
// IT DOES NOT WORK EVERYWHERE. nn::fs allocates through an allocator that is
// installed during nnSdk's own init, and until then there is none. On nnSdk
// 4.4.0 one was in place by the time a subsdk ran - which is why BotW has never
// needed any of this. On 15.3.1 it is not, so MountSdCardForDebug calls through
// a null pointer and dies inside nn::fs::fsa::Register before the loader has
// read a byte. TOTK 1.2.1 does exactly that.
//
// WHY NOT "HOOK A GAME FUNCTION THAT RUNS LATER". That was the first attempt and
// it silently did nothing. nninitStartup and nnMain exist in the game's module,
// but at those addresses they are PLT THUNKS the game uses for its own calls -
// nnSdk resolves the symbols through the dynamic table and jumps to the real
// definitions, so a hook on the thunk is never reached. The log said "deferred"
// and then nothing, for three minutes of gameplay.
//
// So the trigger is a fact about the filesystem rather than a guess about the
// game: the first time anything opens a file, fs works. That is true on every
// SDK and in every title, and it needs no per-game offset.
static void (*volatile s_OrigOpenFile)(void*, const char*, int) = nullptr;
static bool s_LoadDone = false;
static bool s_InLoader = false;

extern "C" uint32_t WiiXLaunch_OpenFileHook(void* handle, const char* path, int mode) {
    // THE GUARD IS NOT OPTIONAL. RunLoader opens .wxlm files, which comes
    // straight back through here; without s_InLoader the first module read
    // recurses until the stack ends.
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

    // The address of an IMPORTED function, which in position-independent code
    // is loaded from the GOT and is therefore nnSdk's real one - not this
    // module's call stub. Checked below rather than trusted, because a stub
    // would hook only our own calls and look exactly like success.
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
