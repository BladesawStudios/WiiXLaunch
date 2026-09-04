// WiiXLaunch's Cemu bootstrap - BASE FRAMEWORK CODE, not sample code.
//
// This used to live at the bottom of src/main.cpp under a "NO TOUCHING BELOW
// THIS POINT" banner. That was tolerable while main.cpp was the mod. It stops
// being tolerable the moment main.cpp becomes a .wxlm like any other module:
// the host would lose its own bootstrap along with it, and the failure is not
// subtle - a fresh main.cpp does not link, because g_CodeCaveBase and the
// relocator are defined here and referenced from all over the framework.
//
// So this is its own translation unit, compiled into every Cemu build
// regardless of what the project's main.cpp contains. src/main.cpp is now
// deletable: an empty one still produces a booting host.
//
// WHAT IS AND IS NOT GAME-SPECIFIC. Everything here is game-agnostic except two
// values, and those come from wiixlaunch.json via
// include/wiixlaunch/generated_cemu_config.hpp:
//
//   WIIXL_CEMU_ENTRY_HOOK        where the pack redirects into the code cave
//   WIIXL_CEMU_ENTRY_DISPLACED   the instruction that redirect overwrites
//
// The bootstrap has to replay the displaced instruction and branch back to
// entry + 4, so it needs both. Hardcoding BotW's `mflr r0` here would put a
// game assumption in the framework, which is exactly what the three-layer split
// exists to prevent.

#include <wiixlaunch/platform.hpp>

#include <cstdint>

#if WIIXL_CEMU
#include <wiixlaunch/generated_cemu_config.hpp>

// THIS TRANSLATION UNIT IS THE HOST, so it pulls in the host's services rather
// than hoping the project's main.cpp does.
//
// Every one of these headers carries `inline` globals that something outside
// the compiler's view depends on: g_CemuRelocTableOffset and g_CemuRelocCount,
// which the asm block below loads; and the four g_Cemu*ShimTableOffset globals
// that scripts/deploy.py patches with their shim tables' offsets. `used` pins
// them against being dropped, but only once a translation unit has actually
// INCLUDED the header - `used` cannot resurrect a header nobody included.
//
// Both halves of that were found by the empty-main.cpp acceptance test: without
// the backend include the link fails on g_CemuRelocTableOffset, and without the
// umbrella the deploy fails on g_CemuFsShimTableOffset. When main.cpp was the
// only translation unit that mattered, it happened to include both.
#include <wiixlaunch.hpp>
#include <wiixl_cemu_backend.hpp>
// Compiled here so the format's static_asserts are checked in every build,
// including the empty-main host test. A layout drift between this header and
// scripts/wxlm.py is the one thing neither side can detect at runtime.
#include <wiixlaunch/loader/wxlm.hpp>
#endif

// The address this payload is running at. Set by the bootstrap below, from the
// address it finds itself loaded at - not baked in at deploy time.
extern "C" uintptr_t g_CodeCaveBase;
uintptr_t g_CodeCaveBase = 0;

#if WIIXL_CEMU

// Stringification, so the generated macros can be pasted into the asm block.
// asm() cannot see C++ constants, only text.
#define WIIXL_STR2(x) #x
#define WIIXL_STR(x) WIIXL_STR2(x)

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
// So the payload ships linked at base 0 and fixes itself up here.
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

// The default WiiXLaunch_Init, used when the project does not define one.
//
// A WEAK DEFINITION, which is a different thing from the undefined weak
// symbols docs/cemu-relocation.md warns about. The hazard there is taking the
// ADDRESS of an absent weak symbol: it links as 0 and the relocator rebases it
// into g_CodeCaveBase, so a null check passes. Nothing takes an address here -
// the bootstrap calls the name, and the linker picks the project's strong
// definition when there is one. No relocation is involved in the choice.
//
// This is what makes src/main.cpp deletable. Without it, an empty main.cpp
// fails to link on an undefined reference from the bootstrap's `bl`.
extern "C" __attribute__((weak)) void WiiXLaunch_Init() {
    WIIXL_LOG("WiiXLaunch: host up, no WiiXLaunch_Init defined by this project "
              "(base default). Nothing will be hooked.");
}

// Cemu's code cave entry point.
//
// Preserves all registers, works out where it is loaded, relocates the payload,
// calls WiiXLaunch_Init, restores all registers, replays the instruction the
// pack's branch displaced, and returns to the hook address + 4.
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

    // The instruction the pack's `b` overwrote at WIIXL_CEMU_ENTRY_HOOK,
    // emitted as its raw word so this file needs no knowledge of which game it
    // came from. For BotW v208 that is 0x7C0802A6 - `mflr r0`.
    ".int " WIIXL_STR(WIIXL_CEMU_ENTRY_DISPLACED) "\n"

    // Back to the hook address + 4. Literal immediates, so no relocation entry
    // is emitted and deploy.py leaves them alone - correct for a game address
    // that is already absolute.
    "lis 12, " WIIXL_STR(WIIXL_CEMU_ENTRY_RETURN_HI) "\n"
    "ori 12, 12, " WIIXL_STR(WIIXL_CEMU_ENTRY_RETURN_LO) "\n"
    "mtctr 12\n"
    "bctr\n"
);

// ---------------------------------------------------------------------------
// The hook demonstration target.
//
// A real function in the payload, hooked by real mods through the real chain,
// so a boot can show call order and the conflict line together. It exists
// because a wrong chain on a real game function is a crash, and the thing that
// needed proving at this stage was the CHAIN - game hooking is already
// demonstrated by the game module's own hooks.
//
// WRITTEN IN ASSEMBLY ON PURPOSE. The manager displaces the first four
// instructions into a trampoline and refuses any prologue containing a
// PC-relative branch. A C function's prologue is whatever the compiler felt
// like emitting that day, and could acquire a relative branch from an inlining
// decision three releases from now - at which point the demonstration would
// start refusing itself and look like a manager bug. These four are chosen and
// fixed:
//
//   mflr 0        7C0802A6   move from link register
//   stwu 1,-32(1) 9421FFE0   push a frame
//   stw  0,36(1)  90010024   save the return address
//   nop           60000000
//
// None is a branch of any form, so all four survive relocation. The `bl` that
// does the real work is the FIFTH instruction and is never moved - the
// trampoline jumps back to target+16, which is exactly this address.
//
// tools/hook_test asserts the encodings above are what IsPcRelativeBranch calls
// safe, so this comment cannot quietly stop being true.
extern "C" void WiiXLaunch_HookProbeBody();

extern "C" __attribute__((used)) void WiiXLaunch_HookProbeBody() {
    WIIXL_LOG("HookProbe: host body ran (this is the end of the chain)");
}

asm(
    ".section .text.WiiXLaunch_HookProbe\n"
    ".global WiiXLaunch_HookProbe\n"
    ".align 2\n"
    "WiiXLaunch_HookProbe:\n"
    // --- the four displaced instructions; none is position-dependent --------
    "mflr 0\n"
    "stwu 1, -32(1)\n"
    "stw 0, 36(1)\n"
    "nop\n"
    // --- everything from here stays put ------------------------------------
    "bl WiiXLaunch_HookProbeBody\n"
    "lwz 0, 36(1)\n"
    "mtlr 0\n"
    "addi 1, 1, 32\n"
    "blr\n"
);

#endif // WIIXL_CEMU
