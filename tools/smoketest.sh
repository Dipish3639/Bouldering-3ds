#!/bin/sh
# Play through the game on a PC with a scripted player: tools/smoketest.sh
set -e
out=${TMPDIR:-/tmp}/bouldering3ds-smoketest
cc -std=gnu11 -g -O1 -Wall -fsanitize=address,undefined -Itools/stub -Isource \
   tools/smoketest.c source/beta.c source/gen.c source/render.c source/gfx.c source/climb.c source/route.c \
   source/font_data.c -lm -o "$out"
"$out"
