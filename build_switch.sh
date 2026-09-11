#!/bin/bash
# Linux equivalent of build_switch.bat.
set -e
cd "$(dirname "$0")"
source scripts/devkitpro_env.sh

echo "Generating config..."
python3 scripts/generate_config.py

# devkitPro's make rules cannot handle spaces in paths, so stage the build in
# a temp dir (space-free) instead of building in-place.
STAGE="${TMPDIR:-/tmp}/wiixlaunch-switch"
echo "Preparing Switch build environment in $STAGE..."
rm -rf "$STAGE"
mkdir -p "$STAGE"

# Copy exlaunch template and generated configs into the stage
cp -r vendor/exlaunch/. "$STAGE/"
cp build/generated/switch/config.json "$STAGE/config.json"
cp build/generated/switch/config.mk "$STAGE/config.mk"

# Copy our source files into the exlaunch source tree
mkdir -p "$STAGE/source/wiixlaunch"
cp -r src/. "$STAGE/source/wiixlaunch/"

# Delete exlaunch template main.cpp to avoid multiple definition conflict with our src/main.cpp
rm -f "$STAGE/source/program/main.cpp"

# Fix GCC anonymous struct typedef error in exlaunch
sed -i 's/typedef struct {/struct context {/; s/} context;/};/' "$STAGE/source/lib/hook/nx64/hook_impl.cpp"

echo "Building for Switch (ARM64)..."
make -C "$STAGE"

# Extract artifacts
mkdir -p build/switch
cp "$STAGE/deploy/subsdk9" build/switch/subsdk9
cp "$STAGE/deploy/main.npdm" build/switch/main.npdm

# THIS SCRIPT BUILDS ONE HOST FOR ONE GAME. That is all it does.
#
# It used to also run every gate, build the six example modules and call
# scripts/deploy.py. Three different jobs behind one command: you could not
# build a host without also publishing one, and a deploy writes the WHOLE mods
# directory, so building for one game could overwrite another game's modules.
#
#   gates and example modules -> test.sh
#   packaging and installing  -> python3 scripts/deploy.py --target <name>

# The linked ELF, kept beside the artifacts: test.sh's test_switch_module
# gate reads it to check which nn:: symbols the host imports, and the build
# stage it was linked in is deleted by the next build.
cp "$STAGE/wiixlaunch-switch.elf" build/switch/wiixlaunch-switch.elf
echo "Switch host built: build/switch/subsdk9"
