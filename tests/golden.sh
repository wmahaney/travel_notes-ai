#!/bin/sh
# Golden files: each tests/golden/NAME.md must render to exactly tests/golden/NAME.html.
#     sh tests/golden.sh build/tnotes
# To accept new output after a deliberate change:  UPDATE=1 sh tests/golden.sh build/tnotes
set -u
bin=$1
dir=$(dirname "$0")/golden
fail=0
count=0
tmp=$(mktemp)
for md in "$dir"/*.md; do
    want=${md%.md}.html
    "$bin" "$md" -o "$tmp" || { echo "golden $(basename "$md"): tnotes failed"; fail=1; continue; }
    if [ "${UPDATE:-0}" = 1 ]; then
        cp "$tmp" "$want"
    elif ! cmp -s "$tmp" "$want"; then
        echo "golden $(basename "$md"): output differs"
        diff "$want" "$tmp" | head -20
        fail=1
    fi
    count=$((count + 1))
done
rm -f "$tmp"
[ "$fail" = 0 ] && echo "golden: $count files ok"
exit $fail
