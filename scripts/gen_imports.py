#!/usr/bin/env python3
"""Emit the import headers a .wxlm includes, from the surface tables themselves.

Usage:
    python scripts/gen_imports.py            # write the headers
    python scripts/gen_imports.py --check    # fail if they are out of date
"""
import glob
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Where surface headers live and where generated headers are emitted.
#
# EVERY game module in vendor/, not the active target's: the import headers are
# the SDK a mod author builds against, and a TotK mod author needs totk_*.h
# whichever target this checkout last built. Surface names carry the game
# (botw.player, totk.player), so two modules' headers cannot collide.
# A module keeps its surfaces in include/wiixlaunch/<game>/surfaces/ plus
# <game>/surfaces.hpp - the layout wiixlaunch-botw set and wiixlaunch-totk follows.
BASE_SOURCE = os.path.join(ROOT, "include", "wiixlaunch", "loader")


def module_sources():
    """(module dir name, [surface paths]) for every vendor/wiixlaunch-* module."""
    out = []
    for mod in sorted(glob.glob(os.path.join(ROOT, "vendor", "wiixlaunch-*"))):
        found = []
        inc = os.path.join(mod, "include", "wiixlaunch")
        for game in sorted(glob.glob(os.path.join(inc, "*", ""))):
            found += sorted(glob.glob(os.path.join(game, "surfaces", "*.hpp")))
            if os.path.isfile(os.path.join(game, "surfaces.hpp")):
                found.append(os.path.join(game, "surfaces.hpp"))
        if found:
            out.append((os.path.basename(mod), found))
    return out


OUT_DIR = os.path.join(ROOT, "include", "wiixlaunch", "imports")

NAME = re.compile(r'constexpr const char\* k\w*(?:Name|Surface)\s*=\s*"([\w.]+)"')
MAJOR = re.compile(r'constexpr uint16_t k\w*VersionMajor\s*=\s*(\d+)')
MINOR = re.compile(r'constexpr uint16_t k\w*VersionMinor\s*=\s*(\d+)')
SYMBOL = re.compile(r'WIIXL_SURFACE_SYMBOL\(\s*"(\w+)"\s*,\s*&(\w+)\s*\)')
FUNC_HEAD = re.compile(r'extern "C" inline\s+([\w:*&<> ]+?)\s*\b(\w+)\s*\(')

BANNER = re.compile(r'^//\s*-{2,}')


def doc_above(body, index):
    """The comment block immediately above a definition, if there is one."""
    lines = body[:index].split("\n")
    out = []
    for line in reversed(lines[:-1]):
        stripped = line.strip()
        if not stripped:
            break
        if not stripped.startswith("//"):
            break
        if BANNER.match(stripped):
            break
        out.append(stripped[2:].strip())
    return list(reversed(out))


def scan_functions(body):
    """name -> (return type, raw parameter text, doc lines), parens balanced."""
    out = {}
    for m in FUNC_HEAD.finditer(body):
        depth, start = 0, m.end() - 1
        for i in range(start, len(body)):
            if body[i] == "(":
                depth += 1
            elif body[i] == ")":
                depth -= 1
                if depth == 0:
                    out[m.group(2)] = (" ".join(m.group(1).split()),
                                       body[start + 1:i],
                                       doc_above(body, m.start()))
                    break
    return out


def namespace_blocks(text):
    """(namespace name, body) for each top-level `namespace X { ... }`."""
    out = []
    for m in re.finditer(r'^namespace ([\w:]+)\s*\{', text, re.M):
        depth, start = 0, m.end() - 1
        for i in range(start, len(text)):
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
                if depth == 0:
                    out.append((m.group(1), text[start:i]))
                    break
    return out


# --- type aliases -----------------------------------------------------------
# Resolve types and aliases to C primitive types for ABI boundary.
ALIAS = re.compile(r'^\s*using (\w+)\s*=\s*([^;]+);', re.M)

PRIMITIVES = {
    "void", "bool", "char", "float", "double", "int", "unsigned", "long",
    "int8_t", "int16_t", "int32_t", "int64_t",
    "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "intptr_t", "uintptr_t", "size_t",
}


def collect_aliases(paths):
    """Two maps: simple aliases to substitute, and callback types to declare."""
    simple, callbacks = {}, {}
    for path in paths:
        text = io.open(path, encoding="utf-8", errors="replace").read()
        for name, target in ALIAS.findall(text):
            target = " ".join(target.split())
            if "(" in target:
                callbacks.setdefault(name, target)
            else:
                simple.setdefault(name, target.split("::")[-1])
    return simple, callbacks


def _unqualify(text):
    """`impl::ModFrameFn` -> `ModFrameFn`."""
    return re.sub(r'\b\w+::', '', text)


def _swap_aliases(token, aliases):
    name, hops = token, 0
    while name not in PRIMITIVES and name in aliases and hops < 8:
        name = aliases[name]
        hops += 1
    return name


