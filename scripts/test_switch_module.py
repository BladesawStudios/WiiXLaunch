#!/usr/bin/env python3
"""Builds a real AArch64 .wxlm and checks what came out.

scripts/test_wxlm.py already proves the writer and wxlm.hpp agree about the
header, in both byte orders, without a toolchain. This is the other half and it
needs devkitA64: that a module COMPILED for Switch produces a file the loader
would accept - right machine, right byte order, and a relocation table
containing only kinds that exist on this architecture.

WHY THAT LAST CHECK IS THE POINT. A PowerPC relocation in an aarch64 module is
the failure this whole path invites: the writer would have to have fallen
through to the wrong reader, and the result is a file that loads, relocates a
16-bit half of an instruction that is not there, and jumps into it. The header
would look perfect. So the kinds are asserted, not just the header.

Run from the repo root. build_switch.bat runs it, which is where devkitA64 is
already a hard requirement.
"""

import io
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import wxlm  # noqa: E402

# Kinds an aarch64 module may contain. Import is the registry fixup and Addr64
# is R_AARCH64_ABS64; everything else A64 does with an address is PC-relative
# and carries no runtime record at all. See scripts/aarch64_relocs.py.
ALLOWED = {wxlm.RELOC_IMPORT, wxlm.RELOC_ADDR64}
KIND_NAMES = {0: "Addr32", 1: "Addr16Ha", 2: "Addr16Hi", 3: "Addr16Lo",
              4: "Import", 5: "Addr64"}


def main():
    example = os.path.join(ROOT, "examples", "sample_mod")
    if not os.path.isdir(example):
        sys.stderr.write("[test_switch_module] %s is missing - nothing to build.\n"
                         % example)
        return 1

    tmp = tempfile.mkdtemp(prefix="wxl_switchmod_")
    try:
        out = os.path.join(tmp, "build")
        r = subprocess.run(
            [sys.executable, os.path.join(HERE, "build_mod.py"),
             "--source", example, "--target", "switch", "--out", out],
            cwd=ROOT)
        if r.returncode != 0:
            sys.stderr.write(
                "\n[test_switch_module] the sample module does not BUILD for Switch.\n"
                "  The host builds for that platform every time, which says nothing\n"
                "  about whether a module can be produced for it.\n\n")
            return 1

        # build_mod puts a switch build in <output>/switch/.
        built = None
        for root, _dirs, files in os.walk(out):
            for name in files:
                if name.endswith(".wxlm"):
                    built = os.path.join(root, name)
        if built is None:
            sys.stderr.write("[test_switch_module] the build reported success and "
                             "produced no .wxlm.\n")
            return 1

        d = io.open(built, "rb").read()
        failures = []
        checks = 0

        endian_byte = d[8]
        checks += 1
        if endian_byte != wxlm.ENDIAN_LITTLE:
            failures.append("  endian byte is %d, expected %d (little)"
                            % (endian_byte, wxlm.ENDIAN_LITTLE))

        # Read with "<" unconditionally rather than with whatever the endian
        # byte said: if that byte is wrong, reading by it would hide the fault.
        f = struct.unpack_from(wxlm.header_format("<"), d, 0)
        magic, _fmt, machine = f[0], f[1], f[2]

        checks += 1
        if magic != wxlm.MAGIC:
            failures.append("  magic read little-endian is 0x%08X, expected 0x%08X"
                            % (magic, wxlm.MAGIC))
        checks += 1
        if machine != wxlm.MACHINE_AARCH64:
            failures.append("  machine is %d, expected %d (aarch64)"
                            % (machine, wxlm.MACHINE_AARCH64))

        reloc_offset, reloc_count = f[15], f[16]
        payload_size = f[14]
        kinds = {}
        for i in range(reloc_count):
            hdr, _val = struct.unpack_from("<II", d, reloc_offset + i * 8)
            kind, off = hdr >> 24, hdr & 0x00FFFFFF
            kinds[kind] = kinds.get(kind, 0) + 1
            checks += 1
            if kind not in ALLOWED:
                failures.append(
                    "  relocation %d is kind %d (%s), which cannot occur in an "
                    "aarch64 module" % (i, kind, KIND_NAMES.get(kind, "?")))
            width = 8 if kind in (wxlm.RELOC_ADDR64, wxlm.RELOC_IMPORT) else 4
            if off + width > payload_size:
                failures.append(
                    "  relocation %d writes %d bytes at +0x%X, past the %d-byte "
                    "payload" % (i, width, off, payload_size))
            if off % 8 != 0:
                failures.append(
                    "  relocation %d targets +0x%X, which is not 8-byte aligned - an "
                    "unaligned 64-bit store is a fault on this architecture"
                    % (i, off))

        # Liveness. A module with no relocations at all would pass every check
        # above without any of them looking at anything.
        if reloc_count == 0:
            failures.append(
                "  the module has NO relocations, so nothing above was tested. A "
                "sample mod that imports even one host function has at least one.")

        if failures:
            sys.stderr.write(
                "\n[test_switch_module] THE AARCH64 MODULE IS WRONG\n\n"
                + "\n".join(failures) + "\n\n")
            return 1

        summary = ", ".join("%d %s" % (v, KIND_NAMES.get(k, "?"))
                            for k, v in sorted(kinds.items()))
        print("[test_switch_module] built an aarch64 module and checked it: "
              "%d byte(s), %d relocation(s) (%s), %d check(s), 0 failures"
              % (len(d), reloc_count, summary, checks))
        return 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
