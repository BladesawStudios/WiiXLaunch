// WiiXLaunch Cemu bootstrap.
// Relocates the base-0 payload to its assigned code cave, installs the
// entry hook, and jumps to WiiXLaunch_Init.
#include <wiixlaunch/platform.hpp>

#include <cstdint>

#if WIIXL_CEMU
#include <wiixlaunch/generated_cemu_config.hpp>

#include <wiixlaunch.hpp>
#include <wiixl_cemu_backend.hpp>
#include <wiixlaunch/loader/wxlm.hpp>
#include <wiixlaunch/hook_probe.hpp>
#include <wiixlaunch/patches.hpp>
#include <wiixlaunch/loader/net_surface.hpp>
#endif

// Base address of the Cemu code cave, set during bootstrap.
extern "C" uintptr_t g_CodeCaveBase;
uintptr_t g_CodeCaveBase = 0;

#if WIIXL_CEMU

#define WIIXL_STR2(x) #x
#define WIIXL_STR(x) WIIXL_STR2(x)

// Applies the relocation table emitted by deploy.py.
// Safe to run pre-relocation: no globals, no strings, no external calls.
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

    // Flush data cache and invalidate instruction cache across patched sites.
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

// Default weak WiiXLaunch_Init, used when the project does not define one.
extern "C" __attribute__((weak)) void WiiXLaunch_Init() {
    WIIXL_LOG("WiiXLaunch: host up, no WiiXLaunch_Init defined by this project "
              "(base default). Nothing will be hooked.");
}

// Cemu code cave entry point: saves registers, computes load base, relocates
// the payload, calls WiiXLaunch_Init, restores registers, replays the
// displaced instruction, and jumps back to the game.
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

    // r31 = load address (runtime address minus link-time base 0).
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

    // Publish runtime base to g_CodeCaveBase.
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

    // Replay displaced instruction at entry hook.
    ".int " WIIXL_STR(WIIXL_CEMU_ENTRY_DISPLACED) "\n"

    // Return to hook address + 4.
    "lis 12, " WIIXL_STR(WIIXL_CEMU_ENTRY_RETURN_HI) "\n"
    "ori 12, 12, " WIIXL_STR(WIIXL_CEMU_ENTRY_RETURN_LO) "\n"
    "mtctr 12\n"
    "bctr\n"
);

// Demonstration target with a fixed assembly prologue for hook testing.
extern "C" void WiiXLaunch_HookProbeBody();

extern "C" __attribute__((used)) void WiiXLaunch_HookProbeBody() {
    WIIXL_LOG("HookProbe: host body ran (this is the end of the chain)");
    WiiXLaunch::HookProbe::MarkHost();
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
