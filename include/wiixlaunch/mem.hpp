#pragma once

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/arena.hpp>

#include <cstddef>
#include <cstdint>

#if WIIXL_CEMU
#include <wiixl_cemu_backend.hpp>
#endif

// WiiXLaunch::Mem - allocating from coreinit's base heaps.
//
// The Cemu payload's built-in heap is the tail of its own code cave, ending at
// the end of Cemu's code-cave area (0x01C00000). That is under 4 MB, shared
// with every other graphic pack, and it is easy for a mod to run out: two
// 1024x1024 font sheets and a render target will do it.
//
// coreinit's base heaps are the way out, and they need no game-specific
// knowledge - MEMGetBaseHeapHandle and MEMAllocFromExpHeapEx are ordinary
// coreinit exports present in every Wii U title, reached through the
// `import.coreinit.<Name>` shims in src/cemu/cemu_mem.asm. That is why this is
// in the base framework: a game's own allocator address could not live here,
// but coreinit's exports can.
//
// Install with UseCoreinitHeap(), which picks a heap with room and hands it to
// Arena::SetHostProvider. Call it once the game is up - from a graphics
// initialisation callback, not from a module entry point, since the base heaps
// do not exist that early.
//
// This moves the HOST's allocations only. A loaded mod's grant stays in the
// code cave whatever is installed here, because a grant holds relocated code
// that gets executed and the cave is the only region this project has
// established is executable. See the HostProvider comment in
// wiixlaunch/loader/arena.hpp.

#if WIIXL_CEMU
extern "C" {
    // Patched by scripts/deploy.py with the offset of
    // wiixlaunch_cemu_mem_shim_table; 0 in the compiled ELF, so a zero here
    // means "deploy has not run" and the shims must not be called.
    // `used` because nothing in C++ may reference this: deploy.py writes it
    // and the paired src/cemu/*.asm table reads through it, neither of which
    // the compiler can see. Without it an inline variable no translation unit
    // odr-uses is never emitted, the symbol is absent from the ELF, deploy.py
    // has nothing to patch, and the shim table ships unreachable.
    __attribute__((section(".data"), used)) inline uint32_t g_CemuMemShimTableOffset = 0;
}
#endif

