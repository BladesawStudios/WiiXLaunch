#!/usr/bin/env python3
"""Host-completeness check: verify host ELF is complete without src/main.cpp.

Usage:  python scripts/test_host.py <path-to-host-test-elf>
"""

import os
import subprocess
import sys


def find_readelf():
    for env in ("DEVKITPPC",):
        root = os.environ.get(env)
        if root:
            for name in ("powerpc-eabi-readelf.exe", "powerpc-eabi-readelf"):
                p = os.path.join(root, "bin", name)
                if os.path.exists(p):
                    return p
    for p in (r"C:\devkitPro\devkitPPC\bin\powerpc-eabi-readelf.exe",
              "/opt/devkitpro/devkitPPC/bin/powerpc-eabi-readelf"):
        if os.path.exists(p):
            return p
    return None


def read_symbols(readelf, elf):
    out = subprocess.check_output([readelf, "-sW", elf], text=True)
    syms = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) < 8:
            continue
        try:
            addr = int(parts[1], 16)
        except ValueError:
            continue
        syms[parts[7]] = (addr, parts[3], parts[4])   # addr, type, bind
    return syms


# Required host symbols: (symbol, description, must_be_at_zero).
REQUIRED = [
    ("WiiXLaunch_Cemu_Init", "the entry-hook stub the pack branches to", True),
    ("__wiixl_bootstrap_start", "deploy.py's relocation skip range", False),
    ("__wiixl_bootstrap_end", "deploy.py's relocation skip range", False),
    ("WiiXLaunch_Cemu_Relocate", "applies the relocation table at load", False),
    ("g_CodeCaveBase", "where the payload found itself; everything reads it", False),
    ("WiiXLaunch_Init", "called by the bootstrap; base provides a weak default", False),
    ("g_CemuRelocTableOffset", "patched by deploy.py, loaded by the bootstrap asm", False),
    ("g_CemuRelocCount", "patched by deploy.py, loaded by the bootstrap asm", False),
    ("g_CemuHeapOffset", "patched by deploy.py; the heap starts there", False),
    ("g_CemuFsShimTableOffset", "coreinit FS shims - the loader reads modules through them", False),
    ("g_CemuLoggingShimTableOffset", "coreinit OSReport shims - the log itself", False),
    ("g_CemuMemShimTableOffset", "coreinit memory shims", False),
    ("g_CemuTimeShimTableOffset", "coreinit time shims", False),
]


def main():
    if len(sys.argv) < 2:
        sys.stderr.write("usage: test_host.py <host-test-elf>\n")
        return 2

    elf = sys.argv[1]
    if not os.path.exists(elf):
        sys.stderr.write("[test_host] %s does not exist - did the build step run?\n" % elf)
        return 1

    readelf = find_readelf()
    if readelf is None:
        sys.stderr.write(
            "\n"
            "============================================================\n"
            "[test_host] SETUP PROBLEM - not a broken source tree.\n"
            "[test_host] This gate requires powerpc-eabi-readelf, which was not\n"
            "[test_host] found. Set DEVKITPPC, or install devkitPPC.\n"
            "[test_host] Host completeness was NOT verified, so this FAILS rather\n"
            "[test_host] than reporting on a host it never inspected.\n"
            "============================================================\n\n")
        return 1

    syms = read_symbols(readelf, elf)

    if len(syms) < len(REQUIRED):
        sys.stderr.write(
            "\n[test_host] readelf returned %d symbols for %s, fewer than the %d this\n"
            "            test requires. That is a tooling failure, not a host failure:\n"
            "            check that %s works and that the ELF is not stripped.\n\n"
            % (len(syms), elf, len(REQUIRED), readelf))
        return 1

    failures = []
    for name, why, at_zero in REQUIRED:
        if name not in syms:
            failures.append(
                "  MISSING  %-30s %s\n"
                "           An empty main.cpp must still produce this. It is an inline\n"
                "           global or function that only exists if some translation unit\n"
                "           includes its header - and main.cpp is no longer one.\n"
                "           Add the include to src/cemu/bootstrap.cpp, which IS the host."
                % (name, why))
            continue
        addr, _type, _bind = syms[name]
        if at_zero and addr != 0:
            failures.append(
                "  MISPLACED %-30s at 0x%X, must be 0\n"
                "           The pack branches to wiixlaunch_codecave_start, which is\n"
                "           payload offset 0. Check scripts/cemu.ld still places\n"
                "           .text.WiiXLaunch_Cemu_Init first."
                % (name, addr))

    if "WiiXLaunch_Init" in syms and syms["WiiXLaunch_Init"][2] != "WEAK":
        failures.append(
            "  UNEXPECTED WiiXLaunch_Init is %s, expected WEAK\n"
            "           With an empty main.cpp this must resolve to the base default in\n"
            "           src/cemu/bootstrap.cpp. A strong definition means main.cpp (or\n"
            "           something else) got compiled into the host test after all."
            % syms["WiiXLaunch_Init"][2])

    if failures:
        sys.stderr.write(
            "\n[test_host] THE HOST IS NOT COMPLETE WITHOUT src/main.cpp\n\n"
            + "\n".join(failures) + "\n\n"
            "  main.cpp becomes a .wxlm at stage 4, so anything the host needs must not\n"
            "  come from it. See docs/framework/loader.md.\n\n")
        return 1

    print("[test_host] Host is complete without main.cpp "
          "(%d required symbols checked against %d in the ELF, %d pinned to address 0, "
          "WiiXLaunch_Init confirmed WEAK)"
          % (len(REQUIRED), len(syms), sum(1 for _n, _w, z in REQUIRED if z)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
