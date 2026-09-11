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

# THIS SCRIPT BUILDS ONE HOST FOR ONE GAME. That is all it does.
#
# It used to also run every gate, build the six example modules and call
# scripts/deploy.py. Three different jobs behind one command: you could not
# build a host without also publishing one, and a deploy writes the WHOLE mods
# directory, so building for one game could overwrite another game's modules.
#
#   gates and example modules -> test.sh
#   packaging and installing  -> python3 scripts/deploy.py --target <name>
echo "Cemu host built: build/wiixlaunch_cemu"
