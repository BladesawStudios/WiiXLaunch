#!/usr/bin/env python3
import glob
import json
import os
import re
import shutil
import sys
import subprocess
import struct

def find_devkitppc_tool(name):
    """Resolve a devkitPPC binary without hardcoding the install location.

    Order: PATH, then $DEVKITPRO_WIN (Windows-path override used by the .bat
    scripts too), then the platform-default install roots. The DEVKITPRO env
    var itself is usually the msys2-style /opt/devkitpro, which only resolves
    inside devkitPro's msys2 - it is still tried last in case this script runs
    in such a shell.
    """
    found = shutil.which(name)
    if found:
        return found

    exe = name + (".exe" if os.name == "nt" else "")
    roots = [os.environ.get("DEVKITPRO_WIN"),
             "C:\\devkitPro" if os.name == "nt" else None,
             "/opt/devkitpro",
             os.environ.get("DEVKITPRO")]
    for root in roots:
        if not root:
            continue
        candidate = os.path.join(root, "devkitPPC", "bin", exe)
        if os.path.exists(candidate):
            return candidate

    raise RuntimeError(
        f"Cannot find {name}. Install devkitPPC (devkitPro) or set "
        f"DEVKITPRO_WIN to your devkitPro install directory.")


def main():
    root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    config_path = os.path.join(root_dir, "wiixlaunch.json")

    if not os.path.exists(config_path):
        print(f"Error: Could not find config at {config_path}")
        sys.exit(1)

    with open(config_path, "r", encoding="utf-8") as f:
        config = json.load(f)

    project_name = config.get("project", {}).get("name", "Mod")
    switch_cfg = config.get("switch", {})
    wiiu_cfg = config.get("wiiu", {})

    switch_title_id = switch_cfg.get("title_id", "01007EF00011E000").upper()
    subsdk_name = switch_cfg.get("subsdk_name", "subsdk9")
    plugin_name = wiiu_cfg.get("plugin_name", f"{project_name}.wps")
    wiiu_tids = ", ".join(wiiu_cfg.get("target_title_ids", ["00050000101C9400", "00050000101C9500"]))

    deploy_dir = os.path.join(root_dir, "deploy")
    switch_deploy_dir = os.path.join(deploy_dir, "switch", "atmosphere", "contents", switch_title_id, "exefs")
    wiiu_deploy_dir = os.path.join(deploy_dir, "wiiu", "wiiu", "environments", "aroma", "plugins")
    cemu_deploy_dir = os.path.join(deploy_dir, "cemu", "graphicPacks", f"WiiXLaunch_{project_name}")

    os.makedirs(switch_deploy_dir, exist_ok=True)
    os.makedirs(wiiu_deploy_dir, exist_ok=True)
    os.makedirs(cemu_deploy_dir, exist_ok=True)

    print("==========================================")
    print(f" WiiXLaunch Deployment Packager for [{project_name}]")
    print("==========================================")

    build_switch_subsdk = os.path.join(root_dir, "build", "switch", subsdk_name)
    build_switch_npdm = os.path.join(root_dir, "build", "switch", "main.npdm")
    
    # Placeholders are only ever written where no deployed file exists yet -
    # a previously deployed real binary must never be clobbered by running
    # deploy.py after building just one of the platforms.
    def deploy_or_placeholder(build_path, deploy_path, placeholder_text, label):
        if os.path.exists(build_path):
            shutil.copy2(build_path, deploy_path)
            print(f"[{label}] Copied {os.path.basename(deploy_path)} -> {os.path.dirname(deploy_path)}")
        elif not os.path.exists(deploy_path):
            with open(deploy_path, "w") as f:
                f.write(placeholder_text)
            print(f"[{label}] Created target placeholder -> {deploy_path}")
        else:
            print(f"[{label}] No fresh build for {os.path.basename(deploy_path)}; keeping existing deployed file")

    deploy_or_placeholder(build_switch_subsdk, os.path.join(switch_deploy_dir, subsdk_name),
                          "# WiiXLaunch Switch SubSDK Binary\n", "Switch")
    deploy_or_placeholder(build_switch_npdm, os.path.join(switch_deploy_dir, "main.npdm"),
                          "# WiiXLaunch Switch NPDM\n", "Switch")

    build_wiiu_wps = os.path.join(root_dir, "build", "wiiu", plugin_name)

    deploy_or_placeholder(build_wiiu_wps, os.path.join(wiiu_deploy_dir, plugin_name),
                          "# WiiXLaunch Wii U Aroma WPS Plugin\n", "Wii U")

    cemu_cfg = config.get("cemu", {})
    gp_path = cemu_cfg.get("graphic_pack_path", f"{project_name}/Mods/WiiXLaunch")
    gp_version = cemu_cfg.get("graphic_pack_version", "V100")
    mod_matches = cemu_cfg.get("module_matches", "0x00000000")

    cemu_rules_content = f"""[Definition]
titleIds = {wiiu_tids}
name = "{project_name} (WiiXLaunch Cemu Pack)"
path = "{gp_path}"
version = 7
"""
    cemu_rules_path = os.path.join(cemu_deploy_dir, "rules.txt")
    with open(cemu_rules_path, "w", encoding="utf-8") as f:
        f.write(cemu_rules_content)

    cemu_asm_content = f"[{project_name}_{gp_version}]\nmoduleMatches = {mod_matches}\n\n"
    
    elf_path = os.path.join(root_dir, "build", "wiixlaunch_cemu")
    if os.path.exists(elf_path):
        bin_path = os.path.join(root_dir, "build", "payload.bin")
        objcopy_cmd = find_devkitppc_tool("powerpc-eabi-objcopy")
        readelf_cmd = find_devkitppc_tool("powerpc-eabi-readelf")
            
        subprocess.run([objcopy_cmd, "-O", "binary", "--set-section-flags", ".bss=alloc,load,contents", elf_path, bin_path], check=True)
        with open(bin_path, "rb") as f:
            payload_data = f.read()

        if len(payload_data) % 4 != 0:
            payload_data += b'\x00' * (4 - (len(payload_data) % 4))

        # WiiXLaunch_Cemu_Init hand-relocates its own g_CodeCaveBase computation
        # in place (see the @h/@l immediates in src/main.cpp and the matching
        # comment in scripts/cemu.ld) - those absolute refs must NOT also be
        # "fixed" below, or the delta would get applied twice and zero out
        # g_CodeCaveBase. cemu.ld brackets that exact instruction range with
        # these two symbols so we can exclude it purely by address, without
        # guessing at offsets.
        # Symbol table, as a name->address dict (the ELF links at base 0, so
        # a symbol's link-time address IS its byte offset into the flat
        # binary) - reused below both for the bootstrap-range exclusion and
        # for patching per-file shim-table offsets into payload_data.
        syms_out = subprocess.check_output([readelf_cmd, "-s", "-W", elf_path], text=True)
        sym_dict = {}
        for line in syms_out.splitlines():
            parts = line.split()
            if len(parts) < 8:
                continue
            try:
                sym_dict[parts[7]] = int(parts[1], 16)
            except ValueError:
                continue  # header row ("Num:  Value  Size Type ..."), not a real symbol

        bootstrap_start = sym_dict.get("__wiixl_bootstrap_start")
        bootstrap_end = sym_dict.get("__wiixl_bootstrap_end")
        if bootstrap_start is None or bootstrap_end is None:
            raise RuntimeError("__wiixl_bootstrap_start/end symbols not found - check scripts/cemu.ld")

        # Full 32-bit absolute words: reading the already-linked (base-0) value
        # and adding the runtime delta directly is exact, no extra info needed.
        readelf_out = subprocess.check_output([readelf_cmd, "-r", elf_path], text=True)
        reloc_offsets = []
        # Split 16-bit absolute-address halves (from `lis`/`addis`+`ori`/`addi`
        # pairs materializing an absolute address in code, e.g. libm's rodata
        # constant loads - see the sqrtf crash this was added to fix). Unlike
        # ADDR32, these can't be fixed by adding delta to the bits already
        # baked into the instruction (that's only half the real address, and
        # HA additionally bakes in a sign-extension rounding adjustment) - so
        # each entry instead carries the relocation's own fully-resolved
        # S+Addend, and the target half is recomputed from scratch at deploy
        # time against (S+Addend+delta).
        lo_entries, ha_entries, hi_entries = [], [], []
        # Only relocations for sections that actually end up in the flat
        # binary may be turned into runtime fixups. Debug sections (.rela.
        # debug_info etc., present whenever the payload is compiled with -g)
        # also carry R_PPC_ADDR32 relocs, but their offsets are relative to
        # the debug sections - applying them would corrupt arbitrary words of
        # the payload at those offsets. (This happened: the resulting garbage
        # jump crashed Cemu's recompiler at boot.)
        current_section = ""
        for line in readelf_out.splitlines():
            parts = line.strip().split()
            if line.startswith("Relocation section"):
                m = re.search(r"'([^']+)'", line)
                current_section = m.group(1) if m else ""
                continue
            if ".debug" in current_section:
                continue
            if "R_PPC_ADDR32" in line or "R_PPC_RELATIVE" in line:
                if len(parts) >= 1:
                    offset = int(parts[0], 16)
                    # The bootstrap keeps raw link-time constants on purpose;
                    # excluded here for the same reason as the 16-bit halves.
                    if not (bootstrap_start <= offset < bootstrap_end):
                        reloc_offsets.append(offset)
                continue
            for rtype, bucket in (("R_PPC_ADDR16_LO", lo_entries),
                                   ("R_PPC_ADDR16_HA", ha_entries),
                                   ("R_PPC_ADDR16_HI", hi_entries)):
                if rtype in line and len(parts) >= 5:
                    offset = int(parts[0], 16)
                    if bootstrap_start <= offset < bootstrap_end:
                        break
                    sym_value = int(parts[3], 16)
                    # "Sym.Name + Addend" (or "- Addend") is everything from
                    # parts[4] onward; addend is always the last token.
                    addend_tok = parts[-1]
                    sign = -1 if (len(parts) >= 6 and parts[-2] == "-") else 1
                    addend = sign * int(addend_tok, 16)
                    s_plus_a = (sym_value + addend) & 0xFFFFFFFF
                    bucket.append((offset, s_plus_a))
                    break

        num_relocs = len(reloc_offsets)
        binary_size = len(payload_data)
        entry_hook = int(cemu_cfg.get("entry_hook", "0x00000000"), 16)

        # --- Runtime relocation table ------------------------------------
        #
        # The payload used to be relocated here against a hardcoded code cave
        # address. Cemu assigns code caves sequentially in graphic-pack load
        # order, so that address depends on which packs the user has enabled
        # and on the Cemu version - nothing this script can determine, and a
        # value that is right on one machine and wrong on the next.
        #
        # Wrong meant every absolute address in the payload was off by the same
        # delta: hooks jumped that far past their callbacks into unrelated
        # code, globals read the wrong memory, and WIIXL_LOG resolved a bogus
        # shim table so nothing was logged to explain it.
        #
        # So the payload now ships linked at base 0 and relocates itself. Each
        # entry is a header word of (kind << 24 | offset) plus the relocation's
        # link-time target; WiiXLaunch_Cemu_Relocate adds the real load address
        # and writes them before any other code runs.
        payload_buf = bytearray(payload_data)
        binary_size = len(payload_data)

        RELOC_ADDR32, RELOC_HA, RELOC_HI, RELOC_LO = 0, 1, 2, 3
        reloc_table = []

        # ADDR32 sites already hold their own base-0 target, so it is read back
        # out of the payload rather than recovered from the relocation entry.
        for offset in reloc_offsets:
            if offset + 4 <= binary_size:
                value = struct.unpack_from(">I", payload_buf, offset)[0]
                reloc_table.append((RELOC_ADDR32, offset, value))

        # A 16-bit half cannot be recovered from the instruction - it is half an
        # address, and HA additionally folds in a sign-extension carry - so
        # these carry the relocation's own resolved S+Addend instead.
        for kind, entries in ((RELOC_LO, lo_entries),
                              (RELOC_HA, ha_entries),
                              (RELOC_HI, hi_entries)):
            for offset, s_plus_a in entries:
                if offset + 2 <= binary_size:
                    reloc_table.append((kind, offset, s_plus_a))

        for _, offset, _ in reloc_table:
            if offset > 0x00FFFFFF:
                raise RuntimeError(
                    f"Relocation offset 0x{offset:X} does not fit the 24-bit field "
                    f"in the table header - the payload has outgrown 16MB")

        reloc_bytes = b"".join(
            struct.pack(">II", (kind << 24) | offset, value)
            for kind, offset, value in reloc_table)

        # The table sits immediately after the payload, so the payload's own
        # size is both where the table starts and how much of the code cave the
        # relocator has to flush from cache.
        reloc_table_offset = binary_size
        for symbol, value in (("g_CemuRelocTableOffset", reloc_table_offset),
                              ("g_CemuRelocCount", len(reloc_table))):
            addr = sym_dict.get(symbol)
            if addr is None:
                raise RuntimeError(f"{symbol} not found - is this built against a "
                                   f"WiiXLaunch with runtime relocation support?")
            struct.pack_into(">I", payload_buf, addr, value)

        print(f"[Cemu] Payload relocates itself at load "
              f"({len(reloc_table)} relocations, {len(reloc_bytes)} bytes of table)")

        # g_CodeCaveBase is deliberately left at 0: the bootstrap computes it
        # from the address it finds itself running at and stores it there.

        # src/cemu/*.asm goes immediately after wiixlaunch_binary's bytes
        cemu_asm_dirs = [os.path.join(root_dir, "src", "cemu")]
        for vendor_dir in sorted(glob.glob(os.path.join(root_dir, "vendor", "wiixlaunch-*"))):
            candidate = os.path.join(vendor_dir, "src", "cemu")
            if os.path.isdir(candidate):
                cemu_asm_dirs.append(candidate)

        cemu_asm_files = []
        for cemu_src_dir in cemu_asm_dirs:
            if not os.path.exists(cemu_src_dir):
                continue
            for cemu_src_root, _, cemu_src_files in os.walk(cemu_src_dir):
                for cemu_src_file in cemu_src_files:
                    if cemu_src_file.endswith(".asm"):
                        cemu_asm_files.append(os.path.join(cemu_src_root, cemu_src_file))
        cemu_asm_files.sort()

        def count_asm_words(asm_text):
            words = 0
            for raw_line in asm_text.splitlines():
                line = raw_line.split("#", 1)[0].strip()
                if not line or line.endswith(":") or line.startswith(".origin"):
                    continue
                words += 1
            return words

        offset_symbol_re = re.compile(r"WIIXL_OFFSET_SYMBOL:\s*(\S+)")
        cemu_included_asm_content = ""
        running_offset = binary_size + len(reloc_bytes)
        for asm_file_path in cemu_asm_files:
            with open(asm_file_path, "r", encoding="utf-8") as f:
                asm_text = f.read()

            m = offset_symbol_re.search(asm_text)
            if m:
                symbol_addr = sym_dict.get(m.group(1))
                if symbol_addr is not None and symbol_addr + 4 <= len(payload_buf):
                    struct.pack_into(">I", payload_buf, symbol_addr, running_offset)

            rel_path = os.path.relpath(asm_file_path, root_dir)
            cemu_included_asm_content += f"\n# --- Included from {rel_path} ---\n"
            cemu_included_asm_content += asm_text + "\n"
            running_offset += count_asm_words(asm_text) * 4

        # Patch g_CemuHeapOffset directly into payload
        heap_offset_sym = sym_dict.get("g_CemuHeapOffset")
        if heap_offset_sym is not None and heap_offset_sym + 4 <= len(payload_buf):
            struct.pack_into(">I", payload_buf, heap_offset_sym, running_offset)

        payload_data = bytes(payload_buf)

        cemu_asm_content += f"# --- WiiXLaunch C++ Payload (linked at 0, relocates itself on entry) ---\n"
        cemu_asm_content += ".origin = codecave\n"
        cemu_asm_content += "wiixlaunch_codecave_start:\n"
        cemu_asm_content += "wiixlaunch_binary:\n"
        for i in range(0, binary_size, 4):
            word = struct.unpack(">I", payload_data[i:i+4])[0]
            cemu_asm_content += f"  .int 0x{word:08X}\n"

        cemu_asm_content += "\n# --- Runtime relocation table (see WiiXLaunch_Cemu_Relocate) ---\n"
        for i in range(0, len(reloc_bytes), 4):
            word = struct.unpack(">I", reloc_bytes[i:i+4])[0]
            cemu_asm_content += f"  .int 0x{word:08X}\n"

        cemu_asm_content += cemu_included_asm_content
        # No heap reservation is emitted. An earlier version wrote one word at
        # `codecave + 0x600000` under a comment claiming to reserve 6 MB; it
        # reserved nothing. Cemu gives a patch group only the bytes it emits -
        # its log line "Applying patch group ... (Codecave: <start>-<end>)" spans
        # exactly the payload plus the relocation table - so that word landed past
        # the end of the cave, and past 0x01C00000, the end of Cemu's code-cave
        # area, which is not mapped at all.
        #
        # The payload's heap runs from g_CemuHeapOffset (patched above) to that
        # 0x01C00000 boundary and is bounded at runtime, so nothing needs to be
        # reserved here. See Backend::AllocCemuHeap in include/wiixl_cemu_backend.hpp.

        if entry_hook != 0:
            cemu_asm_content += f"\n# Entry Hook: redirect 0x{entry_hook:08X} -> wiixlaunch_codecave_start\n"
            cemu_asm_content += f".origin = 0x{entry_hook:08X}\n"
            cemu_asm_content += f"  b wiixlaunch_codecave_start\n\n"

    cemu_asm_path = os.path.join(cemu_deploy_dir, f"patch_{project_name}.asm")
    if not os.path.exists(elf_path) and os.path.exists(cemu_asm_path):
        # Same rule as deploy_or_placeholder: without a fresh build, never
        # replace a previously deployed patch (which contains the compiled
        # payload) with a payload-less shell.
        print(f"[Cemu] No fresh build ({elf_path} missing); keeping existing patch file")
    else:
        with open(cemu_asm_path, "w", encoding="utf-8") as f:
            f.write(cemu_asm_content)
        print(f"[Cemu] Generated Graphic Pack files -> {cemu_deploy_dir}")

    # Package src/resources into content/WiiXLaunch/ for Cemu graphic pack
    resources_src = os.path.join(root_dir, "src", "resources")
    resources_dst = os.path.join(cemu_deploy_dir, "content", "WiiXLaunch")
    if os.path.exists(resources_src):
        pack_script = os.path.join(root_dir, "scripts", "pack_resources.py")
        subprocess.run([sys.executable, pack_script, resources_src, resources_dst], check=True)

    print("\nDeployment structures ready in:")
    print(f" - Switch (Console / Ryujinx / Yuzu): {switch_deploy_dir}")
    print(f" - Wii U  (Real Console / Aroma):    {wiiu_deploy_dir}")
    print(f" - Cemu   (PC Emulator Graphic Pack): {cemu_deploy_dir}\n")

if __name__ == "__main__":
    main()
