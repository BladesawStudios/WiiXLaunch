#!/usr/bin/env python3
"""Writes .wxlm module files.

This is one half of a contract; the other is
include/wiixlaunch/loader/wxlm.hpp. A drift between them is the one failure
neither side can detect at runtime - the loader would read a plausible garbage
offset and relocate into it - so the layout below is asserted against that
header by scripts/test_wxlm.py, which parses the static_asserts out of it.

The relocation extraction is deliberately the same logic scripts/deploy.py uses
for the host itself: same readelf parsing, same (kind << 24 | offset, value)
encoding. A module and the host are the same kind of object, so they get the
same emitter rather than two that drift.

Usage:
    python scripts/wxlm.py <input.elf> <output.wxlm> --id <mod-id> [options]
"""

import argparse
import os
import re
import struct
import subprocess
import sys
import zlib

import ppc_relocs

# --- must match include/wiixlaunch/loader/wxlm.hpp -------------------------
MAGIC = 0x57584C4D
FORMAT_VERSION = 1

MACHINE_NONE, MACHINE_PPC32, MACHINE_AARCH64 = 0, 1, 2
ENDIAN_LITTLE, ENDIAN_BIG = 0, 1

PHASE_LOAD, PHASE_POST_GX2, PHASE_APP_START = 0, 1, 2
PHASES = {"load": PHASE_LOAD, "post_gx2": PHASE_POST_GX2, "app_start": PHASE_APP_START}

RELOC_ADDR32, RELOC_HA, RELOC_HI, RELOC_LO, RELOC_IMPORT = 0, 1, 2, 3, 4

HEADER_SIZE = 144
IMPORT_ENTRY_SIZE = 16
EXPORT_ENTRY_SIZE = 12
REQUIRED_ENTRY_SIZE = 8

# Header field order, packed big-endian for PPC32. Keep in step with the struct.
HEADER_FORMAT = (
    ">"      # big-endian
    "I"      # magic
    "H"      # formatVersion
    "H"      # machine
    "B"      # endian
    "B"      # phase
    "H"      # abiVersion
    "16s"    # modId
    "H"      # verMajor
    "H"      # verMinor
    "H"      # verPatch
    "H"      # reserved0
    "I"      # fileSize
    "I"      # contentCrc32
    "I"      # payloadOffset
    "I"      # payloadSize
    "I"      # relocOffset
    "I"      # relocCount
    "I"      # importOffset
    "I"      # importCount
    "I"      # exportOffset
    "I"      # exportCount
    "I"      # requiredOffset
    "I"      # requiredCount
    "I"      # stringOffset
    "I"      # stringSize
    "I"      # entryOffset
    "I"      # initArrayOffset
    "I"      # initArrayCount
    "I"      # bssSize
    "I"      # heapRequest
    "I"      # declaredHookOffset
    "I"      # declaredHookCount
    "I"      # declaredPatchOffset
    "I"      # declaredPatchCount
    "4I"     # reserved1
)
assert struct.calcsize(HEADER_FORMAT) == HEADER_SIZE, (
    "HEADER_FORMAT packs to %d bytes, the format says %d"
    % (struct.calcsize(HEADER_FORMAT), HEADER_SIZE))


def fnv1a32(text):
    """Must match WiiXLaunch::Surface::Hash."""
    h = 0x811C9DC5
    for b in text.encode("utf-8"):
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h


class StringBlob:
    """Names live in one blob so table entries stay fixed-width."""

    def __init__(self):
        self._data = bytearray(b"\x00")   # offset 0 is always the empty string
        self._seen = {"": 0}

    def add(self, text):
        if text in self._seen:
            return self._seen[text]
        offset = len(self._data)
        self._data += text.encode("utf-8") + b"\x00"
        self._seen[text] = offset
        return offset

    def bytes(self):
        return bytes(self._data)


def find_tool(name):
    for root in (os.environ.get("DEVKITPPC"), r"C:\devkitPro\devkitPPC",
                 "/opt/devkitpro/devkitPPC"):
        if not root:
            continue
        for suffix in (".exe", ""):
            p = os.path.join(root, "bin", "powerpc-eabi-" + name + suffix)
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
            syms[parts[7]] = int(parts[1], 16)
        except ValueError:
            pass
    return syms


IMPORT_PREFIX = "wiixl_import__"


