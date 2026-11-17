#!/bin/sh
# Golden files: each tests/golden/NAME.md must render to exactly tests/golden/NAME.html,
# its LaTeX must be exactly NAME.tex, and its outline exactly NAME.outline.
#     sh tests/golden.sh build/tnotes
# To accept new output after a deliberate change:  UPDATE=1 sh tests/golden.sh build/tnotes
set -u
bin=$1
dir=$(dirname "$0")/golden
fail=0
count=0
tmp=$(mktemp)
for md in "$dir"/*.md; do
    for kind in html tex outline; do
        want=${md%.md}.$kind
        case $kind in html) flag= ;; tex) flag=--latex ;; outline) flag=--outline ;; esac
        "$bin" $flag "$md" -o "$tmp" || { echo "golden $(basename "$md") ($kind): tnotes failed"; fail=1; continue; }
        if [ "${UPDATE:-0}" = 1 ]; then
            cp "$tmp" "$want"
        elif ! cmp -s "$tmp" "$want"; then
            echo "golden $(basename "$md") ($kind): output differs"
            diff "$want" "$tmp" | head -20
            fail=1
        fi
    done
    count=$((count + 1))
done
rm -f "$tmp"
[ "$fail" = 0 ] && echo "golden: $count notes ok (html, tex, outline)"
exit $fail
