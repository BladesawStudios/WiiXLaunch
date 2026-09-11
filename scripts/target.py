#!/usr/bin/env python3
"""Which game this host is being built for.

WiiXLaunch is a framework, not a BotW mod, and it had exactly one config -
wiixlaunch.json at the root - so "which game" was whatever that file happened to
say plus whichever submodules happened to be checked out. That is fine until you
maintain hosts for two games, at which point building the second means editing
the first's config in place and remembering to put it back.

So the configs live in targets/, one per game, and the active one is named
rather than inferred:

    build_switch.bat totk
    set WIIXL_TARGET=totk & build_cemu.bat
    python scripts/deploy.py --target totk

THE DEFAULT IS STATED, NOT ASSUMED. Every script that resolves a target prints
which one it got and whether that was a default, because a build that silently
picks a target is the same class of problem as a gate nothing invokes: the
output looks authoritative and nobody can tell what produced it.

THE GAME MODULE IS A DECISION NOW. generate_config.py used to add every
vendor/wiixlaunch-*/include it could find, so a TOTK host built in a checkout
that has vendor/wiixlaunch-botw would register eighteen BotW surfaces into a
game where every one of those offsets is wrong - and the log would announce the
game module by name while doing it. A target lists the modules it wants:

    "modules": []                    no game module; base surfaces only
    "modules": ["wiixlaunch-botw"]   this one, and it must be present
    (key absent)                     whatever is in vendor/, as before
"""
import json
import os
import sys

DEFAULT_TARGET = "botw"


def targets_dir(root_dir):
    return os.path.join(root_dir, "targets")


def available(root_dir):
    d = targets_dir(root_dir)
    if not os.path.isdir(d):
        return []
    return sorted(f[:-5] for f in os.listdir(d) if f.endswith(".json"))


def resolve(root_dir, requested=None, quiet=False):
    """Returns (name, config dict).

    `requested` wins, then $WIIXL_TARGET, then DEFAULT_TARGET. An unknown name
    is fatal and lists what does exist - a typo must not silently build the
    other game.
    """
    source = "argument"
    name = requested
    if not name:
        name = os.environ.get("WIIXL_TARGET", "").strip()
        source = "WIIXL_TARGET"
    if not name:
        name = DEFAULT_TARGET
        source = "default"

    path = os.path.join(targets_dir(root_dir), name + ".json")
    if not os.path.exists(path):
        have = available(root_dir)
        sys.stderr.write(
            "[target] no such target '%s' - %s\n"
            "  Targets live in targets/<name>.json. This checkout has: %s\n"
            % (name, ("asked for by " + source) if source != "default"
               else "and it is the default, so targets/ may be missing entirely",
               ", ".join(have) if have else "(none)"))
        sys.exit(1)

    with open(path, "r", encoding="utf-8") as f:
        cfg = json.load(f)

    if not quiet:
        project = cfg.get("project", {})
        print("[target] %s (%s) - %s" % (
            name, source, project.get("name", "<unnamed project>")))

    return name, cfg


def module_includes(root_dir, cfg):
    """The vendor module include dirs this target asks for.

    Returns (list_of_dirs, names). A missing "modules" key keeps the old
    behaviour - every wiixlaunch-* in vendor/ - because a checkout that has only
    ever built one game should not have to learn about targets to keep working.
    An explicit list is checked: naming a module that is not there is an error,
    not a shrug, for the same reason an unknown target is.
    """
    vendor = os.path.join(root_dir, "vendor")
    present = []
    if os.path.isdir(vendor):
        present = sorted(n for n in os.listdir(vendor)
                         if n.startswith("wiixlaunch-")
                         and os.path.isdir(os.path.join(vendor, n, "include")))

    wanted = cfg.get("modules")
    if wanted is None:
        chosen = present
    else:
        chosen = []
        for name in wanted:
            if name not in present:
                sys.stderr.write(
                    "[target] this target asks for game module '%s', which is not "
                    "in vendor/\n"
                    "  Present: %s\n"
                    "  A module named and missing is a host that would quietly "
                    "publish fewer\n"
                    "  surfaces than its config claims, so this is an error.\n"
                    % (name, ", ".join(present) if present else "(none)"))
                sys.exit(1)
            chosen.append(name)

    ignored = [n for n in present if n not in chosen]
    dirs = [os.path.join(vendor, n, "include").replace("\\", "/") for n in chosen]
    return dirs, chosen, ignored
