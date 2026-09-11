#!/usr/bin/env python3
"""WIIXL_DECLARE_PATCH_CROSS emits the right architecture's record, and only it.

`targetAddr` is one 32-bit field that means an absolute address on Wii U and
Cemu and an offset from the module base on Switch, and the origin/replacement
bytes are machine code for two different instruction sets.
WIIXL_DECLARE_PATCH_CROSS takes both triples and lets the preprocessor pick. A
macro that picked wrong - or silently emitted nothing - would produce a module
that builds, packs, loads, and is refused at boot on somebody else's console.

So this compiles a fixture that uses the macro, once with each toolchain, and
reads the `.wxlm.patches` section straight out of the two ELFs.

THE ORACLE IS THE DECLARATION, NOT THE MACRO. The expected values below are
written out independently; nothing re-uses the header's own selection logic to
decide what the answer should be. A test that asked the macro what the macro
does could only ever agree with it. The ELF is parsed here rather than through
scripts/wxlm.py for the same reason, and because wxlm.py validates records as
well as reading them - a fixture is not a shippable module.

Run from the repo root:

    python scripts/test_patch_decl.py
"""

import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The fixture, and the answer, written independently of each other.
SWITCH_ADDR = 0x01B299EC
SWITCH_ORIGIN = (0xE1, 0x01, 0x80, 0x52)      # mov w1,#15   little-endian
SWITCH_DATA = (0xA1, 0x05, 0x80, 0x52)        # mov w1,#45

WIIU_ADDR = 0x02000030
WIIU_ORIGIN = (0x7C, 0x9E, 0x23, 0x78)        # or  r30,r4,r4  big-endian
WIIU_DATA = (0x60, 0x9E, 0x00, 0x00)          # ori r30,r4,0

PATCH_ENTRY_SIZE = 40

FIXTURE = """
#include <cstdint>
#include <wiixlaunch/patch_decl.hpp>

WIIXL_DECLARE_PATCH_CROSS(cross_fixture,
    0x%08X,
    WIIXL_PATCH_BYTES(%s),
    WIIXL_PATCH_BYTES(%s),
    0x%08X,
    WIIXL_PATCH_BYTES(%s),
    WIIXL_PATCH_BYTES(%s));

extern "C" __attribute__((used)) void WiiXLaunch_ModEntry() {}
""" % (
    SWITCH_ADDR,
    ", ".join("0x%02X" % b for b in SWITCH_ORIGIN),
    ", ".join("0x%02X" % b for b in SWITCH_DATA),
    WIIU_ADDR,
    ", ".join("0x%02X" % b for b in WIIU_ORIGIN),
    ", ".join("0x%02X" % b for b in WIIU_DATA),
)


def find_compiler(kind):
    """The compiler for a kind, or (None, tried) if none of the candidates exist.

    Looks where the rest of the build looks. devkitpro_env sets DKP_PPC_GXX and
    DKP_ROOT on Windows; DEVKITPRO is the POSIX convention. Reporting every path
    tried matters because "not installed" and "installed somewhere else" want
    different fixes and an unqualified "not found" tells you neither.
    """
    exe = "powerpc-eabi-g++" if kind == "ppc" else "aarch64-none-elf-g++"
    sub = "devkitPPC" if kind == "ppc" else "devkitA64"

    tried = []
    direct = os.environ.get("DKP_PPC_GXX") if kind == "ppc" else None
    if direct:
        tried.append(direct)

    for root in (os.environ.get("DKP_ROOT"), os.environ.get("DEVKITPRO"),
                 r"C:\devkitPro", "/opt/devkitpro"):
        if root:
            tried.append(os.path.join(root, sub, "bin", exe))

    for cand in tried:
        for real in (cand, cand + ".exe"):
            if os.path.isfile(real):
                return real, tried
    return None, tried


def build(kind, workdir):
    exe, tried = find_compiler(kind)
    if exe is None:
        # A MISSING TOOLCHAIN IS A FAILURE, NOT A SKIP. Both are already hard
        # requirements of the scripts that run this gate.
        return None, "compiler not found; tried:\n      " + "\n      ".join(tried)

    defines = (["-DWIIXL_CEMU=1", "-D__CEMU__=1", "-msdata=none"] if kind == "ppc"
               else ["-DWIIXL_SWITCH=1", "-D__SWITCH__=1"])

    src = os.path.join(workdir, "fixture_%s.cpp" % kind)
    obj = os.path.join(workdir, "fixture_%s.o" % kind)
    with open(src, "w", encoding="utf-8") as f:
        f.write(FIXTURE)

    cmd = ([exe, "-std=gnu++20", "-c", "-O2", "-fno-exceptions", "-fno-rtti",
            "-nostdlib", "-ffreestanding",
            "-I", os.path.join(ROOT, "include")]
           + defines + [src, "-o", obj])
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        return None, "did not compile:\n" + p.stderr.strip()[:800]
    return obj, None


