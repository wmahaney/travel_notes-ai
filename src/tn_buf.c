/* Growable buffer, escaping, and the address rules. */
#include "tn_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void tn_buf_init(tn_buf *b) { b->data = NULL; b->len = b->cap = 0; b->failed = 0; }
void tn_buf_free(tn_buf *b) { free(b->data); tn_buf_init(b); }
void tn_buf_clear(tn_buf *b) { b->len = 0; }

static int reserve(tn_buf *b, size_t extra)
{
    if (b->failed) return 0;
    if (extra > (size_t)-1 - b->len - 1) { b->failed = 1; return 0; }
    size_t need = b->len + extra + 1;                 /* room for a final NUL */
    if (need <= b->cap) return 1;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < need) {
        if (cap > (size_t)-1 / 2) { cap = need; break; }
        cap *= 2;
    }
    char *p = realloc(b->data, cap);
    if (!p) { b->failed = 1; return 0; }
    b->data = p; b->cap = cap;
    return 1;
}

void tn_buf_put(tn_buf *b, const char *s, size_t n)
{
    if (!n || !reserve(b, n)) return;
    memcpy(b->data + b->len, s, n);
    b->len += n;
}

void tn_buf_insert(tn_buf *b, size_t pos, const char *s, size_t n)
{
    if (!n || pos > b->len || !reserve(b, n)) return;
    memmove(b->data + pos + n, b->data + pos, b->len - pos);
    memcpy(b->data + pos, s, n);
    b->len += n;
}

void tn_buf_puts(tn_buf *b, const char *s) { tn_buf_put(b, s, strlen(s)); }
void tn_buf_putc(tn_buf *b, char c) { tn_buf_put(b, &c, 1); }

#define REPLACEMENT "\xEF\xBF\xBD"                    /* U+FFFD */

/* Length of the valid UTF-8 sequence starting at s (1 to 4), or 0 if the bytes there
 * are not one: a stray continuation byte, an overlong form, a surrogate, a value
 * above U+10FFFF, or a sequence cut short. */
static size_t utf8_len(const unsigned char *s, size_t n)
{
    unsigned char c = s[0];
    if (c < 0x80) return 1;
    if (c >= 0xC2 && c <= 0xDF)
        return (n >= 2 && (s[1] & 0xC0) == 0x80) ? 2 : 0;
    if (c >= 0xE0 && c <= 0xEF) {
        if (n < 3 || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) return 0;
        if (c == 0xE0 && s[1] < 0xA0) return 0;       /* overlong */
        if (c == 0xED && s[1] > 0x9F) return 0;       /* surrogate */
        return 3;
    }
    if (c >= 0xF0 && c <= 0xF4) {
        if (n < 4 || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80) return 0;
        if (c == 0xF0 && s[1] < 0x90) return 0;       /* overlong */
        if (c == 0xF4 && s[1] > 0x8F) return 0;       /* above U+10FFFF */
        return 4;
    }
    return 0;
}

void tn_put_escaped(tn_buf *b, const char *s, size_t n)
{
    const unsigned char *u = (const unsigned char *)s;
    size_t i = 0, run = 0;                            /* [run, i) is plain and pending */
#define FLUSH() do { tn_buf_put(b, s + run, i - run); } while (0)
    while (i < n) {
        unsigned char c = u[i];
        const char *rep = NULL;
        size_t step = 1;
        if (c == '&') rep = "&amp;";
        else if (c == '<') rep = "&lt;";
        else if (c == '>') rep = "&gt;";
        else if (c == '"') rep = "&quot;";
        else if (c == '\'') rep = "&#39;";
        else if (c < 0x20 ? (c != '\t' && c != '\n' && c != '\r') : c == 0x7F) rep = REPLACEMENT;
        else if (c >= 0x80) {
            step = utf8_len(u + i, n - i);
            if (!step) { rep = REPLACEMENT; step = 1; }
        }
        if (rep) {
            FLUSH();
            tn_buf_puts(b, rep);
            i += step;
            run = i;
        } else {
            i += step;
        }
    }
    FLUSH();
#undef FLUSH
}

void tn_put_plain(tn_buf *b, const char *s, size_t n)
{
    const unsigned char *u = (const unsigned char *)s;
    size_t i = 0;
    while (i < n) {
        unsigned char c = u[i];
        if (c < 0x20 || c == 0x7F) { tn_buf_putc(b, ' '); i++; }
        else if (c < 0x80) { tn_buf_putc(b, (char)c); i++; }
        else {
            size_t step = utf8_len(u + i, n - i);
            if (step) { tn_buf_put(b, s + i, step); i += step; }
            else { tn_buf_puts(b, REPLACEMENT); i++; }
        }
    }
}