def resolve_type(text, aliases):
    """A return type: every identifier must reduce to a primitive."""
    text = _unqualify(text)
    out = []
    for token in re.split(r'(\W+)', text):
        if not token or not token[0].isalpha():
            out.append(token)
            continue
        name = _swap_aliases(token, aliases)
        if name not in PRIMITIVES and name not in ("const", "volatile"):
            return None
        out.append(name)
    return "".join(out)


def resolve_params(text, aliases, callbacks=None, needed=None):
    """A parameter list, per comma-separated segment.

    The LAST identifier in a segment may be the parameter's name rather than a
    type - `uint32_t* hi` - and names are kept because they are most of what
    makes a generated header readable. Anything earlier has to be a type.
    """
    text = _unqualify(text)
    if not text.strip() or text.strip() == "void":
        return "void"
    segments = []
    for segment in text.split(","):
        tokens = re.split(r'(\W+)', segment)
        idents = [i for i, t in enumerate(tokens) if t and t[0].isalpha()]
        if not idents:
            segments.append(" ".join(segment.split()))
            continue
        last = idents[-1]
        # A single identifier is a bare type (`void`), not a name.
        name_position = last if len(idents) > 1 else -1
        out = []
        for i, token in enumerate(tokens):
            if not token or not token[0].isalpha():
                out.append(token)
                continue
            if i == name_position:
                out.append(token)          # the parameter's name; keep as written
                continue
            resolved = _swap_aliases(token, aliases)
            if resolved not in PRIMITIVES and resolved not in ("const", "volatile"):
                if callbacks is not None and resolved in callbacks:
                    if needed is not None:
                        needed.add(resolved)
                    out.append(resolved)
                    continue
                return None
            out.append(resolved)
        segments.append(" ".join("".join(out).split()))
    return ", ".join(segments)


def normalise_params(params):
    """Drop parameter names and defaults - a declaration only needs types."""
    params = re.sub(r'=\s*[^,]+', '', params)
    params = " ".join(params.split())
    if not params or params == "void":
        return "void"
    return params


def surfaces_in(path, aliases, callbacks):
    text = io.open(path, encoding="utf-8", errors="replace").read()
    found = []
    needed = set()
    for _ns, body in namespace_blocks(text):
        name = NAME.search(body)
        major, minor = MAJOR.search(body), MINOR.search(body)
        if not (name and major and minor):
            continue
        # Signatures come from the whole block, symbols from its table.
        sigs = {name: (ret, normalise_params(params), doc)
                for name, (ret, params, doc) in scan_functions(body).items()}
        symbols = []
        for exported, cpp in SYMBOL.findall(body):
            if cpp not in sigs:
                sys.stderr.write(
                    "[gen_imports] %s: '%s' points at %s, whose definition this "
                    "scan could not find. The header would be missing a symbol "
                    "the host publishes.\n" % (os.path.basename(path), exported, cpp))
                return None
            ret, params, doc = sigs[cpp]
            spelled = ret
            ret_resolved = resolve_type(ret, aliases)
            params_resolved = resolve_params(params, aliases, callbacks, needed)
            if ret_resolved is None or params_resolved is None:
                sys.stderr.write(
                    "[gen_imports] %s: %s uses a type this scan cannot reduce to a\n"
                    "  primitive (%s / %s). A mod cannot compile a header naming a\n"
                    "  type it has never seen, so refusing rather than emitting it.\n"
                    % (os.path.basename(path), exported, ret, params))
                return None
            note = "" if ret_resolved == spelled else "   // surface spells this %s" % spelled
            symbols.append((exported, ret_resolved, params_resolved, note, doc))
        if symbols:
            found.append((name.group(1), int(major.group(1)), int(minor.group(1)),
                          symbols, sorted(needed)))
            needed = set()
    return found


