#!/bin/sh
# Compiles the LaTeX written for the golden notes, to prove that it is LaTeX.
#     sh tests/latex.sh build/tnotes
# Needs pdflatex with amsart, hyperref and ulem (on Debian and Ubuntu: texlive-latex-base,
# texlive-latex-recommended, texlive-fonts-recommended, texlive-plain-generic).
# basics.md is left out: it has characters in its prose (such as U+2200) that pdflatex
# does not know without further packages. Its LaTeX is still compared in tests/golden.sh.
set -u
bin=$1
dir=$(cd "$(dirname "$0")" && pwd)/golden
command -v pdflatex >/dev/null || { echo "latex: pdflatex not found"; exit 2; }
tmp=$(mktemp -d)
fail=0
count=0
for name in math images theorems; do
    "$bin" --latex "$dir/$name.md" -o "$tmp/$name.tex" || { echo "latex $name: tnotes failed"; fail=1; continue; }
    # Twice, so that references to headings and equations are resolved.
    for pass in 1 2; do
        (cd "$tmp" && pdflatex -interaction=nonstopmode -halt-on-error "$name.tex" >"$name.stdout" 2>&1) || {
            echo "latex $name: pdflatex failed"
            grep -A6 '^!' "$tmp/$name.log" | head -20
            fail=1
            break
        }
    done
    [ -s "$tmp/$name.pdf" ] || { echo "latex $name: no PDF"; fail=1; }
    count=$((count + 1))
done
rm -rf "$tmp"
[ "$fail" = 0 ] && echo "latex: $count documents compile"
exit $fail