void tn_put_url(tn_buf *b, const char *s, size_t n)
{
    static const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        int plain = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c != 0 && strchr("-_.+!*(),%#@?=;:/$~[]", c) != NULL);
        if (plain) tn_buf_putc(b, (char)c);
        else if (c == '&') tn_buf_puts(b, "&amp;");
        else {
            char enc[3] = { '%', hex[c >> 4], hex[c & 15] };
            tn_buf_put(b, enc, 3);
        }
    }
}

int tn_is_entity(const char *s, size_t n)
{
    if (n < 3 || s[0] != '&' || s[n - 1] != ';') return 0;
    size_t i = 1, end = n - 1;
    if (s[i] == '#') {
        i++;
        if (i < end && (s[i] == 'x' || s[i] == 'X')) {
            i++;
            if (i == end || end - i > 6) return 0;
            for (; i < end; i++) {
                char c = s[i];
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return 0;
            }
            return 1;
        }
        if (i == end || end - i > 7) return 0;
        for (; i < end; i++) if (s[i] < '0' || s[i] > '9') return 0;
        return 1;
    }
    if (end - i > 48) return 0;
    if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z'))) return 0;
    for (; i < end; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return 0;
    }
    return 1;
}

/* Case-insensitive prefix test against a lowercase ASCII prefix. */
static int has_prefix(const char *s, size_t n, const char *prefix)
{
    size_t m = strlen(prefix);
    if (n <= m) return 0;                             /* something must follow the scheme */
    for (size_t i = 0; i < m; i++) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != prefix[i]) return 0;
    }
    return 1;
}

/* No control characters or spaces anywhere in an address we keep: browsers strip some
 * of them before reading the scheme, so they have no business being there. */
static int clean(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c <= 0x20 || c == 0x7F) return 0;
    }
    return 1;
}

int tn_link_allowed(const char *s, size_t n)
{
    return clean(s, n) &&
           (has_prefix(s, n, "http://") || has_prefix(s, n, "https://") || has_prefix(s, n, "mailto:"));
}

int tn_image_allowed(const char *s, size_t n)
{
    return clean(s, n) && has_prefix(s, n, "https://");
}

/* ---- math ------------------------------------------------------------------------ */

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

void tn_math_clean(tn_buf *dst, const char *s, size_t n)
{
    size_t base = dst->len, i = 0, slashes = 0;       /* slashes: backslashes just before i */
    while (i < n && is_space(s[i])) i++;
    while (i < n) {
        char c = s[i];
        if (c == '%' && slashes % 2 == 0) {
            /* A comment: to the end of the line, the line break, and the white space
             * that starts the next line, as TeX reads it. */
            while (i < n && s[i] != '\n') i++;
            if (i < n) i++;
            while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
            slashes = 0;
            continue;
        }
        slashes = c == '\\' ? slashes + 1 : 0;
        tn_buf_putc(dst, c);
        i++;
    }
    if (dst->failed) return;
    /* White space at the end goes, unless a backslash makes it a control space. */
    while (dst->len > base && is_space(dst->data[dst->len - 1])) {
        size_t k = dst->len - 1, b = 0;
        while (k > base && dst->data[k - 1] == '\\') { k--; b++; }
        if (b % 2) break;
        dst->len--;
    }
}

/* Where a line's own text starts: after up to three spaces, at most max_quotes block
 * quote markers, and up to three more spaces. *quotes receives the number of markers. */
static size_t line_text(const char *s, size_t a, size_t b, size_t max_quotes, size_t *quotes)
{
    size_t i = a, k = 0, q = 0;
    while (i < b && s[i] == ' ' && k < 3) { i++; k++; }
    while (i < b && s[i] == '>' && q < max_quotes) {
        q++;
        i++;
        if (i < b && s[i] == ' ') i++;
        k = 0;
        while (i < b && s[i] == ' ' && k < 3) { i++; k++; }
    }
    if (quotes) *quotes = q;
    return i;
}

/* Is the line's text exactly "$$" (white space after it allowed)? */
static int is_fence_of_math(const char *s, size_t t, size_t b)
{
    if (b - t < 2 || s[t] != '$' || s[t + 1] != '$') return 0;
    for (size_t i = t + 2; i < b; i++) if (s[i] != ' ' && s[i] != '\t' && s[i] != '\r') return 0;
    return 1;
}

