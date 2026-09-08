#!/usr/bin/env python3
"""Builds a .wxlm module from a source directory, in or out of this tree.

    python scripts/build_mod.py --source examples/hook_mod_a --id a_first
    python scripts/build_mod.py --source . --id botw_api --wiixlaunch ../WiiXLaunch

THIS IS THE ONLY DEFINITION OF HOW A MODULE IS BUILT. build_cemu calls it for
the in-tree samples and an external project calls it for its own, so the flags,
the linker script and the packing step cannot drift between them.

That mattered enough to write down: the flags are not obvious and getting one
wrong fails in ways that do not look like a flag problem. -fno-pie/-fno-pic
because this toolchain emits GOT-indirect addressing under -fPIE that the
relocator does not handle; -nostdlib with -lgcc anyway because GCC emits calls
to libgcc's PowerPC register save/restore helpers and wxlm.py rejects a module
with undefined symbols; --unresolved-symbols=ignore-all because a module's
imports are DELIBERATELY undefined and are resolved at load through the surface
registry. A second copy of that list would eventually disagree with this one,
and the disagreement would show up as a module that loads and misbehaves.

WHAT A MODULE SOURCE DIRECTORY LOOKS LIKE:

    <source>/mod.cpp     the module (required; --entry-file to rename)
    <source>/data/       optional; deployed to mods/<id>/ on the target

The output is <out>/<id>.wxlm plus <out>/moddata/<id>/ for the resources, which
is the layout scripts/deploy.py expects.
"""

import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_ROOT = os.path.dirname(HERE)


def find_gxx():
    """powerpc-eabi-g++, however devkitPPC is installed on this machine."""
    root = os.environ.get("DEVKITPPC")
    candidates = []
    if root:
        candidates.append(os.path.join(root, "bin", "powerpc-eabi-g++"))
    candidates += [
        r"C:\devkitPro\devkitPPC\bin\powerpc-eabi-g++.exe",
        "/opt/devkitpro/devkitPPC/bin/powerpc-eabi-g++",
    ]
    for c in candidates:
        if os.path.exists(c):
            return c
        if os.path.exists(c + ".exe"):
            return c + ".exe"
    return None


def main():
    ap = argparse.ArgumentParser(description="Build a .wxlm module")
    ap.add_argument("--source", required=True,
                    help="directory holding mod.cpp, and optionally data/")
    ap.add_argument("--id", required=True,
                    help="module id; also the output filename and resource dir")
    ap.add_argument("--out", default=None,
                    help="output directory (default: <wiixlaunch>/build)")
    ap.add_argument("--wiixlaunch", default=DEFAULT_ROOT,
                    help="WiiXLaunch checkout (default: the tree holding this script)")
    ap.add_argument("--entry-file", default="mod.cpp",
                    help="translation unit to compile (default: mod.cpp)")
    ap.add_argument("--phase", default="load")
    ap.add_argument("--heap-request", default=None,
                    help="bytes this module requires; omitted means best effort")
    ap.add_argument("--include", action="append", default=[],
                    help="extra include directory; repeatable")
    # Required surfaces are DERIVED from the imports at v1.0, which is right
    # almost always. This is for the case it is not: a symbol that only exists
    # from a later minor, where resolving against an older host would leave the
    # mod running with a call it cannot make. Repeatable, <surface>@<major>.<minor>.
    ap.add_argument("--require", dest="requires", action="append", default=[],
                    help="require a surface at a minimum version, e.g. botw.map@1.1")
    args = ap.parse_args()

    root = os.path.abspath(args.wiixlaunch)
    source = os.path.abspath(args.source)
    out = os.path.abspath(args.out) if args.out else os.path.join(root, "build")

    # A module id is what its resource directory is named, so the loader's rule
    # about the reserved namespace applies here too - and finding out at build
    # time costs a script run rather than a boot that refuses the module.
    if args.id.startswith("_"):
        sys.stderr.write(
            "[build_mod] '%s' is in the host's reserved id space.\n"
            "  Ids beginning with '_' belong to WiiXLaunch (mods/_host/ holds the\n"
            "  host's own resources), and the loader refuses them by name at load.\n"
            % args.id)
        return 1

    mod_cpp = os.path.join(source, args.entry_file)
    if not os.path.exists(mod_cpp):
        sys.stderr.write("[build_mod] %s does not exist.\n"
                         "  Pass --entry-file if the module's translation unit is\n"
                         "  not called mod.cpp.\n" % mod_cpp)
        return 1

    linker = os.path.join(root, "scripts", "wxlm_mod.ld")
    wxlm_py = os.path.join(root, "scripts", "wxlm.py")
    for needed in (linker, wxlm_py):
        if not os.path.exists(needed):
            sys.stderr.write(
                "[build_mod] %s is missing.\n"
                "  --wiixlaunch points at '%s', which does not look like a\n"
                "  WiiXLaunch checkout.\n" % (needed, root))
            return 1

    gxx = find_gxx()
    if gxx is None:
        # No gate exits 0 on a missing tool - see docs/modules.md.
        sys.stderr.write(
            "\n[build_mod] SETUP PROBLEM - not a broken source tree.\n"
            "  powerpc-eabi-g++ was not found. Set DEVKITPPC, or install devkitPPC.\n\n")
        return 1

    os.makedirs(out, exist_ok=True)
    elf = os.path.join(out, args.id + ".elf")
    wxlm = os.path.join(out, args.id + ".wxlm")

    includes = [os.path.join(root, "include")]
    # A module may sit next to headers of its own.
    if os.path.isdir(os.path.join(source, "include")):
        includes.append(os.path.join(source, "include"))
    includes += [os.path.abspath(i) for i in args.include]

    cmd = [gxx,
           "-std=gnu++20", "-fno-pie", "-fno-pic", "-msdata=none", "-Os",
           "-ffreestanding", "-fno-exceptions", "-fno-rtti",
           "-D__CEMU__=1", "-DWIIXL_CEMU=1"]
    for inc in includes:
        cmd += ["-I", inc]
    cmd += ["-nostartfiles", "-nostdlib", "-T", linker, "-Wl,-q",
            "-Wl,--unresolved-symbols=ignore-all",
            mod_cpp, "-lgcc", "-o", elf]

    r = subprocess.run(cmd)
    if r.returncode != 0:
        sys.stderr.write("[build_mod] %s failed to compile\n" % mod_cpp)
        return 1

    pack = [sys.executable, wxlm_py, elf, wxlm, "--id", args.id,
            "--phase", args.phase]
    for spec in args.requires:
        pack += ["--require", spec]
    if args.heap_request:
        pack += ["--heap-request", str(args.heap_request)]
    r = subprocess.run(pack)
    if r.returncode != 0:
        return 1

    # Resources, staged where deploy.py looks for them. Cleared first: a
    # directory left from a renamed or removed file would otherwise ship
    # forever, which is the same staleness deploy.py guards against for .wxlm.
    data_src = os.path.join(source, "data")
    staged = os.path.join(out, "moddata", args.id)
    if os.path.isdir(staged):
        shutil.rmtree(staged)
    if os.path.isdir(data_src):
        shutil.copytree(data_src, staged)
        files = sum(len(f) for _r, _d, f in os.walk(staged))
        print("[build_mod] %s: staged %d resource file(s) for mods/%s/"
              % (args.id, files, args.id))

    return 0


if __name__ == "__main__":
    sys.exit(main())
