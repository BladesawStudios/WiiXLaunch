#!/usr/bin/env python3
"""Enrol a game build by fingerprinting its dump, so nobody types a hex number.

The host CRCs a slice of read-only data at boot and matches it against the names
in a target's `identity.known`. Getting a name in there used to mean booting the
game, reading the fingerprint out of the log and pasting it into JSON. That is
not a workflow, it is a chore with a typo in it.

The bytes being hashed are read-only data, so they are the same bytes that sit
in the dump on disk. This computes the fingerprint from the file and writes the
row itself:

    python scripts/enrol_build.py --target totk --name 1.2.1 --nso <path>/main
    python scripts/enrol_build.py --target botw --name v208  --rpx <path>/U-King.rpx

THE SLICE IS READ FROM THE TARGET FILE, never restated here. If the host hashes
0x600000 into read-only data for 4096 bytes, so does this - because it asks the
same file the host was generated from. A tool that kept its own copy of those
numbers would be a second place for them to drift.

WHAT THIS DOES NOT DO is verify that the offline answer matches the runtime one.
It cannot: that depends on the loader mapping the segment where this assumes it
does. Run it once, boot the game once, and check the two numbers agree. After
that it is mechanical - and --dry-run prints the fingerprint without writing, so
the comparison costs nothing.
"""

import argparse
import io
import json
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def crc32(data):
    """The host's Crc32, which is standard CRC-32.

    include/wiixlaunch/loader/wxlm.hpp computes it a nibble at a time from a
    16-entry table, which is the usual small-footprint form of the IEEE
    polynomial 0xEDB88320 with the usual init and final xor. zlib is an
    independent implementation of the same thing rather than a copy of that one,
    and the check value below is the standard's, so a divergence would show up
    here instead of as a fingerprint that never matches.
    """
    assert zlib.crc32(b"123456789") & 0xFFFFFFFF == 0xCBF43926, (
        "this zlib does not compute standard CRC-32")
    return zlib.crc32(data) & 0xFFFFFFFF


# ---------------------------------------------------------------------------
# NSO (Switch)
# ---------------------------------------------------------------------------

def lz4_block(src, out_size):
    """LZ4 block decompression.

    Written out rather than pulled in: the machine has no lz4 module, and the
    format is a page of code. Token byte is literal length in the high nibble
    and match length minus 4 in the low; a nibble of 15 continues in
    0xFF-terminated bytes; then a little-endian 2-byte match offset.
    """
    dst = bytearray()
    i, n = 0, len(src)
    while i < n:
        token = src[i]
        i += 1

        lit = token >> 4
        if lit == 15:
            while True:
                b = src[i]
                i += 1
                lit += b
                if b != 0xFF:
                    break
        dst += src[i:i + lit]
        i += lit
        if i >= n or len(dst) >= out_size:
            break

        offset = src[i] | (src[i + 1] << 8)
        i += 2
        if offset == 0:
            raise SystemExit("[enrol] corrupt LZ4 stream: match offset 0")

        match = token & 0xF
        if match == 15:
            while True:
                b = src[i]
                i += 1
                match += b
                if b != 0xFF:
                    break
        match += 4

        start = len(dst) - offset
        if start < 0:
            raise SystemExit("[enrol] corrupt LZ4 stream: match before output")
        for k in range(match):
            dst.append(dst[start + k])
    return bytes(dst)


def nso_rodata(path):
    """The decompressed .rodata segment of an NSO.

    Segment 1 is read-only data, and the host hashes at an offset from where
    that segment begins in memory - so an offset into these bytes is the same
    offset the host uses, with no base to know.
    """
    with open(path, "rb") as f:
        blob = f.read()
    if blob[:4] != b"NSO0":
        raise SystemExit("[enrol] not an NSO (no NSO0 magic): %s" % path)

    flags = struct.unpack_from("<I", blob, 0x0C)[0]
    file_off, _mem_off, dec_size = struct.unpack_from("<III", blob, 0x20)
    comp_size = struct.unpack_from("<I", blob, 0x64)[0]

    raw = blob[file_off:file_off + comp_size]
    if flags & (1 << 1):
        raw = lz4_block(raw, dec_size)
        if len(raw) != dec_size:
            raise SystemExit(
                "[enrol] .rodata decompressed to %d bytes, header says %d - the "
                "LZ4 reader is wrong or the file is damaged"
                % (len(raw), dec_size))
    return raw


# ---------------------------------------------------------------------------
# RPX (Wii U)
# ---------------------------------------------------------------------------

SHF_RPL_ZLIB = 0x08000000


