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
    is("hello", "<p data-line=\"1\">hello</p>\n");
    is("# Title", "<h1 id=\"tn-h-title\" data-line=\"1\">Title</h1>\n");
    is("*a* **b** ~~c~~ `d`", "<p data-line=\"1\"><em>a</em> <strong>b</strong> <del>c</del> <code>d</code></p>\n");
    is("a\nb", "<p data-line=\"1\">a\nb</p>\n");
    is("a  \nb", "<p data-line=\"1\">a<br>\nb</p>\n");
    is("> q", "<blockquote>\n<p data-line=\"1\">q</p>\n</blockquote>\n");
    is("- a\n- b", "<ul>\n<li data-line=\"1\">a</li>\n<li data-line=\"2\">b</li>\n</ul>\n");
    is("3. a", "<ol start=\"3\">\n<li data-line=\"1\">a</li>\n</ol>\n");
    is("***", "<hr>\n");
    is("```c\nint x = a < b;\n```", "<pre data-line=\"1\"><code class=\"language-c\">int x = a &lt; b;\n</code></pre>\n");
    is("| a | b |\n|:-:|--:|\n| 1 | 2 |",
       "<table>\n<thead>\n<tr data-line=\"1\"><th class=\"tn-center\">a</th><th class=\"tn-right\">b</th></tr>\n</thead>\n"
       "<tbody>\n<tr data-line=\"3\"><td class=\"tn-center\">1</td><td class=\"tn-right\">2</td></tr>\n</tbody>\n</table>\n");
    is("- [x] done\n- [ ] todo",
       "<ul>\n<li data-line=\"1\" class=\"tn-task\"><input type=\"checkbox\" disabled=\"\" checked=\"\">done</li>\n"
       "<li data-line=\"2\" class=\"tn-task\"><input type=\"checkbox\" disabled=\"\">todo</li>\n</ul>\n");
    is("&amp; &copy; &#60; &#x3C; &nosemi", "<p data-line=\"1\">&amp; &copy; &#60; &#x3C; &amp;nosemi</p>\n");
    is("a < b > c \"q\" 'r'", "<p data-line=\"1\">a &lt; b &gt; c &quot;q&quot; &#39;r&#39;</p>\n");
}

static void test_math(void)
{
    is("$x$", "<p data-line=\"1\"><span class=\"tn-math tn-inline\" data-line=\"1\">\\(x\\)</span></p>\n");
    is("$$x$$", "<p data-line=\"1\"><span class=\"tn-math tn-display\" data-line=\"1\">\\[x\\]</span></p>\n");
    /* Markdown syntax inside math is left alone: underscores, asterisks, backslashes. */
    is("$a_1 b_2$ and $c*d*e$", "<p data-line=\"1\"><span class=\"tn-math tn-inline\" data-line=\"1\">\\(a_1 b_2\\)</span> and "
       "<span class=\"tn-math tn-inline\" data-line=\"1\">\\(c*d*e\\)</span></p>\n");
    is("$\\alpha\\_\\*\\\\$", "<p data-line=\"1\"><span class=\"tn-math tn-inline\" data-line=\"1\">\\(\\alpha\\_\\*\\\\\\)</span></p>\n");
    is("$a<b \\& c>d$", "<p data-line=\"1\"><span class=\"tn-math tn-inline\" data-line=\"1\">\\(a&lt;b \\&amp; c&gt;d\\)</span></p>\n");
    is("$[x](y)$ and $`z`$", "<p data-line=\"1\"><span class=\"tn-math tn-inline\" data-line=\"1\">\\([x](y)\\)</span> and "
       "<span class=\"tn-math tn-inline\" data-line=\"1\">\\(`z`\\)</span></p>\n");
    /* Next to emphasis. */
    is("*$x$* and **bold $y$**", "<p data-line=\"1\"><em><span class=\"tn-math tn-inline\" data-line=\"1\">\\(x\\)</span></em> and "
       "<strong>bold <span class=\"tn-math tn-inline\" data-line=\"1\">\\(y\\)</span></strong></p>\n");
    /* Dollar signs in prose are not math. */
    is("It costs $5 and then $10.", "<p data-line=\"1\">It costs $5 and then $10.</p>\n");
    is("A \\$ sign and \\$x\\$.", "<p data-line=\"1\">A $ sign and $x$.</p>\n");
    is("price: 5$ or 6 $", "<p data-line=\"1\">price: 5$ or 6 $</p>\n");
    /* Unterminated math is text. */
    is("$x + y", "<p data-line=\"1\">$x + y</p>\n");
    is("$$x + y", "<p data-line=\"1\">$$x + y</p>\n");
    /* Lists and block quotes. */
    is("- item $x^2$", "<ul>\n<li data-line=\"1\">item <span class=\"tn-math tn-inline\" data-line=\"1\">\\(x^2\\)</span></li>\n</ul>\n");
    is("> quote\n> $$y$$", "<blockquote>\n<p data-line=\"1\">quote\n<span class=\"tn-math tn-display\" data-line=\"2\">\\[y\\]</span></p>\n</blockquote>\n");
    /* Code keeps its dollars. */
    is("`$x$`", "<p data-line=\"1\"><code>$x$</code></p>\n");
    is("```\n$x$\n```", "<pre data-line=\"1\"><code>$x$\n</code></pre>\n");
    /* The line is the one the expression starts on. */
    has("a\n\nb $x$\n", "data-line=\"3\"", 1);
    has("a\n\n$$\n\\begin{align}\nx &= y\n\\end{align}\n$$\n", "data-line=\"3\"", 1);
    has("1\n2\n3\n4\n\n- a\n- b $q$\n", "data-line=\"7\"", 1);
    /* Line breaks inside a formula are kept; white space at its ends goes. */
    has("a\n\n$$\n\\begin{align}\nx &= y\n\\end{align}\n$$\n", "\\[\\begin{align}\nx &amp;= y\n\\end{align}\\]", 1);
    is("$ x $ and $$ y $$", "<p data-line=\"1\"><span class=\"tn-math tn-inline\" data-line=\"1\">\\(x\\)</span> and "
       "<span class=\"tn-math tn-display\" data-line=\"1\">\\[y\\]</span></p>\n");
    /* A space that a backslash makes a control space stays. */
    has("$a\\ $", "\\(a\\ \\)", 1);
    has("$a\\\\ $", "\\(a\\\\\\)", 1);
    /* In a block quote, the line is still the one with the dollar signs. */
    has("> a\n>\n> $$\n> x\n> $$\n", "tn-display\" data-line=\"3\"", 1);
}

