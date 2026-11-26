#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# B3+DRUM: rebuild firmware/src/drum_tab.h from the CB-931 kits in tools/cb931/ (on the computer).
set -e
cd "$(dirname "$0")/.."
mkdir -p build
cc -O1 -Wall -Itools/cb931 -o build/gen_drums tools/gen_drums.c tools/cb931/drums.c -lm
build/gen_drums > firmware/src/drum_tab.h
echo "firmware/src/drum_tab.h: $(wc -c < firmware/src/drum_tab.h) bytes"
