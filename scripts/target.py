#!/usr/bin/env python3
"""Target configuration resolution for multi-game host builds."""
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

    `requested` wins, then $WIIXL_TARGET, then DEFAULT_TARGET.
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

    Returns (list_of_dirs, chosen, ignored).
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
