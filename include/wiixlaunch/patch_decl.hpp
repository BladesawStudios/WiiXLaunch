#pragma once

// WIIXL_DECLARE_PATCH - how a mod declares a raw patch.
//
// The patch is data, emitted into a `.wxlm.patches` section that
// scripts/wxlm.py lifts out of the ELF and writes into the module's header
// table. No code runs to declare it; the host applies it before the
// module's entry runs (see the load sequence in wiixlaunch/patches.hpp).
//
//     WIIXL_DECLARE_PATCH(fix_thing, 0x02000030,
//         WIIXL_PATCH_BYTES(0x7C, 0x9E, 0x23, 0x78),   // what must be there
//         WIIXL_PATCH_BYTES(0x60, 0x9E, 0x00, 0x00));  // what to write
//
// Both byte lists are required. The first is the origin: the host refuses
// to write unless the target currently holds it, so a patch built against
// the wrong game build is refused by name instead of silently corrupting an
// unrelated function at that address.
//
// Bytes are in memory order: PowerPC word 0x7C9E2378 is written
// 0x7C, 0x9E, 0x23, 0x78 (big-endian).
//
// `used`: nothing in C++ references these records, only a Python script and
// the host. Without it the compiler emits nothing and the mod ships with no
// patches, silently.

#include <cstdint>

// Byte-for-byte identical to Wxlm::PatchEntry so wxlm.py can copy the
// section without reformatting it. Its own struct so a mod need not include
// the loader's headers.
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
// `targetAddr` can't mean the same thing on both platforms: absolute on Wii
// U/Cemu, an offset from the module start on Switch (an NSO relocates per
// launch). The origin and replacement bytes are different machine code
// entirely, so the whole triple is chosen at compile time, like
// WIIXL_OFFSET. A .wxlm is already built per architecture, so only one
// record is ever emitted.
//
//     WIIXL_DECLARE_PATCH_CROSS(room_cap,
//         /* Switch  offset */ 0x01B299EC,
//         WIIXL_PATCH_BYTES(0xE1, 0x01, 0x80, 0x52),   // mov w1,#15
//         WIIXL_PATCH_BYTES(0xA1, 0x05, 0x80, 0x52),   // mov w1,#45
//         /* Wii U   address */ 0x02000030,
//         WIIXL_PATCH_BYTES(0x7C, 0x9E, 0x23, 0x78),   // or  r30,r4,r4
//         WIIXL_PATCH_BYTES(0x60, 0x9E, 0x00, 0x00));  // ori r30,r4,0
//
// Bytes stay in memory order on both sides: PowerPC big-endian
// (0x7C9E2378 -> 7C 9E 23 78), AArch64 little-endian (0x528001E1 -> E1 01
// 80 52). Writing an AArch64 word the PowerPC way round still produces a
// valid module, just one refused at boot.
//
// For a patch that exists on one platform only, guard the declaration
// instead of inventing an address for the other side:
//
//     #if WIIXL_SWITCH
//     WIIXL_DECLARE_PATCH(switch_only, 0x01B299EC, ..., ...);
//     #endif
// ---------------------------------------------------------------------------
#if defined(WIIXL_SWITCH) && WIIXL_SWITCH

// Emits the record directly rather than delegating to WIIXL_DECLARE_PATCH:
// a macro argument is fully expanded before substitution, so by the time
// switchOrigin reaches an inner function-like macro it's no longer one
// argument (WIIXL_PATCH_BYTES already expanded it to four), and the
// delegation fails to compile.
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

// No default: picking one silently would emit the wrong architecture's
// bytes against the wrong kind of address. scripts/build_mod.py defines
// one of these for every target it supports.
#define WIIXL_DECLARE_PATCH_CROSS(name, switchOffset, switchOrigin, switchData, \
                                  wiiuAddr, wiiuOrigin, wiiuData)               \
    static_assert(false,                                                        \
        "WIIXL_DECLARE_PATCH_CROSS needs WIIXL_SWITCH, WIIXL_CEMU or "          \
        "WIIXL_WIIU defined - build with scripts/build_mod.py, which sets one.")

#endif
