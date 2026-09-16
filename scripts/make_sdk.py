#!/usr/bin/env python3
"""Assemble the standalone WiiXLaunch SDK for mod authors.

Usage:
    python scripts/make_sdk.py            # Refresh sdk/ from the repository
    python scripts/make_sdk.py --check    # Verify sdk/ is up to date
    python scripts/make_sdk.py --verify   # Verify SDK by building a test module
    python scripts/make_sdk.py --host     # Package player-ready host graphic pack
"""
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SCRIPTS = ["build_mod.py", "wxlm.py", "ppc_relocs.py", "aarch64_relocs.py",
           "wxlm_mod.ld", "wxlm_mod_aarch64.ld"]
HEADERS = [os.path.join("wiixlaunch", "mod_runtime.h"),
           os.path.join("wiixlaunch", "mod_log.h"),
           os.path.join("wiixlaunch", "format.hpp"),
           os.path.join("wiixlaunch", "mod_math.h"),
           os.path.join("wiixlaunch", "patch_decl.hpp"),
           os.path.join("wiixlaunch", "mod_config.h"),
           os.path.join("wiixlaunch", "mod_version.h")]
IMPORTS = os.path.join("include", "wiixlaunch", "imports")


def host_versions():
    """Extract surface versions from generated import headers."""
    out = {}
    for name in sorted(os.listdir(os.path.join(ROOT, IMPORTS))):
        if not name.endswith(".h"):
            continue
        text = io.open(os.path.join(ROOT, IMPORTS, name), encoding="utf-8").read()
        head = text.split("\n")[3]          # "// <surface> vMAJ.MIN, N symbol(s), ..."
        parts = head.replace("//", "").strip().split()
        if len(parts) >= 2 and parts[1].startswith("v"):
            out[parts[0]] = parts[1].rstrip(",")
    return out


def assemble(dest):
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    os.makedirs(os.path.join(dest, "scripts"))
    os.makedirs(os.path.join(dest, "include", "wiixlaunch"))

    for name in SCRIPTS:
        shutil.copy2(os.path.join(ROOT, "scripts", name),
                     os.path.join(dest, "scripts", name))
    for rel in HEADERS:
        shutil.copy2(os.path.join(ROOT, "include", rel),
                     os.path.join(dest, "include", rel))
    shutil.copytree(os.path.join(ROOT, IMPORTS),
                    os.path.join(dest, "include", "wiixlaunch", "imports"))

    versions = host_versions()
    io.open(os.path.join(dest, "sdk.json"), "w", encoding="utf-8", newline="").write(
        json.dumps({"surfaces": versions}, indent=2, sort_keys=True) + "\n")
    io.open(os.path.join(dest, "README.md"), "w", encoding="utf-8", newline="").write(
        README % (len(versions),
                  "".join("| `%s` | %s |\n" % (k, v)
                          for k, v in sorted(versions.items()))))
    return versions


README = """# WiiXLaunch mod SDK

Everything needed to build a `.wxlm`, and nothing else. No framework checkout,
no submodules, no game headers.

## Build a mod

```
python scripts/build_mod.py --source path/to/your_mod
```

Your mod is a directory holding `mod.cpp` and a `mod.json`:

```json
{ "id": "your_mod" }
```

## Write a mod

```cpp
#include <wiixlaunch/imports/wiixl_core.h>
#include <wiixlaunch/mod_runtime.h>

namespace C { WXL_USE_wiixl_core(Log); }

extern "C" __attribute__((used)) void WiiXLaunch_ModEntry() {
    if (C::Log) C::Log("hello from a mod built with no framework in sight");
}
```

`include/wiixlaunch/imports/` holds one header per surface. Each DECLARES every
symbol that surface publishes; `WXL_USE_<surface>(Name)` BINDS one, and only
what you bind becomes an import. Do not hand-write import declarations - the
signature is the one thing about a mod that nothing checks, and these are
generated from the host's own tables so it cannot be wrong.

`mod_runtime.h` defines memcpy, memset, memmove and memcmp. GCC synthesises
calls to them even under -ffreestanding, nothing else defines them, and the
link succeeds anyway - so without this a module branches to address 0 the first
time it copies a struct.

`mod_math.h` gives you `WiiXLaunch::ModMath::Sqrt`, `Sin` and `Cos`. There is no
`<cmath>` under -ffreestanding and no libm to link, so these are written out and
their error is measured rather than assumed: 7.1e-08 relative for sqrt, 2.2e-07
absolute for sin and cos over +/- 100 radians. They deliberately do NOT install
themselves into `namespace std`; a mod ported from `<cmath>` changes its call
sites, which is a handful of lines and says plainly which one is running.

## What this SDK was cut from

%d surfaces:

| surface | version |
|---|---|
%s
A host publishes these or later minors. Your mod names what it needs and the
loader refuses it by name if the host cannot provide it, which is the point of
the arrangement.
"""


