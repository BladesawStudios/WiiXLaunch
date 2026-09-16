#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
g++ -O2 -std=c++17 -I. -o nvn_swizzle_test main.cpp
echo
./nvn_swizzle_test
