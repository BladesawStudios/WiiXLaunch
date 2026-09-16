#include <wiixlaunch.hpp>
#include <wiixlaunch/loader/load_point.hpp>
#include <wiixlaunch/game_version.hpp>
#include <wiixlaunch/loader/core_surface.hpp>
#include <wiixlaunch/loader/net_surface.hpp>
#include <wiixlaunch/loader/base_surfaces.hpp>

// Game module registration, if present in the tree.
#if __has_include(<wiixlaunch/module.hpp>)
#include <wiixlaunch/module.hpp>
#define WIIXL_HAVE_GAME_MODULE 1
#endif

// Optional graphics demo for BotW.
#if __has_include(<wiixlaunch/botw/botw.hpp>) && !defined(WIIXL_NO_DEMO)
#include <wiixlaunch/botw/botw.hpp>
#define WIIXL_BOTW_DEMO 1
using namespace WiiXLaunch::BotW;
#endif

#if WIIXL_BOTW_DEMO
#if WIIXL_SWITCH
// Compiled-in NVN packaged texture.
#include <testpic_texture_bytes.hpp>

static NVN::TextureHandle g_LogoTexture = 0;

void OnRender(NVN::CommandBuffer* cmdBuf, void* dstTexture, int width, int height) {
    if (g_LogoTexture) {
        NVN::DrawSprite(cmdBuf, dstTexture, g_LogoTexture, -0.92f, 0.50f, 0.225f, 0.40f);
    }
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
#endif // WIIXL_BOTW_DEMO


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

    // Detect running game version before installing hooks or patches.
    WiiXLaunch::GameVersion::Detect();

    // Register surfaces: core, networking, and base services.
    WiiXLaunch::Core::Register();
    WiiXLaunch::NetSurface::Register();
    WiiXLaunch::BaseSurfaces::RegisterAll();

#if WIIXL_HAVE_GAME_MODULE
    WIIXL_LOG("WiiXLaunch: game module '%s'", WiiXLaunch::GameModule::kName);
    WiiXLaunch::GameModule::Register();
#else
    WIIXL_LOG("WiiXLaunch: no game module in this build - base surfaces only.");
#endif

    WiiXLaunch::Surface::LogRegistered();

    // Probe filesystem availability at the entry hook.
    WiiXLaunch::LoadPoint::Probe("entry-hook");

    // System clock check.
    char clock[20];
    WiiXLaunch::Time::FormatNow(clock, sizeof(clock));
    WIIXL_LOG("WiiXLaunch: system clock %s", clock);

#if WIIXL_BOTW_DEMO
#if WIIXL_SWITCH
    NVN::Init();
    NVN::RegisterDrawCallback(OnRender);
    // Deferred until the game initializes the NVN device.
    NVN::OnInitialized([]() {
        g_LogoTexture = NVN::CreateTexturePackaged(g_TestPicTextureBytes,
                                                   kTestPicTextureSize);
        WIIXL_LOG("WiiXLaunch: NVN logo texture initialized: %p",
                  reinterpret_cast<void*>(g_LogoTexture));
    });
#elif WIIXL_CEMU
    GX2::Init();
    GX2::RegisterDrawCallback(OnRender);
    GX2::OnInitialized([]() {
        g_LogoTexture = GX2::LoadTexture("WiiXLaunch/mods/_host/logo.bin");
    });
#endif
#endif // WIIXL_BOTW_DEMO
}
