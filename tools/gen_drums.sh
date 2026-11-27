#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# B3+DRUM: rebuild firmware/src/drum_tab.h (the kits) and drum_styles.h (the rhythms) from CB-931's in tools/cb931/.
set -e
cd "$(dirname "$0")/.."
mkdir -p build
cc -O1 -Wall -Itools/cb931 -o build/gen_drums tools/gen_drums.c tools/cb931/drums.c -lm
build/gen_drums > firmware/src/drum_tab.h
echo "firmware/src/drum_tab.h: $(wc -c < firmware/src/drum_tab.h) bytes"
cc -O1 -Wall -Itools/cb931 -o build/gen_styles tools/gen_styles.c tools/cb931/factory_styles.c -lm
build/gen_styles > firmware/src/drum_styles.h
echo "firmware/src/drum_styles.h: $(wc -c < firmware/src/drum_styles.h) bytes"
