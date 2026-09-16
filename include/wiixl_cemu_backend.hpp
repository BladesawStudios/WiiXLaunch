#pragma once

#include <cstdint>
#include <cstring>

namespace WiiXLaunch {
namespace Backend {

    constexpr size_t TRAMPOLINE_POOL_SIZE = 0x1000;
    static uint8_t g_TrampolinePool[TRAMPOLINE_POOL_SIZE];
    static size_t g_TrampolineAllocated = 0;

    extern "C" uintptr_t g_CodeCaveBase;
    extern "C" {
        // `used` for the same reason as the relocation globals below: the only
        // writer is scripts/deploy.py, which the compiler cannot see. Without it
        // a build where nothing happens to call CemuHeapBase() - a host with no
        // main.cpp, which is what stage 4 makes normal - drops the symbol, deploy
        // has nothing to patch, and the heap base silently reads as the code-cave
        // base with no offset. Found by scripts/test_host.py.
        __attribute__((section(".data"), used)) inline uint32_t g_CemuHeapOffset = 0;

        // Runtime relocation table, patched by scripts/deploy.py.
        //
        // Cemu hands out code caves sequentially in graphic-pack load order, so
        // the address this payload runs at depends on which packs the user has
        // enabled and on the Cemu version. Nothing at build time can know it.
        //
        // So the payload ships linked at base 0 and relocates itself on entry:
        // deploy.py emits every absolute reference as a table entry, and the
        // bootstrap applies them against the address it finds itself loaded at
        // before a single line of C++ runs. See WiiXLaunch_Cemu_Relocate.
        // `used`, because the only reader is the bootstrap's assembly. Without
        // it these inline definitions are discarded as unreferenced and the
        // link fails on the asm's symbol references.
        __attribute__((section(".data"), used)) inline uint32_t g_CemuRelocTableOffset = 0;
        __attribute__((section(".data"), used)) inline uint32_t g_CemuRelocCount = 0;
    }
    
    // -----------------------------------------------------------------------
    // Heap
    // -----------------------------------------------------------------------
    // The payload's memory comes from the code cave it is loaded into: the
    // bootstrap sets g_CodeCaveBase, deploy.py patches g_CemuHeapOffset with
    // the payload's own size, and everything past that is ours to hand out.
    //
    // How much is actually there, from Cemu's memory map (src/Cafe/HW/MMU/MMU.h):
    //
    //   0x01800000  MEMORY_CODECAVEAREA_ADDR, 4 MB - Cemu allocates the caves
    //               for every enabled graphic pack out of this, sequentially
    //   0x01C00000  end of that area; no Cemu region claims what follows
    //   0x02000000  MEMORY_CODEAREA_ADDR - the game's own code
    //
    // Cemu gives a patch group only the bytes it emits, so the cave is the
    // payload's size and nothing more, and the heap runs off the end of it
    // through whatever is left of the cave AREA. That area's end is the wall.
    //
    // It is NOT 0x02000000. The gap from 0x01C00000 to the game's code area is
    // unclaimed by any Cemu region, and an earlier version of this file treated
    // "unclaimed" as "ours" - it is not mapped, and a payload that allocated
    // past 0x01C00000 died silently on the first write. That is a real bug this
    // found: with one font loaded the heap peaked around 2.4 MB and never
    // reached the boundary, and adding three more fonts walked straight through
    // it. Unclaimed address space is not memory.
    //
    // These are CEMU constants, the same for every Wii U title, which is why
    // they can live in the generic backend when a game allocator's address
    // could not.
    constexpr uintptr_t kCemuCodeCaveAddr = 0x01800000;
    constexpr uintptr_t kCemuCodeCaveSize = 0x00400000;   // 4 MB, ALL packs share it
    constexpr uintptr_t kCemuCodeCaveEnd  = kCemuCodeCaveAddr + kCemuCodeCaveSize;
    constexpr uintptr_t kCemuCodeAreaAddr = 0x02000000;   // the game's code; documentation only

