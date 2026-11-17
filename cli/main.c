/* tnotes: the command-line front end of the core. It exists for testing and for
 * scripting; the web page uses the same core through WebAssembly.
 *
 *     tnotes in.md -o out.html      render a file
 *     tnotes < in.md > out.html     or use standard input and output
 *     tnotes --latex in.md -o out.tex      the note as a LaTeX document
 *     tnotes --outline in.md               its headings, one per line
 *     tnotes --version
 *
 * The HTML is a fragment, not a whole page. The LaTeX is a whole document.
 */
#include "tnotes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_all(FILE *f, size_t *len)
{
    size_t cap = 1 << 16, n = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    for (;;) {
        size_t got = fread(buf + n, 1, cap - n, f);
        n += got;
        if (got == 0) break;
        if (n > TN_MAX_INPUT) break;          /* tn_render will refuse it */
        if (n == cap) {
            char *p = realloc(buf, cap * 2);
            if (!p) { free(buf); return NULL; }
            buf = p; cap *= 2;
        }
    }
    if (ferror(f)) { free(buf); return NULL; }
    *len = n;
    return buf;
}

static int usage(FILE *to)
{
    fputs("usage: tnotes [--latex | --outline] [INPUT.md] [-o OUTPUT]\n"
          "       tnotes --version\n"
          "Renders Markdown with LaTeX math to an HTML fragment. With no INPUT (or \"-\")\n"
          "it reads standard input; with no -o it writes standard output.\n"
          "  --latex    write a LaTeX document (amsart) instead\n"
          "  --outline  write the headings instead: level, line, id and text, tab-separated\n", to);
    return to == stderr ? 2 : 0;
}

int main(int argc, char **argv)
{
    const char *in = NULL, *out = NULL;
    int (*run)(const char *, size_t, char **, size_t *) = tn_render;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--version")) { printf("tnotes %s\n", tn_version()); return 0; }
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) return usage(stdout);
        if (!strcmp(argv[i], "--latex")) { run = tn_latex; continue; }
        if (!strcmp(argv[i], "--outline")) { run = tn_outline; continue; }
        if (!strcmp(argv[i], "-o")) {
            if (++i == argc) return usage(stderr);
            out = argv[i];
        } else if (argv[i][0] == '-' && argv[i][1]) {
            fprintf(stderr, "tnotes: unknown option %s\n", argv[i]);
            return usage(stderr);
        } else if (!in) {
            in = argv[i];
        } else {
            return usage(stderr);
        }
    }

    FILE *fi = stdin;
    if (in && strcmp(in, "-")) {
        fi = fopen(in, "rb");
        if (!fi) { fprintf(stderr, "tnotes: cannot open %s\n", in); return 1; }
    }
    size_t len = 0;
    char *md = read_all(fi, &len);
    if (fi != stdin) fclose(fi);
    if (!md) { fprintf(stderr, "tnotes: cannot read the input\n"); return 1; }

    char *html = NULL;
    size_t html_len = 0;
    int rc = run(md, len, &html, &html_len);
    free(md);
    if (rc != TN_OK) { fprintf(stderr, "tnotes: %s\n", tn_strerror(rc)); return 1; }

    FILE *fo = stdout;
    if (out) {
        fo = fopen(out, "wb");
        if (!fo) { fprintf(stderr, "tnotes: cannot write %s\n", out); tn_free(html); return 1; }
    }
    int bad = fwrite(html, 1, html_len, fo) != html_len;
    if (fo != stdout) bad |= fclose(fo) != 0; else bad |= fflush(fo) != 0;
    tn_free(html);
    if (bad) { fprintf(stderr, "tnotes: write failed\n"); return 1; }
    return 0;
}
