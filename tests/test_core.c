/* Unit tests of the core, and the hostile-input set.
 *     build/test_core tests/hostile
 * Every file in the hostile folder is rendered and its output checked (tests/tn_check.h).
 */
#define _POSIX_C_SOURCE 200809L
#include "tnotes.h"
#include "tn_internal.h"
#include "tn_check.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failed;

#define CHECK(cond, ...) do { \
    checks++; \
    if (!(cond)) { failed++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static char *render(const char *md)
{
    char *html = NULL;
    size_t n = 0;
    int rc = tn_render(md, strlen(md), &html, &n);
    CHECK(rc == TN_OK, "tn_render failed (%s) on: %s", tn_strerror(rc), md);
    if (rc != TN_OK) return NULL;
    char why[200];
    CHECK(tn_check(html, n, why, sizeof why), "unsafe output (%s) for: %s\n  got: %s", why, md, html);
    return html;
}

/* The rendering of md is exactly want. */
static void is(const char *md, const char *want)
{
    char *html = render(md);
    if (!html) return;
    CHECK(!strcmp(html, want), "for: %s\n  want: %s\n  got:  %s", md, want, html);
    tn_free(html);
}

/* The rendering of md contains (or does not contain) the text. */
static void has(const char *md, const char *text, int expected)
{
    char *html = render(md);
    if (!html) return;
    CHECK((strstr(html, text) != NULL) == expected, "for: %s\n  %s: %s\n  got: %s",
          md, expected ? "missing" : "must not contain", text, html);
    tn_free(html);
}

static void test_markdown(void)
{
    is("", "");
    is("hello", "<p>hello</p>\n");
    is("# Title", "<h1>Title</h1>\n");
    is("*a* **b** ~~c~~ `d`", "<p><em>a</em> <strong>b</strong> <del>c</del> <code>d</code></p>\n");
    is("a\nb", "<p>a\nb</p>\n");
    is("a  \nb", "<p>a<br>\nb</p>\n");
    is("> q", "<blockquote>\n<p>q</p>\n</blockquote>\n");
    is("- a\n- b", "<ul>\n<li>a</li>\n<li>b</li>\n</ul>\n");
    is("3. a", "<ol start=\"3\">\n<li>a</li>\n</ol>\n");
    is("***", "<hr>\n");
    is("```c\nint x = a < b;\n```", "<pre><code class=\"language-c\">int x = a &lt; b;\n</code></pre>\n");
    is("| a | b |\n|:-:|--:|\n| 1 | 2 |",
       "<table>\n<thead>\n<tr><th class=\"tn-center\">a</th><th class=\"tn-right\">b</th></tr>\n</thead>\n"
       "<tbody>\n<tr><td class=\"tn-center\">1</td><td class=\"tn-right\">2</td></tr>\n</tbody>\n</table>\n");
    is("- [x] done\n- [ ] todo",
       "<ul>\n<li class=\"tn-task\"><input type=\"checkbox\" disabled=\"\" checked=\"\">done</li>\n"
       "<li class=\"tn-task\"><input type=\"checkbox\" disabled=\"\">todo</li>\n</ul>\n");
    is("&amp; &copy; &#60; &#x3C; &nosemi", "<p>&amp; &copy; &#60; &#x3C; &amp;nosemi</p>\n");
    is("a < b > c \"q\" 'r'", "<p>a &lt; b &gt; c &quot;q&quot; &#39;r&#39;</p>\n");
}

static void test_math(void)
{
    is("$x$", "<p><span class=\"tn-math tn-inline\" data-line=\"1\">\\(x\\)</span></p>\n");
    is("$$x$$", "<p><span class=\"tn-math tn-display\" data-line=\"1\">\\[x\\]</span></p>\n");
    /* Markdown syntax inside math is left alone: underscores, asterisks, backslashes. */
    is("$a_1 b_2$ and $c*d*e$", "<p><span class=\"tn-math tn-inline\" data-line=\"1\">\\(a_1 b_2\\)</span> and "
       "<span class=\"tn-math tn-inline\" data-line=\"1\">\\(c*d*e\\)</span></p>\n");
    is("$\\alpha\\_\\*\\\\$", "<p><span class=\"tn-math tn-inline\" data-line=\"1\">\\(\\alpha\\_\\*\\\\\\)</span></p>\n");
    is("$a<b \\& c>d$", "<p><span class=\"tn-math tn-inline\" data-line=\"1\">\\(a&lt;b \\&amp; c&gt;d\\)</span></p>\n");
    is("$[x](y)$ and $`z`$", "<p><span class=\"tn-math tn-inline\" data-line=\"1\">\\([x](y)\\)</span> and "
       "<span class=\"tn-math tn-inline\" data-line=\"1\">\\(`z`\\)</span></p>\n");
    /* Next to emphasis. */
    is("*$x$* and **bold $y$**", "<p><em><span class=\"tn-math tn-inline\" data-line=\"1\">\\(x\\)</span></em> and "
       "<strong>bold <span class=\"tn-math tn-inline\" data-line=\"1\">\\(y\\)</span></strong></p>\n");
    /* Dollar signs in prose are not math. */
    is("It costs $5 and then $10.", "<p>It costs $5 and then $10.</p>\n");
    is("A \\$ sign and \\$x\\$.", "<p>A $ sign and $x$.</p>\n");
    is("price: 5$ or 6 $", "<p>price: 5$ or 6 $</p>\n");
    /* Unterminated math is text. */
    is("$x + y", "<p>$x + y</p>\n");
    is("$$x + y", "<p>$$x + y</p>\n");
    /* Lists and block quotes. */
    is("- item $x^2$", "<ul>\n<li>item <span class=\"tn-math tn-inline\" data-line=\"1\">\\(x^2\\)</span></li>\n</ul>\n");
    is("> quote\n> $$y$$", "<blockquote>\n<p>quote\n<span class=\"tn-math tn-display\" data-line=\"2\">\\[y\\]</span></p>\n</blockquote>\n");
    /* Code keeps its dollars. */
    is("`$x$`", "<p><code>$x$</code></p>\n");
    is("```\n$x$\n```", "<pre><code>$x$\n</code></pre>\n");
    /* The line is the one the expression starts on. */
    has("a\n\nb $x$\n", "data-line=\"3\"", 1);
    has("a\n\n$$\n\\begin{align}\nx &= y\n\\end{align}\n$$\n", "data-line=\"3\"", 1);
    has("1\n2\n3\n4\n\n- a\n- b $q$\n", "data-line=\"7\"", 1);
    has("a\n\n$$\n\\begin{align}\nx &= y\n\\end{align}\n$$\n", "\\[ \\begin{align} x &amp;= y \\end{align} \\]", 1);
}

static void test_links_and_images(void)
{
    is("[a](https://example.org/x?y=1&z=2)",
       "<p><a href=\"https://example.org/x?y=1&amp;z=2\" rel=\"noopener noreferrer\" target=\"_blank\">a</a></p>\n");
    is("[a](http://example.org \"t <1>\")",
       "<p><a href=\"http://example.org\" title=\"t &lt;1&gt;\" rel=\"noopener noreferrer\" target=\"_blank\">a</a></p>\n");
    is("[m](mailto:someone@example.org)",
       "<p><a href=\"mailto:someone@example.org\" rel=\"noopener noreferrer\" target=\"_blank\">m</a></p>\n");
    is("<https://example.org>",
       "<p><a href=\"https://example.org\" rel=\"noopener noreferrer\" target=\"_blank\">https://example.org</a></p>\n");
    has("[a](HTTPS://EXAMPLE.ORG)", "<a href=\"HTTPS://EXAMPLE.ORG\"", 1);
    /* Schemes that are not kept: the text stays, the link goes. */
    const char *blocked[] = {
        "[a](javascript:alert(1))", "[a](JaVaScRiPt:alert(1))", "[a](data:text/html,<script>alert(1)</script>)",
        "[a](vbscript:x)", "[a](file:///etc/passwd)", "[a](ftp://example.org)", "[a](//example.org)",
        "[a](/relative)", "[a](#frag)", "[a](&#106;avascript:alert(1))", "[a](&#x6A;avascript:alert(1))",
        "[a](java&#9;script:alert(1))", "[a](<java script:alert(1)>)", "[a](https:)", "[a](http://)",
        "[a]( javascript:alert(1))", "<javascript:alert(1)>",
    };
    for (size_t i = 0; i < sizeof blocked / sizeof *blocked; i++) {
        has(blocked[i], "<a ", 0);
        has(blocked[i], "href", 0);
    }
    has("[a](javascript:alert(1))", "<span class=\"tn-blocked-link\"", 1);

    is("![a *b*](https://example.org/i.png \"T\")",
       "<p><img src=\"https://example.org/i.png\" alt=\"a b\" title=\"T\" loading=\"lazy\" decoding=\"async\" referrerpolicy=\"no-referrer\"></p>\n");
    const char *no_image[] = {
        "![d](http://example.org/i.png)", "![d](data:image/png;base64,AAAA)", "![d](i.png)", "![d](/i.png)",
        "![d](javascript:alert(1))", "![d](file:///i.png)", "![d](//example.org/i.png)", "![d](https://)",
    };
    for (size_t i = 0; i < sizeof no_image / sizeof *no_image; i++) {
        has(no_image[i], "<img", 0);
        has(no_image[i], "<span class=\"tn-blocked-image\"", 1);
        has(no_image[i], ">d</span>", 1);
    }
    /* A quote in an address or a description cannot end the attribute. */
    has("![x\" onerror=\"alert(1)](https://example.org/i.png)", "alt=\"x&quot; onerror=&quot;alert(1)\"", 1);
    has("[a](https://example.org/\"onmouseover=\"x)", "href=\"https://example.org/%22onmouseover=%22x\"", 1);
    /* md4c 0.5.2 reports the end of a link it never opened here; the output must
     * still be well nested (found by the fuzzer). */
    is("x [![A logo](https:[//example.org/logo.png)](https:/\"example.org)",
       "<p>x [<span class=\"tn-blocked-image\" title=\"Image not shown: only https addresses are loaded\">A logo</span></p>\n");
    /* An image inside a link, and a link inside a description. */
    has("[![d](https://e.org/i.png)](https://e.org)", "<a href=\"https://e.org\"", 1);
    has("![a [b](https://e.org) c](https://e.org/i.png)", "alt=\"a b c\"", 1);
}

static void test_no_raw_html(void)
{
    const char *raw[] = {
        "<script>alert(1)</script>", "<b>bold</b>", "<img src=x onerror=alert(1)>", "<!-- c -->",
        "<div>\nblock\n</div>", "a <span onclick=\"x\">b</span>", "<iframe src=\"https://example.org\"></iframe>",
        "<style>*{}</style>", "<svg onload=alert(1)>", "<a href=\"javascript:x\">a</a>", "<?php x ?>", "<![CDATA[x]]>",
    };
    for (size_t i = 0; i < sizeof raw / sizeof *raw; i++) {
        char *html = render(raw[i]);
        if (!html) continue;
        for (const char *p = html; (p = strchr(p, '<')) != NULL; p++) {
            const char *ok[] = { "<p>", "</p>" };
            int known = 0;
            for (size_t k = 0; k < 2; k++) known |= !strncmp(p, ok[k], strlen(ok[k]));
            CHECK(known, "raw HTML passed through for: %s\n  got: %s", raw[i], html);
        }
        tn_free(html);
    }
    is("<script>alert(1)</script>", "<p>&lt;script&gt;alert(1)&lt;/script&gt;</p>\n");
}

static void test_bytes(void)
{
    /* Invalid UTF-8 and control characters become U+FFFD; valid text is kept. */
    is("caf\xC3\xA9 \xE2\x88\x80 \xF0\x9F\x98\x80", "<p>caf\xC3\xA9 \xE2\x88\x80 \xF0\x9F\x98\x80</p>\n");
    is("a\xFF" "b", "<p>a\xEF\xBF\xBD" "b</p>\n");
    is("a\xC3", "<p>a\xEF\xBF\xBD</p>\n");
    is("a\xC0\xAF" "b", "<p>a\xEF\xBF\xBD\xEF\xBF\xBD" "b</p>\n");               /* overlong */
    is("a\xED\xA0\x80" "b", "<p>a\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD" "b</p>\n"); /* surrogate */
    is("a\x01\x7F" "b", "<p>a\xEF\xBF\xBD\xEF\xBF\xBD" "b</p>\n");
    is("$\xFF$", "<p><span class=\"tn-math tn-inline\" data-line=\"1\">\\(\xEF\xBF\xBD\\)</span></p>\n");

    /* A NUL in the input (strlen cannot see it, so call the API directly). */
    char *html = NULL; size_t n = 0;
    CHECK(tn_render("a\0b", 3, &html, &n) == TN_OK, "NUL in input");
    if (html) { CHECK(!strcmp(html, "<p>a\xEF\xBF\xBD" "b</p>\n"), "NUL: got %s", html); tn_free(html); }
}

static void test_api(void)
{
    char *html = (char *)1; size_t n = 7;
    CHECK(tn_render("x", 1, NULL, &n) == TN_ERR_ARG, "null output pointer");
    CHECK(tn_render(NULL, 5, &html, &n) == TN_ERR_ARG && html == NULL && n == 0, "null input with a length");
    CHECK(tn_render(NULL, 0, &html, &n) == TN_OK && html && n == 0, "empty input");
    tn_free(html);
    char *big = malloc(TN_MAX_INPUT + 1);
    if (big) {
        memset(big, 'a', TN_MAX_INPUT + 1);
        CHECK(tn_render(big, TN_MAX_INPUT + 1, &html, &n) == TN_ERR_TOO_LARGE && html == NULL, "oversized input");
        free(big);
    }
    char *one = tn_render_cstr("x", 1);
    CHECK(one != NULL && !strcmp(one, "<p>x</p>\n"), "tn_render_cstr");
    tn_free(one);
    CHECK(!strcmp(tn_version(), TN_VERSION), "version");
    CHECK(tn_is_entity("&amp;", 5) && tn_is_entity("&#1;", 4) && tn_is_entity("&#xAf;", 6), "entities");
    CHECK(!tn_is_entity("&;", 2) && !tn_is_entity("&#;", 3) && !tn_is_entity("&a b;", 5) &&
          !tn_is_entity("&#x;", 4) && !tn_is_entity("&1a;", 4) && !tn_is_entity("&#12345678;", 11), "non-entities");
}

/* Very long lines and deep nesting, generated here rather than stored. */
static void test_large(void)
{
    size_t n = 400000;
    char *s = malloc(n + 1);
    if (!s) return;
    const char *fill[] = { "a", "$", "*", "[", "![](", "<", "&", ">", "`", "$$x", "\\", "|" };
    for (size_t f = 0; f < sizeof fill / sizeof *fill; f++) {
        size_t m = strlen(fill[f]), len = 0;
        while (len + m <= n) { memcpy(s + len, fill[f], m); len += m; }
        char *html = NULL; size_t hn = 0; char why[200];
        int rc = tn_render(s, len, &html, &hn);
        CHECK(rc == TN_OK, "long line of \"%s\": %s", fill[f], tn_strerror(rc));
        if (html) { CHECK(tn_check(html, hn, why, sizeof why), "long line of \"%s\": %s", fill[f], why); tn_free(html); }
    }
    size_t depth = 2000, len = 0;                 /* > > > ... deep block quotes */
    for (size_t i = 0; i < depth; i++) { s[len++] = '>'; s[len++] = ' '; }
    s[len++] = 'x';
    char *html = NULL; size_t hn = 0; char why[200];
    CHECK(tn_render(s, len, &html, &hn) == TN_OK, "deep quotes");
    if (html) { CHECK(tn_check(html, hn, why, sizeof why), "deep quotes: %s", why); tn_free(html); }
    free(s);
}

static void test_hostile_files(const char *dir)
{
    DIR *d = opendir(dir);
    CHECK(d != NULL, "cannot open %s", dir);
    if (!d) return;
    int files = 0;
    for (struct dirent *e; (e = readdir(d)) != NULL;) {
        if (e->d_name[0] == '.') continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        static char buf[1 << 20];
        size_t n = fread(buf, 1, sizeof buf, f);
        fclose(f);
        char *html = NULL; size_t hn = 0; char why[200];
        int rc = tn_render(buf, n, &html, &hn);
        CHECK(rc == TN_OK, "%s: %s", path, tn_strerror(rc));
        if (html) {
            CHECK(tn_check(html, hn, why, sizeof why), "%s: %s", path, why);
            CHECK(!strstr(html, "<script") && !strstr(html, "<SCRIPT"), "%s: a script tag survived", path);
            tn_free(html);
        }
        files++;
    }
    closedir(d);
    CHECK(files >= 5, "only %d hostile files found in %s", files, dir);
}

int main(int argc, char **argv)
{
    test_markdown();
    test_math();
    test_links_and_images();
    test_no_raw_html();
    test_bytes();
    test_api();
    test_large();
    if (argc > 1) test_hostile_files(argv[1]);
    printf("%d checks, %d failed\n", checks, failed);
    return failed != 0;
}
