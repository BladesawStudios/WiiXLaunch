#!/bin/bash
# EVERY GATE, AND THE EXAMPLE MODULES. Linux equivalent of test.bat.
#
#   ./test.sh            the default target
#   ./test.sh totk       targets/totk.json
#
# These used to live inside build_cemu.sh and build_switch.sh, which meant
# "build a host" and "verify the tree" were the same command and neither could
# be had without the other. A build script builds one host for one game now and
# does nothing else - no gates, no example modules, no deploy, and nothing
# copied into an emulator. This is the other half.
#
# scripts/audit_gates.py checks that every gate below is still invoked FROM
# HERE, so moving a gate out of this file is a failure rather than a quiet loss
# of coverage. That check is why this is one script and not a list in a README.
set -e
cd "$(dirname "$0")"

[ -n "$1" ] && export WIIXL_TARGET="$1"
: "${WIIXL_TARGET:=botw}"

source scripts/devkitpro_env.sh

# The gates compile against build/generated/include, so the config has to exist
# and has to be this target's.
echo "Generating config..."
python3 scripts/generate_config.py

mkdir -p build
# Module resources are staged fresh; a directory left from a module that has
# been renamed or removed would otherwise sit in build/moddata forever and
# silently join the next deploy.
rm -rf build/moddata

# Optional WiiXLaunch modules (e.g. vendor/wiixlaunch-botw) - not part of
# base WiiXLaunch, picked up automatically if this mod added one as a
# submodule (git submodule add <url> vendor/wiixlaunch-<name>).
MODULE_FLAGS=()
for d in vendor/wiixlaunch-*/; do
  [ -d "${d}include" ] && MODULE_FLAGS+=(-I "${d}include")
done

# Are the gates below actually wired in, and is a failure fatal? Every gate
# self-checks its own liveness, which is the right shape - but no gate can
# detect that nothing calls it. Runs first, so a missing gate is reported
# before the run spends time on the ones that are present.
python3 scripts/audit_gates.py

# Host-completeness check - see scripts/test_host.py. Links the same host from
# an EMPTY main.cpp and asserts it is still complete, because main.cpp becomes a
# .wxlm at stage 4 and nothing the host needs may come from it. Compiled here
# rather than in Python because these flags live with the gate that needs them;
# the shipping payload is built by build_cemu.sh.
: > build/empty_main.cpp
"$DKP_PPC_GXX" \
  -std=gnu++20 -fno-pie -fno-pic -msdata=none \
  -D__CEMU__=1 -DWIIXL_CEMU=1 \
  -I include -I build/generated/include "${MODULE_FLAGS[@]}" \
  -nostartfiles -T scripts/cemu.ld -Wl,-q \
  build/empty_main.cpp src/wiiu_plugin.cpp src/cemu/bootstrap.cpp \
  -o build/wiixlaunch_cemu_hosttest
python3 scripts/test_host.py build/wiixlaunch_cemu_hosttest

# WIIXL_DECLARE_PATCH_CROSS picks an architecture with the preprocessor, and a
# macro that picked wrong would emit the other machine's bytes against the other
# kind of address - a module that builds, packs, loads and is refused at boot on
# somebody else's console. Compiles a fixture with both toolchains and reads the
# emitted record back out of each ELF.
python3 scripts/test_patch_decl.py

# The .wxlm writer and the format header have to agree; a drift between them
# is the one failure neither side can detect at runtime.
python3 scripts/test_wxlm.py

# No WIIXL_LOG line may exceed the 200-char cap; truncation used to be silent.
python3 scripts/test_log_lengths.py

# Surface coverage: every public entry point in the module either has a surface
# symbol or an entry in EXCLUDED with a reason. See scripts/surface_coverage.py.
python3 scripts/surface_coverage.py

# The generated import headers must match the surfaces they came from.
python3 scripts/gen_imports.py --check

# The committed SDK matches the surfaces, and a module built from it alone is
# byte-identical to one built from the tree.
python3 scripts/make_sdk.py --check --verify

