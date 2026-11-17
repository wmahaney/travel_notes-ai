/* A checker for the core's output, shared by the unit tests and the fuzzer.
 *
 * tn_check(html, n, why, whylen) returns 1 if the fragment obeys the rules the core
 * promises, and 0 with a reason otherwise:
 *   - it is valid UTF-8 with no control characters other than tab, LF and CR;
 *   - every '<' starts a tag from a fixed list, every attribute is from the list for
 *     that tag and is written name="value";
 *   - href starts with http://, https:// or mailto: and src starts with https://;
 *   - every '&' starts a character reference;
 *   - tags are properly nested and all closed.
 * Nothing that can run a script passes: no <script>, no event-handler attribute, no
 * javascript: address, no style attribute.
 */
#ifndef TN_CHECK_H
#define TN_CHECK_H

#include <stdio.h>
#include <string.h>

typedef struct { const char *tag; const char *attrs; int is_void; } tn_rule;

static const tn_rule TN_RULES[] = {
    { "p", "", 0 }, { "br", "", 1 }, { "hr", "", 1 },
    { "h1", "", 0 }, { "h2", "", 0 }, { "h3", "", 0 }, { "h4", "", 0 }, { "h5", "", 0 }, { "h6", "", 0 },
    { "blockquote", "", 0 }, { "ul", "", 0 }, { "ol", " start ", 0 }, { "li", " class ", 0 },
    { "pre", "", 0 }, { "code", " class ", 0 },
    { "em", "", 0 }, { "strong", "", 0 }, { "del", "", 0 },
    { "a", " href title rel target ", 0 },
    { "img", " src alt title loading decoding referrerpolicy ", 1 },
    { "span", " class title data-line ", 0 },
    { "table", "", 0 }, { "thead", "", 0 }, { "tbody", "", 0 }, { "tr", "", 0 },
    { "th", " class ", 0 }, { "td", " class ", 0 },
    { "input", " type disabled checked ", 1 },
};

static const tn_rule *tn_find_rule(const char *name, size_t n)
{
    for (size_t i = 0; i < sizeof TN_RULES / sizeof *TN_RULES; i++)
        if (strlen(TN_RULES[i].tag) == n && !memcmp(TN_RULES[i].tag, name, n)) return &TN_RULES[i];
    return NULL;
}

static int tn_ci_prefix(const char *s, size_t n, const char *prefix)
{
    size_t m = strlen(prefix);
    if (n < m) return 0;
    for (size_t i = 0; i < m; i++) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != prefix[i]) return 0;
    }
    return 1;
}

static int tn_valid_utf8(const unsigned char *s, size_t n, size_t *bad)
{
    size_t i = 0;
    while (i < n) {
        unsigned char c = s[i];
        size_t len = 0;
        *bad = i;
        if (c < 0x80) {
            if ((c < 0x20 && c != '\t' && c != '\n' && c != '\r') || c == 0x7F) return 0;
            len = 1;
        } else if (c >= 0xC2 && c <= 0xDF) len = 2;
        else if (c >= 0xE0 && c <= 0xEF) len = 3;
        else if (c >= 0xF0 && c <= 0xF4) len = 4;
        else return 0;
        if (i + len > n) return 0;
        for (size_t k = 1; k < len; k++) if ((s[i + k] & 0xC0) != 0x80) return 0;
        if (c == 0xE0 && s[i + 1] < 0xA0) return 0;
        if (c == 0xED && s[i + 1] > 0x9F) return 0;
        if (c == 0xF0 && s[i + 1] < 0x90) return 0;
        if (c == 0xF4 && s[i + 1] > 0x8F) return 0;
        i += len;
    }
    return 1;
}

#define TN_FAIL(...) do { snprintf(why, whylen, __VA_ARGS__); return 0; } while (0)