static int is_blank(const char *s, size_t t, size_t b)
{
    for (size_t i = t; i < b; i++) if (s[i] != ' ' && s[i] != '\t' && s[i] != '\r') return 0;
    return 1;
}

/* Could Markdown take this line for the start of a block, or a heading underline? */
static int starts_block(const char *s, size_t t, size_t b)
{
    if (t >= b) return 0;
    char c = s[t];
    if (c == '+' || c == '-' || c == '*' || c == '#' || c == '>' || c == '=' || c == '`' || c == '~' || c == '|') return 1;
    if (c >= '0' && c <= '9') {
        size_t i = t;
        while (i < b && s[i] >= '0' && s[i] <= '9') i++;
        return i < b && (s[i] == '.' || s[i] == ')');
    }
    return 0;
}

#define TN_MATH_LOOKAHEAD ((size_t)16 * 1024)        /* no formula is longer than this */

int tn_protect_math(tn_buf *dst, const char *md, size_t len)
{
    size_t a = 0, copied = 0;
    char fence = 0;                                   /* inside a fenced code block: its character */
    int changed = 0;
    while (a < len) {
        const char *nl = memchr(md + a, '\n', len - a);
        size_t b = nl ? (size_t)(nl - md) : len, next = nl ? b + 1 : len;
        size_t depth = 0;                             /* block quotes around this line */
        size_t t = line_text(md, a, b, (size_t)-1, &depth);
        if (b - t >= 3 && (md[t] == '`' || md[t] == '~') && md[t + 1] == md[t] && md[t + 2] == md[t]) {
            if (!fence) fence = md[t];
            else if (fence == md[t]) fence = 0;
        } else if (!fence && is_fence_of_math(md, t, b)) {
            /* Look for the closing line; a blank line first means there is none. The
             * search is bounded, so that a note made of unclosed $$ lines is not slow. */
            size_t p = next, close = 0;
            while (p < len && p - next < TN_MATH_LOOKAHEAD) {
                const char *n2 = memchr(md + p, '\n', len - p);
                size_t e = n2 ? (size_t)(n2 - md) : len, t2 = line_text(md, p, e, depth, NULL);
                if (is_blank(md, t2, e)) break;
                if (is_fence_of_math(md, t2, e)) { close = p; break; }
                p = n2 ? e + 1 : len;
            }
            if (close) {
                for (p = next; p < close;) {
                    const char *n2 = memchr(md + p, '\n', len - p);
                    /* A '>' beyond the quotation the formula is in belongs to the formula. */
                    size_t e = (size_t)(n2 - md), t2 = line_text(md, p, e, depth, NULL);
                    if (starts_block(md, t2, e)) {
                        tn_buf_put(dst, md + copied, t2 - copied);
                        tn_buf_puts(dst, "    ");
                        copied = t2;
                        changed = 1;
                    }
                    p = e + 1;
                }
                a = close;                            /* go on from the closing line */
                nl = memchr(md + a, '\n', len - a);
                next = nl ? (size_t)(nl - md) + 1 : len;
            }
        }
        a = next;
    }
    if (!changed) return 0;
    tn_buf_put(dst, md + copied, len - copied);
    return 1;
}

/* ---- headings -------------------------------------------------------------------- */

void tn_slug(tn_buf *b, const char *s, size_t n)
{
    size_t base = b->len;
    int gap = 0;
    for (size_t i = 0; i < n && b->len - base < 64; i++) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            if (gap && b->len > base) tn_buf_putc(b, '-');
            gap = 0;
            tn_buf_putc(b, c);
        } else {
            gap = 1;
        }
    }
    while (b->len > base && b->data[b->len - 1] == '-') b->len--;
}

void tn_slugs_init(tn_slugs *t) { memset(t, 0, sizeof *t); tn_buf_init(&t->pool); }

void tn_slugs_free(tn_slugs *t)
{
    tn_buf_free(&t->pool);
    free(t->slot); free(t->count);
    memset(t, 0, sizeof *t);
}

static size_t slug_hash(const char *s, size_t n)
{
    size_t h = (size_t)1469598103u;
    for (size_t i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= (size_t)16777619u; }
    return h;
}

/* The slot of key s[0..n): the one holding it, or the empty one where it belongs. */
static size_t slug_find(const tn_slugs *t, const char *s, size_t n)
{
    size_t i = slug_hash(s, n) & (t->cap - 1);
    for (;;) {
        size_t off = t->slot[i];
        if (!off) return i;
        const char *k = t->pool.data + off - 1;
        if (strlen(k) == n && !memcmp(k, s, n)) return i;
        i = (i + 1) & (t->cap - 1);
    }
}

