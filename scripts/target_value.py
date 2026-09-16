#!/usr/bin/env python3
"""Prints one value from the active target's config, for build scripts.

    python scripts/target_value.py switch.title_id
    python scripts/target_value.py wiiu.plugin_name
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import target as target_mod


def main():
    if len(sys.argv) != 2:
        sys.stderr.write("usage: target_value.py <dotted.key>\n")
        return 2

    root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    name, cfg = target_mod.resolve(root_dir, None, quiet=True)

    node = cfg
    for key in sys.argv[1].split("."):
        if not isinstance(node, dict) or key not in node:
            sys.stderr.write(
                "[target] target '%s' has no '%s'\n" % (name, sys.argv[1]))
            return 1
        node = node[key]

    if isinstance(node, bool):
        # "1"/"0" rather than "True"/"False", so a .bat can test it.
        print(1 if node else 0)
        return 0

    if isinstance(node, (dict, list)):
        sys.stderr.write(
            "[target] '%s' is a %s, not a value\n"
            % (sys.argv[1], type(node).__name__))
        return 1

    print(node)
    return 0


if __name__ == "__main__":
    sys.exit(main())
