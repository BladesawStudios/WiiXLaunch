#!/usr/bin/env python3
import json
import os
import sys

def generate_config():
    root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    json_path = os.path.join(root_dir, "wiixlaunch.json")

    if not os.path.exists(json_path):
        print(f"Error: Master config {json_path} not found.")
        sys.exit(1)

    with open(json_path, "r", encoding="utf-8") as f:
        cfg = json.load(f)

    project = cfg.get("project", {})
    memory = cfg.get("memory", {})
    switch_cfg = cfg.get("switch", {})
    wiiu_cfg = cfg.get("wiiu", {})
    cemu_cfg = cfg.get("cemu", {})

    proj_name = project.get("name", "WiiXLaunchMod")
    proj_version = project.get("version", "1.0.0")
    proj_author = project.get("author", "Developer")
    proj_desc = project.get("description", "WiiXLaunch Cross-Platform Mod")
    is_debug = project.get("debug", True)

    heap_size = memory.get("heap_size", "0x50000")
    jit_size = memory.get("jit_size", "0x10000")
    inline_pool_size = memory.get("inline_pool_size", "0x10000")
    log_buf_size = memory.get("log_buffer_size", 512)

    title_id = switch_cfg.get("title_id", "01007EF00011E000").lower()

    gen_dir = os.path.join(root_dir, "build", "generated")
    gen_inc_dir = os.path.join(gen_dir, "include", "program")
    gen_switch_dir = os.path.join(gen_dir, "switch")
    os.makedirs(gen_inc_dir, exist_ok=True)
    os.makedirs(gen_switch_dir, exist_ok=True)

    debug_def = "#define EXL_DEBUG" if is_debug else ""
    setting_hpp_content = f"""#pragma once

#include <cstddef>

#define EXL_MODULE_NAME "{proj_name}"
{debug_def}
#define EXL_USE_FAKEHEAP

namespace WiiXLaunch::Setting {{
    constexpr size_t HeapSize       = {heap_size};
    constexpr size_t JitSize        = {jit_size};
    constexpr size_t InlinePoolSize = {inline_pool_size};
    constexpr size_t LogBufferSize  = {log_buf_size};
}}

namespace exl::setting {{
    constexpr size_t HeapSize       = {heap_size};
    constexpr size_t JitSize        = {jit_size};
    constexpr size_t InlinePoolSize = {inline_pool_size};
    constexpr size_t LogBufferSize  = {log_buf_size};
}}
"""
    setting_hpp_path = os.path.join(gen_inc_dir, "setting.hpp")
    with open(setting_hpp_path, "w", encoding="utf-8") as f:
        f.write(setting_hpp_content)
    print(f"[ConfigGen] Generated {setting_hpp_path}")

    wiixl_setting_path = os.path.join(root_dir, "include", "wiixlaunch", "generated_setting.hpp")
    with open(wiixl_setting_path, "w", encoding="utf-8") as f:
        f.write(setting_hpp_content)

    npdm_json = {
        "name": proj_name,
        "title_id": f"0x{title_id}",
        "title_id_range_min": switch_cfg.get("title_id_range_min", "0x0100000000010000"),
        "title_id_range_max": switch_cfg.get("title_id_range_max", "0x01ffffffffffffff"),
        "main_thread_stack_size": switch_cfg.get("main_thread_stack_size", "0x00100000"),
        "main_thread_priority": switch_cfg.get("main_thread_priority", 44),
        "default_cpu_id": switch_cfg.get("default_cpu_id", 0),
        "process_category": 0,
        "is_retail": True,
        "pool_partition": 0,
        "is_64_bit": True,
        "address_space_type": 3,
        "filesystem_access": {
            "permissions": "0xffffffffffffffff"
        },
        "service_access": switch_cfg.get("service_access", [
            "acc:u0", "aoc:u", "apm", "appletOE", "audin:u", "audout:u", "audren:u",
            "bsd:u", "caps:u", "csrng", "friend:u", "fsp-srv", "hid", "lm", "nifm:u",
            "nvdrv", "pctl", "pl:u", "set", "sfdnsres", "ssl", "time:u", "vi:u"
        ]),
        "kernel_capabilities": [
            {
                "type": "kernel_flags",
                "value": {
                    "highest_thread_priority": 59,
                    "lowest_thread_priority": 28,
                    "lowest_cpu_id": 0,
                    "highest_cpu_id": 2
                }
            },
            {
                "type": "application_type",
                "value": 1
            }
        ]
    }

    if "system_resource_size" in switch_cfg:
        npdm_json["system_resource_size"] = switch_cfg["system_resource_size"]

    exl_json_path = os.path.join(gen_switch_dir, "config.json")
    with open(exl_json_path, "w", encoding="utf-8") as f:
        json.dump(npdm_json, f, indent=4)
    print(f"[ConfigGen] Generated {exl_json_path}")

    root_dir_unix = root_dir.replace('\\', '/')

    # Optional WiiXLaunch modules (e.g. vendor/wiixlaunch-botw), added as
    # submodules by mods that want them - not part of base WiiXLaunch, so
    # only picked up if actually present. Scoped to the "wiixlaunch-*" name
    # so this doesn't also pull in the Wii U-only vendor/wut, vendor/wups,
    # etc., which have their own top-level include/ dirs but are wired up
    # through their own dedicated Makefile flow instead.
    vendor_dir = os.path.join(root_dir, "vendor")
    module_includes = []
    if os.path.isdir(vendor_dir):
        for name in sorted(os.listdir(vendor_dir)):
            if not name.startswith("wiixlaunch-"):
                continue
            inc_dir = os.path.join(vendor_dir, name, "include")
            if os.path.isdir(inc_dir):
                module_includes.append(inc_dir.replace('\\', '/'))

    module_flags = "".join(f' -I"{inc}"' for inc in module_includes)

    # An escape hatch for bisecting, from the environment rather than from
    # wiixlaunch.json: a define you are toggling to find a crash is not a
    # property of the project and should not be committed to find out.
    #
    #   set WIIXL_EXTRA_DEFINES=-DWIIXL_NO_DEMO
    #   build_switch.bat
    #
    # Echoed at generation time, because a flag that silently changes what was
    # built is how you end up debugging a binary you did not think you made.
    extra = os.environ.get("WIIXL_EXTRA_DEFINES", "").strip()
    if extra:
        print(f"[ConfigGen] WIIXL_EXTRA_DEFINES={extra}")
        extra = " " + extra

    config_mk_content = f"""LOAD_KIND := {switch_cfg.get("load_kind", "Module")}
PROGRAM_ID := {title_id}
NPDM_JSON := config.json
PYTHON := python3
MOUNT_PATH := {switch_cfg.get("mount_path", "/mnt/sdcard")}
C_FLAGS := -I"{root_dir_unix}/include" -I"{root_dir_unix}/build/generated/include"{module_flags}{extra}
CXX_FLAGS := -I"{root_dir_unix}/include" -I"{root_dir_unix}/build/generated/include"{module_flags}{extra}
"""
    exl_mk_path = os.path.join(gen_switch_dir, "config.mk")
    with open(exl_mk_path, "w", encoding="utf-8") as f:
        f.write(config_mk_content)
    print(f"[ConfigGen] Generated {exl_mk_path}")

    wiiu_title_ids = wiiu_cfg.get("target_title_ids", ["00050000101C9400", "00050000101C9500"])
    formatted_tids = ", ".join([f"0x{tid}ULL" for tid in wiiu_title_ids])

    wiiu_hpp_content = f"""#pragma once

#include <cstdint>

#define WUPS_PLUGIN_NAME_STR "{proj_name}"
#define WUPS_PLUGIN_AUTHOR_STR "{proj_author}"
#define WUPS_PLUGIN_VERSION_STR "{proj_version}"
#define WUPS_PLUGIN_DESCRIPTION_STR "{proj_desc}"

namespace WiiXLaunch::WiiUConfig {{
    constexpr uint64_t TargetTitleIds[] = {{ {formatted_tids} }};
    constexpr uint32_t TargetTitleIdsCount = sizeof(TargetTitleIds) / sizeof(TargetTitleIds[0]);
}}
"""
    wiiu_hpp_path = os.path.join(root_dir, "include", "wiixlaunch", "generated_wiiu_config.hpp")
    with open(wiiu_hpp_path, "w", encoding="utf-8") as f:
        f.write(wiiu_hpp_content)
    print(f"[ConfigGen] Generated {wiiu_hpp_path}")

    # --- Cemu entry hook -----------------------------------------------------
    #
    # The bootstrap in src/cemu/bootstrap.cpp is base framework code and must
    # not contain a game address, so the two game-specific values it needs come
    # from here:
    #
    #   entry_hook              where the pack redirects into the code cave
    #   entry_hook_instruction  the instruction that redirect overwrites, which
    #                           the bootstrap has to replay before branching
    #                           back to entry_hook + 4
    #
    # For BotW v208 the entry hook is 0x03098928, which holds `mflr r0`
    # (0x7C0802A6) - the prologue of the game's FS bring-up function.
    #
    # Emitted as macros rather than constexpr values because the bootstrap
    # pastes them into an asm() block by stringification, and asm cannot see
    # C++ constants.
    entry_hook = cemu_cfg.get("entry_hook", "0x00000000")
    entry_instr = cemu_cfg.get("entry_hook_instruction", "0x00000000")
    entry_hook_val = int(str(entry_hook), 16)
    entry_instr_val = int(str(entry_instr), 16)
    entry_return = entry_hook_val + 4

    if entry_hook_val != 0 and entry_instr_val == 0:
        raise SystemExit(
            "[ConfigGen] cemu.entry_hook is set to 0x%08X but cemu.entry_hook_instruction "
            "is missing or zero.\n"
            "  The pack overwrites that instruction with a branch, so the bootstrap has to "
            "replay it; without the word it would execute a zero and fall through into the "
            "game mid-prologue.\n"
            "  Disassemble entry_hook and record the instruction word there." %
            entry_hook_val)

    cemu_hpp_content = f"""#pragma once

// GENERATED by scripts/generate_config.py from wiixlaunch.json - do not edit.
//
// The Cemu entry hook, as macros the bootstrap's asm() block can stringify.
// These are the only game-specific values base's bootstrap needs; everything
// else about it is game-agnostic.

#define WIIXL_CEMU_ENTRY_HOOK        0x{entry_hook_val:08X}
#define WIIXL_CEMU_ENTRY_RETURN      0x{entry_return:08X}
#define WIIXL_CEMU_ENTRY_RETURN_HI   0x{(entry_return >> 16) & 0xFFFF:04X}
#define WIIXL_CEMU_ENTRY_RETURN_LO   0x{entry_return & 0xFFFF:04X}
#define WIIXL_CEMU_ENTRY_DISPLACED   0x{entry_instr_val:08X}
"""
    cemu_hpp_path = os.path.join(root_dir, "include", "wiixlaunch", "generated_cemu_config.hpp")
    with open(cemu_hpp_path, "w", encoding="utf-8") as f:
        f.write(cemu_hpp_content)
    print(f"[ConfigGen] Generated {cemu_hpp_path}")

    print("\n[ConfigGen] Single Central Config successfully synchronized without modifying vendor submodules!")

if __name__ == "__main__":
    generate_config()