static void test_links_and_images(void)
{
    is("[a](https://example.org/x?y=1&z=2)",
       "<p data-line=\"1\"><a href=\"https://example.org/x?y=1&amp;z=2\" rel=\"noopener noreferrer\" target=\"_blank\">a</a></p>\n");
    is("[a](http://example.org \"t <1>\")",
       "<p data-line=\"1\"><a href=\"http://example.org\" title=\"t &lt;1&gt;\" rel=\"noopener noreferrer\" target=\"_blank\">a</a></p>\n");
    is("[m](mailto:someone@example.org)",
       "<p data-line=\"1\"><a href=\"mailto:someone@example.org\" rel=\"noopener noreferrer\" target=\"_blank\">m</a></p>\n");
    is("<https://example.org>",
       "<p data-line=\"1\"><a href=\"https://example.org\" rel=\"noopener noreferrer\" target=\"_blank\">https://example.org</a></p>\n");
    has("[a](HTTPS://EXAMPLE.ORG)", "<a href=\"HTTPS://EXAMPLE.ORG\"", 1);
    /* Schemes that are not kept: the text stays, the link goes. */
    const char *blocked[] = {
        "[a](javascript:alert(1))", "[a](JaVaScRiPt:alert(1))", "[a](data:text/html,<script>alert(1)</script>)",
        "[a](vbscript:x)", "[a](file:///etc/passwd)", "[a](ftp://example.org)", "[a](//example.org)",
        "[a](/relative)", "[a](#)", "[a](#!!)", "[a](&#106;avascript:alert(1))", "[a](&#x6A;avascript:alert(1))",
        "[a](java&#9;script:alert(1))", "[a](<java script:alert(1)>)", "[a](https:)", "[a](http://)",
        "[a]( javascript:alert(1))", "<javascript:alert(1)>",
    };
    for (size_t i = 0; i < sizeof blocked / sizeof *blocked; i++) {
        has(blocked[i], "<a ", 0);
        has(blocked[i], "href", 0);
    }
    has("[a](javascript:alert(1))", "<span class=\"tn-blocked-link\"", 1);

    is("![a *b*](https://example.org/i.png \"T\")",
       "<p data-line=\"1\"><img src=\"https://example.org/i.png\" alt=\"a b\" title=\"T\" loading=\"lazy\" decoding=\"async\" referrerpolicy=\"no-referrer\"></p>\n");
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
       "<p data-line=\"1\">x [<span class=\"tn-blocked-image\" title=\"Image not shown: only https addresses are loaded\">A logo</span></p>\n");
    /* An image inside a link, and a link inside a description. */
    has("[![d](https://e.org/i.png)](https://e.org)", "<a href=\"https://e.org\"", 1);
    has("![a [b](https://e.org) c](https://e.org/i.png)", "alt=\"a b c\"", 1);
}

/* TeX comments: an unescaped % to the end of its line, with the line break and the
 * white space that starts the next line. Only in math. */