static int slugs_grow(tn_slugs *t)
{
    size_t cap = t->cap ? t->cap * 2 : 64;
    size_t *slot = calloc(cap, sizeof *slot), *count = calloc(cap, sizeof *count);
    if (!slot || !count) { free(slot); free(count); t->failed = 1; return 0; }
    tn_slugs old = *t;
    t->slot = slot; t->count = count; t->cap = cap;
    for (size_t i = 0; i < old.cap; i++) {
        if (!old.slot[i]) continue;
        const char *k = t->pool.data + old.slot[i] - 1;
        size_t j = slug_find(t, k, strlen(k));
        t->slot[j] = old.slot[i]; t->count[j] = old.count[i];
    }
    free(old.slot); free(old.count);
    return 1;
}

/* Record s[0..n) if it is new. Returns its slot, or (size_t)-1 on a failed allocation;
 * *fresh says whether it was new. */
static size_t slug_add(tn_slugs *t, const char *s, size_t n, int *fresh)
{
    *fresh = 0;
    if (t->failed) return (size_t)-1;
    if ((t->used + 1) * 2 > t->cap && !slugs_grow(t)) return (size_t)-1;
    size_t i = slug_find(t, s, n);
    if (t->slot[i]) return i;
    size_t off = t->pool.len;
    tn_buf_put(&t->pool, s, n);
    tn_buf_put(&t->pool, "", 1);
    if (t->pool.failed) { t->failed = 1; return (size_t)-1; }
    t->slot[i] = off + 1; t->count[i] = 1; t->used++;
    *fresh = 1;
    return i;
}

void tn_slugs_unique(tn_slugs *t, tn_buf *b)
{
    if (!b->len) tn_buf_puts(b, "section");
    if (b->failed) return;
    size_t n = b->len;
    int fresh;
    size_t i = slug_add(t, b->data, n, &fresh);
    if (i == (size_t)-1) { b->failed = 1; return; }
    while (!fresh) {
        /* Taken: try base-2, base-3, ... The count stays with the base, so a long run
         * of equal headings does not start from 2 every time. */
        char tail[32];
        size_t k = ++t->count[i];
        b->len = n;
        snprintf(tail, sizeof tail, "-%zu", k);
        tn_buf_puts(b, tail);
        if (b->failed) return;
        int f2;
        size_t j = slug_add(t, b->data, b->len, &f2);
        if (j == (size_t)-1) { b->failed = 1; return; }
        if (f2) return;
        i = slug_find(t, b->data, n);                 /* the table may have grown */
    }
}

/* ---- theorem-like blocks --------------------------------------------------------- */

const tn_env_kind TN_ENVS[] = {
    { "Theorem", "theorem", 1 }, { "Lemma", "lemma", 1 }, { "Proposition", "proposition", 1 },
    { "Corollary", "corollary", 1 }, { "Conjecture", "conjecture", 1 }, { "Claim", "claim", 1 },
    { "Definition", "definition", 1 }, { "Example", "example", 1 }, { "Exercise", "exercise", 1 },
    { "Remark", "remark", 1 }, { "Proof", "proof", 0 },
    { NULL, NULL, 0 }
};

int tn_env_head(const char *s, size_t n, const char **name, size_t *name_len)
{
    *name = s; *name_len = 0;
    while (n && is_space(s[0])) { s++; n--; }
    while (n && is_space(s[n - 1])) n--;
    if (n && (s[n - 1] == '.' || s[n - 1] == ':')) n--;
    while (n && is_space(s[n - 1])) n--;
    for (int k = 0; TN_ENVS[k].name; k++) {
        size_t m = strlen(TN_ENVS[k].name);
        if (n < m) continue;
        size_t i = 0;
        for (; i < m; i++) {
            char a = s[i], b = TN_ENVS[k].name[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
        }
        if (i < m) continue;
        if (n == m) return k;
        /* " (name)" must follow, to the end. */
        size_t j = m;
        if (!is_space(s[j])) continue;
        while (j < n && is_space(s[j])) j++;
        if (j >= n || s[j] != '(' || s[n - 1] != ')') continue;
        const char *q = s + j + 1;
        size_t qn = n - j - 2;
        while (qn && is_space(q[0])) { q++; qn--; }
        while (qn && is_space(q[qn - 1])) qn--;
        if (!qn) continue;
        for (size_t x = 0; x < qn; x++) if (q[x] == '\n' || q[x] == '\r') return -1;
        *name = q; *name_len = qn;
        return k;
    }
    return -1;
}