def decode_import_symbol(name):
    """wiixl_import__wiixl_core__Log -> ("wiixl.core", "Log"), or None.

    The surface name has its dots written as single underscores, and the symbol
    is separated by a double underscore. Encoding it in the symbol name means
    the module declares its own imports and nothing has to be repeated on a
    command line where the two could disagree.
    """
    if not name.startswith(IMPORT_PREFIX):
        return None
    rest = name[len(IMPORT_PREFIX):]
    if "__" not in rest:
        raise SystemExit(
            "[wxlm] %r looks like an import but has no '__' separating the surface "
            "from the symbol. Expected wiixl_import__<surface>__<Symbol>, with dots "
            "in the surface written as underscores." % name)
    surface_part, symbol = rest.rsplit("__", 1)
    return surface_part.replace("_", "."), symbol


def read_relocations(readelf, elf, payload, payload_size, undefined):
    """Turns the ELF's relocations into (kind, offset, value) triples.

    The parsing itself lives in scripts/ppc_relocs.py and is shared with
    deploy.py, because the host payload and a module are the same problem. This
    function is only the module-specific policy on top: undefined
    wiixl_import__* symbols become kind-4 entries carrying an import index.

    That sharing is not tidiness. This function used to do its own parsing and
    dropped the addend, so every ADDR16_HA/LO pair resolved to its section base
    - and the first module ever loaded printed the same string five times
    because every literal pointed at the start of .rodata.
    """
    relocs = []
    import_specs = []      # (surface, symbol), in the order kind-4 values index
    import_index = {}

    for r in ppc_relocs.read(readelf, elf):
        decoded = (decode_import_symbol(r.sym_name)
                   if r.sym_name in undefined else None)

        if decoded is not None:
            if r.type != "R_PPC_ADDR32":
                raise SystemExit(
                    "[wxlm] %s is imported by %s, which the loader cannot fix up.\n"
                    "  An import's ADDRESS must be taken into a variable, never called\n"
                    "  directly - calling one emits a branch relocation that cannot reach\n"
                    "  an arbitrary host address. See examples/sample_mod/mod.cpp."
                    % (r.sym_name, r.type))
            if r.offset + 4 > payload_size:
                raise SystemExit(
                    "[wxlm] import relocation for %s is at 0x%X, past the %d-byte payload"
                    % (r.sym_name, r.offset, payload_size))
            surface, symbol = decoded
            key = (surface, symbol)
            if key not in import_index:
                import_index[key] = len(import_specs)
                import_specs.append(key)
            relocs.append((RELOC_IMPORT, r.offset, import_index[key]))
            continue

        if r.sym_name and r.sym_name in undefined:
            raise SystemExit(
                "[wxlm] %s references undefined symbol %r, which is not an import.\n"
                "  A module cannot link against the host. Reach host functions through\n"
                "  wiixl_import__<surface>__<Symbol>; anything else has to be defined\n"
                "  inside the module. (libgcc helpers like _restgpr_* mean the module\n"
                "  was linked without -lgcc.)" % (os.path.basename(elf), r.sym_name))

        if r.type == "R_PPC_ADDR32":
            if r.offset + 4 <= payload_size:
                # The site already holds its own base-0 target, so it is read
                # back rather than recovered from the relocation entry.
                value = struct.unpack_from(">I", payload, r.offset)[0]
                relocs.append((RELOC_ADDR32, r.offset, value))
        elif r.is_half:
            if r.offset + 2 <= payload_size:
                # A half cannot be recovered from the instruction, so it carries
                # its own fully-resolved S+Addend.
                relocs.append((r.kind, r.offset, r.s_plus_a))

    for _kind, offset, _value in relocs:
        if offset > 0x00FFFFFF:
            raise SystemExit(
                "[wxlm] relocation offset 0x%X does not fit the 24-bit field in the "
                "table header - the payload has outgrown 16 MB" % offset)
    return relocs, import_specs


def read_bss_size(readelf, elf):
    """objcopy -O binary drops .bss, so its size has to come from the sections."""
    out = subprocess.check_output([readelf, "-SW", elf], text=True)
    total = 0
    for line in out.splitlines():
        parts = line.replace("[", " ").replace("]", " ").split()
        if len(parts) < 7:
            continue
        # Nr Name Type Addr Off Size ...
        try:
            idx = parts.index("NOBITS")
        except ValueError:
            continue
        try:
            total += int(parts[idx + 3], 16)
        except (IndexError, ValueError):
            pass
    return total


def parse_import_spec(spec):
    """'surface:Symbol@major.minor' -> (surface, symbol, major, minor)."""
    m = re.match(r"^([A-Za-z0-9_.]+):([A-Za-z0-9_]+)(?:@(\d+)\.(\d+))?$", spec)
    if not m:
        raise SystemExit(
            "[wxlm] bad --import %r; expected surface:Symbol or surface:Symbol@1.0" % spec)
    return m.group(1), m.group(2), int(m.group(3) or 1), int(m.group(4) or 0)


