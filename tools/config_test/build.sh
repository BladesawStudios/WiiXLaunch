#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
g++ -O2 -std=c++17 -I. -I../../include -o config_test main.cpp
echo
./config_test