def elf_section(path, want):
    """(bytes, endian) for one section, or (None, endian) if it is not there.

    A minimal section-header walk, so this gate needs no readelf and no objcopy.
    Endianness comes from the ELF itself rather than from an assumption about
    which toolchain produced it - the record's uint32 fields are written in the
    target's byte order, and that is the same order the header declares.
    """
    with open(path, "rb") as f:
        blob = f.read()
    if blob[:4] != b"\x7fELF":
        raise SystemExit("[test_patch_decl] not an ELF: %s" % path)

    is64 = blob[4] == 2
    endian = "<" if blob[5] == 1 else ">"

    if is64:
        shoff = struct.unpack_from(endian + "Q", blob, 0x28)[0]
        shentsize, shnum, shstrndx = struct.unpack_from(endian + "HHH", blob, 0x3A)
    else:
        shoff = struct.unpack_from(endian + "I", blob, 0x20)[0]
        shentsize, shnum, shstrndx = struct.unpack_from(endian + "HHH", blob, 0x2E)

    def header(i):
        base = shoff + i * shentsize
        name = struct.unpack_from(endian + "I", blob, base)[0]
        if is64:
            off, size = struct.unpack_from(endian + "QQ", blob, base + 0x18)
        else:
            off, size = struct.unpack_from(endian + "II", blob, base + 0x10)
        return name, off, size

    _, stroff, _ = header(shstrndx)
    for i in range(shnum):
        name, off, size = header(i)
        end = blob.index(b"\0", stroff + name)
        if blob[stroff + name:end].decode() == want:
            return blob[off:off + size], endian
    return None, endian


def check(kind, blob, endian, want_addr, want_origin, want_data, failures):
    """One record, decoded and compared against the independently written answer."""
    tag = "[test_patch_decl] %s" % kind
    if blob is None:
        failures.append("%s: the ELF has no .wxlm.patches section at all - the "
                        "macro emitted NOTHING, which is the silent failure this "
                        "gate exists for" % tag)
        return
    if len(blob) != PATCH_ENTRY_SIZE:
        failures.append("%s: section is %d bytes, expected exactly one %d-byte "
                        "record - two would mean both architectures were emitted"
                        % (tag, len(blob), PATCH_ENTRY_SIZE))
        return

    addr, size = struct.unpack(endian + "II", blob[:8])
    origin = tuple(blob[8:12])
    data = tuple(blob[24:28])

    if addr != want_addr:
        failures.append("%s: targetAddr is 0x%08X, expected 0x%08X - the wrong "
                        "architecture's address was selected"
                        % (tag, addr, want_addr))
    if size != 4:
        failures.append("%s: size is %d, expected 4" % (tag, size))
    if origin != want_origin:
        failures.append("%s: origin is %s, expected %s"
                        % (tag, " ".join("%02X" % b for b in origin),
                           " ".join("%02X" % b for b in want_origin)))
    if data != want_data:
        failures.append("%s: data is %s, expected %s"
                        % (tag, " ".join("%02X" % b for b in data),
                           " ".join("%02X" % b for b in want_data)))


def main():
    failures = []
    checked = 0

    with tempfile.TemporaryDirectory(prefix="wxl_patchdecl_") as workdir:
        for kind, want_endian, addr, origin, data in (
                ("ppc", ">", WIIU_ADDR, WIIU_ORIGIN, WIIU_DATA),
                ("aarch64", "<", SWITCH_ADDR, SWITCH_ORIGIN, SWITCH_DATA)):

            obj, err = build(kind, workdir)
            if obj is None:
                failures.append("[test_patch_decl] %s: %s" % (kind, err))
                continue

            blob, endian = elf_section(obj, ".wxlm.patches")
            if endian != want_endian:
                failures.append("[test_patch_decl] %s: ELF declares %s-endian, "
                                "expected %s - wrong toolchain?"
                                % (kind, "little" if endian == "<" else "big",
                                   "little" if want_endian == "<" else "big"))
                continue

            check(kind, blob, endian, addr, origin, data, failures)
            checked += 1

    if failures:
        print("[test_patch_decl] FAILED")
        for f in failures:
            print("  " + f)
        return 1

    if checked != 2:
        print("[test_patch_decl] FAILED: checked %d architecture(s), expected 2"
              % checked)
        return 1

    print("[test_patch_decl] WIIXL_DECLARE_PATCH_CROSS selected correctly for "
          "both architectures: one %d-byte record each, PowerPC got the absolute "
          "address and big-endian bytes, AArch64 got the module offset and "
          "little-endian bytes" % PATCH_ENTRY_SIZE)
    return 0


if __name__ == "__main__":
    sys.exit(main())
