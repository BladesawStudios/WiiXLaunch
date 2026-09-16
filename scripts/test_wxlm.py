#!/usr/bin/env python3
"""Asserts scripts/wxlm.py agreement with wiixlaunch/loader/wxlm.hpp.

Parses static_asserts from the C++ header to verify struct sizes, member offsets,
constants, CRC32, and FNV-1a hashing against the Python packer.
"""

import os
import re
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
HEADER = os.path.join(ROOT, "include", "wiixlaunch", "loader", "wxlm.hpp")

# Minimum parsed assertion floors.
EXPECTED_MIN_SIZES = 5
EXPECTED_MIN_OFFSETS = 8

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

    # Verify assertions were successfully parsed.
    if len(sizes) < EXPECTED_MIN_SIZES:
        failures.append(
            "  parsed only %d sizeof static_asserts, expected at least %d - either\n"
            "           they were removed, or parse_asserts() stopped matching them."
            % (len(sizes), EXPECTED_MIN_SIZES))
    if len(offsets) < EXPECTED_MIN_OFFSETS:
        failures.append(
            "  parsed only %d offsetof static_asserts, expected at least %d - the\n"
            "           comparison below iterates whatever was parsed, so an empty\n"
            "           parse checks nothing and still reports success."
            % (len(offsets), EXPECTED_MIN_OFFSETS))

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

    # --- CRC32: zlib vs header nibble table ----------------------------------
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

    # --- FNV-1a: wxlm.py vs WiiXLaunch::Surface::Hash -------------------------
    surface_hpp = os.path.join(ROOT, "include", "wiixlaunch", "loader", "surface.hpp")
    if not os.path.exists(surface_hpp):
        failures.append(
            "  BROKEN TREE, not a setup problem: %s is missing, so the FNV-1a\n"
            "           agreement was not checked at all. That file is part of this\n"
            "           repository - if it moved, update this path; do not let the\n"
            "           check silently disappear." % surface_hpp)
    else:
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

    # --- Header packing round-trip -------------------------------------------
    try:
        blob = wxlm.pack_header(
            0, 1, b"audit".ljust(16, b"\0"), 1, 2, 3,
            wxlm.HEADER_SIZE + 64, 0,
            wxlm.HEADER_SIZE, 64,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 16, 0)
    except Exception as exc:                        # noqa: BLE001
        blob = None
        failures.append("  wxlm.pack_header() raised %s: %s"
                        % (type(exc).__name__, exc))

    if blob is not None:
        if len(blob) != wxlm.HEADER_SIZE:
            failures.append("  wxlm.pack_header() emitted %d bytes, HEADER_SIZE is %d"
                            % (len(blob), wxlm.HEADER_SIZE))
        else:
            magic, = struct.unpack_from(">I", blob, 0)
            if magic != wxlm.MAGIC:
                failures.append("  wxlm.pack_header() wrote magic 0x%08X, expected 0x%08X"
                                % (magic, wxlm.MAGIC))
            for field, want in (("payloadSize", 64), ("bssSize", 16),
                                ("payloadOffset", wxlm.HEADER_SIZE)):
                if field in offsets:
                    got, = struct.unpack_from(">I", blob, offsets[field])
                    if got != want:
                        failures.append(
                            "  %s read back as %d at the offset wxlm.hpp pins (%d), "
                            "expected %d" % (field, got, offsets[field], want))

    # --- AArch64 little-endian round-trip -------------------------------------
    aarch64_checks = 0
    try:
        blob64 = wxlm.pack_header(
            0, 1, b"audit".ljust(16, b"\0"), 1, 2, 3,
            wxlm.HEADER_SIZE + 64, 0,
            wxlm.HEADER_SIZE, 64,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 16, 0,
            machine=wxlm.MACHINE_AARCH64)
    except Exception as exc:                        # noqa: BLE001
        blob64 = None
        failures.append("  wxlm.pack_header(machine=aarch64) raised %s: %s"
                        % (type(exc).__name__, exc))

    if blob64 is not None:
        if len(blob64) != wxlm.HEADER_SIZE:
            failures.append("  the aarch64 header is %d bytes, HEADER_SIZE is %d - "
                            "both architectures must pack to one size"
                            % (len(blob64), wxlm.HEADER_SIZE))
        else:
            aarch64_checks += 1
            magic, = struct.unpack_from("<I", blob64, 0)
            if magic != wxlm.MAGIC:
                failures.append("  aarch64 header read little-endian gives magic "
                                "0x%08X, expected 0x%08X - the writer is still "
                                "packing big-endian somewhere" % (magic, wxlm.MAGIC))
            aarch64_checks += 1
            if "machine" in offsets:
                mach, = struct.unpack_from("<H", blob64, offsets["machine"])
                if mach != wxlm.MACHINE_AARCH64:
                    failures.append("  aarch64 header says machine %d, expected %d"
                                    % (mach, wxlm.MACHINE_AARCH64))
                aarch64_checks += 1
            if "endian" in offsets:
                en, = struct.unpack_from("<B", blob64, offsets["endian"])
                if en != wxlm.ENDIAN_LITTLE:
                    failures.append("  aarch64 header says endian %d, expected %d "
                                    "(little) - the loader refuses a module whose "
                                    "endian field disagrees with the host before it "
                                    "overlays a single structure"
                                    % (en, wxlm.ENDIAN_LITTLE))
                aarch64_checks += 1
            for field, want in (("payloadSize", 64), ("bssSize", 16)):
                if field in offsets:
                    got, = struct.unpack_from("<I", blob64, offsets[field])
                    if got != want:
                        failures.append(
                            "  aarch64 %s read back as %d, expected %d"
                            % (field, got, want))
                    aarch64_checks += 1

    # Verify at least one AArch64 check was executed.
    if aarch64_checks == 0:
        failures.append("  no aarch64 header check ran at all - the second byte "
                        "order is untested and this script would still say PASSED")

    if failures:
        sys.stderr.write(
            "\n[test_wxlm] THE .wxlm WRITER AND THE FORMAT HEADER DISAGREE\n\n"
            + "\n".join(failures) + "\n\n"
            "  wiixlaunch/loader/wxlm.hpp is the source of truth. A drift here is not\n"
            "  caught at runtime: the loader reads a plausible garbage offset and\n"
            "  relocates into it.\n\n")
        return 1

    print("[test_wxlm] writer agrees with wxlm.hpp (%d sizes, %d pinned offsets, "
          "CRC32 and FNV-1a verified, %d-byte header round-tripped big-endian "
          "and little-endian, %d aarch64 field(s) read back)"
          % (len(sizes), len(offsets), wxlm.HEADER_SIZE, aarch64_checks))
    return 0


if __name__ == "__main__":
    sys.exit(main())