static void test_comments(void)
{
    has("$$\na % one\nb\n$$", "\\[a b\\]", 1);
    has("$$\n% a whole line\na\n$$", "\\[a\\]", 1);
    has("$$\na\n% last line\n$$", "\\[a\\]", 1);
    has("$$\na %x\n   b\n$$", "\\[a b\\]", 1);
    has("$a %x$", "\\(a\\)", 1);
    has("$%x$", "\\(\\)", 1);
    has("$50\\% + 1$", "\\(50\\% + 1\\)", 1);                 /* an escaped percent sign is kept */
    has("$a \\\\% x\nb$", "\\(a \\\\b\\)", 1);              /* \\\\ then a comment */
    has("$a\n% x\n% y\nb$", "\\(a\nb\\)", 1);
    is("100% sure, 50% % not a comment", "<p data-line=\"1\">100% sure, 50% % not a comment</p>\n");
    has("`a % b`", "<code>a % b</code>", 1);
}

/* A formula between two $$ lines survives lines that Markdown would take for a list,
 * a heading, a quotation or a rule. */
static void test_protected_math(void)
{
    const char *starts[] = { "+ c", "- c", "* c", "# c", "> c", "===", "---", "1. c", "2) c", "| c |", "```", "~~~" };
    for (size_t i = 0; i < sizeof starts / sizeof *starts; i++) {
        char md[100];
        snprintf(md, sizeof md, "$$\na = b\n%s\n$$\n", starts[i]);
        char *html = render(md);
        if (!html) continue;
        CHECK(strstr(html, "tn-display") && !strstr(html, "<li") && !strstr(html, "<h") &&
              !strstr(html, "<blockquote") && !strstr(html, "<pre") && !strstr(html, "<table") &&
              strstr(html, "\\]</span></p>\n") && !strstr(html, "$$"),
              "formula cut by the line \"%s\": %s", starts[i], html);
        tn_free(html);
    }
    is("$$\na\n+ b\n$$", "<p data-line=\"1\"><span class=\"tn-math tn-display\" data-line=\"1\">\\[a\n+ b\\]</span></p>\n");
    /* In a block quote and in a list item. */
    has("> $$\n> a\n> - b\n> $$", "\\[a\n- b\\]", 1);
    has("- item\n  $$\n  a\n  - b\n  $$", "\\[a\n- b\\]", 1);
    /* Line numbers after the formula are not moved. */
    has("$$\na\n+ b\n$$\n\nlast $x$", "data-line=\"6\">\\(x\\)", 1);
    /* Not a formula: a blank line before the closing $$, or code. Then nothing changes. */
    has("$$\na\n\n- b\n$$", "<li", 1);
    is("```\n$$\n- code\n$$\n```", "<pre data-line=\"1\"><code>$$\n- code\n$$\n</code></pre>\n");
    is("~~~\n$$\n- code\n$$\n~~~", "<pre data-line=\"1\"><code>$$\n- code\n$$\n</code></pre>\n");
    is("    $$\n    - code\n    $$", "<pre data-line=\"1\"><code>$$\n- code\n$$\n</code></pre>\n");
}

/* data-line on paragraphs, headings, list items, code blocks and table rows. */
static void test_lines(void)
{
    is("a\n\nb\nc\n\n## d", "<p data-line=\"1\">a</p>\n<p data-line=\"3\">b\nc</p>\n<h2 id=\"tn-h-d\" data-line=\"6\">d</h2>\n");
    is("\n\n*a*", "<p data-line=\"3\"><em>a</em></p>\n");
    is("x\n\n```\ncode\n```", "<p data-line=\"1\">x</p>\n<pre data-line=\"3\"><code>code\n</code></pre>\n");
    is("x\n\n    code", "<p data-line=\"1\">x</p>\n<pre data-line=\"3\"><code>code\n</code></pre>\n");
    is("- a\n\n  b\n- c", "<ul>\n<li data-line=\"1\"><p data-line=\"1\">a</p>\n<p data-line=\"3\">b</p>\n</li>\n"
       "<li data-line=\"4\"><p data-line=\"4\">c</p>\n</li>\n</ul>\n");
    is("- a\n  - b", "<ul>\n<li data-line=\"1\">a<ul>\n<li data-line=\"2\">b</li>\n</ul>\n</li>\n</ul>\n");
    is("> q\n>\n> r", "<blockquote>\n<p data-line=\"1\">q</p>\n<p data-line=\"3\">r</p>\n</blockquote>\n");
    is("x\n\n$$\ny\n$$", "<p data-line=\"1\">x</p>\n<p data-line=\"3\"><span class=\"tn-math tn-display\" data-line=\"3\">\\[y\\]</span></p>\n");
    /* A block with no text has no line. */
    is("#", "<h1 id=\"tn-h-section\"></h1>\n");
    is("-\n- a", "<ul>\n<li></li>\n<li data-line=\"2\">a</li>\n</ul>\n");
    is("```\n```", "<pre><code></code></pre>\n");
    is("x\r\n\r\ny", "<p data-line=\"1\">x</p>\n<p data-line=\"3\">y</p>\n");
    /* Many blocks nested before the first text: the innermost ones get the line. */
    has("- - - - - - - - - - - - - - - - - - - - x", "data-line=\"1\">x", 1);
}

