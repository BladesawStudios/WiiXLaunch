#!/usr/bin/env python3
"""Asserts every build gate is actually WIRED IN and that its result is checked.

This is the fourth rule (docs/modules.md) applied to the build scripts:

    Every check must be able to fail. Where a check could pass because the
    thing it watches never ran, pair it with a positive assertion that the
    thing DID run.

Each gate now self-checks its own liveness - test_wxlm asserts it parsed a
non-zero number of static_asserts, loader_fuzz asserts a case-count floor and
that both halves of its containment property ran, format_test asserts a check
floor, test_host asserts readelf returned symbols. A gate that self-checks does
not need a second gate watching it, and that is the right shape: we have now
seen that a watcher is exactly as disarmable as the watched.

But there is one thing a gate CANNOT detect about itself: whether anything
invokes it. That is not a hypothetical. `tools/format_test` was written on
2026-09-03 in commit 73596ed, specifically so the WIIXL_LOG formatter would have
a real test - and no build script referenced it until 2026-09-04. It passed
every time it was run by hand and had never once run as part of a build. A gate
nothing calls is the limit case: it cannot fail, because it cannot execute.

So this file checks exactly the two properties a gate cannot check about itself:

  1. Every gate is invoked by every build script that should invoke it.
  2. That invocation's exit code is tested, so a failing gate fails the build.

Nothing else belongs here. Anything a gate can assert about itself, it should.

Run by the build; no arguments. Exit 0 means every gate is wired and guarded.
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# (script, [(gate token that must appear, human name)])
#
# Only build_cemu runs the gates: the Switch and Wii U builds do not produce a
# .wxlm, a Cemu host payload or a relocation table, so test_host/test_wxlm/
# loader_fuzz have nothing to say about them. format_test is arguably
# cross-platform - the formatter ships on all three - but it is run once per
# full build rather than three times, which build_all covers.
WIRING = [
    ("build_cemu.bat", [
        ("test_host.py",            "test_host"),
        ("test_wxlm.py",            "test_wxlm"),
        ("test_log_lengths.py",     "test_log_lengths"),
        ("format_test\\build.bat",  "format_test"),
        ("hook_test\\build.bat",    "hook_test"),
        ("loader_fuzz\\build.bat",  "loader_fuzz"),
        ("audit_gates.py",          "audit_gates (this file)"),
    ]),
    ("build_cemu.sh", [
        ("test_host.py",           "test_host"),
        ("test_wxlm.py",           "test_wxlm"),
        ("test_log_lengths.py",    "test_log_lengths"),
        ("format_test/build.sh",   "format_test"),
        ("hook_test/build.sh",     "hook_test"),
        ("loader_fuzz/build.sh",   "loader_fuzz"),
        ("audit_gates.py",         "audit_gates (this file)"),
    ]),
]

# Gates whose scripts must exist at all. A path that silently stops existing is
# the same failure one directory up.
# Floors on this file's OWN numbers. It reports three counts and floored none of
# them: deleting a row from WIRING would have dropped the count and still
# printed success. A gate that checks other gates for liveness and has none of
# its own is the joke writing itself.
EXPECTED_MIN_INVOCATIONS = 14
EXPECTED_MIN_SCRIPTS = 14

MUST_EXIST = [
    "scripts/test_host.py",
    "scripts/test_wxlm.py",
    "scripts/test_log_lengths.py",
    "scripts/wxlm.py",
    "tools/format_test/build.bat",
    "tools/format_test/build.sh",
    "tools/format_test/extract.py",
    "tools/format_test/main.cpp",
    "tools/hook_test/build.bat",
    "tools/hook_test/build.sh",
    "tools/hook_test/main.cpp",
    "tools/loader_fuzz/build.bat",
    "tools/loader_fuzz/build.sh",
    "tools/loader_fuzz/main.cpp",
]


def code_lines(text, comment):
    """Lines with the comment lines removed, paired with their index.

    Comments must not count as invocations. This checker got that wrong on its
    first run: its own explanatory comment in build_cemu.bat mentioned
    scripts/audit_gates.py, so the file appeared to invoke a gate it did not
    call. A substring search over a whole file finds prose as readily as code.
    """
    out = []
    for i, line in enumerate(text.replace("\r\n", "\n").split("\n")):
        if line.strip().startswith(comment):
            continue
        out.append((i, line))
    return out


def invokes(text, token, comment):
    return any(token.lower() in line.lower() for _i, line in code_lines(text, comment))


def guarded_bat(text, token):
    """Is the line invoking `token` followed by an errorlevel test?

    cmd has no `set -e`; a gate whose exit code nobody reads is wired in and
    still cannot fail the build.
    """
    lines = text.replace("\r\n", "\n").split("\n")
    for i, line in code_lines(text, "::"):
        if token.lower() not in line.lower():
            continue
        for follow in lines[i + 1:i + 6]:
            # Both cmd idioms count. "if errorlevel N" means "errorlevel >= N",
            # which is FALSE for the negative value a crashing process leaves -
            # so the codebase moved to an explicit NEQ 0 test. This recognises
            # the old form too, because a guard that only knows one spelling
            # reports a guarded gate as unguarded, which is what it did the
            # moment the sweep landed.
            if re.search(r"if\s+errorlevel\s+[12]", follow, re.I):
                return True
            if re.search(r"if\s+%ERRORLEVEL%\s+NEQ\s+0", follow, re.I):
                return True
        return False
    return False


def guarded_sh(text, token):
    """Bash builds run under `set -e`, so a bare invocation already aborts.

    An invocation inside a `set +e` region must test the captured status
    instead.

    This tracks the actual errexit state by scanning from the top of the file,
    rather than looking for "set +e" inside a window around the invocation. Two
    window sizes were tried and both were wrong in opposite directions: a small
    one could not see loader_fuzz's `exit 1` ten lines below its banner and
    reported a false failure; a large one saw loader_fuzz's `set +e` from
    test_wxlm six lines above and reported a different false failure. A window
    approximates the property; the state IS the property.
    """
    lines = text.replace("\r\n", "\n").split("\n")
    code = dict(code_lines(text, "#"))

    errexit = False
    for i, raw in enumerate(lines):
        if i in code:
            stripped = code[i].strip()
            if stripped == "set -e":
                errexit = True
            elif stripped == "set +e":
                errexit = False
            elif token in code[i]:
                if errexit:
                    return True              # a failure aborts the script
                # errexit is off here, so the status has to be captured and
                # acted on explicitly. Look forward to the matching `set -e`.
                rest = []
                for j in range(i + 1, len(lines)):
                    rest.append(lines[j])
                    if lines[j].strip() == "set -e" and len(rest) > 1:
                        rest.extend(lines[j + 1:j + 15])
                        break
                window = "\n".join(rest)
                return bool(re.search(r"(_RC|\$\?)", window)) and "exit 1" in window
    return False


def main():
    failures = []
    checked_invocations = 0
    checked_guards = 0

    for name in MUST_EXIST:
        if not os.path.exists(os.path.join(ROOT, name)):
            failures.append("  %s does not exist - a gate script has been removed "
                            "or moved" % name)

    for script, gates in WIRING:
        path = os.path.join(ROOT, script)
        if not os.path.exists(path):
            failures.append("  %s does not exist" % script)
            continue
        text = open(path, encoding="utf-8", errors="replace").read()
        is_bat = script.endswith(".bat")
        guarded = guarded_bat if is_bat else guarded_sh
        comment = "::" if is_bat else "#"

        for token, human in gates:
            checked_invocations += 1
            if not invokes(text, token, comment):
                failures.append(
                    "  %s does not run %s.\n"
                    "           A gate nothing invokes cannot fail. This is exactly how\n"
                    "           tools/format_test sat unrun from 73596ed until 2026-09-04."
                    % (script, human))
                continue
            checked_guards += 1
            if not guarded(text, token):
                failures.append(
                    "  %s runs %s but does not check its exit code.\n"
                    "           A gate that cannot fail the build is decoration."
                    % (script, human))

    # The split, not just the sum: every invocation found must also be guarded,
    # and the tables must not have shrunk.
    if checked_invocations < EXPECTED_MIN_INVOCATIONS:
        failures.append(
            "  only %d gate invocations were checked, expected at least %d - the\n"
            "           WIRING table has shrunk, so this file is watching less than\n"
            "           it was." % (checked_invocations, EXPECTED_MIN_INVOCATIONS))
    if checked_guards != checked_invocations:
        failures.append(
            "  %d invocations found but only %d had their exit code examined"
            % (checked_invocations, checked_guards))
    if len(MUST_EXIST) < EXPECTED_MIN_SCRIPTS:
        failures.append(
            "  MUST_EXIST lists only %d gate scripts, expected at least %d"
            % (len(MUST_EXIST), EXPECTED_MIN_SCRIPTS))

    if failures:
        sys.stderr.write(
            "\n[audit_gates] A BUILD GATE IS NOT WIRED IN\n\n"
            + "\n".join(failures) + "\n\n"
            "  Each gate self-checks its own liveness; this checks the one thing a\n"
            "  gate cannot check about itself - that something calls it, and that a\n"
            "  failure is fatal. See the fourth rule in docs/modules.md.\n\n")
        return 1

    print("[audit_gates] %d gate invocations across %d build scripts, %d exit codes "
          "guarded, %d gate scripts present"
          % (checked_invocations, len(WIRING), checked_guards, len(MUST_EXIST)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
