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
// exactly the case the first rule in docs/modules.md exists for - without
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
