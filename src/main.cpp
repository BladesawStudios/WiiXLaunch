#include <wiixlaunch.hpp>
#include <wiixlaunch/loader/load_point.hpp>
#include <wiixlaunch/loader/core_surface.hpp>
#include <wiixlaunch/loader/net_surface.hpp>
#include <wiixlaunch/botw/botw.hpp>

using namespace WiiXLaunch::BotW;

// feel free to remove this, its just proof your toolchain works end-end -
// the OnRender callbacks here and their registration in WiiXLaunch_Init
// below can both go. The WIIXL_LOG lines in there are the proof-of-life
// that works on every platform.
#if WIIXL_SWITCH
// Draws nothing. The logo demo the other targets run needs a texture in the
// container NVN::CreateTexture expects - a 0x200-byte header with width at
// 0x40, height at 0x44, format at 0x50 - and nothing in this repo produces
// one. The header it used to include was never tracked, so the Switch target
// did not build from a clean clone at all.
//
// The callback is still registered below, so the draw path is exercised and
// this is where your own drawing goes. Cemu's logo demo still runs: its asset
// is src/resources/logo.png, packaged to logo.bin by scripts/pack_resources.py
// at deploy time, and GX2 reads a different (16-byte header) format.
void OnRender(NVN::CommandBuffer* cmdBuf, void* dstTexture, int width, int height) {
}
#elif WIIXL_CEMU
static GX2::TextureHandle g_LogoTexture = 0;

void OnRender(GX2::CommandBuffer* cmdBuf, void* dstTexture, int width, int height) {
    if (g_LogoTexture) {
        GX2::DrawSprite(cmdBuf, dstTexture, g_LogoTexture, -0.92f, 0.50f, 0.225f, 0.40f);
    }
}
#elif WIIXL_WIIU
static GX2::TextureHandle g_LogoTexture = 0;

void OnRender(GX2::CommandBuffer* cmdBuf, void* dstTexture, int width, int height) {
    GX2::DrawSprite(cmdBuf, dstTexture, g_LogoTexture, -0.92f, 0.50f, 0.225f, 0.40f);
}
#endif


// Entry point called once at plugin/module load. Install your hooks here.
extern "C" void WiiXLaunch_Init() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

#if WIIXL_WIIU
    if (!WiiXLaunch::Backend::InitWiiUBackend()) return;
#elif WIIXL_CEMU
    if (!WiiXLaunch::Backend::InitCemuBackend()) return;
#endif

    WIIXL_LOG("WiiXLaunch: init OK");

    // --- STAGE 2: the surface registry --------------------------------------
    //
    // wiixl.core first, then whatever game modules this project installed.
    //
    // Registration is an explicit call, NOT static self-registration. The flat
    // Cemu payload does not run C++ static constructors: scripts/cemu.ld has no
    // .init_array output section, the linked ELF has none at all, and the
    // bootstrap goes straight from relocating to WiiXLaunch_Init without ever
    // walking one. Everything works today only because every global here is POD
    // with a constant initialiser. A module that tried to register itself from
    // a static constructor would simply never run.
    //
    // A weak-symbol hook would be the other obvious way to let base call into
    // modules it cannot name, and it is a trap in THIS codebase specifically:
    // deploy.py relocates every ADDR32 site by adding the code-cave base, so an
    // undefined weak symbol resolving to 0 would come out as g_CodeCaveBase and
    // test as non-null. Explicit calls avoid the whole question.
    WiiXLaunch::Core::Register();

    // wiixl.net is base's too, but SEPARATE, because not every platform can
    // back it. On Switch this deliberately registers nothing and says so, and
    // a mod that declared wiixl.net required is then refused by name at load
    // rather than loading and silently failing at its first send.
    WiiXLaunch::NetSurface::Register();

    // Game modules register here. Base must never name one - this line lives in
    // the project's own source, which is where knowledge of what was installed
    // belongs. Stage 4 calls this before loading any .wxlm.
    //
    // A project with no game module simply does not have this line, registers
    // only wiixl.core, and LogRegistered says so.
    WiiXLaunch::BotW::Surfaces::Register();

    WiiXLaunch::Surface::LogRegistered();

    // --- STAGE 1: load-point validation -------------------------------------
    //
    // Site 1 of 2: the Cemu entry hook, which is the earliest point the host
    // can possibly run. Ghidra (v208 Wii U RPX) says this is NOT early enough,
    // and says so precisely rather than by guess:
    //
    //   FUN_03098928 - the function the entry hook sits on - IS the game's FS
    //   bring-up. It calls FSInit at 0x030989bc and FSAddClient at 0x030989c8.
    //   The entry hook replaces its FIRST instruction, so we are running before
    //   both. FS should therefore be absent here.
    //
    // Expected verdict: FS-ABSENT. If it comes back FS-UP-FILE-MISSING or
    // FS-USABLE, that assumption is wrong and the load point is cheaper than we
    // thought - which is exactly the kind of thing worth measuring rather than
    // reasoning about.
    WiiXLaunch::LoadPoint::Probe("entry-hook");


    // Proof the system clock is reachable: console RTC on hardware,
    // host PC clock under Cemu. Reads "unavailable" on Switch.
    char clock[20];
    WiiXLaunch::Time::FormatNow(clock, sizeof(clock));
    WIIXL_LOG("WiiXLaunch: system clock %s", clock);

#if WIIXL_SWITCH
    NVN::Init();
    NVN::RegisterDrawCallback(OnRender);
#elif WIIXL_CEMU
    GX2::Init();
    GX2::RegisterDrawCallback(OnRender);
    GX2::OnInitialized([]() {
        // Under the reserved host id, like every other module's resources. The
        // host is not an exception to its own namespacing scheme - an exception
        // is how someone later decides the scheme is optional. See
        // wiixlaunch/mod_fs.hpp; a mod cannot claim an id starting with '_'.
        g_LogoTexture = GX2::LoadTexture("WiiXLaunch/mods/_host/logo.bin");
    });
// Would probably work on WUH, but I really can't be bothered unless someone finds a use for it. All you probably have to do is give it a proper texture path.
// #elif WIIXL_WIIU
//     GX2::Init();
//     GX2::RegisterDrawCallback(OnRender);
//     GX2::OnInitialized([]() {
//         g_LogoTexture = GX2::CreateTexture(g_TestpicTextureBytes, kTestpicTextureSize, kTestpicTextureWidth, kTestpicTextureHeight);
//         WIIXL_LOG("WiiXLaunch: GX2 logo texture initialized: %p", reinterpret_cast<void*>(g_LogoTexture));
//     });
#endif
}

// The Cemu bootstrap - g_CodeCaveBase, WiiXLaunch_Cemu_Relocate and the
// entry-hook stub - used to sit below here under a "NO TOUCHING" banner. It is
// framework code, not sample code, and it now lives in src/cemu/bootstrap.cpp,
// which every Cemu build compiles regardless of what this file contains.
//
// That means this file is now deletable. Everything above is a sample; an empty
// main.cpp still produces a host that boots, relocates itself and logs, using
// the weak default WiiXLaunch_Init in the bootstrap.