def parse_require_spec(spec):
    """'surface@major.minor' -> (surface, major, minor)."""
    m = re.match(r"^([A-Za-z0-9_.]+)(?:@(\d+)\.(\d+))?$", spec)
    if not m:
        raise SystemExit(
            "[wxlm] bad --require %r; expected surface or surface@1.0" % spec)
    return m.group(1), int(m.group(2) or 1), int(m.group(3) or 0)


def build(args):
    readelf = find_tool("readelf")
    objcopy = find_tool("objcopy")
    if not readelf or not objcopy:
        raise SystemExit("[wxlm] devkitPPC readelf/objcopy not found")

    if not os.path.exists(args.elf):
        raise SystemExit("[wxlm] %s does not exist" % args.elf)

    # Flat payload, linked at 0.
    payload_path = args.output + ".payload.tmp"
    subprocess.check_call([objcopy, "-O", "binary", args.elf, payload_path])
    with open(payload_path, "rb") as f:
        payload = f.read()
    os.remove(payload_path)
    payload_size = len(payload)

    syms = read_symbols(readelf, args.elf)

    entry_offset = syms.get(args.entry)
    if entry_offset is None:
        raise SystemExit(
            "[wxlm] entry symbol %r is not in %s.\n"
            "  The mod's entry point must be an exported symbol; check it is not "
            "static, and that it is __attribute__((used)) if nothing in the mod "
            "itself calls it - which for an entry point is the normal case."
            % (args.entry, os.path.basename(args.elf)))
    if entry_offset >= payload_size:
        raise SystemExit(
            "[wxlm] entry symbol %r is at 0x%X, past the end of the %d-byte payload"
            % (args.entry, entry_offset, payload_size))

    undefined = ppc_relocs.read_undefined_symbols(readelf, args.elf)
    relocs, discovered = read_relocations(readelf, args.elf, payload, payload_size,
                                          undefined)
    bss_size = args.bss_size if args.bss_size else read_bss_size(readelf, args.elf)

    strings = StringBlob()

    # Imports discovered from the ELF come first, because their order IS the
    # index a kind-4 relocation carries. Command-line --import entries are
    # additional requirements, appended after.
    imports = []
    for surface, symbol in discovered:
        imports.append((strings.add(surface), strings.add(symbol),
                        fnv1a32(symbol), 1, 0))
    for spec in args.imports:
        surface, symbol, major, minor = parse_import_spec(spec)
        imports.append((strings.add(surface), strings.add(symbol),
                        fnv1a32(symbol), major, minor))

    required = []
    required_names = set()
    for spec in args.requires:
        surface, major, minor = parse_require_spec(spec)
        required.append((strings.add(surface), major, minor))
        required_names.add(surface)

    # Every surface an import names is required, whether or not it was listed.
    # Otherwise a module could import from a surface it never declared and get
    # an UNRESOLVED-IMPORT at the site instead of a MISSING-SURFACE up front,
    # which is a worse diagnostic for the same problem.
    for surface, _symbol in discovered:
        if surface not in required_names:
            required.append((strings.add(surface), 1, 0))
            required_names.add(surface)
    for spec in args.imports:
        surface, _symbol, major, minor = parse_import_spec(spec)
        if surface not in required_names:
            required.append((strings.add(surface), major, minor))
            required_names.add(surface)

    exports = []
    for spec in args.exports:
        m = re.match(r"^([A-Za-z0-9_]+)(?:@(\d+)\.(\d+))?$", spec)
        if not m:
            raise SystemExit("[wxlm] bad --export %r; expected Symbol or Symbol@1.0" % spec)
        name, major, minor = m.group(1), int(m.group(2) or 1), int(m.group(3) or 0)
        offset = syms.get(name)
        if offset is None:
            raise SystemExit("[wxlm] export symbol %r is not in the ELF" % name)
        exports.append((fnv1a32(name), offset, major, minor))

    init_offset, init_count = 0, 0
    if args.init_array_start and args.init_array_end:
        start = syms.get(args.init_array_start)
        end = syms.get(args.init_array_end)
        if start is not None and end is not None and end > start:
            init_offset = start
            init_count = (end - start) // 4

    mod_id = args.id.encode("utf-8")
    if len(mod_id) > 15:
        raise SystemExit("[wxlm] --id %r is longer than 15 bytes" % args.id)

    # --- lay the sections out ------------------------------------------------
    reloc_bytes = b"".join(
        struct.pack(">II", (kind << 24) | offset, value) for kind, offset, value in relocs)
    import_bytes = b"".join(
        struct.pack(">IIIHH", s, y, h, mj, mn) for s, y, h, mj, mn in imports)
    export_bytes = b"".join(
        struct.pack(">IIHH", h, o, mj, mn) for h, o, mj, mn in exports)
    required_bytes = b"".join(
        struct.pack(">IHH", n, mj, mn) for n, mj, mn in required)
    string_bytes = strings.bytes()

    def align4(n):
        return (n + 3) & ~3

    offset = HEADER_SIZE
    payload_offset = offset;   offset = align4(offset + payload_size)
    reloc_offset = offset;     offset = align4(offset + len(reloc_bytes))
    import_offset = offset;    offset = align4(offset + len(import_bytes))
    export_offset = offset;    offset = align4(offset + len(export_bytes))
    required_offset = offset;  offset = align4(offset + len(required_bytes))
    string_offset = offset;    offset = align4(offset + len(string_bytes))
    file_size = offset

    content = bytearray(file_size - HEADER_SIZE)

    def place(dst_offset, blob):
        start = dst_offset - HEADER_SIZE
        content[start:start + len(blob)] = blob

    place(payload_offset, payload)
    place(reloc_offset, reloc_bytes)
    place(import_offset, import_bytes)
    place(export_offset, export_bytes)
    place(required_offset, required_bytes)
    place(string_offset, string_bytes)

    content_crc = zlib.crc32(bytes(content)) & 0xFFFFFFFF

    header = struct.pack(
        HEADER_FORMAT,
        MAGIC,
        FORMAT_VERSION,
        MACHINE_PPC32,
        ENDIAN_BIG,
        PHASES[args.phase],
        args.abi_version,
        mod_id,
        args.ver_major, args.ver_minor, args.ver_patch,
        0,                        # reserved0
        file_size,
        content_crc,
        payload_offset, payload_size,
        reloc_offset, len(relocs),
        import_offset, len(imports),
        export_offset, len(exports),
        required_offset, len(required),
        string_offset, len(string_bytes),
        entry_offset,
        init_offset, init_count,
        bss_size,
        args.heap_request,
        0, 0, 0, 0,               # declared hooks / patches, stages 6 and 7
        0, 0, 0, 0,               # reserved1
    )

    with open(args.output, "wb") as f:
        f.write(header)
        f.write(bytes(content))

    print("[wxlm] %s  id=%s v%d.%d.%d  phase=%s" %
          (os.path.basename(args.output), args.id,
           args.ver_major, args.ver_minor, args.ver_patch, args.phase))
    for surface, symbol in discovered:
        print("[wxlm]   import %s:%s" % (surface, symbol))
    print("[wxlm]   payload %d B, %d relocs, %d imports, %d exports, %d required, "
          "%d B strings" % (payload_size, len(relocs), len(imports), len(exports),
                            len(required), len(string_bytes)))
    print("[wxlm]   entry %s @0x%X, init_array %d, bss %d B, heap request %d B" %
          (args.entry, entry_offset, init_count, bss_size, args.heap_request))
    print("[wxlm]   file %d B, content crc32 0x%08X" % (file_size, content_crc))
    return 0


