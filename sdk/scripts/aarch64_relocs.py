#!/usr/bin/env python3
"""Parsing AArch64 relocations out of readelf, for .wxlm modules.

The PowerPC counterpart is scripts/ppc_relocs.py and the two are deliberately
NOT merged. They share a shape - read `readelf -rW`, hand back records - and
almost nothing else: PowerPC needs four fixup kinds because a 32-bit address is
built from instruction halves, while AArch64 needs exactly one, because
everything else it does with an address is PC-relative.

WHY ONE KIND IS ENOUGH, and it is the whole design:

  * A64 code reaches its own data with `adrp` + `add`/`ldr :lo12:`. Both are
    PC-relative, the linker has already resolved them, and they stay correct
    when the image moves PROVIDED it moves by a whole number of pages. The
    loader page-aligns aarch64 images for exactly this reason; nothing here has
    to fix them up, and nothing could - there is no runtime information in an
    adrp that says which symbol it meant.
  * Branches (CALL26/JUMP26/CONDBR19/TSTBR14) are PC-relative within the module
    and likewise survive the move untouched.
  * What is left is absolute 64-bit data: vtables, function pointer tables,
    .init_array entries, and the import pointers a mod declares. Those are
    R_AARCH64_ABS64 and those are what the loader fixes.

ANYTHING NOT ON THE LIST IS AN ERROR, not a skip. A relocation silently ignored
is a wrong pointer at runtime with no diagnostic anywhere, which is the failure
shape this project keeps paying for. If a compiler change starts emitting
something new, this stops the build and names it.
"""

import re
import subprocess


# Absolute, and therefore the loader's problem.
ABS64 = "R_AARCH64_ABS64"

# PC-relative or already-resolved-within-the-module: correct after a
# page-aligned move, so they need no runtime fixup. Listed BY NAME rather than
# skipped by default, so that "we considered this and it needs nothing" is
# distinguishable from "we have never seen this".
_NO_FIXUP = frozenset([
    # Code addressing a symbol: adrp gives the page, one of these adds the rest.
    "R_AARCH64_ADR_PREL_PG_HI21",
    "R_AARCH64_ADR_PREL_PG_HI21_NC",
    "R_AARCH64_ADD_ABS_LO12_NC",
    "R_AARCH64_LDST8_ABS_LO12_NC",
    "R_AARCH64_LDST16_ABS_LO12_NC",
    "R_AARCH64_LDST32_ABS_LO12_NC",
    "R_AARCH64_LDST64_ABS_LO12_NC",
    "R_AARCH64_LDST128_ABS_LO12_NC",
    # Branches and PC-relative loads.
    "R_AARCH64_CALL26",
    "R_AARCH64_JUMP26",
    "R_AARCH64_CONDBR19",
    "R_AARCH64_TSTBR14",
    "R_AARCH64_LD_PREL_LO19",
    "R_AARCH64_ADR_PREL_LO21",
    "R_AARCH64_PREL16",
    "R_AARCH64_PREL32",
    "R_AARCH64_PREL64",
    # Emitted for unwind tables and the like; nothing executes them.
    "R_AARCH64_NONE",
    "R_AARCH64_NULL",
])


class Reloc:
    """One relocation, in the fields a module writer needs."""

    def __init__(self, section, rtype, offset, sym_value, addend, sym_name):
        self.section = section
        self.type = rtype
        self.offset = offset
        self.sym_value = sym_value
        self.addend = addend
        self.sym_name = sym_name

    @property
    def s_plus_a(self):
        return (self.sym_value + self.addend) & 0xFFFFFFFFFFFFFFFF

    def __repr__(self):
        return "Reloc(%s, %s, 0x%X, S=0x%X, A=0x%X, %r)" % (
            self.section, self.type, self.offset, self.sym_value,
            self.addend, self.sym_name)


def read(readelf_cmd, elf_path, skip_debug=True):
    """Every relocation in `elf_path`, ABS64 and no-fixup alike.

    The caller decides what to do with each; returning the no-fixup ones too is
    what lets it tell an unknown type from an ignorable one.

    Debug sections are skipped for the same reason as on PowerPC: their offsets
    are relative to the debug section rather than to the flat image, so acting
    on one corrupts an unrelated word of the payload.
    """
    out = subprocess.check_output([readelf_cmd, "-rW", elf_path], text=True)
    relocs = []
    section = ""

    for line in out.splitlines():
        if line.startswith("Relocation section"):
            m = re.search(r"'([^']+)'", line)
            section = m.group(1) if m else ""
            continue
        if skip_debug and ".debug" in section:
            continue

        parts = line.split()
        if len(parts) < 3 or not parts[2].startswith("R_AARCH64"):
            continue

        try:
            offset = int(parts[0], 16)
        except ValueError:
            continue

        rtype = parts[2]
        sym_value = 0
        addend = 0
        sym_name = ""

        if len(parts) >= 4:
            try:
                sym_value = int(parts[3], 16)
            except ValueError:
                sym_value = 0
        if len(parts) >= 5:
            sym_name = parts[4]
            # "Sym.Name + Addend", where readelf writes the addend last. The
            # PowerPC module lost exactly this once and every string literal in
            # the first module ever loaded pointed at its section base.
            if len(parts) >= 6 and parts[-2] in ("+", "-"):
                try:
                    addend = int(parts[-1], 16)
                    if parts[-2] == "-":
                        addend = -addend
                except ValueError:
                    addend = 0
            elif sym_name.startswith("0x"):
                # No symbol: readelf prints the addend where the name would be.
                try:
                    addend = int(sym_name, 16)
                except ValueError:
                    addend = 0
                sym_name = ""

        relocs.append(Reloc(section, rtype, offset, sym_value, addend, sym_name))

    return relocs


def needs_fixup(rtype):
    return rtype == ABS64


def is_known(rtype):
    return rtype == ABS64 or rtype in _NO_FIXUP


# There is deliberately no read_undefined_symbols here. Finding the symbols an
# ELF references without defining is symbol-table parsing with nothing
# architecture-specific in it, and ppc_relocs.read_undefined_symbols already
# does it for deploy.py. A second copy would be a second thing to get wrong -
# which is the exact mistake ppc_relocs.py's own header records.
