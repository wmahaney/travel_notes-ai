/* A small mutation fuzzer for the renderer.
 *     build/asan/fuzz RUNS seed-file...
 * Each run takes a seed file, mutates it (flips, inserts, deletes and copies bytes, and
 * splices in Markdown and HTML tokens), renders it, and checks the output with
 * tests/tn_check.h. Built with AddressSanitizer and UBSan, so memory errors stop it.
 * The random sequence is fixed, so a failure can be reproduced.
 */
#include "tnotes.h"
#include "tn_check.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void)
{
    rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7; rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state >> 32);
}

static const char *TOKENS[] = {
    "$", "$$", "\\$", "\\(", "\\)", "\\[", "\\]", "*", "**", "_", "`", "```", "~~", "\n", "\n\n", "> ", "- ", "1. ",
    "# ", "|", "|-|", "[", "]", "(", ")", "![", "](", "<", ">", "&", "&amp;", "&#x6A;", "&#0;", "\"", "'", "\\",
    "javascript:", "https://", "http://", "data:", "mailto:", "<script>", "</script>", "<img src=x onerror=x>",
    "- [x] ", "\t", "  \n", "\r\n", "\xC3", "\xFF", "\xE2\x88\x80", "\\begin{align}", "\\end{align}", "</span>",
    " onerror=\"", "<!--", "-->", "[a]: ", "===", "---",
};
#define NTOK (sizeof TOKENS / sizeof *TOKENS)

enum { MAX = 1 << 16 };

static size_t mutate(char *buf, size_t n)
{
    int rounds = 1 + (int)(rnd() % 8);
    while (rounds--) {
        switch (rnd() % 6) {
        case 0: if (n) buf[rnd() % n] = (char)rnd(); break;                       /* change a byte */
        case 1: if (n) { size_t i = rnd() % n, k = 1 + rnd() % 8;                 /* delete */
                    if (i + k > n) k = n - i;
                    memmove(buf + i, buf + i + k, n - i - k); n -= k; } break;
        case 2: { const char *t = TOKENS[rnd() % NTOK]; size_t k = strlen(t);     /* insert a token */
                    if (n + k < MAX) { size_t i = n ? rnd() % (n + 1) : 0;
                        memmove(buf + i + k, buf + i, n - i); memcpy(buf + i, t, k); n += k; } } break;
        case 3: if (n > 1) { size_t i = rnd() % n, k = 1 + rnd() % 64, j = rnd() % n;   /* copy a chunk */
                    if (i + k > n) k = n - i;
                    if (n + k < MAX) { char tmp[64]; memcpy(tmp, buf + i, k);
                        memmove(buf + j + k, buf + j, n - j); memcpy(buf + j, tmp, k); n += k; } } break;
        case 4: if (n) n = rnd() % (n + 1); break;                                /* truncate */
        default: if (n + 1 < MAX) { size_t i = n ? rnd() % (n + 1) : 0;           /* insert a byte */
                    memmove(buf + i + 1, buf + i, n - i); buf[i] = (char)rnd(); n++; } break;
        }
    }
    return n;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: fuzz RUNS seed-file...\n"); return 2; }
    long runs = atol(argv[1]);
    int nseeds = argc - 2;
    static char seed[MAX], buf[MAX];
    for (long r = 0; r < runs; r++) {
        const char *path = argv[2 + rnd() % (uint32_t)nseeds];
        FILE *f = fopen(path, "rb");
        if (!f) { fprintf(stderr, "fuzz: cannot open %s\n", path); return 2; }
        size_t n = fread(seed, 1, MAX / 2, f);
        fclose(f);
        memcpy(buf, seed, n);
        n = mutate(buf, n);

        char *html = NULL; size_t hn = 0; char why[200];
        int rc = tn_render(buf, n, &html, &hn);
        if (rc != TN_OK) { fprintf(stderr, "fuzz: run %ld (%s): %s\n", r, path, tn_strerror(rc)); return 1; }
        if (!tn_check(html, hn, why, sizeof why)) {
            fprintf(stderr, "fuzz: run %ld (%s): %s\n", r, path, why);
            FILE *out = fopen("build/fuzz-failure.md", "wb");
            if (out) { fwrite(buf, 1, n, out); fclose(out); fprintf(stderr, "input saved to build/fuzz-failure.md\n"); }
            tn_free(html);
            return 1;
        }
        tn_free(html);
    }
    printf("fuzz: %ld runs ok\n", runs);
    return 0;
}
