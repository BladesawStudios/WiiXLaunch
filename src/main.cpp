#include <wiixlaunch.hpp>
#include <wiixlaunch/loader/load_point.hpp>
#include <wiixlaunch/loader/core_surface.hpp>
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


// --- STAGE 1 STAND-IN: the BotW v208 load point ----------------------------
//
// THIS ADDRESS IS GAME-SPECIFIC AND DOES NOT BELONG IN BASE. It lives in
// main.cpp because stage 1 has no module-side nomination yet. Stage 2 moves the
// WIIXL_DECLARE_LOAD_POINT call into the BotW module's own header, so that
// installing wiixlaunch-botw is what nominates BotW's load point. The mechanism
// does not change - only who calls the macro. A host with no game module
// declares nothing, and deploy.py says so rather than emitting a silent no-op.
//
// WHY THIS ADDRESS (v208 Wii U RPX, read in Ghidra):
//
//   FUN_03098928 is the game's FS bring-up, and is also the function the Cemu
//   entry hook already sits on. It runs exactly once - FUN_03098a64 wraps it in
//   an `if (singleton == 0)` guard - and internally does:
//
//     030989bc  bl 0x04004ed0     FSInit()
//     030989c0  addi r3,r31,0x24  client = this + 0x24
//     030989c4  li   r4,0
//     030989c8  bl 0x04004e78     FSAddClient(client, 0)
//     030989cc  lis  r11,0x30a    <- LOAD POINT. FS is up from here.
//
// So the load point is four instructions past the entry hook, inside the same
// function. There is no cleaner site: the instruction after FUN_03098a64
// returns is a vtable dispatch (0309f284 bctrl), so any post-FS-init point is
// necessarily mid-function.
//
// THE BRANCH IS EMITTED BY THE PACK, NOT WRITTEN AT RUNTIME. deploy.py puts
// `.origin = 0x030989CC / b wiixlaunch_loadpoint_stub` in patch_*.asm next to
// the entry hook. Writing it from WiiXLaunch_Init would mean modifying code
// inside a function Cemu may already have recompiled on entry at 0x03098928 -
// the cache flush probably covers that, but "probably" is exactly what this
// probe exists to remove. It also keeps the rule that only the host pack writes
// into game memory.
//
// SAFETY OF THE SITE, checked rather than assumed: nothing xrefs 0x030989CC, so
// the branch cannot be landed on from elsewhere, and the displaced instruction
// is position-independent (`lis r11,0x30a` - no relative branch, no PC-relative
// addressing), so it re-executes correctly from the codecave.
#if WIIXL_CEMU

WIIXL_DECLARE_LOAD_POINT(0x030989CC);

extern "C" void WiiXLaunch_LoadPointProbe() {
    // Site 2 of 2, immediately after FSAddClient. The three path verdicts are
    // separable, which is the point:
    //   STOCK verified             -> /vol/content is mounted AND readable here
    //   STOCK verified, PACK not   -> the graphic-pack content/ overlay is not
    //                                 live yet, which changes mod distribution
    //   STOCK not found            -> nothing is mounted; the load point moves
    WiiXLaunch::LoadPoint::Probe("post-fsaddclient");
}

