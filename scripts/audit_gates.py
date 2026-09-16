#!/usr/bin/env python3
"""Asserts every build gate is wired into test runners and checked for failure."""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# (script, [(gate token that must appear, human name)])
WIRING = [
    ("test.bat", [
        ("test_host.py",            "test_host"),
        ("test_wxlm.py",            "test_wxlm"),
        ("test_patch_decl.py",      "test_patch_decl"),
        ("test_log_lengths.py",     "test_log_lengths"),
        ("format_test\\build.bat",  "format_test"),
        ("mathtest\\build.bat",     "mathtest"),
        ("nvn_swizzle_test\\build.bat", "nvn_swizzle_test"),
        ("config_test\\build.bat",  "config_test"),
        ("hook_test\\build.bat",    "hook_test"),
        ("net_test\\build.bat",     "net_test"),
        ("loader_fuzz\\build.bat",  "loader_fuzz"),
        ("surface_coverage.py",     "surface_coverage"),
        ("gen_imports.py",          "gen_imports --check"),
        ("make_sdk.py",             "make_sdk --verify"),
        ("audit_gates.py",          "audit_gates (this file)"),
        ("test_switch_module.py",   "test_switch_module"),
    ]),
    ("test.sh", [
        ("test_host.py",           "test_host"),
        ("test_wxlm.py",           "test_wxlm"),
        ("test_patch_decl.py",     "test_patch_decl"),
        ("test_log_lengths.py",    "test_log_lengths"),
        ("format_test/build.sh",   "format_test"),
        ("mathtest/build.sh",      "mathtest"),
        ("nvn_swizzle_test/build.sh", "nvn_swizzle_test"),
        ("config_test/build.sh",   "config_test"),
        ("hook_test/build.sh",     "hook_test"),
        ("net_test/build.sh",      "net_test"),
        ("loader_fuzz/build.sh",   "loader_fuzz"),
        ("surface_coverage.py",    "surface_coverage"),
        ("gen_imports.py",         "gen_imports --check"),
        ("make_sdk.py",            "make_sdk --verify"),
        ("audit_gates.py",         "audit_gates (this file)"),
        ("test_switch_module.py",  "test_switch_module"),
    ]),
]

# Minimum count floors to guard against accidental deletion of entries.
EXPECTED_MIN_INVOCATIONS = 35
EXPECTED_MIN_SCRIPTS = 32

MUST_EXIST = [
    "scripts/test_host.py",
    "scripts/test_wxlm.py",
    "scripts/test_patch_decl.py",
    "scripts/test_switch_module.py",
    "scripts/aarch64_relocs.py",
    "scripts/test_log_lengths.py",
    "scripts/surface_coverage.py",
    "scripts/gen_imports.py",
    "scripts/make_sdk.py",
    "scripts/wxlm.py",
    "tools/format_test/build.bat",
    "tools/format_test/build.sh",
    "tools/format_test/extract.py",
    "tools/format_test/main.cpp",
    "tools/mathtest/build.bat",
    "tools/mathtest/build.sh",
    "tools/mathtest/main.cpp",
    "tools/nvn_swizzle_test/build.bat",
    "tools/nvn_swizzle_test/build.sh",
    "tools/nvn_swizzle_test/main.cpp",
    "tools/config_test/build.bat",
    "tools/config_test/build.sh",
    "tools/config_test/main.cpp",
    "tools/hook_test/build.bat",
    "tools/hook_test/build.sh",
    "tools/hook_test/main.cpp",
    "tools/net_test/build.bat",
    "tools/net_test/build.sh",
    "tools/net_test/main.cpp",
    "tools/loader_fuzz/build.bat",
    "tools/loader_fuzz/build.sh",
    "tools/loader_fuzz/main.cpp",
]


def code_lines(text, comment):
    """Lines with comment lines removed, paired with their index."""
    out = []
    for i, line in enumerate(text.replace("\r\n", "\n").split("\n")):
        if line.strip().startswith(comment):
            continue
        out.append((i, line))
    return out


def invokes(text, token, comment):
    return any(token.lower() in line.lower() for _i, line in code_lines(text, comment))


def guarded_bat(text, token):
    """Check if the line invoking `token` is followed by an errorlevel check."""
    lines = text.replace("\r\n", "\n").split("\n")
    for i, line in code_lines(text, "::"):
        if token.lower() not in line.lower():
            continue
        for follow in lines[i + 1:i + 6]:
            if re.search(r"if\s+errorlevel\s+[12]", follow, re.I):
                return True
            if re.search(r"if\s+%ERRORLEVEL%\s+NEQ\s+0", follow, re.I):
                return True
        return False
    return False


def guarded_sh(text, token):
    """Check if token invocation is guarded by errexit or explicit status check."""
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
                    return True
                rest = []
                for j in range(i + 1, len(lines)):
                    rest.append(lines[j])
                    if lines[j].strip() == "set -e" and len(rest) > 1:
                        rest.extend(lines[j + 1:j + 15])
                        break
                window = "\n".join(rest)
                return bool(re.search(r"(_RC|\$\?)", window)) and "exit 1" in window
    return False


# Scripts that call other scripts, and must do it by a path rather than by bare
# name.
#
# `call build_switch.bat` relies on cmd searching the CURRENT DIRECTORY for the
# command, and that search is disabled on any machine with
# NoDefaultCurrentDirectoryInExePath set. On such a machine build_all.bat died
# on its first line, which means it had never once run to completion there -
# while all three individual scripts passed. Those are different claims, and the
# gap between them is the same shape as tools/format_test sitting unwired: a
# thing that looks like it runs and does not.
#
# The failing form and the working form differ by two characters, so this is
# checked rather than remembered.
CALLERS = [
    ("build_all.bat", ["build_switch.bat", "build_wiiu.bat", "build_cemu.bat"]),
]

BARE_CALL = re.compile(r"^[ \t]*call[ \t]+(?![\"']?%~dp0)(?![\"']?[.\\/])([A-Za-z0-9_.-]+\.bat)",
                       re.MULTILINE)


def bare_invocations(text):
    """Sub-script calls that go through cmd's current-directory search."""
    return [m.group(1) for m in BARE_CALL.finditer(text)]


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

    # Ensure scripts invoked by caller scripts are referenced with %~dp0 paths.
    for script, expected in CALLERS:
        path = os.path.join(ROOT, script)
        if not os.path.exists(path):
            failures.append("  %s does not exist" % script)
            continue
        text = open(path, encoding="utf-8", errors="replace").read()

        bare = bare_invocations(text)
        if bare:
            failures.append(
                "  %s calls %s by bare name.\n"
                "           cmd resolves that through the CURRENT DIRECTORY, and that\n"
                "           search is disabled wherever NoDefaultCurrentDirectoryInExePath\n"
                "           is set - so the script fails before it builds anything.\n"
                "           Write it as call \"%%~dp0<name>.bat\"."
                % (script, ", ".join(bare)))

        for name in expected:
            checked_invocations += 1
            if name not in text:
                failures.append("  %s no longer calls %s at all" % (script, name))
            else:
                checked_guards += 1

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
            "  failure is fatal. See the fourth rule in docs/framework/modules.md.\n\n")
        return 1

    print("[audit_gates] %d gate invocations across %d build scripts, %d exit codes "
          "guarded, %d gate scripts present"
          % (checked_invocations, len(WIRING), checked_guards, len(MUST_EXIST)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
