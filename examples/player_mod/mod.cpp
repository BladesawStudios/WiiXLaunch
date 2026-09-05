// e_player.wxlm - the botw.player v1.1 demonstration.
//
// It reads Link's real life through the surface's REAL SYMBOLS and prints it,
// so a boot proves the accessors rather than the build proving they compile.
// Take a hit and the numbers move.
//
// The point of the exercise is that this mod contains NO OFFSETS. GetLife goes
// through a vtable slot pinned in wiixlaunch-botw's actor.hpp, which means the
// day that slot turns out to be wrong for some version, fixing it there fixes
// this compiled binary without rebuilding it. A mod that had read +0x2bc for
// itself would keep reading the wrong place forever, and nobody could do
// anything about it from the framework side.
//
// It also uses the escape hatch ONCE, deliberately, so a boot log shows the
// "opted out of versioning" line happening rather than a comment claiming it
// would. That line naming this module is the feature.

#include <cstdint>

extern "C" {
    extern void     wiixl_import__wiixl_core__Log(const char* text);

    // botw.player v1.1. Importing any of these makes botw.player a required
    // surface, so on a host with no BotW game module installed this .wxlm is
    // refused by name at load and never runs.
    extern uint32_t wiixl_import__botw_player__Init(void);
    extern uint32_t wiixl_import__botw_player__RegisterTick(void (*fn)());
    extern uint32_t wiixl_import__botw_player__GetPlayerActor(void);
    extern uint32_t wiixl_import__botw_player__ActorIsValid(uint32_t handle);
    extern int32_t  wiixl_import__botw_player__ActorGetLife(uint32_t handle);
    extern int32_t  wiixl_import__botw_player__ActorGetMaxLife(uint32_t handle);
    extern uint32_t wiixl_import__botw_player__SupportsLife(void);
    extern uint32_t wiixl_import__botw_player__ConsumeAttackEvent(void);
    extern uint32_t wiixl_import__botw_player__SupportsAttackTracking(void);
    // The escape hatch. Named Unsafe at the call site because using it opts
    // this module out of the surface's versioning - see surfaces.hpp.
    extern uintptr_t wiixl_import__botw_player__ActorUnsafeRawPointer(uint32_t handle);
}

using LogFn      = void (*)(const char*);
using VoidU32Fn  = uint32_t (*)(void);
using TickRegFn  = uint32_t (*)(void (*)());
using HandleU32Fn = uint32_t (*)(uint32_t);
using HandleI32Fn = int32_t (*)(uint32_t);
using RawFn      = uintptr_t (*)(uint32_t);

static LogFn       volatile g_Log        = &wiixl_import__wiixl_core__Log;
static VoidU32Fn   volatile g_Init       = &wiixl_import__botw_player__Init;
static TickRegFn   volatile g_RegTick    = &wiixl_import__botw_player__RegisterTick;
static VoidU32Fn   volatile g_GetPlayer  = &wiixl_import__botw_player__GetPlayerActor;
static HandleU32Fn volatile g_IsValid    = &wiixl_import__botw_player__ActorIsValid;
static HandleI32Fn volatile g_GetLife    = &wiixl_import__botw_player__ActorGetLife;
static HandleI32Fn volatile g_GetMaxLife = &wiixl_import__botw_player__ActorGetMaxLife;
static VoidU32Fn   volatile g_SuppLife   = &wiixl_import__botw_player__SupportsLife;
static VoidU32Fn   volatile g_ConsumeHit = &wiixl_import__botw_player__ConsumeAttackEvent;
static VoidU32Fn   volatile g_SuppAttack = &wiixl_import__botw_player__SupportsAttackTracking;
static RawFn       volatile g_UnsafeRaw  = &wiixl_import__botw_player__ActorUnsafeRawPointer;

// .bss, so the loader has to have zeroed it. volatile so the compiler cannot
// fold counters it can see only this file writing.
static volatile uint32_t g_Reports;
static volatile uint32_t g_Swings;
static volatile int32_t  g_LastLife;
static volatile uint32_t g_TookRaw;

// No libc here.
static char* AppendText(char* out, char* end, const char* text) {
    while (text && *text && out < end - 1) *out++ = *text++;
    return out;
}