// Register-preserving stub. The frame layout matches WiiXLaunch_Cemu_Init
// exactly (0x2000 bytes, r2-r31 at 0x1F80, LR at 0x2004, CR at 0x2008) because
// that one is known to work; this is not the place to invent a new one.
//
// Only ONE instruction is displaced, not four: the pack emits a single `b`,
// the same shape as the entry hook, rather than a 16-byte long jump.
asm(
    ".section .text.WiiXLaunch_LoadPointStub\n"
    ".global WiiXLaunch_LoadPointStub\n"
    "WiiXLaunch_LoadPointStub:\n"
    "mflr 0\n"
    "stwu 1, -0x2000(1)\n"
    "stw 0, 0x2004(1)\n"
    "mfcr 0\n"
    "stw 0, 0x2008(1)\n"
    "stmw 2, 0x1F80(1)\n"

    "bl WiiXLaunch_LoadPointProbe\n"

    "lmw 2, 0x1F80(1)\n"
    "lwz 0, 0x2008(1)\n"
    "mtcr 0\n"
    "lwz 0, 0x2004(1)\n"
    "mtlr 0\n"
    "addi 1, 1, 0x2000\n"

    // The ONE instruction displaced from 0x030989CC by the pack's `b`. It has to
    // run AFTER the restore above: r11 is inside the r2-r31 range lmw rewrites,
    // so setting it any earlier would simply be undone.
    "lis 11, 0x30a\n"

    // Back to 0x030989D0, the instruction after the one we displaced. Literal
    // immediates, so no relocation entry is emitted and deploy.py leaves them
    // alone - correct for a game address that is already absolute.
    "lis 12, 0x0309\n"
    "ori 12, 12, 0x89d0\n"
    "mtctr 12\n"
    "bctr\n"
);

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

    // Game modules go here. Base must never name one - this line lives in the
    // project's own source, which is where knowledge of what was installed
    // belongs. Stage 4 calls this before loading any .wxlm.
    //
    //   WiiXLaunch::BotW::Surfaces::Register();

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
        g_LogoTexture = GX2::LoadTexture("WiiXLaunch/logo.bin");
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

// NO TOUCHING BELOW THIS POINT. This is Cemu-specific code that handles the trampoline pool and code cave entry point. You don't need to touch this unless you know what you're doing.

// The address this payload is running at. Set by the bootstrap below, from the
// address it finds itself loaded at - not baked in at deploy time.
extern "C" uintptr_t g_CodeCaveBase;
uintptr_t g_CodeCaveBase = 0;

#if WIIXL_CEMU

// Applies the relocation table scripts/deploy.py emitted after the payload.
//
// Cemu assigns code caves sequentially in graphic-pack load order, so the
// address this payload runs at depends on which packs the user has enabled and
// on the Cemu version. deploy.py used to guess it with a hardcoded constant,
// which is fine until it is wrong - and when it is wrong, every absolute
// address in the payload is off by the same delta. Hooks then jump that far
// past their callbacks into unrelated code, globals read the wrong memory, and
// WIIXL_LOG resolves a bogus shim table, so there is no log output to explain
// any of it. The symptom is a crash seconds after boot with an empty log.
//
// So the payload now ships linked at base 0 and fixes itself up here.
//
// Three rules make this function safe to run before relocation has happened:
// it touches no globals, contains no string literals, and calls nothing. Every
// address it uses arrives in a parameter or is derived from `base`. The only
// control transfer into it is the bootstrap's `bl`, which is PC-relative and so
// correct at any load address.
//
// Entries are pairs: a header word of (kind << 24 | offset), and the
// relocation's link-time target. Applying `base + target` from the table rather
// than adding a delta to whatever is already stored makes this idempotent - the
// entry hook running twice writes the same values the second time.
extern "C" __attribute__((used, noinline))
void WiiXLaunch_Cemu_Relocate(uint32_t base, uint32_t tableOffset, uint32_t count) {
    const uint32_t* table = reinterpret_cast<const uint32_t*>(base + tableOffset);

    for (uint32_t i = 0; i < count; i++) {
        uint32_t header = table[i * 2];
        uint32_t value = table[i * 2 + 1] + base;
        uint8_t* site = reinterpret_cast<uint8_t*>(base + (header & 0x00FFFFFF));

        switch (header >> 24) {
            case 0:  // R_PPC_ADDR32 - a whole pointer
                *reinterpret_cast<uint32_t*>(site) = value;
                break;
            case 1:  // R_PPC_ADDR16_HA - `lis` half, with the sign-extension carry
                *reinterpret_cast<uint16_t*>(site) =
                    static_cast<uint16_t>(((value + 0x8000) >> 16) & 0xFFFF);
                break;
            case 2:  // R_PPC_ADDR16_HI - `lis` half, plain
                *reinterpret_cast<uint16_t*>(site) =
                    static_cast<uint16_t>((value >> 16) & 0xFFFF);
                break;
            default:  // R_PPC_ADDR16_LO - `ori`/`addi` half
                *reinterpret_cast<uint16_t*>(site) =
                    static_cast<uint16_t>(value & 0xFFFF);
                break;
        }
    }

    // Half of those writes edited instruction immediates, so the payload's code
    // has to be pushed out of the data cache and out of the instruction cache
    // before any of it is executed. tableOffset is where the payload ends,
    // which makes it the size of everything just patched. 32 bytes is the
    // Espresso cache line.
    for (uint32_t offset = 0; offset < tableOffset; offset += 32) {
        uint8_t* line = reinterpret_cast<uint8_t*>(base + offset);
        asm volatile("dcbst 0,%0" :: "r"(line) : "memory");
    }
    asm volatile("sync" ::: "memory");
    for (uint32_t offset = 0; offset < tableOffset; offset += 32) {
        uint8_t* line = reinterpret_cast<uint8_t*>(base + offset);
        asm volatile("icbi 0,%0" :: "r"(line) : "memory");
    }
    asm volatile("isync" ::: "memory");
}

