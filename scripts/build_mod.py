#!/usr/bin/env python3
"""Builds a .wxlm module from a source directory.

Usage:
    python scripts/build_mod.py --source examples/hook_mod_a --id a_first
    python scripts/build_mod.py --source . --id botw_api --wiixlaunch ../WiiXLaunch

Expected module source directory layout:
    <source>/mod.cpp     module entry (required; --entry-file to rename)
    <source>/data/       optional; deployed to mods/<id>/ on target
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

# Target platform architecture and toolchain definitions.
TARGETS = {
    "cemu": {
        "machine":  "ppc32",
        "env":      "DEVKITPPC",
        "roots":    (r"C:\devkitPro\devkitPPC", "/opt/devkitpro/devkitPPC"),
        "prefix":   "powerpc-eabi-",
        "linker":   "wxlm_mod.ld",
        "defines":  ["__CEMU__=1", "WIIXL_CEMU=1"],
        "arch":     ["-msdata=none"],
        "toolchain": "devkitPPC",
        "subdir":   "",
    },
    "switch": {
        "machine":  "aarch64",
        "env":      "DEVKITA64",
        "roots":    (r"C:\devkitPro\devkitA64", "/opt/devkitpro/devkitA64"),
        "prefix":   "aarch64-none-elf-",
        "linker":   "wxlm_mod_aarch64.ld",
        "defines":  ["__SWITCH__=1", "WIIXL_SWITCH=1"],
        "arch":     ["-mcmodel=small"],
        "toolchain": "devkitA64",
        "subdir":   "switch-mods",
    },
}

# Wii U runs the same PowerPC module as Cemu.


def find_gxx(target):
    """The cross g++ for this target, however devkitPro is installed here."""
    t = TARGETS[target]
    roots = [os.environ.get(t["env"])] + list(t["roots"])
    for root in roots:
        if not root:
            continue
        c = os.path.join(root, "bin", t["prefix"] + "g++")
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
    "entry":       "translation unit with WiiXLaunch_ModEntry (default mod.cpp)",
    "sources":     "further .cpp files to compile, relative to the mod",
    "phase":       "when the loader calls the entry point (default load)",
    "heapRequest": "bytes of arena this module needs; omit for best effort",
    "include":     "list of extra include directories, relative to the mod",
    "require":     "list of surfaces at a minimum version, e.g. botw.map@1.1",
    "target":      "cemu (PowerPC, also Wii U) or switch (AArch64)",
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


# --- editor integration -----------------------------------------------------
def write_ide_files(source, cmd, mod_cpp, includes, gxx):
    """compile_commands.json for clangd, c_cpp_properties.json for VS Code."""
    # Strip link flags before emitting compile commands for indexers.
    compile_only = [a for a in cmd
                    if a not in ("-nostartfiles", "-nostdlib", "-Wl,-q",
                                 "-Wl,--unresolved-symbols=ignore-all", "-lgcc")]
    if "-T" in compile_only:
        i = compile_only.index("-T")
        del compile_only[i:i + 2]
    if "-o" in compile_only:
        i = compile_only.index("-o")
        del compile_only[i:i + 2]

    db = [{"directory": source.replace("\\", "/"),
           "file": mod_cpp.replace("\\", "/"),
           "arguments": compile_only}]
    io.open(os.path.join(source, "compile_commands.json"), "w",
            encoding="utf-8", newline="").write(json.dumps(db, indent=2) + "\n")

    vscode = os.path.join(source, ".vscode")
    os.makedirs(vscode, exist_ok=True)
    io.open(os.path.join(vscode, "c_cpp_properties.json"), "w",
            encoding="utf-8", newline="").write(json.dumps({
        "version": 4,
        "configurations": [{
            "name": "WiiXLaunch mod",
            "compilerPath": gxx.replace("\\", "/"),
            "compilerArgs": ["-ffreestanding", "-fno-exceptions", "-fno-rtti"],
            "cStandard": "c17",
            "cppStandard": "c++20",
            "intelliSenseMode": "gcc-x64",
            "includePath": [i.replace("\\", "/") for i in includes] + ["${workspaceFolder}"],
            "defines": ["__CEMU__=1", "WIIXL_CEMU=1"],
        }],
    }, indent=2) + "\n")


TEMPLATE_MOD_CPP = """\
// %(id)s - a WiiXLaunch module.
//
// Every host call is an import. Include the header for the surface you want,
// bind the symbols you use, and call them. Only what you BIND is imported, so
// including a header costs nothing.
//
// The headers are in the SDK under include/wiixlaunch/imports/ - one per
// surface, each listing what it publishes and, where the surface said so, why.

