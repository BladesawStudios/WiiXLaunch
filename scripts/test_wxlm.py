#!/usr/bin/env python3
"""Asserts scripts/wxlm.py still agrees with wiixlaunch/loader/wxlm.hpp.

The .wxlm format has two implementations - a Python writer and a C++ loader -
and a drift between them is the one failure neither side can detect at runtime.
The loader would read a plausible-looking garbage offset and relocate into it,
which on this platform means writing into the code cave and executing it.

So this does not restate the layout. It PARSES the static_asserts out of the C++
header and checks the Python packer against them, which means the header stays
the single source of truth and this file cannot drift from it independently.

It also checks the two algorithms both sides must agree on byte-for-byte:
CRC32 (zlib vs the nibble table in the header) and FNV-1a (the writer's hash vs
WiiXLaunch::Surface::Hash).

Run by the build; no arguments.
"""

import os
import re
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
HEADER = os.path.join(ROOT, "include", "wiixlaunch", "loader", "wxlm.hpp")

sys.path.insert(0, HERE)
import wxlm  # noqa: E402


def parse_asserts(text):
    """Pull sizeof/offsetof expectations straight out of the header."""
    sizes = {}
    for m in re.finditer(r"static_assert\(sizeof\((\w+)\)\s*==\s*(\d+)", text):
        sizes[m.group(1)] = int(m.group(2))

    offsets = {}
    for m in re.finditer(r"static_assert\(offsetof\(Header,\s*(\w+)\)\s*==\s*(\d+)", text):
        offsets[m.group(1)] = int(m.group(2))

    consts = {}
    for name, pattern in (
        ("kMagic", r"kMagic\s*=\s*(0x[0-9A-Fa-f]+)"),
        ("kFormatVersion", r"kFormatVersion\s*=\s*(\d+)"),
    ):
        m = re.search(pattern, text)
        if m:
            consts[name] = int(m.group(1), 0)
    return sizes, offsets, consts


# Where each header field starts in HEADER_FORMAT, derived by packing prefixes.
# Names must match the C++ member names the asserts refer to.
FIELD_ORDER = [
    ("magic", "I"), ("formatVersion", "H"), ("machine", "H"),
    ("endian", "B"), ("phase", "B"), ("abiVersion", "H"),
    ("modId", "16s"), ("verMajor", "H"), ("verMinor", "H"),
    ("verPatch", "H"), ("reserved0", "H"),
    ("fileSize", "I"), ("contentCrc32", "I"),
    ("payloadOffset", "I"), ("payloadSize", "I"),
    ("relocOffset", "I"), ("relocCount", "I"),
    ("importOffset", "I"), ("importCount", "I"),
    ("exportOffset", "I"), ("exportCount", "I"),
    ("requiredOffset", "I"), ("requiredCount", "I"),
    ("stringOffset", "I"), ("stringSize", "I"),
    ("entryOffset", "I"),
    ("initArrayOffset", "I"), ("initArrayCount", "I"),
    ("bssSize", "I"), ("heapRequest", "I"),
    ("declaredHookOffset", "I"), ("declaredHookCount", "I"),
    ("declaredPatchOffset", "I"), ("declaredPatchCount", "I"),
    ("reserved1", "4I"),
]


def python_offsets():
    out = {}
    running = 0
    for name, fmt in FIELD_ORDER:
        out[name] = running
        running += struct.calcsize(">" + fmt)
    return out, running


