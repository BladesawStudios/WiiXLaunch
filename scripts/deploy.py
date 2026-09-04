#!/usr/bin/env python3
import glob
import json
import os
import re
import shutil
import sys
import subprocess
import struct

import ppc_relocs

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
        # Relocations, parsed by scripts/ppc_relocs.py - the same code
        # scripts/wxlm.py uses for modules, because the host payload and a
        # module are the same problem.
        #
        # They were separate implementations once, and the copy dropped the
        # ADDEND: every ADDR16_HA/LO pair resolved to its section base instead
        # of the symbol it named. Nothing crashed - the first module to load
        # simply printed the same string five times, because every literal
        # pointed at the start of .rodata.
        #
        # deploy.py keeps its own policy on top:
        #
        #   The bootstrap range is EXCLUDED. WiiXLaunch_Cemu_Init hand-computes
        #   its own runtime-vs-link-time delta from @h/@l immediates that are
        #   deliberately left as raw link-time constants; "fixing" them like any
        #   other absolute reference would double-apply the delta and zero out
        #   g_CodeCaveBase. cemu.ld brackets that range for exactly this.
        reloc_offsets = []
        lo_entries, ha_entries, hi_entries = [], [], []

        for r in ppc_relocs.read(readelf_cmd, elf_path):
            if bootstrap_start <= r.offset < bootstrap_end:
                continue
            if r.type in ("R_PPC_ADDR32", "R_PPC_RELATIVE"):
                reloc_offsets.append(r.offset)
            elif r.type == "R_PPC_ADDR16_LO":
                lo_entries.append((r.offset, r.s_plus_a))
            elif r.type == "R_PPC_ADDR16_HA":
                ha_entries.append((r.offset, r.s_plus_a))
            elif r.type == "R_PPC_ADDR16_HI":
                hi_entries.append((r.offset, r.s_plus_a))

        num_relocs = len(reloc_offsets)
        binary_size = len(payload_data)
        entry_hook = int(cemu_cfg.get("entry_hook", "0x00000000"), 16)

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

            rel_path = os.path.relpath(asm_file_path, root_dir)
            is_module_asm = rel_path.replace(chr(92), "/").startswith("vendor/")

            # An .asm that declares an offset symbol must have it resolve, OR
            # not be emitted at all. The table is only reachable through the C++
            # global named here, patched with the table's offset - so shipping
            # the table without the symbol means shipping bytes nothing can
            # call, and every call through it reads a null pointer. Silent at
            # build time, silent at boot.
            #
            # There are two ways the symbol can be missing, and they need
            # different answers:
            #
            #   BASE (src/cemu/*.asm) - the host always needs these, and
            #   src/cemu/bootstrap.cpp includes the umbrella precisely so their
            #   globals are always emitted. Missing means something is wrong:
            #   hard error.
            #
            #   MODULE (vendor/wiixlaunch-*/src/cemu/*.asm) - a project may
            #   legitimately vendor a module and not use it, in which case the
            #   header declaring the global is never included and the symbol
            #   genuinely should not exist. Emitting its shim table anyway is
            #   just dead weight in a shared 4 MB code cave. Skip the file and
            #   say so.
            #
            # This is stricter than the original skip either way: the old code
            # emitted the table AND failed to patch it. Nothing now ships a
            # table it cannot reach.
            #
            # THE UNDERLYING RULE, because it keeps coming up: anything
            # referenced only from outside the compiler's view MUST be
            # __attribute__((used)) - data or code, no distinction. GCC emits an
            # inline definition only when a translation unit odr-uses it, and a
            # reference the compiler cannot see does not count.
            #
            # It has now fired from both directions. A data global written by
            # this script and read by a src/cemu/*.asm table
            # (g_CemuMemShimTableOffset) was dropped and shipped unreachable -
            # silent at runtime. An inline function whose only caller was a
            # hand-written asm() block (WiiXLaunch_LoadPointProbe) was dropped
            # and failed at link - loud. Same cause, opposite symptoms.
            #
            # `used` cannot rescue a header nobody included, which is why
            # src/cemu/bootstrap.cpp includes the umbrella: the host's own
            # translation unit is what guarantees the base globals exist at all.
            m = offset_symbol_re.search(asm_text)
            if m:
                symbol_name = m.group(1)
                symbol_addr = sym_dict.get(symbol_name)

                if symbol_addr is None and is_module_asm:
                    # Which module is this, and is it actually compiled in?
                    #
                    # The module declares WIIXL_DECLARE_MODULE(<name>) from its
                    # umbrella header, which emits g_WiiXLaunchModule_<name>.
                    # Present means some translation unit included that header,
                    # so the module IS part of this build - and a missing shim
                    # symbol is then a dropped global, not an unused module.
                    #
                    # Without this the two cases printed the same line and only
                    # one of them was acceptable.
                    module_name = None
                    parts = rel_path.replace(chr(92), "/").split("/")
                    for part in parts:
                        if part.startswith("wiixlaunch-"):
                            module_name = part[len("wiixlaunch-"):]
                            break

                    marker = f"g_WiiXLaunchModule_{module_name}" if module_name else None
                    module_is_used = marker is not None and marker in sym_dict

                    if module_is_used:
                        raise RuntimeError(
                            f"{rel_path} declares WIIXL_OFFSET_SYMBOL: {symbol_name}, but that "
                            f"symbol is not in {os.path.basename(elf_path)} - while the module "
                            f"IS compiled into this build ({marker} is present).\n"
                            f"  So this is a dropped symbol, not an unused module. Its shim "
                            f"table would ship in the code cave with nothing able to call it, "
                            f"and every call through it would read a null pointer.\n"
                            f"  Almost certainly {symbol_name} is missing "
                            f"__attribute__((used)), or the header declaring it is no longer "
                            f"included by anything in the module.")

                    if marker is None:
                        print(f"[Cemu] Skipping {rel_path}: could not work out which module it "
                              f"belongs to, and {symbol_name} is absent. Expected the path to "
                              f"contain a 'wiixlaunch-<name>' directory.")
                    else:
                        print(f"[Cemu] Skipping {rel_path}: module '{module_name}' is vendored "
                              f"but not compiled into this build ({marker} absent), so its shim "
                              f"table would be dead weight in the code cave.")
                    continue

                if symbol_addr is None:
                    raise RuntimeError(
                        f"{rel_path} declares WIIXL_OFFSET_SYMBOL: {symbol_name}, but there "
                        f"is no such symbol in {os.path.basename(elf_path)}.\n"
                        f"  This is base framework asm, so the host always needs it - "
                        f"src/cemu/bootstrap.cpp includes the umbrella so these globals are "
                        f"always emitted.\n"
                        f"  Usually the header declaring {symbol_name} lost its include, or "
                        f"the global is missing __attribute__((used)) - an inline variable no "
                        f"translation unit odr-uses is never emitted.")

                if symbol_addr + 4 > len(payload_buf):
                    raise RuntimeError(
                        f"{rel_path} declares WIIXL_OFFSET_SYMBOL: {symbol_name} at "
                        f"0x{symbol_addr:X}, which is past the end of the {len(payload_buf)}-byte "
                        f"payload.\n"
                        f"  The offset cannot be written, so the shim table would ship "
                        f"unreachable. Check that the global lives in a section the flat binary "
                        f"actually contains (see scripts/cemu.ld).")

                struct.pack_into(">I", payload_buf, symbol_addr, running_offset)

            cemu_included_asm_content += f"\n# --- Included from {rel_path} ---\n"
            cemu_included_asm_content += asm_text + "\n"
            running_offset += count_asm_words(asm_text) * 4

        # Patch g_CemuHeapOffset directly into payload.
        #
        # Strict, for the same reason the shim offsets are: this was the last
        # silent skip in this script. A missing symbol here does not fail the
        # build - it ships a payload whose heap base is the code-cave base with
        # no offset, so the first allocation hands back memory overlapping the
        # payload's own code. There is no diagnostic; things simply get
        # corrupted later.
        heap_offset_sym = sym_dict.get("g_CemuHeapOffset")
        if heap_offset_sym is None:
            raise RuntimeError(
                "g_CemuHeapOffset is not in %s, so the payload's heap base cannot be "
                "patched.\n"
                "  Everything allocated at runtime would come out of the payload's own "
                "code. The global lives in include/wiixl_cemu_backend.hpp and is "
                "__attribute__((used)); a build missing it is one where no translation "
                "unit included that header at all." % os.path.basename(elf_path))
        if heap_offset_sym + 4 > len(payload_buf):
            raise RuntimeError(
                "g_CemuHeapOffset is at 0x%X, past the end of the %d-byte payload - it is "
                "not in a section the flat binary contains (see scripts/cemu.ld)."
                % (heap_offset_sym, len(payload_buf)))
        struct.pack_into(">I", payload_buf, heap_offset_sym, running_offset)

        payload_data = bytes(payload_buf)

        # --- Load point (build-time nomination) ---
        #
        # A project declares one with WIIXL_DECLARE_LOAD_POINT(addr) (see
        # include/wiixlaunch/loader/load_point.hpp) and provides a stub named
        # WiiXLaunch_LoadPointStub. Both are read out of the ELF here: the
        # declared game address becomes an `.origin` patch in this pack, and the
        # stub's offset in the payload becomes the branch target label.
        #
        # Deliberately NOT a runtime patch. The load point sits inside a
        # function Cemu may already have recompiled by the time the payload
        # runs, and only the host pack should ever write into game memory.
        # Emitting it here removes both questions.
        #
        # No declaration means no load point: nothing is emitted and the reason
        # is printed. That is the correct state for a host with no game module,
        # not a silent boot that does nothing.
        load_point_addr = 0
        load_point_stub_offset = None
        lp_addr_sym = sym_dict.get("g_WiiXLaunchLoadPointAddr")
        lp_stub_sym = sym_dict.get("WiiXLaunch_LoadPointStub")
        if lp_addr_sym is not None and lp_addr_sym + 4 <= len(payload_data):
            load_point_addr = struct.unpack_from(">I", payload_data, lp_addr_sym)[0]

        if load_point_addr != 0 and lp_stub_sym is None:
            raise RuntimeError(
                f"A load point is declared at 0x{load_point_addr:08X} "
                f"(g_WiiXLaunchLoadPointAddr), but there is no WiiXLaunch_LoadPointStub "
                f"symbol to branch to.\n"
                f"  WIIXL_DECLARE_LOAD_POINT requires a stub with that exact name - "
                f"deploy.py places the branch target label by looking it up.")
        if load_point_addr == 0 and lp_stub_sym is not None:
            raise RuntimeError(
                "WiiXLaunch_LoadPointStub exists but no load point address is declared "
                "(g_WiiXLaunchLoadPointAddr absent or zero).\n"
                "  The stub would sit in the codecave and never be reached. Declare where "
                "it is branched from with WIIXL_DECLARE_LOAD_POINT(addr).")

        if load_point_addr != 0:
            load_point_stub_offset = lp_stub_sym
            if load_point_stub_offset % 4 != 0 or load_point_stub_offset >= binary_size:
                raise RuntimeError(
                    f"WiiXLaunch_LoadPointStub is at 0x{load_point_stub_offset:X}, which is not "
                    f"a 4-byte-aligned offset inside the {binary_size}-byte payload.")
            print(f"[Cemu] Load point 0x{load_point_addr:08X} -> stub at payload "
                  f"+0x{load_point_stub_offset:X}")
        else:
            print("[Cemu] No load point declared (no WIIXL_DECLARE_LOAD_POINT in this build) "
                  "- modules will not be loaded")

        cemu_asm_content += f"# --- WiiXLaunch C++ Payload (linked at 0, relocates itself on entry) ---\n"
        cemu_asm_content += ".origin = codecave\n"
        cemu_asm_content += "wiixlaunch_codecave_start:\n"
        cemu_asm_content += "wiixlaunch_binary:\n"
        for i in range(0, binary_size, 4):
            # The load-point branch needs a label Cemu's assembler can resolve.
            # The stub lives inside the payload blob rather than in one of the
            # .asm files, so its label is planted at its offset here.
            if load_point_stub_offset is not None and i == load_point_stub_offset:
                cemu_asm_content += "wiixlaunch_loadpoint_stub:\n"
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

        if load_point_addr != 0:
            # One instruction, exactly like the entry hook below. The stub is
            # responsible for executing the single displaced instruction and
            # branching back to load_point_addr + 4.
            cemu_asm_content += (f"\n# Load Point: redirect 0x{load_point_addr:08X} -> "
                                 f"wiixlaunch_loadpoint_stub\n")
            cemu_asm_content += f".origin = 0x{load_point_addr:08X}\n"
            cemu_asm_content += f"  b wiixlaunch_loadpoint_stub\n\n"

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

    # --- STAGE 1 SCAFFOLDING: the load-point probe's PACK test file ---
    #
    # WiiXLaunch::LoadPoint's PACK probe opens
    # /vol/content/wiixlaunch/mods/probe.bin to find out whether Cemu's
    # graphic-pack content/ overlay is live at the load point. Without this file
    # the probe reports NOT-FOUND for the uninteresting reason that nothing ever
    # shipped one, which answers nothing.
    #
    # CANONICAL CASE: content/WiiXLaunch/. Nothing else ships, and this is
    # enforced below rather than left to convention.
    #
    # Wii U's filesystem is case-sensitive. The host filesystem this pack is
    # BUILT on usually is not: on Windows/NTFS, asking for content/wiixlaunch/
    # when content/WiiXLaunch/ already exists silently resolves to the existing
    # directory, so a lower-case path in this script ships under the capitalised
    # name anyway - and two directories differing only in case cannot coexist
    # there at all. A boot on Windows therefore cannot tell you which spelling
    # is correct; it resolves both. Linux Cemu will not.
    #
    # WiiXLaunch wins because it is what already ships: pack_resources.py writes
    # content/WiiXLaunch/logo.bin, and GX2::LoadTexture("WiiXLaunch/logo.bin")
    # and the docs all name it that way. Changing those to match a lower-case
    # mods/ would be a bigger and more breakable change than picking the
    # capitalisation already in use.
    #
    # The PACK-LC probe in load_point.hpp deliberately asks for the lower-case
    # spelling. On a case-insensitive host it answers (NTFS resolving both); on
    # a case-sensitive one it must report NOT-FOUND, which is the correct result
    # and confirms this enforcement is doing something.
    WIIXL_CONTENT_DIR = "WiiXLaunch"
    probe_dir = os.path.join(cemu_deploy_dir, "content", WIIXL_CONTENT_DIR, "mods")
    os.makedirs(probe_dir, exist_ok=True)
    probe_path = os.path.join(probe_dir, "probe.bin")
    probe_magic = b"WXLP"
    probe_body = probe_magic + b"ROBE stage-1 load point probe file. "
    probe_body += bytes(range(0x10)) * 2
    probe_body = probe_body[:64].ljust(64, b"\x00")
    with open(probe_path, "wb") as f:
        f.write(probe_body)
    print(f"[Cemu] Load-point probe file -> content/WiiXLaunch/mods/probe.bin "
          f"({len(probe_body)} bytes, magic {probe_magic.decode()})")

    # The sample module, if this build produced one. Copied rather than
    # generated: build_cemu.bat compiles and packs it, because the flags belong
    # with the other compile flags.
    sample_src = os.path.join(root_dir, "build", "sample.wxlm")
    if os.path.exists(sample_src):
        shutil.copy2(sample_src, os.path.join(probe_dir, "sample.wxlm"))
        print(f"[Cemu] Module -> content/WiiXLaunch/mods/sample.wxlm "
              f"({os.path.getsize(sample_src)} bytes)")
    else:
        print("[Cemu] No build/sample.wxlm - the pack ships no module, and the loader "
              "will log that it found nothing to load")

    # Package src/resources into content/WiiXLaunch/ for Cemu graphic pack
    resources_src = os.path.join(root_dir, "src", "resources")
    resources_dst = os.path.join(cemu_deploy_dir, "content", "WiiXLaunch")
    if os.path.exists(resources_src):
        pack_script = os.path.join(root_dir, "scripts", "pack_resources.py")
        subprocess.run([sys.executable, pack_script, resources_src, resources_dst], check=True)

    # Enforce the canonical capitalisation on the tree that actually ships.
    #
    # A case-insensitive build host will happily produce content/wiixlaunch/ or
    # content/WIIXLAUNCH/ if some path string drifts, and nothing on Windows
    # will ever complain - the mistake only surfaces on a real Wii U or on Linux
    # Cemu, as a mod that silently is not found. Checking the emitted names here
    # is the only place it can be caught on the machine that built it.
    content_root = os.path.join(cemu_deploy_dir, "content")
    if os.path.isdir(content_root):
        for entry in os.listdir(content_root):
            if entry.lower() == WIIXL_CONTENT_DIR.lower() and entry != WIIXL_CONTENT_DIR:
                raise RuntimeError(
                    f"Graphic pack content directory is named '{entry}', but the canonical "
                    f"spelling is '{WIIXL_CONTENT_DIR}'.\n"
                    f"  Wii U's filesystem is case-sensitive; this builds fine here and fails "
                    f"to find its files on hardware and on Linux Cemu.\n"
                    f"  Rename {content_root}{os.sep}{entry} to "
                    f"{content_root}{os.sep}{WIIXL_CONTENT_DIR}.")

    print("\nDeployment structures ready in:")
    print(f" - Switch (Console / Ryujinx / Yuzu): {switch_deploy_dir}")
    print(f" - Wii U  (Real Console / Aroma):    {wiiu_deploy_dir}")
    print(f" - Cemu   (PC Emulator Graphic Pack): {cemu_deploy_dir}\n")

if __name__ == "__main__":
    main()