#include <wiixlaunch/imports/wiixl_core.h>
#include <wiixlaunch/mod_runtime.h>

namespace Core {
WXL_USE_wiixl_core(Log);
}

// The loader calls this once, at load. `used` because nothing here references
// it and the optimizer would otherwise drop it.
extern "C" __attribute__((used)) void WiiXLaunch_ModEntry() {
    if (Core::Log) Core::Log("%(id)s: loaded");
}
"""

TEMPLATE_CLANGD = """\
# clangd reads compile_commands.json, which build_mod.py writes next to this
# file every time it runs. Build once and the editor knows everything.
CompileFlags:
  Add: [-ffreestanding, -fno-exceptions, -fno-rtti]
  Remove: [-msdata=none, -T*, -Wl*, -nostdlib, -nostartfiles]
"""

TEMPLATE_GITIGNORE = """\
build/
compile_commands.json
.vscode/
"""


def scaffold(dest, mod_id):
    """A new mod folder that opens in an editor and builds."""
    if os.path.exists(dest) and os.listdir(dest):
        sys.stderr.write("[build_mod] --init: %s already exists and is not empty.\n"
                         % dest)
        return 1
    os.makedirs(dest, exist_ok=True)
    io.open(os.path.join(dest, "mod.cpp"), "w", encoding="utf-8", newline="").write(
        TEMPLATE_MOD_CPP % {"id": mod_id})
    io.open(os.path.join(dest, "mod.json"), "w", encoding="utf-8", newline="").write(
        json.dumps({"id": mod_id}, indent=2) + "\n")
    io.open(os.path.join(dest, ".clangd"), "w", encoding="utf-8", newline="").write(
        TEMPLATE_CLANGD)
    io.open(os.path.join(dest, ".gitignore"), "w", encoding="utf-8", newline="").write(
        TEMPLATE_GITIGNORE)
    print("[build_mod] created %s" % dest)
    print("[build_mod]   mod.cpp  mod.json  .clangd  .gitignore")
    print("[build_mod] build it once and your editor will resolve everything:")
    print("[build_mod]   python %s --source %s"
          % (os.path.join("<sdk>", "scripts", "build_mod.py"), dest))
    return 0


def main():
    ap = argparse.ArgumentParser(description="Build a .wxlm module")
    ap.add_argument("--source", required=False,
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
    ap.add_argument("--target", default=None, choices=sorted(TARGETS),
                    help="which console's toolchain to build for; overrides "
                         "mod.json's \"target\" (default cemu, which is also "
                         "the Wii U module)")
    ap.add_argument("--phase", default=None)
    ap.add_argument("--heap-request", default=None,
                    help="bytes this module requires; omitted means best effort")
    ap.add_argument("--include", action="append", default=[],
                    help="extra include directory; repeatable")
    # Overrides derived v1.0 floor when a symbol requires a later minor version.
    ap.add_argument("--require", dest="requires", action="append", default=[],
                    help="require a surface at a minimum version, e.g. botw.map@1.1")
    ap.add_argument("--init", metavar="DIR",
                    help="create a new mod folder, ready to open in an editor")
    args = ap.parse_args()

    if args.init:
        dest = os.path.abspath(args.init)
        return scaffold(dest, args.id or os.path.basename(dest))

    if not args.source:
        ap.error("--source is required (or use --init to create a new mod)")

    root = os.path.abspath(args.wiixlaunch)
    source = os.path.abspath(args.source)

    # In framework tree: default to <root>/build. In an SDK: default to <mod>/build.
    if args.out:
        out = os.path.abspath(args.out)
    elif os.path.exists(os.path.join(root, "sdk.json")):
        out = os.path.join(source, "build")
    else:
        out = os.path.join(root, "build")

    manifest = read_manifest(source)
    if manifest is None:
        return 1

    # CLI flags override manifest values.
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
    target = settle(args.target, "target", "cemu")
    if target not in TARGETS:
        sys.stderr.write(
            "[build_mod] unknown target %r. Known targets:\n%s\n"
            % (target, "".join("    %-8s %s\n" % (k, v["toolchain"])
                               for k, v in sorted(TARGETS.items()))))
        return 1
    # Accumulate list values from manifest and CLI.
    includes_cfg = list(manifest.get("include", [])) + list(args.include)
    sources_cfg = list(manifest.get("sources", []))
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

    # Ids beginning with '_' are reserved for the host (e.g. mods/_host/).
    if mod_id.startswith("_"):
        sys.stderr.write(
            "[build_mod] '%s' is in the host's reserved id space.\n"
            "  Ids beginning with '_' belong to WiiXLaunch (mods/_host/ holds the\n"
            "  host's own resources), and the loader refuses them by name at load.\n"
            % mod_id)
        return 1

    extra_cpp = []
    for rel in sources_cfg:
        path = rel if os.path.isabs(rel) else os.path.join(source, rel)
        if not os.path.exists(path):
            sys.stderr.write("[build_mod] mod.json lists a source that does not "
                             "exist: %s\n" % path)
            return 1
        extra_cpp.append(path)

    mod_cpp = os.path.join(source, entry_file)
    if not os.path.exists(mod_cpp):
        sys.stderr.write("[build_mod] %s does not exist.\n"
                         "  Pass --entry-file if the module's translation unit is\n"
                         "  not called mod.cpp.\n" % mod_cpp)
        return 1

    tcfg = TARGETS[target]
    linker = os.path.join(root, "scripts", tcfg["linker"])
    wxlm_py = os.path.join(root, "scripts", "wxlm.py")
    for needed in (linker, wxlm_py):
        if not os.path.exists(needed):
            sys.stderr.write(
                "[build_mod] %s is missing.\n"
                "  --wiixlaunch points at '%s', which does not look like a\n"
                "  WiiXLaunch checkout.\n" % (needed, root))
            return 1

    gxx = find_gxx(target)
    if gxx is None:
        # No gate exits 0 on a missing tool - see docs/framework/modules.md.
        sys.stderr.write(
            "\n[build_mod] SETUP PROBLEM - not a broken source tree.\n"
            "  %sg++ was not found. Set %s, or install %s.\n\n"
            % (tcfg["prefix"], tcfg["env"], tcfg["toolchain"]))
        return 1

    if tcfg["subdir"]:
        out = os.path.join(out, tcfg["subdir"])
    os.makedirs(out, exist_ok=True)
    elf = os.path.join(out, mod_id + ".elf")
    wxlm = os.path.join(out, mod_id + ".wxlm")

    includes = [os.path.join(root, "include")]
    if os.path.isdir(os.path.join(source, "include")):
        includes.append(os.path.join(source, "include"))
    # Manifest include paths are relative to the mod directory.
    includes += [i if os.path.isabs(i) else os.path.join(source, i)
                 for i in includes_cfg]

    cmd = [gxx,
           "-std=gnu++20", "-fno-pie", "-fno-pic", "-Os",
           "-ffreestanding", "-fno-exceptions", "-fno-rtti",
           "-Wall", "-Wextra", "-Wduplicated-cond", "-Wduplicated-branches",
           "-Wno-unused-parameter", "-Wno-unused-function"]
    cmd += tcfg["arch"]
    cmd += ["-D" + d for d in tcfg["defines"]]
    for inc in includes:
        cmd += ["-I", inc]
    cmd += ["-nostartfiles", "-nostdlib", "-T", linker, "-Wl,-q",
            "-Wl,--unresolved-symbols=ignore-all",
            mod_cpp] + extra_cpp + ["-lgcc", "-o", elf]

    # Configure editor files before compile so IDE works even if build fails.
    write_ide_files(source, cmd, mod_cpp, includes, gxx)

    r = subprocess.run(cmd)
    if r.returncode != 0:
        sys.stderr.write("[build_mod] %s failed to compile\n" % mod_cpp)
        return 1

    pack = [sys.executable, wxlm_py, elf, wxlm, "--id", mod_id,
            "--phase", phase, "--machine", tcfg["machine"]]
    for spec in requires_cfg:
        pack += ["--require", spec]
    if heap_request:
        pack += ["--heap-request", str(heap_request)]
    r = subprocess.run(pack)
    if r.returncode != 0:
        return 1

    print("[build_mod] %s" % wxlm)

    # Stage resources where deploy.py looks for them.
    data_src = os.path.join(source, "data")
    staged = os.path.join(out, "moddata", mod_id)
    if os.path.isdir(staged):
        shutil.rmtree(staged)
    if os.path.isdir(data_src):
        shutil.copytree(data_src, staged)
        files = sum(len(f) for _r, _d, f in os.walk(staged))
        print("[build_mod] %s: staged %d resource file(s) for mods/%s/"
              % (mod_id, files, mod_id))

    return 0


if __name__ == "__main__":
    sys.exit(main())
