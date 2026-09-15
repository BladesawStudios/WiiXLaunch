#pragma once

// WIIXL_DECLARE_PATCH - how a mod declares a raw patch.
//
// The patch is DATA, emitted into a `.wxlm.patches` section that
// scripts/wxlm.py lifts out of the ELF verbatim and writes into the module's
// header table. No code runs to declare it, and the host applies it before the
// module's entry is ever called - see the load sequence in
// wiixlaunch/patches.hpp.
//
// USAGE, and both byte lists are required:
//
//     WIIXL_DECLARE_PATCH(fix_thing, 0x02000030,
//         WIIXL_PATCH_BYTES(0x7C, 0x9E, 0x23, 0x78),   // what must be there
//         WIIXL_PATCH_BYTES(0x60, 0x9E, 0x00, 0x00));  // what to write
//
// The first list is the ORIGIN and it is not optional. A patch is written by
// absolute address against one build of one game; applied to another build the
// address means something else and the write succeeds anyway, into a function
// the mod has never heard of. The host refuses to write unless the target holds
// what the patch says it should, which turns "wrong game version" from silent
// corruption into a named refusal.
//
// Bytes are in MEMORY order, so a PowerPC instruction word 0x7C9E2378 is
// written 0x7C, 0x9E, 0x23, 0x78 - big-endian, as it sits in the game.
//
// `used` because nothing in C++ ever references these records: the section is
// read by a Python script and the entries are consumed by the host. That is
// exactly the case the first rule in docs/framework/modules.md exists for - without
// `used` the compiler emits nothing and the mod ships with no patches at all,
// silently.

#include <cstdint>

// A patch record as it sits in the module, byte-for-byte identical to
// Wxlm::PatchEntry so wxlm.py can copy the section without reformatting it.
// Kept as its own struct so a mod need not include the loader's headers.
struct WiiXLaunchPatchRecord {
    uint32_t targetAddr;
    uint32_t size;
    uint8_t  origin[16];
    uint8_t  data[16];
};
static_assert(sizeof(WiiXLaunchPatchRecord) == 40,
              "must match Wxlm::PatchEntry - wxlm.py copies the section verbatim");

#define WIIXL_PATCH_BYTES(...) __VA_ARGS__

#define WIIXL_DECLARE_PATCH(name, addr, originBytes, dataBytes)                \
    extern "C" __attribute__((section(".wxlm.patches"), used))                 \
    const WiiXLaunchPatchRecord wiixl_patch__##name = {                        \
        (addr),                                                                \
        sizeof((const uint8_t[]){ dataBytes }),                                \
        { originBytes },                                                       \
        { dataBytes },                                                         \
    }

// ---------------------------------------------------------------------------
// WIIXL_DECLARE_PATCH_CROSS - one declaration, both architectures.
//
// `targetAddr` is a single 32-bit field and it CANNOT mean the same thing on
// both platforms: on Wii U and Cemu it is an absolute address in the game, and
// on Switch it is an offset from the main module's start, because an NSO is
// relocated to a different base every launch. That is the same reason
// wiixl.call takes an offset and the hook macros take a pair.
//
// Widening the field would not have helped, because the ADDRESS is not the only
// thing that differs. The origin and the replacement are machine code: a
// PowerPC `or r30,r4,r4` is 7C 9E 23 78 and an AArch64 `mov w1,#15` is
// E1 01 80 52. There is nothing a shared byte list could say.
//
// So the whole triple is selected at compile time, exactly like WIIXL_OFFSET.
// A .wxlm is already built per architecture - PPC32 for Wii U and Cemu, AArch64
// for Switch - so only one of these was ever going to be emitted, and the
// module carries no dead record for the other.
//
//     WIIXL_DECLARE_PATCH_CROSS(room_cap,
//         /* Switch  offset */ 0x01B299EC,
//         WIIXL_PATCH_BYTES(0xE1, 0x01, 0x80, 0x52),   // mov w1,#15
//         WIIXL_PATCH_BYTES(0xA1, 0x05, 0x80, 0x52),   // mov w1,#45
//         /* Wii U   address */ 0x02000030,
//         WIIXL_PATCH_BYTES(0x7C, 0x9E, 0x23, 0x78),   // or  r30,r4,r4
//         WIIXL_PATCH_BYTES(0x60, 0x9E, 0x00, 0x00));  // ori r30,r4,0
//
// Bytes stay in MEMORY order on both sides, which means they are not written
// the same way round: PowerPC is big-endian, so the word 0x7C9E2378 reads
// 7C 9E 23 78, and AArch64 is little-endian, so 0x528001E1 reads E1 01 80 52.
// Writing an AArch64 word the PowerPC way round produces a valid record, a
// valid module and a refusal at boot, which is the good outcome but a confusing
// one to read.
//
// FOR A PATCH THAT ONLY EXISTS ON ONE PLATFORM, do not invent an address for
// the other: guard the declaration instead, and the module for the other
// architecture simply carries one fewer patch.
//
//     #if WIIXL_SWITCH
//     WIIXL_DECLARE_PATCH(switch_only, 0x01B299EC, ..., ...);
//     #endif
// ---------------------------------------------------------------------------
#if defined(WIIXL_SWITCH) && WIIXL_SWITCH