def render(surface, major, minor, symbols, callback_types):
    flat = surface.replace(".", "_")
    guard_note = ("// GENERATED FILE - do not edit.\n"
                  "// Regenerate with: python scripts/gen_imports.py\n"
                  "//\n"
                  "// %s v%d.%d, %d symbol(s), from the surface's own table.\n"
                  "//\n"
                  "// Declaring a symbol here costs nothing. BINDING one is what makes it an\n"
                  "// import, and that is opt-in:\n"
                  "//\n"
                  "//     namespace S { WXL_USE_%s(%s); }\n"
                  "//     S::%s(...);\n"
                  "//\n"
                  "// so a mod that uses two symbols imports two, not all %d.\n"
                  "//\n"
                  "// The comments are the SURFACE's own, carried across - they say why a\n"
                  "// symbol behaves as it does, which is the half a signature cannot.\n"
                  % (surface, major, minor, len(symbols), flat,
                     symbols[0][0], symbols[0][0], len(symbols)))

    lines = [guard_note, "#pragma once\n\n#include <cstdint>\n\n"]
    if callback_types:
        lines.append("// Callback types this surface takes. Declared rather than substituted:\n"
                     "// a function-pointer alias cannot be spliced into a declarator without\n"
                     "// moving the parameter's name inside the parens.\n")
        for name, target in callback_types:
            lines.append("using %s = %s;\n" % (name, target))
        lines.append("\n")
    lines.append('extern "C" {\n')
    documented = 0
    for exported, ret, params, note, doc in symbols:
        if doc:
            documented += 1
            lines.append("\n")
            for line in doc:
                lines.append(("// %s\n" % line) if line else "//\n")
        lines.append("extern %s wiixl_import__%s__%s(%s);%s\n"
                     % (ret, flat, exported, params, note))
    lines.append('}\n\n')

    lines.append("// The version this header was generated from. A mod that needs a symbol\n"
                 "// added in a later minor should pass --require %s@%d.%d when packing,\n"
                 "// so an older host refuses it by name instead of resolving short.\n"
                 % (surface, major, minor))
    lines.append("namespace wiixl_surface_%s {\n" % flat)
    lines.append("inline constexpr unsigned kVersionMajor = %d;\n" % major)
    lines.append("inline constexpr unsigned kVersionMinor = %d;\n" % minor)
    lines.append("}\n\n")

    lines.append("// VOLATILE is not style. Without it the compiler folds the indirect call\n"
                 "// into a direct branch and emits a relocation kind that cannot reach a host\n"
                 "// address - the module fails to relocate. See docs/framework/modules.md.\n")
    lines.append("#define WXL_USE_%s(sym) \\\n"
                 "    inline decltype(&wiixl_import__%s__##sym) volatile sym = \\\n"
                 "        &wiixl_import__%s__##sym\n" % (flat, flat, flat))
    return "".join(lines)


def main():
    check = "--check" in sys.argv

    groups = [("base", [os.path.join(BASE_SOURCE, n)
                        for n in sorted(os.listdir(BASE_SOURCE)) if n.endswith(".hpp")])]
    groups += module_sources()
    paths = [p for _, ps in groups for p in ps]

    # Aliases are collected per group, so a type one module defines can never
    # resolve a symbol in another module's header.
    wanted = {}
    per_group = {}
    for group, gpaths in groups:
        aliases, callbacks = collect_aliases(gpaths if group == "base"
                                             else groups[0][1] + gpaths)
        count = 0
        for path in gpaths:
            found = surfaces_in(path, aliases, callbacks)
            if found is None:
                return 1
            for surface, major, minor, symbols, needed in found:
                flat = surface.replace(".", "_") + ".h"
                if flat in wanted:
                    sys.stderr.write("[gen_imports] surface %s is defined twice - in %s and "
                                     "an earlier source. One header cannot say both.\n"
                                     % (surface, os.path.relpath(path, ROOT)))
                    return 1
                types = [(n, callbacks[n]) for n in needed]
                wanted[flat] = render(surface, major, minor, symbols, types)
                count += 1
        per_group[group] = count

    # Floors, because "0 surfaces found, nothing to do, exit 0" is the failure
    # this would otherwise report as success. Per group, so a game module whose
    # surfaces stopped parsing is not hidden by the other module's count.
    empty = [g for g, n in per_group.items() if n == 0]
    if empty:
        sys.stderr.write("[gen_imports] no surfaces parsed from: %s - each of these was "
                         "found on disk, so the scan is broken, not the tree.\n"
                         % ", ".join(empty))
        return 1
    if len(wanted) < 20:
        sys.stderr.write("[gen_imports] only %d surface(s) parsed, expected at least "
                         "20 - the scan is broken, not the tree.\n" % len(wanted))
        return 1
    print("[gen_imports] sources: %s" % ", ".join("%s %d" % (g, n) for g, n in per_group.items()))

    os.makedirs(OUT_DIR, exist_ok=True)
    stale, written = [], 0
    for name, body in sorted(wanted.items()):
        path = os.path.join(OUT_DIR, name)
        current = None
        if os.path.exists(path):
            current = io.open(path, encoding="utf-8", newline="").read()
        if current == body:
            continue
        if check:
            stale.append(name)
        else:
            io.open(path, "w", encoding="utf-8", newline="").write(body)
            written += 1

    # A header for a surface that no longer exists is worse than a missing one.
    for name in sorted(os.listdir(OUT_DIR)) if os.path.isdir(OUT_DIR) else []:
        if name.endswith(".h") and name not in wanted:
            if check:
                stale.append(name + " (surface no longer exists)")
            else:
                os.remove(os.path.join(OUT_DIR, name))
                written += 1

    total = sum(len(re.findall(r'^extern ', b, re.M)) for b in wanted.values())
    if check:
        if stale:
            sys.stderr.write(
                "\n[gen_imports] %d generated header(s) are out of date:\n%s\n"
                "  Run: python scripts/gen_imports.py\n"
                "  A generated file that has drifted from its source reads as\n"
                "  authoritative and is not.\n"
                % (len(stale), "".join("    %s\n" % s for s in stale)))
            return 1
        print("[gen_imports] %d header(s) up to date, %d symbol(s) declared"
              % (len(wanted), total))
        return 0

    print("[gen_imports] %d surface(s), %d symbol(s) declared, %d header(s) written"
          % (len(wanted), total, written))
    return 0


if __name__ == "__main__":
    sys.exit(main())
