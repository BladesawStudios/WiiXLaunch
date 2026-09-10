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
#include <wiixlaunch/loader/core_surface.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/patches.hpp>

// Called from the weak exl_main in wiixlaunch/switch/switch_backend.hpp, after
// exl::hook::Initialize() and WiiXLaunch_Init().
//
// It lives in a .cpp rather than in that header because loader.hpp is a large
// header and switch_backend.hpp rides in through the umbrella on every single
// translation unit. Same split as the Wii U side, for the same reason.
extern "C" void WiiXLaunch_SwitchLoadPoint() {
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
    // A host shipping REAL patch mods must delete the RestoreAll call; a patch
    // is meant to persist.
    WiiXLaunch::Patches::VerifyApplied();
    WiiXLaunch::Patches::RestoreAll();
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
