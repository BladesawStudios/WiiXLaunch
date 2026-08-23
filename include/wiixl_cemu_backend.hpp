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
        __attribute__((section(".data"))) inline uint32_t g_CemuHeapOffset = 0;

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
    
    inline void* AllocCemuHeap(size_t size, size_t align = 256) {
        static size_t s_allocated = 0;
        uintptr_t base = g_CodeCaveBase + g_CemuHeapOffset;
        uintptr_t current = base + s_allocated;
        uintptr_t aligned = (current + (align - 1)) & ~(align - 1);
        s_allocated = (aligned - base) + size;
        return reinterpret_cast<void*>(aligned);
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
