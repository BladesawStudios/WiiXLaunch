#!/bin/bash
# Linux equivalent of build_wiiu.bat.
#
# Requires wut + wut-tools from dkp-pacman, plus WUPS, libfunctionpatcher and
# libnotifications installed into $DEVKITPRO - run scripts/setup_wiiu_deps.sh
# once to build those from the vendor/ submodules.
set -e
cd "$(dirname "$0")"
source scripts/devkitpro_env.sh

echo "Generating config..."
python3 scripts/generate_config.py

# Output goes under build/<target>/, so hosts for two games never overwrite
# each other. The name comes from scripts/target.py.
TARGET_NAME="$(python3 scripts/target_value.py --name)"
OUT="build/$TARGET_NAME/wiiu"

# The plugin filename lives in exactly one place - wiiu.plugin_name in
# the active target in targets/ - and is read from there rather than repeated
# the Makefile's TARGET (passed on the command line below) and the copy step at
# the end; hardcoding it in either spot is how it drifts on a rename.
WPS_NAME=$(python3 scripts/target_value.py wiiu.plugin_name)
if [ -z "$WPS_NAME" ]; then
    echo "[WiiXLaunch] Could not read wiiu.plugin_name from this target"
    exit 1
fi
# Makefile's TARGET is the same name without the .wps extension
WPS_TARGET="${WPS_NAME%.wps}"

# devkitPro's make rules cannot handle spaces in paths, so stage the build in
# a temp dir (space-free) instead of building in-place. See scripts/wiiu/Makefile.
STAGE="${TMPDIR:-/tmp}/wiixlaunch-wiiu"
echo "Preparing Wii U build environment in $STAGE..."
rm -rf "$STAGE"
mkdir -p "$STAGE" "$STAGE/generated"
cp -r src "$STAGE/src"
cp -r include "$STAGE/include"
cp -r build/generated/include "$STAGE/generated/include"

# The game module(s) this target builds, as scripts/target.py decides - see
# build_cemu.bat. Only those are staged; scripts/wiiu/Makefile picks up
# modules/*/include, so staging is the selection.
MODULES="$(python3 scripts/target_modules.py)" || exit 1
for name in $MODULES; do
  mkdir -p "$STAGE/modules/$name" && cp -r "vendor/$name/include" "$STAGE/modules/$name/include"
done

cp scripts/wiiu/Makefile "$STAGE/Makefile"

echo "Building for Wii U (PowerPC)..."
make -C "$STAGE" TARGET="$WPS_TARGET"

mkdir -p "$OUT"
cp "$STAGE/$WPS_NAME" "$OUT/$WPS_NAME"

# THIS SCRIPT BUILDS ONE HOST FOR ONE GAME. That is all it does.
#
# It used to also run every gate, build the six example modules and call
# scripts/deploy.py. Three different jobs behind one command: you could not
# build a host without also publishing one, and a deploy writes the WHOLE mods
# directory, so building for one game could overwrite another game's modules.
#
#   gates and example modules -> test.sh
#   packaging and installing  -> python3 scripts/deploy.py --target <name>
echo "Wii U host built: $OUT/$WPS_NAME"
