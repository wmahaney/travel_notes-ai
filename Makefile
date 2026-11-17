# travel_notes: one Makefile for everything.
#
#   make              native build: build/libtnotes.a and build/tnotes
#   make test         unit tests, golden files, hostile inputs
#   make asan         the same tests under AddressSanitizer and UBSan
#   make fuzz-smoke   a short mutation fuzz run of the renderer, under the sanitizers
#   make latex-test   compile the LaTeX written for the golden notes (needs pdflatex)
#   make web          the site, into build/web/ (needs Emscripten: emcc)
#   make serve        build the site and serve it at http://localhost:8000
#   make browser-test load the built site in headless Chrome (needs Node and Chrome)
#   make clean

CC      ?= cc
EMCC    ?= emcc
CFLAGS  ?= -O2 -g
WARN    := -Wall -Wextra -Wpedantic -Wshadow
STD     := -std=c11
INC     := -Iinclude -Ithird_party/md4c
SAN     := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
B       := build
PORT    ?= 8000

CORE_SRC := src/tn_buf.c src/tn_render.c src/tn_latex.c
CORE_OBJ := $(CORE_SRC:src/%.c=$(B)/%.o)
MD4C     := third_party/md4c/md4c.c
HEADERS  := include/tnotes.h src/tn_internal.h third_party/md4c/md4c.h
WEB_SRC  := web/index.html web/style.css web/app.js web/theme.js

all: $(B)/libtnotes.a $(B)/tnotes

$(B):
	mkdir -p $(B)

$(B)/%.o: src/%.c $(HEADERS) | $(B)
	$(CC) $(STD) $(CFLAGS) $(WARN) $(INC) -c $< -o $@

# md4c is vendored: compiled as it is, without our warning flags.
$(B)/md4c.o: $(MD4C) third_party/md4c/md4c.h | $(B)
	$(CC) $(STD) $(CFLAGS) -w -Ithird_party/md4c -c $< -o $@

$(B)/libtnotes.a: $(CORE_OBJ) $(B)/md4c.o
	ar rcs $@ $^

$(B)/tnotes: cli/main.c $(B)/libtnotes.a
	$(CC) $(STD) $(CFLAGS) $(WARN) $(INC) $< $(B)/libtnotes.a -o $@

# ---- tests ---------------------------------------------------------------------

$(B)/test_core: tests/test_core.c tests/tn_check.h $(B)/libtnotes.a
	$(CC) $(STD) $(CFLAGS) $(WARN) $(INC) -Isrc $< $(B)/libtnotes.a -o $@

test: $(B)/tnotes $(B)/test_core
	$(B)/test_core tests/hostile
	sh tests/golden.sh $(B)/tnotes

# The whole suite again, with the core and md4c built under the sanitizers.
$(B)/asan:
	mkdir -p $(B)/asan

$(B)/asan/test_core: tests/test_core.c tests/tn_check.h $(CORE_SRC) $(MD4C) $(HEADERS) | $(B)/asan
	$(CC) $(STD) -O1 -g $(SAN) $(WARN) -Wno-unused-parameter $(INC) -Isrc tests/test_core.c $(CORE_SRC) -w $(MD4C) -o $@

$(B)/asan/tnotes: cli/main.c $(CORE_SRC) $(MD4C) $(HEADERS) | $(B)/asan
	$(CC) $(STD) -O1 -g $(SAN) $(INC) -w cli/main.c $(CORE_SRC) $(MD4C) -o $@

asan: $(B)/asan/test_core $(B)/asan/tnotes
	$(B)/asan/test_core tests/hostile
	sh tests/golden.sh $(B)/asan/tnotes

$(B)/asan/fuzz: tests/fuzz.c tests/tn_check.h $(CORE_SRC) $(MD4C) $(HEADERS) | $(B)/asan
	$(CC) $(STD) -O1 -g $(SAN) $(INC) -Isrc -w tests/fuzz.c $(CORE_SRC) $(MD4C) -o $@

FUZZ_RUNS ?= 20000
fuzz-smoke: $(B)/asan/fuzz
	$(B)/asan/fuzz $(FUZZ_RUNS) tests/golden/*.md tests/hostile/*.md

# The LaTeX output is real LaTeX: pdflatex compiles it.
latex-test: $(B)/tnotes
	sh tests/latex.sh $(B)/tnotes

# ---- the site ------------------------------------------------------------------
# Single-threaded WebAssembly: GitHub Pages cannot send the headers threads need.
# No file system, no eval, loaded on demand by web/app.js.

EMFLAGS := -O2 $(STD) $(INC) \
	-sMODULARIZE=1 -sEXPORT_NAME=createTnotes -sENVIRONMENT=web \
	-sFILESYSTEM=0 -sDYNAMIC_EXECUTION=0 -sALLOW_MEMORY_GROWTH=1 \
	-sEXPORTED_FUNCTIONS=_tn_render_cstr,_tn_latex_cstr,_tn_free,_malloc,_free \
	-sEXPORTED_RUNTIME_METHODS=UTF8ToString,stringToUTF8,lengthBytesUTF8

web: $(B)/web/tnotes.js $(WEB_SRC:web/%=$(B)/web/%) $(B)/web/vendor/mathjax/tex-svg-full.js $(B)/web/THIRD_PARTY.txt

$(B)/web:
	mkdir -p $(B)/web/vendor/mathjax

$(B)/web/tnotes.js: $(CORE_SRC) $(MD4C) $(HEADERS) | $(B)/web
	$(EMCC) $(EMFLAGS) $(CORE_SRC) $(MD4C) -o $@

$(B)/web/%: web/% | $(B)/web
	cp $< $@

$(B)/web/vendor/mathjax/tex-svg-full.js: third_party/mathjax/tex-svg-full.js | $(B)/web
	cp $< $@

$(B)/web/THIRD_PARTY.txt: third_party/NOTICE.txt third_party/md4c/LICENSE.md third_party/mathjax/LICENSE | $(B)/web
	cat third_party/NOTICE.txt > $@
	printf '\n\n===== md4c =====\n\n' >> $@
	cat third_party/md4c/LICENSE.md >> $@
	printf '\n\n===== MathJax =====\n\n' >> $@
	cat third_party/mathjax/LICENSE >> $@

serve: web
	@echo "travel_notes at http://localhost:$(PORT)  (Ctrl+C to stop)"
	python3 -m http.server $(PORT) --bind 127.0.0.1 --directory $(B)/web

browser-test: web
	node tests/browser/check.mjs $(B)/web

clean:
	rm -rf $(B)

.PHONY: all test asan fuzz-smoke latex-test web serve browser-test clean
