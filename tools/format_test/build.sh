#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
python3 extract.py
g++ -O2 -std=c++17 -I. -o format_test main.cpp
echo
./format_test
