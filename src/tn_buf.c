/* Growable buffer, escaping, and the address rules. */
#include "tn_internal.h"

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