static int tn_check(const char *html, size_t n, char *why, size_t whylen)
{
    enum { MAX_DEPTH = 4096 };
    static const tn_rule *stack[MAX_DEPTH];
    size_t depth = 0, deeper = 0, i = 0, bad = 0;     /* deeper: open tags beyond the stack */

    if (!tn_valid_utf8((const unsigned char *)html, n, &bad)) TN_FAIL("invalid UTF-8 or control character at byte %zu", bad);
    while (i < n) {
        char c = html[i];
        if (c == '>' || c == '"' || c == '\'') TN_FAIL("unescaped %c in text at byte %zu", c, i);
        if (c == '&') {
            size_t j = i + 1;
            while (j < n && j - i < 60 && html[j] != ';' && html[j] != '&' && html[j] != '<' && html[j] != ' ') j++;
            if (j >= n || html[j] != ';' || j == i + 1) TN_FAIL("bare & at byte %zu", i);
            i = j + 1;
            continue;
        }
        if (c != '<') { i++; continue; }

        int closing = i + 1 < n && html[i + 1] == '/';
        size_t s = i + 1 + (size_t)closing, e = s;
        while (e < n && ((html[e] >= 'a' && html[e] <= 'z') || (html[e] >= '0' && html[e] <= '9'))) e++;
        const tn_rule *rule = tn_find_rule(html + s, e - s);
        if (!rule) TN_FAIL("tag not on the list at byte %zu: <%.12s", i, html + i + 1);
        if (closing) {
            if (e >= n || html[e] != '>') TN_FAIL("malformed closing tag at byte %zu", i);
            if (rule->is_void) TN_FAIL("closing tag for <%s> at byte %zu", rule->tag, i);
            if (deeper) deeper--;
            else {
                if (!depth || stack[depth - 1] != rule) TN_FAIL("</%s> does not match the open tag, at byte %zu", rule->tag, i);
                depth--;
            }
            i = e + 1;
            continue;
        }
        /* attributes: name="value", separated by one space */
        size_t k = e;
        while (k < n && html[k] != '>') {
            if (html[k] != ' ') TN_FAIL("unexpected character in <%s> at byte %zu", rule->tag, k);
            size_t a = ++k;
            while (k < n && ((html[k] >= 'a' && html[k] <= 'z') || html[k] == '-')) k++;
            if (k == a || k + 1 >= n || html[k] != '=' || html[k + 1] != '"') TN_FAIL("malformed attribute in <%s> at byte %zu", rule->tag, a);
            char name[32];
            size_t alen = k - a;
            if (alen >= sizeof name - 2) TN_FAIL("attribute name too long at byte %zu", a);
            name[0] = ' '; memcpy(name + 1, html + a, alen); name[alen + 1] = ' '; name[alen + 2] = 0;
            if (!strstr(rule->attrs, name)) TN_FAIL("attribute%snot allowed on <%s>, at byte %zu", name, rule->tag, a);
            size_t v = k + 2, w = v;
            while (w < n && html[w] != '"') {
                if (html[w] == '<' || html[w] == '>') TN_FAIL("unescaped %c in an attribute at byte %zu", html[w], w);
                w++;
            }
            if (w >= n) TN_FAIL("unterminated attribute at byte %zu", a);
            if (!strcmp(name, " href ") && !(tn_ci_prefix(html + v, w - v, "http://") ||
                tn_ci_prefix(html + v, w - v, "https://") || tn_ci_prefix(html + v, w - v, "mailto:")))
                TN_FAIL("href with a scheme that is not kept, at byte %zu", v);
            if (!strcmp(name, " src ") && !tn_ci_prefix(html + v, w - v, "https://"))
                TN_FAIL("src that is not https, at byte %zu", v);
            k = w + 1;
        }
        if (k >= n) TN_FAIL("unterminated tag at byte %zu", i);
        if (!rule->is_void) {
            if (depth == MAX_DEPTH) deeper++;         /* counted, not matched by name */
            else stack[depth++] = rule;
        }
        i = k + 1;
    }
    if (depth || deeper) TN_FAIL("<%s> is never closed", stack[depth ? depth - 1 : 0]->tag);
    return 1;
}

#endif
