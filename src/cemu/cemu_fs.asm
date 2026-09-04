# --- WiiXLaunch coreinit filesystem import shims (Cemu code-cave only) ---
#
# Same mechanism as cemu_mem.asm / cemu_time.asm / cemu_logging.asm: the
# payload is a raw codecave blob, never a real RPL module, so nothing it calls
# goes through OS import resolution. Cemu's own patch assembler DOES resolve
# `import.<lib>.<Name>`, so each entry below is a one-instruction tail call
# that Cemu points at the real coreinit export.
#
# Why this is in the BASE framework and not a game module: these are coreinit
# exports present in every Wii U title, reachable with no game-specific address
# - the same argument cemu_mem.asm's header makes for the memory shims. The
# module loader reads .wxlm blobs off the title's filesystem, so FSAddClient /
# FSOpenFile / FSReadFile are load-bearing for base WiiXLaunch itself and
# cannot be behind "did someone vendor wiixlaunch-botw". Contrast
# gx2_imports.asm, which stays in the BotW module: GX2 is a graphics concern
# the base framework has no business assuming.
#
# scripts/deploy.py includes this file into the same codecave as the compiled
# payload and patches g_CemuFsShimTableOffset (see
# include/wiixlaunch/cemu/cemu_fs.hpp) with THIS table's own offset - the line
# below tells deploy.py which C++ global to patch. Keep
# wiixlaunch_cemu_fs_shim_table in EXACTLY the same order as the CemuFsImport
# enum in that header.
#
# WIIXL_OFFSET_SYMBOL: g_CemuFsShimTableOffset

wiixlaunch_cemu_fs_shim_table:
  .int wiixlaunch_cemu_fs_shim_FSAddClient
  .int wiixlaunch_cemu_fs_shim_FSDelClient
  .int wiixlaunch_cemu_fs_shim_FSInitCmdBlock
  .int wiixlaunch_cemu_fs_shim_FSOpenFile
  .int wiixlaunch_cemu_fs_shim_FSGetStatFile
  .int wiixlaunch_cemu_fs_shim_FSReadFile
  .int wiixlaunch_cemu_fs_shim_FSWriteFile
  .int wiixlaunch_cemu_fs_shim_FSCloseFile
  .int wiixlaunch_cemu_fs_shim_FSReadFileWithPos
  .int wiixlaunch_cemu_fs_shim_FSOpenDir
  .int wiixlaunch_cemu_fs_shim_FSReadDir
  .int wiixlaunch_cemu_fs_shim_FSCloseDir

# None of these are variadic, so unlike the OSReport shim they need no
# CR-bit-6 setup; a plain tail call passes the arguments through untouched.

wiixlaunch_cemu_fs_shim_FSAddClient:
  b import.coreinit.FSAddClient
wiixlaunch_cemu_fs_shim_FSDelClient:
  b import.coreinit.FSDelClient
wiixlaunch_cemu_fs_shim_FSInitCmdBlock:
  b import.coreinit.FSInitCmdBlock
wiixlaunch_cemu_fs_shim_FSOpenFile:
  b import.coreinit.FSOpenFile
wiixlaunch_cemu_fs_shim_FSGetStatFile:
  b import.coreinit.FSGetStatFile
wiixlaunch_cemu_fs_shim_FSReadFile:
  b import.coreinit.FSReadFile
wiixlaunch_cemu_fs_shim_FSWriteFile:
  b import.coreinit.FSWriteFile
wiixlaunch_cemu_fs_shim_FSCloseFile:
  b import.coreinit.FSCloseFile
wiixlaunch_cemu_fs_shim_FSReadFileWithPos:
  b import.coreinit.FSReadFileWithPos
wiixlaunch_cemu_fs_shim_FSOpenDir:
  b import.coreinit.FSOpenDir
wiixlaunch_cemu_fs_shim_FSReadDir:
  b import.coreinit.FSReadDir
wiixlaunch_cemu_fs_shim_FSCloseDir:
  b import.coreinit.FSCloseDir