def rpx_bytes_at(path, addr, length):
    """`length` bytes at virtual address `addr`, out of an RPX.

    An RPX is a big-endian ELF32 whose sections may be zlib-compressed, marked
    by SHF_RPL_ZLIB and prefixed with a big-endian uncompressed size.
    """
    with open(path, "rb") as f:
        blob = f.read()
    if blob[:4] != b"\x7fELF":
        raise SystemExit("[enrol] not an ELF/RPX: %s" % path)

    e_shoff = struct.unpack_from(">I", blob, 0x20)[0]
    e_shentsize, e_shnum = struct.unpack_from(">HH", blob, 0x2E)

    for i in range(e_shnum):
        base = e_shoff + i * e_shentsize
        sh_flags, sh_addr, sh_off, sh_size = struct.unpack_from(
            ">IIII", blob, base + 8)
        if sh_addr == 0 or sh_size == 0:
            continue
        if not (sh_addr <= addr and addr + length <= sh_addr + sh_size):
            continue

        data = blob[sh_off:sh_off + sh_size]
        if sh_flags & SHF_RPL_ZLIB:
            want = struct.unpack_from(">I", data, 0)[0]
            data = zlib.decompress(data[4:])
            if len(data) != want:
                raise SystemExit(
                    "[enrol] section decompressed to %d bytes, header says %d"
                    % (len(data), want))
        start = addr - sh_addr
        if start + length > len(data):
            raise SystemExit(
                "[enrol] 0x%08X+%d runs past the end of its section" % (addr, length))
        return data[start:start + length]

    raise SystemExit(
        "[enrol] no section of %s contains 0x%08X..0x%08X - is this the right "
        "build, and does the target's identity.wiiu.address point into .rodata?"
        % (os.path.basename(path), addr, addr + length))


# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", required=True, help="targets/<name>.json")
    ap.add_argument("--name", required=True, help="what to call this build, e.g. 1.2.1")
    ap.add_argument("--nso", help="path to a Switch main NSO")
    ap.add_argument("--rpx", help="path to a Wii U RPX")
    ap.add_argument("--dry-run", action="store_true",
                    help="print the fingerprint, write nothing")
    args = ap.parse_args()

    if bool(args.nso) == bool(args.rpx):
        raise SystemExit("[enrol] pass exactly one of --nso or --rpx")

    tpath = os.path.join(ROOT, "targets", args.target + ".json")
    if not os.path.isfile(tpath):
        raise SystemExit("[enrol] no such target: %s" % tpath)
    doc = json.loads(io.open(tpath, encoding="utf-8").read())

    ident = doc.get("identity") or {}
    platform = "switch" if args.nso else "wiiu"
    slice_cfg = ident.get(platform) or {}

    def num(v):
        if v is None:
            return 0
        return int(v, 0) if isinstance(v, str) else int(v)

    length = num(slice_cfg.get("length"))
    if length == 0:
        raise SystemExit(
            "[enrol] targets/%s.json declares no %s identity slice, so there is "
            "nothing to fingerprint. The host would report 'cannot fingerprint' "
            "on this platform too." % (args.target, platform))

    if args.nso:
        offset = num(slice_cfg.get("offset"))
        rodata = nso_rodata(args.nso)
        if offset + length > len(rodata):
            raise SystemExit(
                "[enrol] offset 0x%X+%d is past the end of .rodata (%d bytes). "
                "The host bounds-checks the same way and would report 'cannot "
                "fingerprint'." % (offset, length, len(rodata)))
        blob = rodata[offset:offset + length]
        where = "0x%X into .rodata of %s" % (offset, os.path.basename(args.nso))
    else:
        addr = num(slice_cfg.get("address"))
        blob = rpx_bytes_at(args.rpx, addr, length)
        where = "0x%08X in %s" % (addr, os.path.basename(args.rpx))

    fp = crc32(blob)
    print("[enrol] %s %s: %s" % (args.target, platform, where))
    print("[enrol] fingerprint 0x%08X over %d bytes" % (fp, length))

    # A sanity note the caller can act on: read-only data that is all one byte
    # is not identifying anything.
    if len(set(blob)) <= 2:
        print("[enrol] WARNING: that slice has %d distinct byte value(s). It "
              "will not distinguish builds - move the offset."
              % len(set(blob)))

    known = list(ident.get("known") or [])
    for row in known:
        if row.get("platform") != platform:
            continue
        same_name = row.get("name") == args.name
        same_fp = num(row.get("fingerprint")) == fp
        if same_name and same_fp:
            print("[enrol] already enrolled, unchanged")
            return 0
        if same_name:
            raise SystemExit(
                "[enrol] '%s' is already enrolled for %s as 0x%08X, and this "
                "dump fingerprints as 0x%08X. Two different builds cannot share "
                "a name - rename one, or check which dump is the one you mean."
                % (args.name, platform, num(row.get("fingerprint")), fp))
        if same_fp:
            raise SystemExit(
                "[enrol] this exact build is already enrolled as '%s'. Enrolling "
                "it twice under two names would make which one a mod matches "
                "depend on table order." % row.get("name"))

    if args.dry_run:
        print("[enrol] --dry-run: nothing written")
        return 0

    known.append({"name": args.name, "platform": platform,
                  "fingerprint": "0x%08X" % fp})
    ident["known"] = known
    doc["identity"] = ident
    io.open(tpath, "w", encoding="utf-8", newline="\n").write(
        json.dumps(doc, indent=2) + "\n")
    print("[enrol] wrote %s -> \"%s\" into targets/%s.json (%d build(s) enrolled)"
          % ("0x%08X" % fp, args.name, args.target, len(known)))
    print("[enrol] rebuild the host for it to take effect")
    return 0


if __name__ == "__main__":
    sys.exit(main())