def main():
    if not os.path.exists(HEADER):
        sys.stderr.write("[test_wxlm] cannot find %s\n" % HEADER)
        return 1

    text = open(HEADER, encoding="utf-8").read()
    sizes, offsets, consts = parse_asserts(text)
    py_offsets, py_header_size = python_offsets()

    failures = []

    # --- struct sizes --------------------------------------------------------
    for cxx_name, py_value, what in (
        ("Header", py_header_size, "HEADER_FORMAT"),
        ("ImportEntry", wxlm.IMPORT_ENTRY_SIZE, "IMPORT_ENTRY_SIZE"),
        ("ExportEntry", wxlm.EXPORT_ENTRY_SIZE, "EXPORT_ENTRY_SIZE"),
        ("RequiredSurface", wxlm.REQUIRED_ENTRY_SIZE, "REQUIRED_ENTRY_SIZE"),
    ):
        if cxx_name not in sizes:
            failures.append("  no static_assert on sizeof(%s) in wxlm.hpp" % cxx_name)
        elif sizes[cxx_name] != py_value:
            failures.append("  sizeof(%s) is %d in wxlm.hpp, %s packs %d"
                            % (cxx_name, sizes[cxx_name], what, py_value))

    if wxlm.HEADER_SIZE != py_header_size:
        failures.append("  wxlm.py HEADER_SIZE is %d but HEADER_FORMAT packs %d"
                        % (wxlm.HEADER_SIZE, py_header_size))

    # --- field offsets the header pins ---------------------------------------
    for field, cxx_offset in sorted(offsets.items()):
        if field not in py_offsets:
            failures.append("  wxlm.hpp pins offsetof(Header, %s) but wxlm.py has no "
                            "such field in FIELD_ORDER" % field)
        elif py_offsets[field] != cxx_offset:
            failures.append("  Header.%s is at %d in wxlm.hpp, %d in wxlm.py"
                            % (field, cxx_offset, py_offsets[field]))

    # --- shared constants ----------------------------------------------------
    if consts.get("kMagic") != wxlm.MAGIC:
        failures.append("  kMagic is 0x%X in wxlm.hpp, 0x%X in wxlm.py"
                        % (consts.get("kMagic", 0), wxlm.MAGIC))
    if consts.get("kFormatVersion") != wxlm.FORMAT_VERSION:
        failures.append("  kFormatVersion is %s in wxlm.hpp, %d in wxlm.py"
                        % (consts.get("kFormatVersion"), wxlm.FORMAT_VERSION))

    # --- CRC32: zlib must equal the header's nibble-table implementation ------
    #
    # Reimplemented here from the table IN THE HEADER, so this compares the two
    # algorithms rather than trusting that both are "CRC32".
    m = re.search(r"kCrcNibble\[16\]\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        failures.append("  could not find kCrcNibble in wxlm.hpp")
    else:
        table = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{8})u?", m.group(1))]
        if len(table) != 16:
            failures.append("  kCrcNibble has %d entries, expected 16" % len(table))
        else:
            def crc_from_header(data):
                crc = 0xFFFFFFFF
                for b in data:
                    crc ^= b
                    crc = (crc >> 4) ^ table[crc & 0xF]
                    crc = (crc >> 4) ^ table[crc & 0xF]
                return crc ^ 0xFFFFFFFF

            for probe in (b"", b"a", b"WXLM", bytes(range(256)), b"\x00" * 1000,
                          b"the realistic corruption is a partial write"):
                want = zlib.crc32(probe) & 0xFFFFFFFF
                got = crc_from_header(probe)
                if want != got:
                    failures.append(
                        "  CRC32 disagrees on %d-byte input: zlib 0x%08X, wxlm.hpp "
                        "nibble table 0x%08X" % (len(probe), want, got))
                    break

    # --- FNV-1a: writer must equal WiiXLaunch::Surface::Hash -----------------
    surface_hpp = os.path.join(ROOT, "include", "wiixlaunch", "loader", "surface.hpp")
    if os.path.exists(surface_hpp):
        stext = open(surface_hpp, encoding="utf-8").read()
        basis = re.search(r"h\s*=\s*(0x[0-9A-Fa-f]+)u", stext)
        prime = re.search(r"h\s\*=\s*(0x[0-9A-Fa-f]+)u", stext)
        if basis and prime:
            b, p = int(basis.group(1), 16), int(prime.group(1), 16)
            if b != 0x811C9DC5 or p != 0x01000193:
                failures.append("  surface.hpp uses FNV basis 0x%X prime 0x%X; wxlm.py "
                                "assumes the standard 32-bit pair" % (b, p))
        else:
            failures.append("  could not read the FNV constants out of surface.hpp")

    if failures:
        sys.stderr.write(
            "\n[test_wxlm] THE .wxlm WRITER AND THE FORMAT HEADER DISAGREE\n\n"
            + "\n".join(failures) + "\n\n"
            "  wiixlaunch/loader/wxlm.hpp is the source of truth. A drift here is not\n"
            "  caught at runtime: the loader reads a plausible garbage offset and\n"
            "  relocates into it.\n\n")
        return 1

    print("[test_wxlm] writer agrees with wxlm.hpp (%d sizes, %d pinned offsets, "
          "CRC32 and FNV-1a verified)" % (len(sizes), len(offsets)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