# Files preserved in mods/ for the host.
HOST_KEEP = ("_host", "probe.bin")


def cut_host(dest):
    """The deployed graphic pack, with other people's mods taken out."""
    packs = os.path.join(ROOT, "deploy", "cemu", "graphicPacks")
    if not os.path.isdir(packs):
        sys.stderr.write("[make_sdk] --host: nothing at %s. Run build_cemu first;\n"
                         "  the host is what that produces.\n" % packs)
        return None
    names = sorted(n for n in os.listdir(packs) if os.path.isdir(os.path.join(packs, n)))
    if len(names) != 1:
        sys.stderr.write("[make_sdk] --host: expected exactly one graphic pack in %s, "
                         "found %d: %s\n" % (packs, len(names), ", ".join(names) or "none"))
        return None
    src = os.path.join(packs, names[0])

    if os.path.isdir(dest):
        shutil.rmtree(dest)
    shutil.copytree(src, dest)

    mods = os.path.join(dest, "content", "WiiXLaunch", "mods")
    removed = []
    if os.path.isdir(mods):
        for entry in sorted(os.listdir(mods)):
            if entry in HOST_KEEP:
                continue
            path = os.path.join(mods, entry)
            removed.append(entry)
            shutil.rmtree(path) if os.path.isdir(path) else os.remove(path)

    # Empty mods/ needs a file so zip/copy tools preserve the directory.
    io.open(os.path.join(mods, "README.txt"), "w", encoding="utf-8", newline="").write(
        "Put .wxlm files in this directory.\n"
        "\n"
        "They load in filename order, which is also hook order, so a mod that\n"
        "must wrap another sorts before it. The game's log names every module\n"
        "as it loads, and names any it refuses and why.\n"
        "\n"
        "_host/ and probe.bin belong to WiiXLaunch. Leave them alone.\n")

    left = sorted(os.listdir(mods))
    print("[make_sdk] host: %s -> %s" % (names[0], dest))
    print("[make_sdk]   removed %d module file(s)/dir(s): %s"
          % (len(removed), ", ".join(removed) or "none"))
    print("[make_sdk]   mods/ now holds: %s" % ", ".join(left))
    return dest


def verify_host(host):
    """A host install must contain no modules and still be a loadable pack."""
    mods = os.path.join(host, "content", "WiiXLaunch", "mods")
    leftover = [n for n in sorted(os.listdir(mods)) if n.endswith(".wxlm")]
    if leftover:
        sys.stderr.write("[make_sdk] --host: %d module(s) still in the pack: %s\n"
                         % (len(leftover), ", ".join(leftover)))
        return False
    for required in ("rules.txt",):
        if not os.path.exists(os.path.join(host, required)):
            sys.stderr.write("[make_sdk] --host: the pack has no %s, so Cemu would "
                             "not load it.\n" % required)
            return False
    if not any(n.endswith(".asm") for n in os.listdir(host)):
        sys.stderr.write("[make_sdk] --host: the pack has no patch .asm, so there is "
                         "no host in it.\n")
        return False
    if not os.path.isdir(os.path.join(mods, "_host")):
        sys.stderr.write("[make_sdk] --host: _host/ is missing - the host's own "
                         "resources were taken out with the mods.\n")
        return False
    print("[make_sdk] host verified: rules.txt, a patch, _host/, and no modules")
    return True


# Ignore bytecode artifacts created when executing SDK scripts.
def is_debris(rel):
    parts = rel.replace("\\", "/").split("/")
    return "__pycache__" in parts or rel.endswith(".pyc")


def compare_trees(built, committed):
    """Every path that differs between a fresh assembly and what is on disk."""
    out = []
    if not os.path.isdir(committed):
        return ["sdk/ does not exist"]
    for root, _dirs, files in os.walk(built):
        for name in files:
            rel = os.path.relpath(os.path.join(root, name), built)
            if is_debris(rel):
                continue
            other = os.path.join(committed, rel)
            if not os.path.exists(other):
                out.append(rel + " (missing)")
            elif io.open(os.path.join(root, name), "rb").read() != io.open(other, "rb").read():
                out.append(rel + " (differs)")
    for root, _dirs, files in os.walk(committed):
        for name in files:
            rel = os.path.relpath(os.path.join(root, name), committed)
            if is_debris(rel):
                continue
            if not os.path.exists(os.path.join(built, rel)):
                out.append(rel + " (should not be there)")
    return sorted(out)


