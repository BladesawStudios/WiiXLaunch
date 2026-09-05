#!/bin/bash
# Linux equivalent of build_cemu.bat: one direct devkitPPC compile+link, no
# CMake. No -g: the payload ships as a raw binary, and debug sections would
# add .rela.debug_* entries the deploy-time relocator must ignore.
set -e
cd "$(dirname "$0")"
source scripts/devkitpro_env.sh

echo "Generating config..."
python3 scripts/generate_config.py

mkdir -p build

# Optional WiiXLaunch modules (e.g. vendor/wiixlaunch-botw) - not part of
# base WiiXLaunch, picked up automatically if this mod added one as a
# submodule (git submodule add <url> vendor/wiixlaunch-<name>).
MODULE_FLAGS=()
for d in vendor/wiixlaunch-*/; do
  [ -d "${d}include" ] && MODULE_FLAGS+=(-I "${d}include")
done

echo "Building Cemu payload (PowerPC)..."
# -fno-pie -fno-pic, NOT -fPIE: see build_cemu.bat for the full explanation -
# on this machine's devkitPPC (GCC 16.1.0), -fPIE emits GOT-indirect
# (.got2 + R_PPC_REL32) addressing that deploy.py's relocation patching
# doesn't handle, confirmed via two independent reproducible crashes and a
# clean build of the sibling repo showing the same pattern on this toolchain.
"$DKP_PPC_GXX" \
  -std=gnu++20 -fno-pie -fno-pic -msdata=none \
  -D__CEMU__=1 -DWIIXL_CEMU=1 \
  -I include -I build/generated/include "${MODULE_FLAGS[@]}" \
  -nostartfiles -T scripts/cemu.ld -Wl,-q \
  src/main.cpp src/wiiu_plugin.cpp src/cemu/bootstrap.cpp \
  -o build/wiixlaunch_cemu

# Host-completeness check - see scripts/test_host.py. Links the same host from
# an EMPTY main.cpp and asserts it is still complete, because main.cpp becomes a
# .wxlm at stage 4 and nothing the host needs may come from it.
: > build/empty_main.cpp
"$DKP_PPC_GXX" \
  -std=gnu++20 -fno-pie -fno-pic -msdata=none \
  -D__CEMU__=1 -DWIIXL_CEMU=1 \
  -I include -I build/generated/include "${MODULE_FLAGS[@]}" \
  -nostartfiles -T scripts/cemu.ld -Wl,-q \
  build/empty_main.cpp src/wiiu_plugin.cpp src/cemu/bootstrap.cpp \
  -o build/wiixlaunch_cemu_hosttest
python3 scripts/test_host.py build/wiixlaunch_cemu_hosttest

# The .wxlm writer and the format header have to agree; a drift between them
# is the one failure neither side can detect at runtime.
python3 scripts/test_wxlm.py

# No WIIXL_LOG line may exceed the 200-char cap; truncation used to be silent.
python3 scripts/test_log_lengths.py

# Are the gates below actually wired in, and is a failure fatal? Every gate
# self-checks its own liveness, which is the right shape - but no gate can
# detect that nothing calls it. Runs first, so a missing gate is reported
# before the build spends time on the ones that are present.
python3 scripts/audit_gates.py

# WIIXL_LOG's formatter. Every platform's logging goes through it and it cannot
# be exercised on a console. NOT WIRED IN UNTIL 2026-09-04 - written, passing
# when run by hand, and never called by a build script, so it could not fail at
# all. A gate nothing invokes is the limit case of the fourth rule in
# docs/modules.md, and the one thing a gate cannot detect about itself; see
# scripts/audit_gates.py.
bash tools/format_test/build.sh

# The central hook manager: a three-deep chain verified by decoding the
# instructions it emitted. Construction, not execution - the boot proves that.
bash tools/hook_test/build.sh

# Fuzz the loader. Runs on every build rather than on request - a check that
# has to be remembered is a check that stops happening.
#
# A MISSING TOOLCHAIN IS A FAILURE, NOT A WARNING. It used to print a banner and
# let the build succeed; "skipped" is a state that has to be seen, and a banner
# scrolls past. No gate exits 0 on a missing input.
set +e
bash tools/loader_fuzz/build.sh
FUZZ_RC=$?
set -e
if [ $FUZZ_RC -eq 2 ]; then
    echo
    echo ============================================================
    echo "[loader_fuzz] SETUP PROBLEM - not a broken source tree."
    echo "[loader_fuzz] This gate requires a host C++ compiler, which was not"
    echo "[loader_fuzz] found. Install one, or build on a machine that has it."
    echo "[loader_fuzz] The loader was NOT fuzzed, so this build FAILS rather"
    echo "[loader_fuzz] than shipping an untested loader."
    echo ============================================================
    echo
    exit 1
elif [ $FUZZ_RC -ne 0 ]; then
    echo "[loader_fuzz] FAILED - see above."
    exit 1
fi

python3 scripts/deploy.py
echo "Cemu build complete!"
