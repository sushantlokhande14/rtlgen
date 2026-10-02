#!/bin/sh
# Simulate the primitive library with iverilog; used by ctest.
set -e
rtlgen="$1"
dir=$(mktemp -d)
"$rtlgen" --prims > "$dir/prims.v"
iverilog -g2005 -o "$dir/tb" "$(dirname "$0")/tb_prims.v" "$dir/prims.v"
vvp -n "$dir/tb" | tee "$dir/out.txt"
grep -q '^PASS' "$dir/out.txt"