def verify(sdk):
    """Build a module using only the SDK, then again from the tree, and diff."""
    src = os.path.join(tempfile.mkdtemp(prefix="wxl_sdk_"), "probe_mod")
    os.makedirs(src)
    io.open(os.path.join(src, "mod.cpp"), "w", encoding="utf-8", newline="").write(
        '#include <wiixlaunch/imports/wiixl_core.h>\n'
        '#include <wiixlaunch/mod_runtime.h>\n'
        '\n'
        'namespace C { WXL_USE_wiixl_core(Log); }\n'
        '\n'
        'struct Blob { char bytes[64]; };\n'
        '\n'
        'extern "C" __attribute__((used)) void WiiXLaunch_ModEntry() {\n'
        '    Blob a{}; Blob b = a; a = b;\n'
        '    if (C::Log) C::Log(a.bytes[0] ? "probe" : "probe: built from the SDK alone");\n'
        '}\n')
    io.open(os.path.join(src, "mod.json"), "w", encoding="utf-8", newline="").write(
        '{ "id": "sdk_probe" }\n')

    outs = {}
    for label, root in (("sdk", sdk), ("tree", ROOT)):
        out = os.path.join(src, "build_" + label)
        r = subprocess.run(
            [sys.executable, os.path.join(root, "scripts", "build_mod.py"),
             "--source", src, "--out", out],
            cwd=tempfile.gettempdir(),
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if r.returncode != 0:
            sys.stderr.write("[make_sdk] --verify: the %s build failed.\n%s\n"
                             % (label, r.stdout.decode("utf-8", "replace")))
            return False
        outs[label] = io.open(os.path.join(out, "sdk_probe.wxlm"), "rb").read()

    # Verify compile_commands.json syntax by replaying with -fsyntax-only.
    db_path = os.path.join(src, "compile_commands.json")
    if not os.path.exists(db_path):
        sys.stderr.write("[make_sdk] --verify: the build wrote no compile_commands.json, "
                         "so an editor would resolve nothing.\n")
        return False
    db = json.loads(io.open(db_path, encoding="utf-8").read())
    replay = subprocess.run(db[0]["arguments"] + ["-fsyntax-only"],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if replay.returncode != 0:
        sys.stderr.write("[make_sdk] --verify: replaying compile_commands.json fails, so "
                         "an editor reading it would show errors:\n%s\n"
                         % replay.stdout.decode("utf-8", "replace"))
        return False

    if outs["sdk"] != outs["tree"]:
        sys.stderr.write(
            "[make_sdk] --verify: the SDK and the tree produced DIFFERENT modules\n"
            "  (%d vs %d bytes). The SDK is not a faithful copy of the build.\n"
            % (len(outs["sdk"]), len(outs["tree"])))
        return False

    print("[make_sdk] verified: a module built from the SDK alone is byte-identical\n"
          "           to the same module built from the full tree (%d bytes),\n"
          "           and its compile_commands.json resolves every include"
          % len(outs["sdk"]))
    return True


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    dest = os.path.abspath(args[0]) if args else os.path.join(ROOT, "sdk")

    # SDK sits at sdk/; host pack product defaults to build/host.
    host_dest = (os.path.join(os.path.dirname(dest), "host") if args
                 else os.path.join(ROOT, "build", "host"))

    r = subprocess.run([sys.executable, os.path.join(ROOT, "scripts", "gen_imports.py"),
                        "--check"])
    if r.returncode != 0:
        sys.stderr.write("[make_sdk] the import headers are out of date; refusing to\n"
                         "  cut an SDK around them. Run scripts/gen_imports.py.\n")
        return 1

    if "--check" in sys.argv:
        import filecmp
        staging = os.path.join(tempfile.mkdtemp(prefix="wxl_sdkchk_"), "sdk")
        assemble(staging)
        diff = compare_trees(staging, dest)
        if diff:
            sys.stderr.write(
                "\n[make_sdk] sdk/ is out of date - %d file(s) differ or are missing:\n"
                "%s"
                "  Run: python scripts/make_sdk.py\n"
                "  A checked-in artifact that has drifted from its source looks\n"
                "  authoritative and is stale.\n"
                % (len(diff), "".join("    %s\n" % d for d in diff)))
            return 1
        files = sum(1 for r, _d, f in os.walk(dest) for n in f
                     if not is_debris(os.path.relpath(os.path.join(r, n), dest)))
        print("[make_sdk] sdk/ up to date: %d file(s)" % files)
    else:
        versions = assemble(dest)
        files = sum(len(f) for _r, _d, f in os.walk(dest))
        print("[make_sdk] %s: %d file(s), %d surface(s)" % (dest, files, len(versions)))

    if "--verify" in sys.argv:
        if not verify(dest):
            return 1

    if "--host" in sys.argv:
        if cut_host(host_dest) is None:
            return 1
        if not verify_host(host_dest):
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
