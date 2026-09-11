#!/usr/bin/env python3
"""Prints one value from the active target's config, for the build scripts.

    python scripts/target_value.py switch.title_id
    python scripts/target_value.py wiiu.plugin_name

Exists so a .bat or .sh can ask the same resolver the build used instead of
repeating a constant. build_switch.bat hardcoded BotW's title id, so the first
build of a second target copied a TOTK subsdk9 into BotW's Ryujinx folder - a
value repeated in two places is a value that will disagree with itself.

Exits non-zero and says so if the key is absent, rather than printing nothing
and letting the caller set an empty variable.
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

    if isinstance(node, (dict, list)):
        sys.stderr.write(
            "[target] '%s' is a %s, not a value\n"
            % (sys.argv[1], type(node).__name__))
        return 1

    print(node)
    return 0


if __name__ == "__main__":
    sys.exit(main())
