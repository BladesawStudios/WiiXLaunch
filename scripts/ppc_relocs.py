#!/usr/bin/env python3
"""Parsing PowerPC relocations out of readelf, for the host and for modules.

ONE implementation, imported by both scripts/deploy.py (the host payload) and
scripts/wxlm.py (.wxlm modules), because they are the same problem and a second
implementation is a second thing to get wrong.

It was already gotten wrong once, in exactly the way duplication invites.
wxlm.py said in its own comments that it reused deploy.py's logic; it did not,
it reimplemented it, and it dropped the ADDEND. Every R_PPC_ADDR16_HA/LO pair
then resolved to its section's base address instead of the symbol it named, so
in the first module ever loaded every string literal pointed at the first string
in .rodata - the module ran, called its logger six times, and printed the same
line five times over. Nothing crashed. The count was right. Only the content was
wrong, which is the hardest kind of failure to see.

WHY THE HALVES CARRY S+A RATHER THAN BEING PATCHED IN PLACE. An R_PPC_ADDR32
site already holds its own base-0 target, so the value can be read back out of
the payload and rebased. A 16-bit half cannot: it is half an address, and HA
additionally folds in a sign-extension carry from the low half. So each half
entry carries the relocation's own fully-resolved S+Addend, and the target is
recomputed from scratch at load time against S+Addend+base.
"""

import re
import subprocess

# The relocation kinds that become runtime fixups, and the kind code each maps
# to in the (kind << 24 | offset, value) encoding both emitters produce.
KIND_ADDR32 = 0
KIND_HA = 1
KIND_HI = 2
KIND_LO = 3

_HALF_KINDS = {
    "R_PPC_ADDR16_HA": KIND_HA,
    "R_PPC_ADDR16_HI": KIND_HI,
    "R_PPC_ADDR16_LO": KIND_LO,
}
_WHOLE_KINDS = ("R_PPC_ADDR32", "R_PPC_RELATIVE")


class Reloc:
    """One relocation, with its symbol reference already resolved to S+Addend."""

    __slots__ = ("section", "type", "offset", "sym_value", "addend", "sym_name")

    def __init__(self, section, rtype, offset, sym_value, addend, sym_name):
        self.section = section
        self.type = rtype
        self.offset = offset
        self.sym_value = sym_value
        self.addend = addend
        self.sym_name = sym_name

    @property
    def s_plus_a(self):
        return (self.sym_value + self.addend) & 0xFFFFFFFF

    @property
    def is_half(self):
        return self.type in _HALF_KINDS

    @property
    def kind(self):
        if self.type in _HALF_KINDS:
            return _HALF_KINDS[self.type]
        return KIND_ADDR32

    def __repr__(self):
        return "Reloc(%s %s @0x%X %s+0x%X)" % (
            self.section, self.type, self.offset, self.sym_name or "?", self.addend)


def read(readelf_cmd, elf_path, skip_debug=True):
    """Every relocation in `elf_path` that could become a runtime fixup.

    Debug sections are skipped by default. They carry R_PPC_ADDR32 relocations
    whose offsets are relative to the debug section, not the flat binary, so
    applying them corrupts arbitrary words of the payload - which happened once,
    and the resulting garbage jump crashed Cemu's recompiler at boot.
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
        if len(parts) < 3:
            continue

        rtype = parts[2]
        if rtype not in _HALF_KINDS and rtype not in _WHOLE_KINDS:
            continue

        try:
            offset = int(parts[0], 16)
        except ValueError:
            continue

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
            # "Sym.Name + Addend" (or "- Addend"): the addend is the last token,
            # and dropping it is the bug this module exists to prevent.
            if len(parts) >= 6 and parts[-2] in ("+", "-"):
                sign = -1 if parts[-2] == "-" else 1
                try:
                    addend = sign * int(parts[-1], 16)
                except ValueError:
                    addend = 0

        relocs.append(Reloc(section, rtype, offset, sym_value, addend, sym_name))

    return relocs


def read_undefined_symbols(readelf_cmd, elf_path):
    """Symbols the object references but does not define."""
    out = subprocess.check_output([readelf_cmd, "-sW", elf_path], text=True)
    undefined = set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) < 8:
            continue
        if parts[6] == "UND" and parts[7]:
            undefined.add(parts[7])
    return undefined
