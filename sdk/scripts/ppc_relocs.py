#!/usr/bin/env python3
"""Parsing PowerPC relocations out of readelf, for the host and modules."""

import re
import subprocess

# Relocation fixup kinds: (kind << 24 | offset, value).
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
    """Every relocation in `elf_path` that could become a runtime fixup."""
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
            # "Sym.Name + Addend" (or "- Addend").
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
