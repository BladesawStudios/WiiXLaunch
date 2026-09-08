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
import io
import json
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


# --- mod.json ---------------------------------------------------------------
#
# What a mod IS belongs in the mod's own directory: its id, which phase it wants,
# how much arena it needs, which surface minors it depends on. What a particular
# BUILD is - where the source is, where the output goes, which WiiXLaunch to
# build against - stays on the command line, because those change per machine
# and per invocation while the mod does not.
#
# The split matters for distribution. A mod repo that carries its own manifest
# can be built by anyone with one flag; a mod whose identity lives in a command
# line is a mod whose identity lives in whoever remembers the command line.
MANIFEST = "mod.json"

# Every key, and what it maps to. An UNKNOWN key is an error rather than
# something quietly ignored: a typo in a manifest that silently does nothing is
# the same failure as a gate that cannot fail - it reads as configured and is
# not.
MANIFEST_KEYS = {
    "id":          "module id; also the output filename and resource directory",
    "entry":       "translation unit to compile (default mod.cpp)",
    "phase":       "when the loader calls the entry point (default load)",
    "heapRequest": "bytes of arena this module needs; omit for best effort",
    "include":     "list of extra include directories, relative to the mod",
    "require":     "list of surfaces at a minimum version, e.g. botw.map@1.1",
}


def read_manifest(source):
    """The mod's own description of itself, or {} when it has none."""
    path = os.path.join(source, MANIFEST)
    if not os.path.exists(path):
        return {}
    try:
        data = json.loads(io.open(path, encoding="utf-8").read())
    except ValueError as exc:
        sys.stderr.write("[build_mod] %s is not valid JSON: %s\n" % (path, exc))
        return None
    if not isinstance(data, dict):
        sys.stderr.write("[build_mod] %s must be a JSON object.\n" % path)
        return None

    unknown = [k for k in data if k not in MANIFEST_KEYS]
    if unknown:
        sys.stderr.write(
            "[build_mod] %s has %d key(s) this build does not understand: %s\n"
            "  A key nobody reads looks configured and is not, so this is an\n"
            "  error rather than a shrug. Known keys:\n%s"
            % (path, len(unknown), ", ".join(sorted(unknown)),
               "".join("    %-12s %s\n" % (k, v)
                       for k, v in sorted(MANIFEST_KEYS.items()))))
        return None

    for key in ("include", "require"):
        if key in data and not isinstance(data[key], list):
            sys.stderr.write("[build_mod] %s: '%s' must be a list.\n" % (path, key))
            return None
    return data


def main():
    ap = argparse.ArgumentParser(description="Build a .wxlm module")
    ap.add_argument("--source", required=True,
                    help="directory holding mod.cpp, and optionally data/")
    ap.add_argument("--id", default=None,
                    help="module id; also the output filename and resource dir. "
                         "Read from mod.json when not given here.")
    ap.add_argument("--out", default=None,
                    help="output directory (default: <wiixlaunch>/build)")
    ap.add_argument("--wiixlaunch", default=DEFAULT_ROOT,
                    help="WiiXLaunch checkout (default: the tree holding this script)")
    ap.add_argument("--entry-file", default=None,
                    help="translation unit to compile (default: mod.cpp)")
    ap.add_argument("--phase", default=None)
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

    # Where output goes when nobody says.
    #
    # In the framework tree that is <root>/build, because deploy.py collects
    # from there and the in-tree samples are part of the host's own build.
    #
    # From an SDK it is <mod>/build instead. An SDK is somebody's installed
    # toolchain, not their workspace: writing their module into it would put
    # build artifacts inside the thing they downloaded, and leave the answer to
    # "where did my .wxlm go" somewhere they have no reason to look. sdk.json is
    # only ever written by make_sdk.py, so its presence is what distinguishes
    # the two.
    if args.out:
        out = os.path.abspath(args.out)
    elif os.path.exists(os.path.join(root, "sdk.json")):
        out = os.path.join(source, "build")
    else:
        out = os.path.join(root, "build")

    manifest = read_manifest(source)
    if manifest is None:
        return 1

    # The command line wins, so a one-off build can override without editing the
    # mod - but it says which value came from where, because a flag silently
    # shadowing a manifest is how you debug the wrong file for twenty minutes.
    def settle(flag_value, key, default):
        if flag_value is not None:
            if key in manifest and str(manifest[key]) != str(flag_value):
                print("[build_mod] --%s overrides %s's %r" % (key, MANIFEST, manifest[key]))
            return flag_value
        return manifest.get(key, default)

    mod_id = settle(args.id, "id", None)
    entry_file = settle(args.entry_file, "entry", "mod.cpp")
    phase = settle(args.phase, "phase", "load")
    heap_request = settle(args.heap_request, "heapRequest", None)
    # Lists ACCUMULATE rather than override: a manifest listing what the mod
    # needs and a command line adding one more are not in conflict.
    includes_cfg = list(manifest.get("include", [])) + list(args.include)
    requires_cfg = list(manifest.get("require", [])) + list(args.requires)

    if not mod_id:
        sys.stderr.write(
            "[build_mod] no module id. Pass --id, or give the mod a %s with\n"
            '  {"id": "yourmod"} in it.\n' % MANIFEST)
        return 1
    if manifest:
        print("[build_mod] %s: id=%s phase=%s%s" %
              (MANIFEST, mod_id, phase,
               ", %d required surface(s)" % len(requires_cfg) if requires_cfg else ""))

    # A module id is what its resource directory is named, so the loader's rule
    # about the reserved namespace applies here too - and finding out at build
    # time costs a script run rather than a boot that refuses the module.
    if mod_id.startswith("_"):
        sys.stderr.write(
            "[build_mod] '%s' is in the host's reserved id space.\n"
            "  Ids beginning with '_' belong to WiiXLaunch (mods/_host/ holds the\n"
            "  host's own resources), and the loader refuses them by name at load.\n"
            % mod_id)
        return 1

    mod_cpp = os.path.join(source, entry_file)
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
    elf = os.path.join(out, mod_id + ".elf")
    wxlm = os.path.join(out, mod_id + ".wxlm")

    includes = [os.path.join(root, "include")]
    # A module may sit next to headers of its own.
    if os.path.isdir(os.path.join(source, "include")):
        includes.append(os.path.join(source, "include"))
    # A manifest's include path is relative to the MOD, not to the shell's
    # working directory - the manifest travels with the mod and the cwd does not.
    includes += [i if os.path.isabs(i) else os.path.join(source, i)
                 for i in includes_cfg]

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

    pack = [sys.executable, wxlm_py, elf, wxlm, "--id", mod_id,
            "--phase", phase]
    for spec in requires_cfg:
        pack += ["--require", spec]
    if heap_request:
        pack += ["--heap-request", str(heap_request)]
    r = subprocess.run(pack)
    if r.returncode != 0:
        return 1

    # Resources, staged where deploy.py looks for them. Cleared first: a
    # directory left from a renamed or removed file would otherwise ship
    # forever, which is the same staleness deploy.py guards against for .wxlm.
    data_src = os.path.join(source, "data")
    staged = os.path.join(out, "moddata", mod_id)
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
