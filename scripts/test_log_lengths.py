#!/usr/bin/env python3
"""Refuses a WIIXL_LOG whose format string alone exceeds the per-line cap.

WIIXL_LOG truncates at kMaxLogTextLen (200) and used to do it silently, so a
message that ran over simply stopped mid-sentence and read like a message that
ended there. For a diagnostic that is the worst possible failure mode, because
the half that gets discarded is the half saying what to do about it. A boot cut
two patch refusals off at "not the game; the" and "would corrupt a func", and
neither line looked truncated.

Four messages were over the cap when this was written - two added that same day,
and two older ones including the PC-relative prologue refusal, which had never
been SEEN truncated only because that path is not taken on a normal boot. It
would have been, the first time it mattered.

THIS IS A LOWER BOUND, deliberately and unavoidably. The format string is the
shortest the output can be: every %u, %p and %02X expands to something at least
as long as the specifier it replaces, and a single %s can be arbitrarily long.
So a literal already over the cap is GUARANTEED to truncate, but staying under
it guarantees nothing. That is why include/wiixlaunch/debug_log.hpp also marks a
truncated line with [..CUT] at runtime - this gate catches what can be known at
build time, and the marker catches everything else where it happens.

Run by the build; no arguments.
"""

import io
import os
import re
import sys

CAP = 200

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(HERE)

# A floor, so a scan that stops finding calls fails instead of passing over
# nothing. Fourth rule in docs/framework/modules.md: a checker that examines zero things
# reports success exactly like one that examined everything.
EXPECTED_MIN_CALLS = 150

SKIP_DIRS = (".git", os.sep + "build", os.sep + "deploy", "__pycache__")

LIT = re.compile(r'"((?:[^"\\]|\\.)*)"')
CALL = re.compile(r"WIIXL_LOG\s*\(")


def format_string_of(call_text):
    """The leading run of adjacent string literals - the format string.

    Adjacent literals are concatenated by the compiler, which is how every
    message here is written; the run ends at the first comma outside a literal.
    """
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