// Cemu's code cave entry point (hooks 0x03098928).
//
// Preserves all registers, works out where it is loaded, relocates the payload,
// calls WiiXLaunch_Init, restores all registers, executes the replaced
// instruction and branches to 0x0309892c.
//
// The `@h`/`@l` immediates here are deliberately raw link-time constants:
// cemu.ld brackets this section with __wiixl_bootstrap_start/end and deploy.py
// skips that range, so these are the one place in the payload that still holds
// base-0 values at run time. Adding the computed base to them is what turns
// them into real addresses - which is exactly why the base has to be computed
// before anything else here touches memory.
asm(
    ".section .text.WiiXLaunch_Cemu_Init\n"
    ".global WiiXLaunch_Cemu_Init\n"
    "WiiXLaunch_Cemu_Init:\n"
    "mflr 0\n"
    "stwu 1, -0x2000(1)\n"
    "stw 0, 0x2004(1)\n"
    "mfcr 0\n"
    "stw 0, 0x2008(1)\n"
    "stmw 2, 0x1F80(1)\n"

    // r31 = load address. `bl` to the next instruction puts its runtime address
    // in LR; the same label's link-time address is a base-0 constant, so the
    // difference is where the payload actually got loaded.
    "bl __wiixl_here\n"
    "__wiixl_here:\n"
    "mflr 31\n"
    "lis 30, __wiixl_here@h\n"
    "ori 30, 30, __wiixl_here@l\n"
    "subf 31, 30, 31\n"

    // WiiXLaunch_Cemu_Relocate(base, tableOffset, count)
    "lis 3, g_CemuRelocTableOffset@h\n"
    "ori 3, 3, g_CemuRelocTableOffset@l\n"
    "add 3, 3, 31\n"
    "lwz 4, 0(3)\n"
    "lis 3, g_CemuRelocCount@h\n"
    "ori 3, 3, g_CemuRelocCount@l\n"
    "add 3, 3, 31\n"
    "lwz 5, 0(3)\n"
    "mr 3, 31\n"
    "bl WiiXLaunch_Cemu_Relocate\n"

    // Publish the base for the trampoline pool and the shim tables. Safe to
    // write normally now, but still addressed the bootstrap way - this store
    // is what everything downstream depends on.
    "lis 3, g_CodeCaveBase@h\n"
    "ori 3, 3, g_CodeCaveBase@l\n"
    "add 3, 3, 31\n"
    "stw 31, 0(3)\n"

    "bl WiiXLaunch_Init\n"
    "lmw 2, 0x1F80(1)\n"
    "lwz 0, 0x2008(1)\n"
    "mtcr 0\n"
    "lwz 0, 0x2004(1)\n"
    "mtlr 0\n"
    "addi 1, 1, 0x2000\n"
    "mflr 0\n"
    "lis 12, 0x0309\n"
    "ori 12, 12, 0x892c\n"
    "mtctr 12\n"
    "bctr\n"
);
#endif
