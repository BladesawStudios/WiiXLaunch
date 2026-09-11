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
#include <wiixlaunch/loader/core_surface.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/loader/arena.hpp>
#include <wiixlaunch/patches.hpp>

#include <lib/util/sys/jit.hpp>

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
extern "C" void WiiXLaunch_SwitchLoadPoint() {
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
    // rather than an enumeration artefact. See docs/loader.md.
    const uint32_t loaded = WiiXLaunch::Loader::LoadAll("WiiXLaunch/mods");

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

#endif