# WIIXL_LOG's formatter. Every platform's logging goes through it and it cannot
# be exercised on a console. NOT WIRED IN UNTIL 2026-09-04 - written, passing
# when run by hand, and never called by a build script, so it could not fail at
# all. A gate nothing invokes is the limit case of the fourth rule in
# docs/modules.md, and the one thing a gate cannot detect about itself.
bash tools/format_test/build.sh

# sqrt, sin and cos for modules: a .wxlm has no libm, so mod_math.h writes them
# out, and an approximation nobody measured is a wrong answer with good manners.
# A million points against the host's libm, asserting the bounds the header
# quotes. It caught a 1.7e-6 error in cos at large angles that reading could not.
bash tools/mathtest/build.sh

# The NVN block-linear swizzle, checked against a texture NVN has actually
# accepted rather than against a restatement of the rules.
bash tools/nvn_swizzle_test/build.sh

# mod_config.h. Hand-written settings files, and the ways they go wrong:
# prefix keys, CRLF, words where numbers go, no trailing newline.
bash tools/config_test/build.sh

# The central hook manager: a three-deep chain verified by decoding the
# instructions it emitted. Construction, not execution - the boot proves that.
bash tools/hook_test/build.sh

# Socket ownership. The fake transport underneath RECYCLES file descriptors,
# because a use-after-close only becomes cross-mod corruption once the number
# has been handed to somebody else - and no real platform will do that on cue.
bash tools/net_test/build.sh

# Fuzz the loader. Runs on every pass rather than on request - a check that
# has to be remembered is a check that stops happening.
#
# A MISSING TOOLCHAIN IS A FAILURE, NOT A WARNING. "skipped" is a state that has
# to be seen, and a banner scrolls past. No gate exits 0 on a missing input.
set +e
bash tools/loader_fuzz/build.sh
FUZZ_RC=$?
set -e
if [ $FUZZ_RC -eq 2 ]; then
    echo
    echo ============================================================
    echo "[loader_fuzz] SETUP PROBLEM - not a broken source tree."
    echo "[loader_fuzz] This gate requires a host C++ compiler, which was not"
    echo "[loader_fuzz] found. Install one, or run on a machine that has it."
    echo "[loader_fuzz] The loader was NOT fuzzed, so this run FAILS rather"
    echo "[loader_fuzz] than reporting an untested loader as passing."
    echo ============================================================
    echo
    exit 1
elif [ $FUZZ_RC -ne 0 ]; then
    echo "[loader_fuzz] FAILED - see above."
    exit 1
fi

# Can a MODULE be built for AArch64? The host building says nothing about that -
# it was true for months while wxlm.py wrote MACHINE_PPC32 into every file it
# produced.
#
# Needs the Switch host ELF that build_switch.sh leaves in build/switch. A
# MISSING ONE IS A FAILURE: "no host built yet" and "the host is fine" are
# different answers and a skip makes them look the same.
if [ ! -f build/switch/wiixlaunch-switch.elf ]; then
    echo "[test_switch_module] build/switch/wiixlaunch-switch.elf is missing."
    echo "[test_switch_module] Run ./build_switch.sh $WIIXL_TARGET first - this gate"
    echo "[test_switch_module] reads the host ELF to check the nn:: symbols it imports."
    exit 1
fi
python3 scripts/test_switch_module.py build/switch/wiixlaunch-switch.elf

# --- the example modules ---------------------------------------------------
# They exist to be EXERCISED: the two hook mods hook the same address on
# purpose, so load order, call order and the conflict line all land in one boot.
# Filenames decide load order - a_first sorts before b_second.
#
# Built for both machine types, because a module is a different binary per
# target and "the samples pass" used to be a statement about PowerPC only.
# Building them is a check on build_mod.py, not a step towards shipping: no
# build script builds them and no build script deploys them.
if [ "$(python3 scripts/target_value.py samples)" = "0" ]; then
    echo "[WiiXLaunch] this target does not carry the example mods - skipping"
else
    for mod in sample_mod hook_mod_a hook_mod_b patch_mod net_mod player_mod; do
        python3 scripts/build_mod.py --source "examples/$mod"
        python3 scripts/build_mod.py --source "examples/$mod" \
            --target switch --out "build/$WIIXL_TARGET"
    done
fi

echo
echo "All gates passed."
