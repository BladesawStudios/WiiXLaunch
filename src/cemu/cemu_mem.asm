# --- WiiXLaunch coreinit memory import shims (Cemu code-cave only) ---
#
# Same mechanism as cemu_time.asm / cemu_logging.asm: the payload is a raw
# codecave blob, never a real RPL module, so nothing it calls goes through OS
# import resolution. Cemu's own patch assembler DOES resolve
# `import.<lib>.<Name>`, so each entry below is a one-instruction tail call
# that Cemu points at the real coreinit export.
#
# Why these four: the payload's built-in heap is the tail of its code cave and
# ends at 0x01C00000, which is under 4 MB shared with every other pack. That is
# not enough for a mod that wants several 1024x1024 font sheets or a large
# render target. coreinit's base heaps are, and they are reachable from any Wii
# U title without a single game-specific address - which is exactly why this
# lives in the base framework and not in a game module.
#
# MEMAllocFromDefaultHeapEx is deliberately NOT used: it is a coreinit DATA
# export (a function pointer a game may replace), so branching to the symbol
# would jump to the pointer's storage rather than through it. The three
# expanded-heap entry points below are real function exports.
#
# scripts/deploy.py includes this file into the same codecave as the compiled
# payload and patches g_CemuMemShimTableOffset (see include/wiixlaunch/mem.hpp)
# with THIS table's own offset - the line below tells deploy.py which C++
# global to patch. Keep wiixlaunch_cemu_mem_shim_table in EXACTLY the same
# order as the CemuMemImport enum in that header.
#
# WIIXL_OFFSET_SYMBOL: g_CemuMemShimTableOffset

wiixlaunch_cemu_mem_shim_table:
  .int wiixlaunch_cemu_mem_shim_MEMGetBaseHeapHandle
  .int wiixlaunch_cemu_mem_shim_MEMAllocFromExpHeapEx
  .int wiixlaunch_cemu_mem_shim_MEMFreeToExpHeap
  .int wiixlaunch_cemu_mem_shim_MEMGetAllocatableSizeForExpHeapEx

# None of these are variadic, so unlike the OSReport shim they need no CR-bit-6
# setup; a plain tail call passes the arguments through untouched.
#
#   MEMGetBaseHeapHandle(MEMBaseHeapType type)          -> MEMHeapHandle
#   MEMAllocFromExpHeapEx(heap, u32 size, s32 align)    -> void*
#   MEMFreeToExpHeap(heap, void* block)                 -> void
#   MEMGetAllocatableSizeForExpHeapEx(heap, s32 align)  -> u32
#
# A NEGATIVE align means "allocate from the end of the heap" in coreinit's
# expanded heaps; the C++ side always passes a positive one.

wiixlaunch_cemu_mem_shim_MEMGetBaseHeapHandle:
  b import.coreinit.MEMGetBaseHeapHandle

wiixlaunch_cemu_mem_shim_MEMAllocFromExpHeapEx:
  b import.coreinit.MEMAllocFromExpHeapEx

wiixlaunch_cemu_mem_shim_MEMFreeToExpHeap:
  b import.coreinit.MEMFreeToExpHeap

wiixlaunch_cemu_mem_shim_MEMGetAllocatableSizeForExpHeapEx:
  b import.coreinit.MEMGetAllocatableSizeForExpHeapEx