namespace WiiXLaunch::Mem {

// coreinit's MEMBaseHeapType.
enum class BaseHeap : uint32_t {
    MEM1 = 0,     // ~32 MB of fast memory; the game usually carves it up itself
    MEM2 = 1,     // main RAM, and much larger - the one to prefer
    FG   = 2,     // foreground bucket, only valid while in the foreground
};

#if WIIXL_CEMU

// Keep in sync with wiixlaunch_cemu_mem_shim_table in src/cemu/cemu_mem.asm.
enum class CemuMemImport : uint32_t {
    MEMGetBaseHeapHandle = 0,
    MEMAllocFromExpHeapEx,
    MEMFreeToExpHeap,
    MEMGetAllocatableSizeForExpHeapEx,
    Count
};

namespace impl {

// Set by the bootstrap before any C++ runs; the same symbol time.hpp uses.
extern "C" uintptr_t g_CodeCaveBase;

inline uintptr_t ShimAt(CemuMemImport which) {
    if (g_CemuMemShimTableOffset == 0) return 0;
    auto* table = reinterpret_cast<uintptr_t*>(g_CodeCaveBase + g_CemuMemShimTableOffset);
    return table[static_cast<uint32_t>(which)];
}

using FnGetBaseHeapHandle = void* (*)(uint32_t type);
using FnAllocFromExpHeapEx = void* (*)(void* heap, uint32_t size, int32_t align);
using FnFreeToExpHeap = void (*)(void* heap, void* block);
using FnGetAllocatableSize = uint32_t (*)(void* heap, int32_t align);

// The heap UseCoreinitHeap settled on; the provider closes over it.
__attribute__((section(".data"))) inline void* g_Heap = nullptr;

} // namespace impl

// True once the shim table has been patched in, i.e. deploy.py has run and
// these calls are safe to make.
inline bool ShimsAvailable() { return g_CemuMemShimTableOffset != 0; }

inline void* GetBaseHeapHandle(BaseHeap heap) {
    const uintptr_t fn = impl::ShimAt(CemuMemImport::MEMGetBaseHeapHandle);
    if (!fn) return nullptr;
    return reinterpret_cast<impl::FnGetBaseHeapHandle>(fn)(static_cast<uint32_t>(heap));
}

// Bytes the largest single allocation from `heap` could be, at `align`.
inline uint32_t GetAllocatableSize(void* heap, int32_t align = 256) {
    const uintptr_t fn = impl::ShimAt(CemuMemImport::MEMGetAllocatableSizeForExpHeapEx);
    if (!fn || !heap) return 0;
    return reinterpret_cast<impl::FnGetAllocatableSize>(fn)(heap, align);
}

inline void* AllocFromExpHeap(void* heap, uint32_t size, int32_t align = 256) {
    const uintptr_t fn = impl::ShimAt(CemuMemImport::MEMAllocFromExpHeapEx);
    if (!fn || !heap || size == 0) return nullptr;
    return reinterpret_cast<impl::FnAllocFromExpHeapEx>(fn)(heap, size, align);
}

inline void FreeToExpHeap(void* heap, void* block) {
    const uintptr_t fn = impl::ShimAt(CemuMemImport::MEMFreeToExpHeap);
    if (!fn || !heap || !block) return;
    reinterpret_cast<impl::FnFreeToExpHeap>(fn)(heap, block);
}

// The provider handed to Arena::SetHostProvider. Allocations are never freed,
// matching the arena - but these come out of the GAME's heap, so host code that
// allocates in a loop exhausts the game rather than itself. Allocate at load,
// not per frame.
inline void* CoreinitProvider(size_t size, size_t align) {
    return AllocFromExpHeap(impl::g_Heap, static_cast<uint32_t>(size),
                            static_cast<int32_t>(align ? align : 256));
}

// Which base heap is in use, or nullptr for the arena itself.
inline void* CurrentHeap() { return impl::g_Heap; }

// Point every later Arena::AllocHost at a coreinit base heap.
//
// Tries MEM2 first (much larger), then MEM1, and takes the first with at least
// `needBytes` allocatable - a heap that exists but is full is no use, and
// finding that out here is better than at the first texture. Returns the heap
// it installed, or nullptr if none qualified, in which case the built-in heap
// is left in place and nothing changes.
//
// Call once the game is running. `report` receives one line per heap examined,
// so a caller with a logger can say what was found; pass nullptr for silence.
using ReportFn = void (*)(const char* what, uint32_t type, void* heap, uint32_t allocatable);

inline void* UseCoreinitHeap(uint32_t needBytes = 1u << 20, ReportFn report = nullptr) {
    if (!ShimsAvailable()) {
        if (report) report("shim table not patched", 0, nullptr, 0);
        return nullptr;
    }
    const BaseHeap order[2] = { BaseHeap::MEM2, BaseHeap::MEM1 };
    for (BaseHeap type : order) {
        void* heap = GetBaseHeapHandle(type);
        const uint32_t free = GetAllocatableSize(heap, 256);
        if (report) report("base heap", static_cast<uint32_t>(type), heap, free);
        if (heap && free >= needBytes) {
            impl::g_Heap = heap;
            WiiXLaunch::Arena::SetHostProvider(&CoreinitProvider);
            return heap;
        }
    }
    return nullptr;
}

// Back to the arena's own memory. Anything already allocated from a base heap
// stays where it is.
inline void UseCodeCaveHeap() {
    impl::g_Heap = nullptr;
    WiiXLaunch::Arena::SetHostProvider(nullptr);
}

#else

inline bool ShimsAvailable() { return false; }
inline void* GetBaseHeapHandle(BaseHeap) { return nullptr; }
inline uint32_t GetAllocatableSize(void*, int32_t = 256) { return 0; }
inline void* AllocFromExpHeap(void*, uint32_t, int32_t = 256) { return nullptr; }
inline void FreeToExpHeap(void*, void*) {}
inline void* CurrentHeap() { return nullptr; }
using ReportFn = void (*)(const char* what, uint32_t type, void* heap, uint32_t allocatable);
inline void* UseCoreinitHeap(uint32_t = 1u << 20, ReportFn = nullptr) { return nullptr; }
inline void UseCodeCaveHeap() {}

#endif

} // namespace WiiXLaunch::Mem