/* Heading ids, links to them, and the outline. */
static void test_headings(void)
{
    is("# Hello, World!", "<h1 id=\"tn-h-hello-world\" data-line=\"1\">Hello, World!</h1>\n");
    is("## The graph $G_p(\\ell)$", "<h2 id=\"tn-h-the-graph-g-p-ell\" data-line=\"1\">The graph "
       "<span class=\"tn-math tn-inline\" data-line=\"1\">\\(G_p(\\ell)\\)</span></h2>\n");
    is("# *a* `b` [c](https://e.org)", "<h1 id=\"tn-h-a-b-c\" data-line=\"1\"><em>a</em> <code>b</code> "
       "<a href=\"https://e.org\" rel=\"noopener noreferrer\" target=\"_blank\">c</a></h1>\n");
    is("Setext\n===", "<h1 id=\"tn-h-setext\" data-line=\"1\">Setext</h1>\n");
    /* The same text twice, a clash with a number that is already there, no letters at all. */
    is("# a\n# a\n# a-2\n# a", "<h1 id=\"tn-h-a\" data-line=\"1\">a</h1>\n<h1 id=\"tn-h-a-2\" data-line=\"2\">a</h1>\n"
       "<h1 id=\"tn-h-a-2-2\" data-line=\"3\">a-2</h1>\n<h1 id=\"tn-h-a-3\" data-line=\"4\">a</h1>\n");
    is("# \xC3\xA9\n# !!\n# section", "<h1 id=\"tn-h-section\" data-line=\"1\">\xC3\xA9</h1>\n<h1 id=\"tn-h-section-2\" data-line=\"2\">!!</h1>\n"
       "<h1 id=\"tn-h-section-3\" data-line=\"3\">section</h1>\n");
    /* An id never carries text from the note beyond a-z, 0-9 and hyphens. */
    is("# \"><script>x</script>", "<h1 id=\"tn-h-script-x-script\" data-line=\"1\">&quot;&gt;&lt;script&gt;x&lt;/script&gt;</h1>\n");
    has("# aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "id=\"tn-h-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"", 1);

    /* A link to a heading: the address is slugged the same way, and stays in the page. */
    is("[x](#Hello, World!)", "<p data-line=\"1\">[x](#Hello, World!)</p>\n");     /* a space: not a link in Markdown */
    is("[x](#hello-world)", "<p data-line=\"1\"><a href=\"#tn-h-hello-world\">x</a></p>\n");
    is("[x](<#Hello, World!>)", "<p data-line=\"1\"><a href=\"#tn-h-hello-world\">x</a></p>\n");
    is("[x](#\"onclick=\"y)", "<p data-line=\"1\"><a href=\"#tn-h-onclick-y\">x</a></p>\n");
    has("[x](#)", "tn-blocked-link", 1);

    /* Many equal headings: ids stay unique, and it does not take forever. */
    {
        size_t count = 20000, len = 0;
        char *md = malloc(count * 4 + 1);
        if (md) {
            for (size_t i = 0; i < count; i++) { memcpy(md + len, "# a\n", 4); len += 4; }
            char *html = NULL; size_t n = 0;
            CHECK(tn_render(md, len, &html, &n) == TN_OK, "many equal headings");
            if (html) {
                CHECK(strstr(html, "id=\"tn-h-a-20000\"") && !strstr(html, "id=\"tn-h-a-20001\""), "ids of many equal headings");
                tn_free(html);
            }
            free(md);
        }
    }

    char *out = NULL; size_t n = 0;
    const char *md = "# One $x$\n\ntext\n\n## Two *b*\tc\n\n> ### In a quote\n\n#\n";
    CHECK(tn_outline(md, strlen(md), &out, &n) == TN_OK, "tn_outline");
    if (out) {
        const char *want = "1\t1\ttn-h-one-x\tOne $x$\n2\t5\ttn-h-two-b-c\tTwo b c\n3\t7\ttn-h-in-a-quote\tIn a quote\n1\t0\ttn-h-section\t\n";
        CHECK(!strcmp(out, want) && n == strlen(want), "outline: got\n%s", out);
        tn_free(out);
    }
    CHECK(tn_outline("no headings", 11, &out, &n) == TN_OK && out && n == 0, "empty outline");
    tn_free(out);
    CHECK(tn_outline("x", 1, NULL, &n) == TN_ERR_ARG, "outline: null output pointer");
}

/* A block quote that opens with a bold kind is a theorem-like block. */
static void test_envs(void)
{
    is("> **Theorem.** x", "<blockquote class=\"tn-env tn-theorem\">\n<p data-line=\"1\"><strong class=\"tn-env-head\">Theorem 1.</strong> x</p>\n</blockquote>\n");
    is("> **Lemma (Zorn).** x", "<blockquote class=\"tn-env tn-lemma\">\n<p data-line=\"1\"><strong class=\"tn-env-head\">Lemma 1 (Zorn).</strong> x</p>\n</blockquote>\n");
    is("> **Proof.** x", "<blockquote class=\"tn-env tn-proof\">\n<p data-line=\"1\"><strong class=\"tn-env-head\">Proof.</strong> x</p>\n</blockquote>\n");
    is("> **Proof (sketch):** x", "<blockquote class=\"tn-env tn-proof\">\n<p data-line=\"1\"><strong class=\"tn-env-head\">Proof (sketch).</strong> x</p>\n</blockquote>\n");
    is("> __definition__ x", "<blockquote class=\"tn-env tn-definition\">\n<p data-line=\"1\"><strong class=\"tn-env-head\">Definition 1.</strong> x</p>\n</blockquote>\n");
    /* One counter for all numbered kinds; proofs are not numbered. */
    has("> **Theorem.** a\n\n> **Proof.** b\n\n> **Corollary.** c\n\n> **Remark.** d", "Corollary 2.", 1);
    has("> **Theorem.** a\n\n> **Proof.** b\n\n> **Corollary.** c\n\n> **Remark.** d", "Remark 3.", 1);
    const char *kinds[] = { "Theorem", "Lemma", "Proposition", "Corollary", "Conjecture", "Claim", "Definition",
                            "Example", "Exercise", "Remark", "Proof" };
    for (size_t i = 0; i < sizeof kinds / sizeof *kinds; i++) {
        char md[64];
        snprintf(md, sizeof md, "> **%s.** x", kinds[i]);
        has(md, "class=\"tn-env tn-", 1);
    }
    /* The name is text from the note: escaped. */
    has("> **Theorem (<b> & \"q\").** x", "Theorem 1 (&lt;b&gt; &amp; &quot;q&quot;).</strong>", 1);
    /* Not a head: these stay ordinary quotations with ordinary bold text. */
    const char *plain[] = {
        "> **Theorem 7.** x", "> **Theorems.** x", "> **The theorem.** x", "> text **Theorem.** x", "> *Theorem.* x",
        "> **Theorem (*a*).** x", "> **Theorem ().** x", "> **Theorem** **x**\n\n**Theorem.** outside",
        "> - **Theorem.** in a list", "> # **Theorem.**", "**Theorem.** not quoted", "> **Theorem. More** x",
        "> **Theorem (a) b.** x", "> `x` **Theorem.**", "> **Theorem (aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa).** x",
    };
    for (size_t i = 0; i < sizeof plain / sizeof *plain; i++) {
        if (i == 7) continue;                         /* checked below */
        has(plain[i], "tn-env", 0);
    }
    /* Only the first bold run of the quotation counts. */
    is("> **Theorem** **x**", "<blockquote class=\"tn-env tn-theorem\">\n<p data-line=\"1\"><strong class=\"tn-env-head\">Theorem 1.</strong> <strong>x</strong></p>\n</blockquote>\n");
    /* Nested, and with more blocks inside. */
    has("> **Theorem.** a\n>\n> > **Proof.** b\n>\n> - c", "<blockquote class=\"tn-env tn-proof\">", 1);
    has("> **Theorem.** a\n>\n> $$\n> x\n> $$", "tn-display", 1);
}

static const char *latex_body(const char *tex)
{
    const char *p = strstr(tex, "\\begin{document}\n\n");
    return p ? p + 18 : tex;
}

/* The LaTeX of md is the preamble, then exactly body, then \end{document}. */
static void tex_is(const char *md, const char *body)
{
    char *tex = NULL; size_t n = 0, bad = 0;
    int rc = tn_latex(md, strlen(md), &tex, &n);
    CHECK(rc == TN_OK, "tn_latex failed (%s) on: %s", tn_strerror(rc), md);
    if (rc != TN_OK) return;
    CHECK(tn_valid_utf8((const unsigned char *)tex, n, &bad), "LaTeX is not clean UTF-8 at byte %zu for: %s", bad, md);
    CHECK(!strncmp(tex, "% Written by travel_notes", 25) && strstr(tex, "\\documentclass{amsart}\n"), "preamble for: %s", md);
    const char *got = latex_body(tex);
    size_t gl = strlen(got), el = strlen("\\end{document}\n");
    int ends = gl >= el && !strcmp(got + gl - el, "\\end{document}\n");
    CHECK(ends && gl - el == strlen(body) && !strncmp(got, body, gl - el), "for: %s\n  want: %s\n  got:  %s", md, body, got);
    tn_free(tex);
}

static void tex_has(const char *md, const char *text, int expected)
{
    char *tex = NULL; size_t n = 0;
    int rc = tn_latex(md, strlen(md), &tex, &n);
    CHECK(rc == TN_OK, "tn_latex failed (%s) on: %s", tn_strerror(rc), md);
    if (rc != TN_OK) return;
    CHECK((strstr(tex, text) != NULL) == expected, "for: %s\n  %s: %s\n  got: %s", md, expected ? "missing" : "must not contain", text, latex_body(tex));
    tn_free(tex);
}

static void test_latex(void)
{
    tex_is("", "");
    tex_is("hello", "hello\n\n");
    tex_is("*a* **b** ~~c~~ `d`", "\\emph{a} \\textbf{b} \\sout{c} \\texttt{d}\n\n");
    tex_has("~~c~~", "\\usepackage[normalem]{ulem}", 1);
    tex_has("c", "ulem", 0);
    /* Prose is escaped: no character of it reaches TeX as a command. */
    tex_is("\\\\ { } $5 & # _ % ~ ^ < > | \"", "\\textbackslash{} \\{ \\} \\$5 \\& \\# \\_ \\% \\textasciitilde{} "
           "\\textasciicircum{} \\textless{} \\textgreater{} \\textbar{} \\textquotedbl{}\n\n");
    tex_is("\\input{/etc/passwd} \\write18{x}", "\\textbackslash{}input\\{/etc/passwd\\} \\textbackslash{}write18\\{x\\}\n\n");
    tex_is("`\\end{document}`", "\\texttt{\\textbackslash{}end\\{document\\}}\n\n");
    tex_is("&amp; &#65; &#x42; &lt; &copy;", "\\& A B \\textless{} \\&copy;\n\n");
    tex_is("caf\xC3\xA9 a\xFF" "b a\x01" "b", "caf\xC3\xA9 a?b ab\n\n");
    /* Headings: a leading level-1 heading is the title, and if it is the only one the
     * others move up a level. */
    tex_is("# T\n\n## A\n\n### B", "\\title{T}\n\\maketitle\n\n\\section{A}\\label{tn-h-a}\n\n\\subsection{B}\\label{tn-h-b}\n\n");
    tex_is("# T\n\n# U\n\n## A", "\\title{T}\n\\maketitle\n\n\\section{U}\\label{tn-h-u}\n\n\\subsection{A}\\label{tn-h-a}\n\n");
    tex_is("text\n\n# A\n\n###### F", "text\n\n\\section{A}\\label{tn-h-a}\n\n\\paragraph{F}\\label{tn-h-f}\n\n");
    tex_is("## A\n\n## A", "\\subsection{A}\\label{tn-h-a}\n\n\\subsection{A}\\label{tn-h-a-2}\n\n");
    /* Math: inline, display, a display environment on its own, comments gone. */
    tex_is("$x_1$ and $$y$$", "\\(x_1\\) and\n\\[\ny\n\\]\n\n");
    tex_is("a\n$$\nx % c\n+ y\n$$\nb", "a\n\\[\nx + y\n\\]\nb\n\n");
    tex_is("$$\n\\begin{align}\na &= b\n\\end{align}\n$$", "\\begin{align}\na &= b\n\\end{align}\n\n");
    tex_is("$$\\begin{gather*}a\\end{gather*}$$", "\\begin{gather*}a\\end{gather*}\n\n");
    tex_has("$$\\begin{aligned}a\\end{aligned}$$", "\\[\n\\begin{aligned}", 1);
    tex_has("$$\\begin{align}a\\end{align*}$$", "\\[\n\\begin{align}", 1);
    tex_has("# $$x$$", "\\title{\\(x\\)}", 1);
    /* Definitions move in front of the formula, so they last as they do in the preview. */
    tex_is("$\\newcommand{\\R}{\\mathbb{R}} f \\colon \\R$", "\\newcommand{\\R}{\\mathbb{R}} \\(f \\colon \\R\\)\n\n");
    tex_is("$\\newcommand\\R{\\mathbb{R}}\\renewcommand{\\S}[1]{S_{#1}}$", "\\newcommand\\R{\\mathbb{R}} \\renewcommand{\\S}[1]{S_{#1}} \n\n");
    tex_is("$$\n\\newcommand{\\Z}[2][x]{#1^{\\{#2\\}}}\n\\Z{1}\n$$", "\\newcommand{\\Z}[2][x]{#1^{\\{#2\\}}}\n\\[\n\\Z{1}\n\\]\n\n");
    tex_has("$\\newcommand{\\R}{unclosed$", "\\(\\newcommand{\\R}{unclosed\\)", 1);
    tex_has("$\\newcommandx{\\R}{a}$", "\\(\\newcommandx", 1);
    /* Links and images. */
    tex_is("[a](https://e.org/x_y?a=1&b=2#f%20~)", "\\href{https://e.org/x\\_y?a=1\\&b=2\\#f\\%20\\%7E}{a}\n\n");
    tex_is("[a](https://e.org/}{\\x)", "\\href{https://e.org/\\%7D\\%7B\\%5Cx}{a}\n\n");
    tex_is("[a](javascript:x) [b](#Some-Heading)", "a \\hyperref[tn-h-some-heading]{b}\n\n");
    tex_is("![d *e*](https://e.org/i.png) ![f](x.png)", "\\href{https://e.org/i.png}{{[}Image: d e]} {[}Image: f]\n\n");
    /* Lists, tasks, a start number, and a bracket that must not become an argument. */
    tex_is("- a\n- [b]", "\\begin{itemize}\n\\item a\n\\item {[}b]\n\\end{itemize}\n\n");
    tex_is("3. a\n   - [x] t\n   - [ ] u", "\\begin{enumerate}\n\\setcounter{enumi}{2}\n\\item a\\begin{itemize}\n"
           "\\item[$\\boxtimes$] t\n\\item[$\\square$] u\n\\end{itemize}\n\n\n\\end{enumerate}\n\n");
    tex_is("a  \n[b]", "a\\\\\n{[}b]\n\n");
    /* Deep nesting is flattened where LaTeX would stop. */
    tex_has("- - - - - - x", "\\par\\textendash{} ", 1);
    tex_has("> > > > > > x", "\\begin{quote}\n\\begin{quote}\n\\begin{quote}\n\\begin{quote}\nx", 1);
    /* Quotations and theorem-like blocks. */
    tex_is("> q", "\\begin{quote}\nq\n\n\\end{quote}\n\n");
    tex_is("> **Theorem (Pizer).** x", "\\begin{theorem}[{Pizer}]\nx\n\n\\end{theorem}\n\n");
    tex_is("> **Proof.** x\n>\n> y", "\\begin{proof}\nx\n\ny\n\n\\end{proof}\n\n");
    tex_is("> **Proof (sketch).** x", "\\begin{proof}[{Proof (sketch)}]\nx\n\n\\end{proof}\n\n");
    tex_is("> **Theorem 7.** x", "\\begin{quote}\n\\textbf{Theorem 7.} x\n\n\\end{quote}\n\n");
    tex_has("> **Lemma (a]b).** x", "\\begin{lemma}[{a]b}]", 1);
    tex_has("> **Lemma (a_b $ {).** x", "\\begin{lemma}[{a\\_b \\$ \\{}]", 1);
    /* Code: verbatim, unless the code holds the one thing verbatim cannot. */
    tex_is("```\na\t$b\n```", "\\begin{verbatim}\na    $b\n\\end{verbatim}\n\n");
    tex_is("```\n\\end{verbatim} x\n```", "{\\ttfamily\\noindent\n\\mbox{}\\textbackslash{}end\\{verbatim\\}\\ x\\\\\n\\par}\n\n");
    /* Tables. */
    tex_is("| a | b | c |\n|:--|:-:|--:|\n| 1 | [2] | 3 |\n| [4] | 5 | 6 |",
           "\\begin{center}\n\\begin{tabular}{lcr}\n\\hline\na & b & c \\\\\n\\hline\n1 & [2] & 3 \\\\\n{[}4] & 5 & 6 \\\\\n"
           "\\hline\n\\end{tabular}\n\\end{center}\n\n");
    tex_is("***", "\\par\\noindent\\rule{\\linewidth}{0.4pt}\\par\n\n");

    char *tex = (char *)1; size_t n = 7;
    CHECK(tn_latex("x", 1, NULL, &n) == TN_ERR_ARG, "latex: null output pointer");
    CHECK(tn_latex(NULL, 5, &tex, &n) == TN_ERR_ARG && tex == NULL && n == 0, "latex: null input with a length");
    char *one = tn_latex_cstr("x", 1);
    CHECK(one != NULL && strstr(one, "\nx\n\n\\end{document}\n"), "tn_latex_cstr");
    tn_free(one);
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
            const char *ok[] = { "<p data-line=\"1\">", "</p>" };
            int known = 0;
            for (size_t k = 0; k < 2; k++) known |= !strncmp(p, ok[k], strlen(ok[k]));
            CHECK(known, "raw HTML passed through for: %s\n  got: %s", raw[i], html);
        }
        tn_free(html);
    }
    is("<script>alert(1)</script>", "<p data-line=\"1\">&lt;script&gt;alert(1)&lt;/script&gt;</p>\n");
}

