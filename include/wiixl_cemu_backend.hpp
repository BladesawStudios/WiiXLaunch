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

    // A game layer can install its own allocator here - BotW, for instance, has
    // a MEM1 allocator inside the RPX that is not bounded by any of the above.
    // Its address is a per-game fact, so the address stays in the game's own
    // module and only the function pointer crosses into the framework. Null
    // (the default) means the built-in code-cave heap below.
    using HeapProvider = void* (*)(size_t size, size_t align);
    __attribute__((section(".data"))) inline HeapProvider g_HeapProvider = nullptr;

    // Set before the first allocation. Allocations already handed out by the
    // previous provider stay valid - nothing here is ever freed - but mixing
    // the two mid-run means the heap statistics only describe the built-in one.
    inline void SetHeapProvider(HeapProvider provider) { g_HeapProvider = provider; }
    inline HeapProvider GetHeapProvider() { return g_HeapProvider; }

    // Built-in code-cave heap state.
    __attribute__((section(".data"))) inline size_t g_HeapAllocated = 0;
    __attribute__((section(".data"))) inline size_t g_HeapRefusedBytes = 0;
    __attribute__((section(".data"))) inline uint32_t g_HeapRefusedCount = 0;
    // A voluntary cap below the wall, for a mod that would rather fail early
    // than find out at 0x02000000. 0 = use the whole distance to the wall.
    __attribute__((section(".data"))) inline size_t g_HeapLimitOverride = 0;

    inline void SetHeapLimit(size_t bytes) { g_HeapLimitOverride = bytes; }

    inline uintptr_t CemuHeapBase() {
        return g_CodeCaveBase + g_CemuHeapOffset;
    }

    // Bytes between the heap base and the wall, after any voluntary cap. 0 if
    // the base is not known yet or is already past the wall - in which case
    // every allocation is refused rather than guessed at.
    inline size_t CemuHeapLimit() {
        const uintptr_t base = CemuHeapBase();
        if (base == 0 || base >= kCemuCodeCaveEnd) return 0;
        size_t toWall = static_cast<size_t>(kCemuCodeCaveEnd - base);
        if (g_HeapLimitOverride != 0 && g_HeapLimitOverride < toWall) {
            toWall = g_HeapLimitOverride;
        }
        return toWall;
    }

    inline size_t CemuHeapUsed() { return g_HeapAllocated; }
    inline size_t CemuHeapRemaining() {
        const size_t limit = CemuHeapLimit();
        return limit > g_HeapAllocated ? limit - g_HeapAllocated : 0;
    }
    // True once anything has been refused. The framework cannot log from here
    // without dragging the logging headers into the backend, so a caller that
    // gets a null pointer should report this.
    inline bool CemuHeapExhausted() { return g_HeapRefusedCount != 0; }

    // The built-in allocator: a bump pointer, bounded, never freed. Returns
    // null when the request would cross the limit - it used to return a pointer
    // regardless, which meant an over-allocating mod silently wrote through the
    // end of the cave area and into whatever Cemu had placed after it.
    inline void* AllocCemuHeapRaw(size_t size, size_t align) {
        if (align == 0) align = 256;
        const uintptr_t base = CemuHeapBase();
        const size_t limit = CemuHeapLimit();
        if (limit == 0) {
            g_HeapRefusedBytes += size;
            g_HeapRefusedCount++;
            return nullptr;
        }
        const uintptr_t current = base + g_HeapAllocated;
        const uintptr_t aligned = (current + (align - 1)) & ~static_cast<uintptr_t>(align - 1);
        const size_t end = static_cast<size_t>(aligned - base) + size;
        if (end > limit || end < g_HeapAllocated) {      // second test: overflow
            g_HeapRefusedBytes += size;
            g_HeapRefusedCount++;
            return nullptr;
        }
        g_HeapAllocated = end;
        return reinterpret_cast<void*>(aligned);
    }

    inline void* AllocCemuHeap(size_t size, size_t align = 256) {
        if (g_HeapProvider) return g_HeapProvider(size, align);
        return AllocCemuHeapRaw(size, align);
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