// WHY THIS EMITS THE RECORD ITSELF INSTEAD OF CALLING WIIXL_DECLARE_PATCH.
//
// It tried to, and it does not work. A macro argument is fully expanded before
// it is substituted, so by the time `switchOrigin` reaches an inner
// function-like macro it is no longer one argument - WIIXL_PATCH_BYTES has
// already turned it into `0xE1, 0x01, 0x80, 0x52`, and the inner macro counts
// four. The compiler says "passed 10 arguments, but takes just 4".
//
// The parentheses only protect the commas while the arguments to THIS macro are
// being separated. Inside a brace initialiser a comma list is exactly what is
// wanted, so the body is written out here and the delegation dropped. Anyone
// tidying this into a call to WIIXL_DECLARE_PATCH will get the same error.
#define WIIXL_DECLARE_PATCH_CROSS(name, switchOffset, switchOrigin, switchData, \
                                  wiiuAddr, wiiuOrigin, wiiuData)               \
    extern "C" __attribute__((section(".wxlm.patches"), used))                  \
    const WiiXLaunchPatchRecord wiixl_patch__##name = {                         \
        (switchOffset),                                                         \
        sizeof((const uint8_t[]){ switchData }),                                \
        { switchOrigin },                                                       \
        { switchData },                                                         \
    }

#elif (defined(WIIXL_CEMU) && WIIXL_CEMU) || (defined(WIIXL_WIIU) && WIIXL_WIIU)

#define WIIXL_DECLARE_PATCH_CROSS(name, switchOffset, switchOrigin, switchData, \
                                  wiiuAddr, wiiuOrigin, wiiuData)               \
    extern "C" __attribute__((section(".wxlm.patches"), used))                  \
    const WiiXLaunchPatchRecord wiixl_patch__##name = {                         \
        (wiiuAddr),                                                             \
        sizeof((const uint8_t[]){ wiiuData }),                                  \
        { wiiuOrigin },                                                         \
        { wiiuData },                                                           \
    }

#else

// NOT A DEFAULT. Picking one silently would emit the wrong architecture's bytes
// against the wrong kind of address, and the first sign of it would be a
// refusal at boot on somebody else's console. scripts/build_mod.py defines one
// of these for every target it supports, so reaching here means the module is
// being compiled by something that has not said what it is building for.
//
// The failure is deferred to the point of USE rather than raised here, so
// including this header for WIIXL_DECLARE_PATCH alone still costs nothing.
#define WIIXL_DECLARE_PATCH_CROSS(name, switchOffset, switchOrigin, switchData, \
                                  wiiuAddr, wiiuOrigin, wiiuData)               \
    static_assert(false,                                                        \
        "WIIXL_DECLARE_PATCH_CROSS needs WIIXL_SWITCH, WIIXL_CEMU or "          \
        "WIIXL_WIIU defined - build with scripts/build_mod.py, which sets one.")

#endif
