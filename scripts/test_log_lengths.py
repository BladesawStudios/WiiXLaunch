#!/usr/bin/env python3
"""Check that WIIXL_LOG format strings do not exceed the per-line cap (kMaxLogTextLen = 200)."""

import io
import os
import re
import sys

CAP = 200

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(HERE)

# Minimum call floor to guard against broken scanning.
EXPECTED_MIN_CALLS = 150

SKIP_DIRS = (".git", os.sep + "build", os.sep + "deploy", "__pycache__")

LIT = re.compile(r'"((?:[^"\\]|\\.)*)"')
CALL = re.compile(r"WIIXL_LOG\s*\(")


def format_string_of(call_text):
    """Concatenate leading adjacent string literals in call_text."""
    parts = []
    pos = 0
    while True:
        m = LIT.search(call_text, pos)
        if not m:
            break
        between = call_text[pos:m.start()]
        if parts and "," in between:
            break
        parts.append(m.group(1))
        pos = m.end()
    return "".join(parts)


def main():
    hits = []
    scanned = 0
    files = 0

    for base, dirs, names in os.walk(ROOT):
        if any(sk in base for sk in SKIP_DIRS):
            continue
        dirs[:] = [d for d in dirs if d not in (".git", "build", "deploy", "__pycache__")]
        for name in names:
            if not name.endswith((".hpp", ".cpp")):
                continue
            path = os.path.join(base, name)
            text = io.open(path, encoding="utf-8", errors="replace").read()
            if "WIIXL_LOG" not in text:
                continue
            files += 1
            for m in CALL.finditer(text):
                depth = 0
                end = m.end() - 1
                for j in range(m.end() - 1, min(len(text), m.end() + 6000)):
                    if text[j] == "(":
                        depth += 1
                    elif text[j] == ")":
                        depth -= 1
                        if depth == 0:
                            end = j
                            break
                fmt = format_string_of(text[m.end() - 1:end])
                if not fmt:
                    continue
                scanned += 1
                # Count real characters, not source escapes.
                real = fmt.replace("\\n", "\n").replace('\\"', '"').replace("\\\\", "\\")
                if len(real) >= CAP:
                    line = text[:m.start()].count("\n") + 1
                    hits.append((len(real), os.path.relpath(path, ROOT), line, real[:70]))

    if scanned < EXPECTED_MIN_CALLS:
        sys.stderr.write(
            "\n[test_log_lengths] only %d WIIXL_LOG call(s) found across %d file(s), "
            "expected at least %d.\n"
            "  Either the scan is pointed at the wrong tree, or the matcher stopped\n"
            "  matching. A checker that examines nothing passes exactly like one that\n"
            "  examined everything.\n\n" % (scanned, files, EXPECTED_MIN_CALLS))
        return 1

    if hits:
        hits.sort(reverse=True)
        sys.stderr.write("\n[test_log_lengths] THESE LOG MESSAGES WILL BE TRUNCATED\n\n")
        for n, path, line, preview in hits:
            sys.stderr.write("  %4d chars  %s:%d\n      %s...\n"
                             % (n, path.replace(os.sep, "/"), line, preview))
        sys.stderr.write(
            "\n  WIIXL_LOG caps a line at %d characters, and the half that gets cut is\n"
            "  the half explaining what to do. Split the message into a verdict line\n"
            "  and a following explanation line, the way Arena does with\n"
            "  kSharedArenaNote.\n\n" % CAP)
        return 1

    print("[test_log_lengths] %d WIIXL_LOG call(s) across %d file(s), none over the "
          "%d-char cap" % (scanned, files, CAP))
    print("[test_log_lengths] this is a lower bound - one %s argument can be "
          "arbitrarily long, so")
    print("[test_log_lengths] debug_log.hpp also marks a truncated line [..CUT] at "
          "runtime")
    return 0


if __name__ == "__main__":
    sys.exit(main())