def main():
    ap = argparse.ArgumentParser(description="Write a .wxlm module file")
    ap.add_argument("elf")
    ap.add_argument("output")
    ap.add_argument("--id", required=True, help="module id, max 15 bytes")
    ap.add_argument("--entry", default="WiiXLaunch_ModEntry")
    ap.add_argument("--phase", default="load", choices=sorted(PHASES))
    ap.add_argument("--abi-version", type=int, default=1)
    ap.add_argument("--ver-major", type=int, default=1)
    ap.add_argument("--ver-minor", type=int, default=0)
    ap.add_argument("--ver-patch", type=int, default=0)
    ap.add_argument("--bss-size", type=int, default=0)
    ap.add_argument("--heap-request", type=int, default=0,
                    help="bytes requested; the host may grant less")
    ap.add_argument("--import", dest="imports", action="append", default=[],
                    metavar="surface:Symbol[@maj.min]")
    ap.add_argument("--require", dest="requires", action="append", default=[],
                    metavar="surface[@maj.min]")
    ap.add_argument("--export", dest="exports", action="append", default=[],
                    metavar="Symbol[@maj.min]")
    ap.add_argument("--init-array-start", default="__init_array_start")
    ap.add_argument("--init-array-end", default="__init_array_end")
    return build(ap.parse_args())


if __name__ == "__main__":
    sys.exit(main())
