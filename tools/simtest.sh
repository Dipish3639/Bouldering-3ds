#!/bin/sh
# Check on a PC that the built-in routes can be climbed: tools/simtest.sh [-v]
set -e
out=${TMPDIR:-/tmp}/bouldering3ds-simtest
cc -std=gnu11 -O2 -Wall -Isource tools/simtest.c source/climb.c source/route.c -lm -o "$out"
"$out" "$@"