static char* AppendI32(char* out, char* end, int32_t v) {
    if (v < 0) { out = AppendText(out, end, "-"); v = -v; }
    char digits[12];
    uint32_t n = 0;
    uint32_t u = static_cast<uint32_t>(v);
    do { digits[n++] = static_cast<char>('0' + (u % 10u)); u /= 10u; } while (u);
    while (n && out < end - 1) *out++ = digits[--n];
    return out;
}

// Runs after Player's own state refresh, which is the only point in the frame
// where this frame's life and this frame's attack event are both readable.
extern "C" __attribute__((used)) void WiiXLaunch_ModTick() {
    LogFn log = g_Log;
    if (!log) return;

    VoidU32Fn getPlayer = g_GetPlayer;
    HandleI32Fn getLife = g_GetLife;
    HandleI32Fn getMax = g_GetMaxLife;
    HandleU32Fn isValid = g_IsValid;
    if (!getPlayer || !getLife || !getMax || !isValid) return;

    const uint32_t link = getPlayer();
    if (!link || !isValid(link)) return;   // not spawned yet - normal early on

    const int32_t life = getLife(link);
    const int32_t max = getMax(link);

    // The escape hatch, used exactly once, so the host's "opted out" line lands
    // in the boot log with this module's name on it.
    if (!g_TookRaw) {
        g_TookRaw = 1;
        RawFn raw = g_UnsafeRaw;
        if (raw) {
            const uintptr_t p = raw(link);
            log(p ? "e_player: took a raw pointer once, on purpose - see the "
                    "botw.player line above naming me"
                  : "e_player: the raw pointer came back null");
        }
    }

    // Life, on the first few frames it is readable and then only when it
    // CHANGES. A per-frame log is a denial of service against every other line
    // in the log; a change is the thing worth seeing.
    const bool firstFew = g_Reports < 3;
    if (firstFew || life != g_LastLife) {
        g_LastLife = life;
        g_Reports = g_Reports + 1;

        char line[128];
        char* o = AppendText(line, line + sizeof(line), "e_player: life ");
        o = AppendI32(o, line + sizeof(line), life);
        o = AppendText(o, line + sizeof(line), " / ");
        o = AppendI32(o, line + sizeof(line), max);
        o = AppendText(o, line + sizeof(line), " quarter-hearts");
        *o = 0;
        log(line);
    }

    // One consumer per swing. Consuming is the contract - two mods polling would
    // otherwise both see the same swing.
    VoidU32Fn consume = g_ConsumeHit;
    if (consume && consume()) {
        const uint32_t n = g_Swings + 1;
        g_Swings = n;
        if (n <= 3) {
            char line[96];
            char* o = AppendText(line, line + sizeof(line), "e_player: swing ");
            o = AppendI32(o, line + sizeof(line), static_cast<int32_t>(n));
            if (n == 3) o = AppendText(o, line + sizeof(line), " (quiet from here)");
            *o = 0;
            log(line);
        }
    }
}

extern "C" __attribute__((used)) void WiiXLaunch_ModEntry() {
    LogFn log = g_Log;
    if (!log) return;

    // Every cached accessor needs this hook. Idempotent in the module, so
    // several mods each calling it is fine and only the first installs; 1 means
    // this call was the one that did.
    VoidU32Fn init = g_Init;
    if (init) {
        log(init() ? "e_player: installed the player tick hook"
                   : "e_player: the player tick hook was already installed");
    }

    // Capability flags, not runtime discovery by getting zeroes back. 0 life is
    // a legitimate value - a dead actor - so without these a mod cannot tell
    // "this platform cannot read life" from "Link is dead".
    VoidU32Fn suppLife = g_SuppLife;
    VoidU32Fn suppAttack = g_SuppAttack;
    if (suppLife && suppAttack) {
        char line[128];
        char* o = AppendText(line, line + sizeof(line), "e_player: this host supports life=");
        o = AppendText(o, line + sizeof(line), suppLife() ? "yes" : "no");
        o = AppendText(o, line + sizeof(line), " attack-tracking=");
        o = AppendText(o, line + sizeof(line), suppAttack() ? "yes" : "no");
        *o = 0;
        log(line);
    }

    // Player's tick, not the host's. The host's fires at the GX2 swap after the
    // frame is drawn; this one fires right after Player's state refresh, which
    // is where this frame's life and this frame's attack event are readable.
    TickRegFn regTick = g_RegTick;
    if (regTick && regTick(&WiiXLaunch_ModTick)) {
        log("e_player: registered a player tick");
    } else {
        log("e_player: RegisterTick was refused - see the PlayerTick: line above");
    }
}