static void test_bytes(void)
{
    /* Invalid UTF-8 and control characters become U+FFFD; valid text is kept. */
    is("caf\xC3\xA9 \xE2\x88\x80 \xF0\x9F\x98\x80", "<p data-line=\"1\">caf\xC3\xA9 \xE2\x88\x80 \xF0\x9F\x98\x80</p>\n");
    is("a\xFF" "b", "<p data-line=\"1\">a\xEF\xBF\xBD" "b</p>\n");
    is("a\xC3", "<p data-line=\"1\">a\xEF\xBF\xBD</p>\n");
    is("a\xC0\xAF" "b", "<p data-line=\"1\">a\xEF\xBF\xBD\xEF\xBF\xBD" "b</p>\n");               /* overlong */
    is("a\xED\xA0\x80" "b", "<p data-line=\"1\">a\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD" "b</p>\n"); /* surrogate */
    is("a\x01\x7F" "b", "<p data-line=\"1\">a\xEF\xBF\xBD\xEF\xBF\xBD" "b</p>\n");
    is("$\xFF$", "<p data-line=\"1\"><span class=\"tn-math tn-inline\" data-line=\"1\">\\(\xEF\xBF\xBD\\)</span></p>\n");

    /* A NUL in the input (strlen cannot see it, so call the API directly). */
    char *html = NULL; size_t n = 0;
    CHECK(tn_render("a\0b", 3, &html, &n) == TN_OK, "NUL in input");
    if (html) { CHECK(!strcmp(html, "<p data-line=\"1\">a\xEF\xBF\xBD" "b</p>\n"), "NUL: got %s", html); tn_free(html); }
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
    CHECK(one != NULL && !strcmp(one, "<p data-line=\"1\">x</p>\n"), "tn_render_cstr");
    tn_free(one);
    CHECK(!strcmp(tn_version(), TN_VERSION), "version");
    CHECK(tn_is_entity("&amp;", 5) && tn_is_entity("&#1;", 4) && tn_is_entity("&#xAf;", 6), "entities");
    {
        tn_buf b; tn_buf_init(&b);
        const char *m = "  a % c\n  b\\ ", *h = "  Hello,  W\xC3\xB6rld 42! ";
        tn_math_clean(&b, m, strlen(m));
        CHECK(b.len == 5 && !memcmp(b.data, "a b\\ ", 5), "tn_math_clean");
        b.len = 0; tn_slug(&b, h, strlen(h));
        CHECK(b.len == 14 && !memcmp(b.data, "hello-w-rld-42", 14), "tn_slug: %.*s", (int)b.len, b.data);
        tn_buf_free(&b);
        const char *name; size_t nl;
        CHECK(tn_env_head(" Lemma ( Zorn ) . ", 18, &name, &nl) == 1 && nl == 4 && !memcmp(name, "Zorn", 4), "tn_env_head");
        CHECK(tn_env_head("Lemmas", 6, &name, &nl) == -1 && tn_env_head("", 0, &name, &nl) == -1, "tn_env_head: not a head");
    }
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
        rc = tn_latex(s, len, &html, &hn);
        CHECK(rc == TN_OK, "long line of \"%s\" as LaTeX: %s", fill[f], tn_strerror(rc));
        tn_free(html);
    }
    size_t depth = 2000, len = 0;                 /* > > > ... deep block quotes */
    for (size_t i = 0; i < depth; i++) { s[len++] = '>'; s[len++] = ' '; }
    s[len++] = 'x';
    char *html = NULL; size_t hn = 0; char why[200];
    CHECK(tn_render(s, len, &html, &hn) == TN_OK, "deep quotes");
    if (html) { CHECK(tn_check(html, hn, why, sizeof why), "deep quotes: %s", why); tn_free(html); }
    CHECK(tn_latex(s, len, &html, &hn) == TN_OK, "deep quotes as LaTeX");
    tn_free(html);
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
        /* The other two outputs: no crash, and clean UTF-8. */
        char *other = NULL; size_t on = 0, bad = 0;
        rc = tn_latex(buf, n, &other, &on);
        CHECK(rc == TN_OK, "%s as LaTeX: %s", path, tn_strerror(rc));
        if (other) { CHECK(tn_valid_utf8((const unsigned char *)other, on, &bad), "%s as LaTeX: byte %zu", path, bad); tn_free(other); }
        rc = tn_outline(buf, n, &other, &on);
        CHECK(rc == TN_OK, "%s outline: %s", path, tn_strerror(rc));
        if (other) {                                  /* tabs separate the outline's fields */
            for (size_t x = 0; x < on; x++) if (other[x] == '\t') other[x] = ' ';
            CHECK(tn_valid_utf8((const unsigned char *)other, on, &bad), "%s outline: byte %zu", path, bad);
            tn_free(other);
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
    test_comments();
    test_protected_math();
    test_lines();
    test_headings();
    test_envs();
    test_links_and_images();
    test_latex();
    test_no_raw_html();
    test_bytes();
    test_api();
    test_large();
    if (argc > 1) test_hostile_files(argv[1]);
    printf("%d checks, %d failed\n", checks, failed);
    return failed != 0;
}
