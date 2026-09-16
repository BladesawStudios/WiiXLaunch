#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
g++ -O2 -std=c++17 -I. -o mathtest main.cpp
echo
./mathtest
