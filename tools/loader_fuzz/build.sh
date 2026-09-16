#!/usr/bin/env bash
# Exit codes are meaningful to the caller:
#   0  the fuzzer ran and passed
#   1  the fuzzer ran and FAILED, or would not compile
#   2  no C++ toolchain on this machine, so it did not run
#
# 2 is separated from 1 on purpose. A test that quietly vanishes on a machine
# without a toolchain is worse than no test, because the build still says OK -
# so the caller turns 2 into a loud warning rather than silence.
set -e
cd "$(dirname "$0")"

if ! command -v g++ >/dev/null 2>&1; then
    exit 2
fi

g++ -O1 -std=c++20 -Wall -I../../include -o loader_fuzz main.cpp || exit 1
./loader_fuzz
