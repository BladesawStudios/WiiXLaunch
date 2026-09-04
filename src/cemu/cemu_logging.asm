# --- WiiXLaunch coreinit logging import shims (Cemu code-cave only) ---
#
# Same mechanism as cemu_mem.asm / cemu_time.asm: the payload is a raw
# codecave blob, never a real RPL module, so nothing it calls goes through OS
# import resolution. Cemu's own patch assembler DOES resolve
# `import.<lib>.<Name>`, so each entry below is a one-instruction tail call
# that Cemu points at the real coreinit export.
#
# Why this is in the BASE framework and not a game module: OSReport and the
# MEM2-arena allocators are coreinit exports, present in every Wii U title, and
# reaching them needs no game-specific address - exactly the argument
# cemu_mem.asm's header makes for the memory shims. Base WiiXLaunch's own
# logger (include/wiixlaunch/debug_log.hpp) relays through this table, and the
# module loader has to be able to say something when no game module is
# installed at all, so this cannot live behind "did someone vendor
# wiixlaunch-botw". Contrast gx2_imports.asm, which stays in the BotW module:
# GX2 is a graphics concern the base framework has no business assuming.
#
# scripts/deploy.py includes this file into the same codecave as the compiled
# payload, and patches g_CemuLoggingShimTableOffset (see
# include/wiixlaunch/cemu/cemu_logging.hpp) with THIS table's own offset - the
# line below tells deploy.py which C++ global to patch. Keep
# wiixlaunch_cemu_logging_shim_table in EXACTLY the same order as the
# CemuLogImport enum in that header.
#
# WIIXL_OFFSET_SYMBOL: g_CemuLoggingShimTableOffset

wiixlaunch_cemu_logging_shim_table:
  .int wiixlaunch_cemu_logging_shim_OSReport
  .int wiixlaunch_cemu_logging_shim_MEMAllocFromDefaultHeapEx
  .int wiixlaunch_cemu_logging_shim_MEMFreeToDefaultHeap

wiixlaunch_cemu_logging_shim_OSReport:
  # OSReport is variadic - the PowerPC EABI varargs convention requires the
  # CALLER to set CR bit 6 (cr1's eq bit) to indicate whether any FLOATING
  # POINT args were passed through "...": 0 = none. A real, working graphic
  # pack (BreathOfTheWild/Cheats/PreventRandomSpawns/patch_PreventActorSpawns.asm)
  # does exactly this before its own `bl import.coreinit.OSReport` call.
  # GCC devkitPPC's own variadic-call codegen has no reason to know about
  # this Cafe-SDK-specific bit, so calling through our shim from ordinary
  # C++ left it as whatever garbage was already in CR. This did not crash
  # Cemu's OSReport - it just silently produced nothing, which for a while
  # looked like the cause of the "message never shows up" mystery. The real
  # cause turned out to be simpler and unrelated: Cemu's OSReport
  # implementation (WriteCafeConsole, coreinit_Misc.cpp) line-buffers and
  # only flushes to the log on '\n' - the callers always append one now.
  # This CR-bit fix is still correct and still required regardless - keeping
  # it clears real garbage, it just wasn't the whole story.
  crxor 4*cr1+eq, 4*cr1+eq, 4*cr1+eq
  b import.coreinit.OSReport

wiixlaunch_cemu_logging_shim_MEMAllocFromDefaultHeapEx:
  b import.coreinit.OSAllocFromMEM2Arena

wiixlaunch_cemu_logging_shim_MEMFreeToDefaultHeap:
  b import.coreinit.OSFreeToMEM2Arena