    // WHERE THE ALLOCATABLE MEMORY STARTS, AND NOTHING ELSE.
    //
    // This header used to own the whole heap: CemuHeapLimit, CemuHeapUsed,
    // CemuHeapRemaining, CemuHeapExhausted, AllocCemuHeap, a HeapProvider hook
    // and a SetHeapLimit override. All of it is gone, deliberately and without
    // a compatibility shim.
    //
    // The reason is the bug it kept reachable. Every caller in the project -
    // the loader, the GX2 layer, the BotW heap shim - called CemuHeapLimit()
    // and then did its own bookkeeping against the answer, and each of them
    // believed it had the whole distance to the wall to itself. That is how two
    // allocators end up handing out the same bytes. Leaving a wrapper here
    // would have kept every one of those callers compiling and kept the overlap
    // reachable, just harder to see.
    //
    // The single owner is WiiXLaunch::Arena (include/wiixlaunch/loader/arena.hpp).
    // It reads this base, works out the distance to kCemuCodeCaveEnd itself,
    // and hands out host allocations from one end and module grants from the
    // other. The host allocates with Arena::AllocHost; a mod allocates through
    // wiixl.core's Alloc and can never reach past its own grant.
    //
    // The name stays CemuHeapBase because deploy.py patches g_CemuHeapOffset by
    // name and scripts/test_host.py pins it; it means "first byte past the
    // payload", which is exactly what the arena needs.
    inline uintptr_t CemuHeapBase() {
        return g_CodeCaveBase + g_CemuHeapOffset;
    }

    inline void* AllocateTrampoline(size_t size) {
        if (g_TrampolineAllocated + size > TRAMPOLINE_POOL_SIZE) {
            return nullptr;
        }
        void* ptr = &g_TrampolinePool[g_TrampolineAllocated];
        g_TrampolineAllocated += size;
        
        uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
        if (p < 0x01000000) {
            p += g_CodeCaveBase;
        }
        return reinterpret_cast<void*>(p);
    }

    inline bool InitCemuBackend() {
        return true;
    }

    inline void Nop(uintptr_t addr) {
        *(volatile uint32_t*)addr = 0x60000000;
    }

    inline void FlushCache(uintptr_t addr, size_t size = 4) {
        uintptr_t p = addr & ~31;
        uintptr_t end = addr + size;
        for (; p < end; p += 32) {
            asm volatile(
                "li 0, 0\n"
                "dcbst 0, %0\n"
                "sync\n"
                "icbi 0, %0\n"
                : : "r"(p) : "r0", "memory"
            );
        }
        asm volatile("isync");
    }

    inline void Branch(uintptr_t addr, uintptr_t dest, bool link = false) {
        uint32_t delta = dest - addr;
        uint32_t insn = 0x48000000 | (delta & 0x03FFFFFC);
        if (link) {
            insn |= 1;
        }
        *(volatile uint32_t*)addr = insn;
        FlushCache(addr, 4);
    }

    inline void WriteLongJump(uintptr_t addr, uintptr_t dest) {
        uint32_t hi = static_cast<uint32_t>(dest) >> 16;
        uint32_t lo = static_cast<uint32_t>(dest) & 0xFFFF;
        volatile uint32_t* p = reinterpret_cast<volatile uint32_t*>(addr);
        p[0] = 0x3D800000 | hi;  // lis  r12, hi
        p[1] = 0x618C0000 | lo;  // ori  r12, r12, lo
        p[2] = 0x7D8903A6;       // mtctr r12
        p[3] = 0x4E800420;       // bctr
        FlushCache(addr, 16);
    }

    template <typename Callback, typename Original>
    inline void InstallHook(uintptr_t target, Callback callback, Original* originalOut) {
        uint32_t* tramp = reinterpret_cast<uint32_t*>(AllocateTrampoline(8 * 4));
        if (!tramp) return;

        for (int i = 0; i < 4; i++) {
            tramp[i] = *(volatile uint32_t*)(target + i * 4);
        }
        FlushCache(reinterpret_cast<uintptr_t>(tramp), 16);

        WriteLongJump(reinterpret_cast<uintptr_t>(&tramp[4]), target + 16);

        *originalOut = reinterpret_cast<Original>(tramp);

        uintptr_t cbAddr = reinterpret_cast<uintptr_t>(callback);
        if (cbAddr < 0x01000000) {
            cbAddr += g_CodeCaveBase;
        }
        WriteLongJump(target, cbAddr);
    }

}

}
