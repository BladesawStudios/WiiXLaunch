# --- WiiXLaunch coreinit dynamic-loader import shims (Cemu code-cave only) ---
#
# Same mechanism as cemu_fs.asm / cemu_mem.asm / cemu_logging.asm: the payload
# is a raw codecave blob, never a real RPL, so nothing it calls goes through OS
# import resolution. Cemu's patch assembler DOES resolve `import.<lib>.<Name>`,
# so each entry below is a one-instruction tail call Cemu points at the real
# coreinit export.
#
# WHY THESE THREE EXIST AT ALL, which is the interesting part.
#
# The obvious way to call nsysnet from the cave is a table of
# `import.nsysnet.socket` shims. That is what the old single-mod API server
# did, with a comment telling the user to delete the file if it broke.
#
# It cannot be in BASE, because Cemu fails the ENTIRE graphic pack when a patch
# import does not resolve - and BotW v208 does not import nsysnet (445 imports,
# no socket calls; checked against the RPX). On a setup where nsysnet.rpl is
# not in the process, a static nsysnet import would take down all of WiiXLaunch
# and every mod, with no log and no partial function, because the pack would
# simply never apply.
#
# coreinit's dynamic loader is imported by BotW and by every other title, so
# these three always resolve. Everything nsysnet is looked up through them at
# runtime, and "nsysnet is not available" becomes a value the log names on a
# host that otherwise works normally. See include/wiixlaunch/cemu/cemu_dynload.hpp.
#
# Keep this table in EXACTLY the same order as the CemuDynLoadImport enum in
# that header.
#
# WIIXL_OFFSET_SYMBOL: g_CemuDynLoadShimTableOffset

wiixlaunch_cemu_dynload_shim_table:
  .int wiixlaunch_cemu_dynload_shim_OSDynLoad_Acquire
  .int wiixlaunch_cemu_dynload_shim_OSDynLoad_FindExport
  .int wiixlaunch_cemu_dynload_shim_OSDynLoad_Release

wiixlaunch_cemu_dynload_shim_OSDynLoad_Acquire:
  b import.coreinit.OSDynLoad_Acquire
wiixlaunch_cemu_dynload_shim_OSDynLoad_FindExport:
  b import.coreinit.OSDynLoad_FindExport
wiixlaunch_cemu_dynload_shim_OSDynLoad_Release:
  b import.coreinit.OSDynLoad_Release
