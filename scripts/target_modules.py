#!/usr/bin/env python3
"""Prints the game modules the active target builds, one name per line.

    python scripts/target_modules.py          # wiixlaunch-botw
    WIIXL_TARGET=totk python scripts/target_modules.py

For build scripts, which turn each name into vendor/<name>/include. Every
script used to glob vendor/wiixlaunch-* itself, which was fine with one game
module in the tree and a coin flip with two: each provides
<wiixlaunch/module.hpp>, so include order decided which game a host was built
for. The choice now lives in one place, scripts/target.py, and every build
asks it.

Prints nothing for a target that builds no game module. Exits non-zero, with
the reason on stderr, for a module named and missing or an ambiguous tree - a
.bat cannot see the exit code of a command inside `for /f`, so callers run
this once on its own first and check it there.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import target as target_mod


def main():
    root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    _, cfg = target_mod.resolve(root_dir, None, quiet=True)
    _, chosen, _ = target_mod.module_includes(root_dir, cfg)
    for name in chosen:
        print(name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
