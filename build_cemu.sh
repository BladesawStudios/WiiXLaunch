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

# Fuzz the loader. Runs on every build rather than on request - a check that
# has to be remembered is a check that stops happening. Exit 2 means no
# toolchain, which is a loud warning rather than a silent pass.
set +e
bash tools/loader_fuzz/build.sh
FUZZ_RC=$?
set -e
if [ $FUZZ_RC -eq 2 ]; then
    echo
    echo ============================================================
    echo "[loader_fuzz] NOT RUN - no C++ toolchain found on this machine."
    echo "[loader_fuzz] The loader was NOT fuzzed for this build."
    echo ============================================================
    echo
elif [ $FUZZ_RC -ne 0 ]; then
    echo "[loader_fuzz] FAILED - see above."
    exit 1
fi

python3 scripts/deploy.py
echo "Cemu build complete!"
