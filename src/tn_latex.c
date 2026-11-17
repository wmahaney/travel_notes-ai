/* The LaTeX renderer: the same parser, a second set of callbacks, an amsart document.
 *
 * Prose is escaped, so nothing a note says in words can become a TeX command. Math is
 * the author's own TeX and is copied as written, without its comments. The differences
 * from the preview that matter:
 *   - \newcommand and \renewcommand at the start of a formula are moved in front of
 *     it. In LaTeX a definition made inside a formula ends with the formula; MathJax
 *     keeps it. Moving it gives the note the same meaning in both.
 *   - A display formula that is itself an environment (align, gather, equation, ...)
 *     is written without \[ \], which LaTeX would refuse around it.
 *   - Images cannot be fetched by TeX: an image is its description, linked to its
 *     address if the address is one the preview would load.
 * See docs/ARCHITECTURE.md.
 */
#include "tnotes.h"
#include "tn_internal.h"

#include "md4c.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPAN_STACK 64
#define ENV_HEAD   160
#define MAX_COLS   64
#define MAX_REAL   4          /* LaTeX allows four levels of each list; deeper ones are flattened */

/* What a span wrote when it opened, and so what closes it. */
enum { SP_BRACE, SP_NONE, SP_IMG, SP_MATH };

typedef struct { MD_SPANTYPE type; int close; } lx_span;

/* The list-like environments that are open: block quotes, theorem-like blocks, lists. */
enum { EV_QUOTE, EV_THEOREM, EV_UL, EV_OL, EV_FLAT };

typedef struct {
    tn_buf      out;
    tn_buf      cap;          /* the math span's TeX, the image's description, or the code block */
    tn_buf      tex;          /* cleaned TeX */
    tn_buf      htext;        /* the heading's text, for its label */
    tn_buf      slug;
    tn_buf      img_src;      /* the image's address, as written */
    tn_buf      envs;         /* the open environments: two bytes each, kind and detail */
    tn_slugs    ids;
    const char *src;
    size_t      src_len;

    int         math;         /* 0, or the math span's type + 1 */
    int         img_depth;
    lx_span     spans[SPAN_STACK];
    int         span_depth, span_over;

    int         title_first;  /* the note starts with a level-1 heading: its title */
    int         shift;        /* 1 if that is the only level-1 heading: the rest move up */
    int         blocks;       /* blocks seen so far, not counting the document */
    int         in_heading, heading_is_title;
    int         in_cell, cells_in_row, in_code;
    int         real;         /* environments open that LaTeX counts as nesting */
    int         ol_depth;

    size_t      table_spec_pos;
    unsigned    table_cols;
    int         table_rows;
    char        aligns[MAX_COLS];

    size_t      guard_pos;    /* a '[' written here would be read as an optional argument */
    size_t      fresh_pos;    /* a line has just been started here: no space or line break needed */

    int         env_state;    /* as in tn_render.c */
    size_t      env_quote_pos;
    char        env_raw[ENV_HEAD];
    size_t      env_len;
} lx;

static int in_source(const lx *c, const char *p)
{
    return c->src && p >= c->src && p < c->src + c->src_len;
}

/* ------------------------------------------------------------------ text */

/* Length of the valid UTF-8 sequence at s, or 0. */
static size_t u8len(const unsigned char *s, size_t n)
{
    unsigned char c = s[0];
    if (c < 0x80) return 1;
    if (c >= 0xC2 && c <= 0xDF) return (n >= 2 && (s[1] & 0xC0) == 0x80) ? 2 : 0;
    if (c >= 0xE0 && c <= 0xEF) {
        if (n < 3 || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) return 0;
        if (c == 0xE0 && s[1] < 0xA0) return 0;
        if (c == 0xED && s[1] > 0x9F) return 0;
        return 3;
    }
    if (c >= 0xF0 && c <= 0xF4) {
        if (n < 4 || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80) return 0;
        if (c == 0xF0 && s[1] < 0x90) return 0;
        if (c == 0xF4 && s[1] > 0x8F) return 0;
        return 4;
    }
    return 0;
}

/* Prose, escaped for LaTeX. Invalid bytes become '?', control characters go. */
static void put_text(lx *c, const char *s, size_t n)
{
    tn_buf *o = &c->out;
    const unsigned char *u = (const unsigned char *)s;
    size_t i = 0;
    while (i < n) {
        unsigned char ch = u[i];
        const char *rep = NULL;
        switch (ch) {
        case '\\': rep = "\\textbackslash{}"; break;
        case '{':  rep = "\\{"; break;
        case '}':  rep = "\\}"; break;
        case '$':  rep = "\\$"; break;
        case '&':  rep = "\\&"; break;
        case '#':  rep = "\\#"; break;
        case '_':  rep = "\\_"; break;
        case '%':  rep = "\\%"; break;
        case '~':  rep = "\\textasciitilde{}"; break;
        case '^':  rep = "\\textasciicircum{}"; break;
        case '<':  rep = "\\textless{}"; break;
        case '>':  rep = "\\textgreater{}"; break;
        case '|':  rep = "\\textbar{}"; break;
        case '"':  rep = "\\textquotedbl{}"; break;
        case '\t': case '\n': case '\r': rep = " "; break;
        case '[':  if (o->len == c->guard_pos) rep = "{[}"; break;   /* after \item or \\ */
        default: break;
        }
        if (rep) { tn_buf_puts(o, rep); i++; continue; }
        if (ch < 0x20 || ch == 0x7F) { i++; continue; }
        if (ch < 0x80) { tn_buf_putc(o, (char)ch); i++; continue; }
        size_t step = u8len(u + i, n - i);
        if (step) { tn_buf_put(o, s + i, step); i += step; }
        else { tn_buf_putc(o, '?'); i++; }
    }
}

/* TeX or code copied as written: only bytes that no TeX file should hold are changed. */
static void put_raw(tn_buf *o, const char *s, size_t n)
{
    const unsigned char *u = (const unsigned char *)s;
    size_t i = 0;
    while (i < n) {
        unsigned char ch = u[i];
        if (ch == '\n') { tn_buf_putc(o, '\n'); i++; }
        else if (ch == '\t') { tn_buf_puts(o, "    "); i++; }
        else if (ch < 0x20 || ch == 0x7F) i++;
        else if (ch < 0x80) { tn_buf_putc(o, (char)ch); i++; }
        else {
            size_t step = u8len(u + i, n - i);
            if (step) { tn_buf_put(o, s + i, step); i += step; }
            else { tn_buf_putc(o, '?'); i++; }
        }
    }
}

/* An address for \href: percent-encoded down to a small safe set, and then the four
 * characters TeX would still trip over get a backslash, which hyperref removes. */
static void put_url(tn_buf *o, const char *s, size_t n)
{
    static const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)s[i];
        int plain = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                    (ch != 0 && strchr("-.+!*(),@?=;:/", ch) != NULL);
        if (plain) tn_buf_putc(o, (char)ch);
        else if (ch == '%' || ch == '#' || ch == '&' || ch == '_') { tn_buf_putc(o, '\\'); tn_buf_putc(o, (char)ch); }
        else {
            char enc[4] = { '\\', '%', hex[ch >> 4], hex[ch & 15] };
            tn_buf_put(o, enc, 4);
        }
    }
}

/* Append the scalar value cp as UTF-8, if it is one. */
static int put_scalar(tn_buf *b, unsigned long cp)
{
    char u[4];
    if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
    if (cp < 0x80) { u[0] = (char)cp; tn_buf_put(b, u, 1); }
    else if (cp < 0x800) { u[0] = (char)(0xC0 | (cp >> 6)); u[1] = (char)(0x80 | (cp & 63)); tn_buf_put(b, u, 2); }
    else if (cp < 0x10000) {
        u[0] = (char)(0xE0 | (cp >> 12)); u[1] = (char)(0x80 | ((cp >> 6) & 63)); u[2] = (char)(0x80 | (cp & 63));
        tn_buf_put(b, u, 3);
    } else {
        u[0] = (char)(0xF0 | (cp >> 18)); u[1] = (char)(0x80 | ((cp >> 12) & 63));
        u[2] = (char)(0x80 | ((cp >> 6) & 63)); u[3] = (char)(0x80 | (cp & 63));
        tn_buf_put(b, u, 4);
    }
    return 1;
}

/* A character reference, decoded to plain text in b: numeric ones, and the five that
 * Markdown authors type. Any other is left as written. */
static void decode_entity(tn_buf *b, const char *s, size_t n)
{
    static const struct { const char *name; const char *text; } known[] = {
        { "&amp;", "&" }, { "&lt;", "<" }, { "&gt;", ">" }, { "&quot;", "\"" }, { "&apos;", "'" },
        { "&nbsp;", "\xC2\xA0" },
    };
    if (tn_is_entity(s, n)) {
        if (s[1] == '#') {
            int is_hex = s[2] == 'x' || s[2] == 'X';
            char num[16];
            size_t from = is_hex ? 3 : 2, m = n - 1 - from;
            if (m < sizeof num) {
                memcpy(num, s + from, m); num[m] = 0;
                if (put_scalar(b, strtoul(num, NULL, is_hex ? 16 : 10))) return;
            }
        }
        for (size_t k = 0; k < sizeof known / sizeof *known; k++)
            if (strlen(known[k].name) == n && !memcmp(known[k].name, s, n)) { tn_buf_puts(b, known[k].text); return; }
    }
    tn_buf_put(b, s, n);
}

/* An attribute (an address) as written, with its character references decoded. */
static void attribute(const MD_ATTRIBUTE *a, tn_buf *raw)
{
    if (!a->text || !a->substr_offsets) return;
    for (int i = 0; a->substr_offsets[i] < a->size; i++) {
        MD_OFFSET from = a->substr_offsets[i], to = a->substr_offsets[i + 1];
        if (a->substr_types[i] == MD_TEXT_ENTITY) decode_entity(raw, a->text + from, (size_t)(to - from));
        else if (a->substr_types[i] != MD_TEXT_NULLCHAR) tn_buf_put(raw, a->text + from, (size_t)(to - from));
    }
}

/* ------------------------------------------------------------------ math */

/* Skip a balanced {...} group starting at s[i] == '{'. Returns the offset after it, or
 * 0 if it does not close. */
static size_t skip_group(const char *s, size_t n, size_t i)
{
    size_t depth = 0;
    for (; i < n; i++) {
        if (s[i] == '\\') { i++; continue; }
        if (s[i] == '{') depth++;
        else if (s[i] == '}' && --depth == 0) return i + 1;
    }
    return 0;
}

static size_t skip_ws(const char *s, size_t n, size_t i)
{
    while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n')) i++;
    return i;
}

/* If s starts with a whole \newcommand or \renewcommand definition, its length; else 0. */
static size_t definition(const char *s, size_t n)
{
    static const char *const cmds[] = { "\\newcommand", "\\renewcommand" };
    size_t i = 0;
    for (size_t k = 0; k < 2 && !i; k++) {
        size_t m = strlen(cmds[k]);
        if (n > m && !memcmp(s, cmds[k], m)) i = m;
    }
    if (!i) return 0;
    if (i < n && s[i] == '*') i++;
    if (i < n && ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z'))) return 0;   /* \newcommandx */
    i = skip_ws(s, n, i);
    if (i >= n) return 0;
    if (s[i] == '{') { i = skip_group(s, n, i); if (!i) return 0; }
    else if (s[i] == '\\') {
        size_t j = i + 1;
        while (j < n && ((s[j] >= 'a' && s[j] <= 'z') || (s[j] >= 'A' && s[j] <= 'Z'))) j++;
        if (j == i + 1) return 0;
        i = j;
    } else return 0;
    for (int opt = 0; opt < 2; opt++) {              /* [arguments] and [default] */
        size_t j = skip_ws(s, n, i);
        if (j >= n || s[j] != '[') break;
        while (j < n && s[j] != ']') j++;
        if (j >= n) return 0;
        i = j + 1;
    }
    i = skip_ws(s, n, i);
    if (i >= n || s[i] != '{') return 0;
    return skip_group(s, n, i);
}

/* Is the formula one display environment from \begin to \end? */
static int is_display_env(const char *s, size_t n)
{
    static const char *const envs[] = { "equation", "align", "gather", "multline", "flalign", "alignat", "eqnarray" };
    if (n < 8 || memcmp(s, "\\begin{", 7)) return 0;
    for (size_t k = 0; k < sizeof envs / sizeof *envs; k++) {
        size_t m = strlen(envs[k]), i = 7 + m;
        if (n < i + 1 || memcmp(s + 7, envs[k], m)) continue;
        int star = s[i] == '*';
        if (star) i++;
        if (i >= n || s[i] != '}') continue;
        /* It must end with the matching \end{name}. */
        size_t tail = 5 + m + (size_t)star + 1;          /* \end{ name [*] } */
        if (n < i + 1 + tail) return 0;
        const char *e = s + n - tail;
        if (memcmp(e, "\\end{", 5) || memcmp(e + 5, envs[k], m)) return 0;
        if (star && e[5 + m] != '*') return 0;
        return e[tail - 1] == '}';
    }
    return 0;
}

static void finish_math(lx *c)
{
    tn_buf *o = &c->out;
    int display = c->math == (int)MD_SPAN_LATEXMATH_DISPLAY + 1 && !c->in_heading && !c->in_cell;
    tn_buf_clear(&c->tex);
    tn_math_clean(&c->tex, c->cap.data ? c->cap.data : "", c->cap.len);
    const char *s = c->tex.data ? c->tex.data : "";
    size_t n = c->tex.len;

    if (c->in_heading) {
        tn_buf_putc(&c->htext, '$'); tn_buf_put(&c->htext, s, n); tn_buf_putc(&c->htext, '$');
    }
    /* Definitions first, outside the formula, so that they last. */
    for (;;) {
        size_t d = definition(s, n);
        if (!d) break;
        put_raw(o, s, d);
        s += d; n -= d;
        size_t w = skip_ws(s, n, 0);
        s += w; n -= w;
        if (!display) tn_buf_putc(o, ' ');
        else if (!n) { tn_buf_putc(o, '\n'); c->fresh_pos = o->len; }
    }
    if (!n) return;
    if (!display) {
        tn_buf_puts(o, "\\(");
        put_raw(o, s, n);
        tn_buf_puts(o, "\\)");
    } else {
        /* On lines of its own, with no blank line around it: a blank line would start
         * a new paragraph in LaTeX. */
        int env = is_display_env(s, n);
        while (o->len && o->data[o->len - 1] == ' ') o->len--;
        if (o->len && o->data[o->len - 1] != '\n') tn_buf_putc(o, '\n');
        if (!env) tn_buf_puts(o, "\\[\n");
        put_raw(o, s, n);
        tn_buf_puts(o, env ? "\n" : "\n\\]\n");
        c->fresh_pos = o->len;
    }
}

/* ---------------------------------------------------------------- blocks */

static void env_push(lx *c, int kind, int detail)
{
    char e[2] = { (char)kind, (char)detail };
    tn_buf_put(&c->envs, e, 2);
}

static int env_top(const lx *c) { return c->envs.len >= 2 ? c->envs.data[c->envs.len - 2] : -1; }

static void close_all_spans(lx *c);

static const char *const ENUM_COUNTER[] = { "enumi", "enumii", "enumiii", "enumiv" };

static void finish_heading(lx *c)
{
    tn_buf *o = &c->out;
    tn_buf_clear(&c->slug);
    tn_slug(&c->slug, c->htext.data ? c->htext.data : "", c->htext.len);
    tn_slugs_unique(&c->ids, &c->slug);              /* the same ids as the HTML */
    tn_buf_putc(o, '}');
    if (c->heading_is_title) tn_buf_puts(o, "\n\\maketitle\n\n");
    else {
        tn_buf_puts(o, "\\label{tn-h-");
        tn_buf_put(o, c->slug.data, c->slug.len);
        tn_buf_puts(o, "}\n\n");
    }
    if (c->slug.failed || c->htext.failed) o->failed = 1;
    c->in_heading = 0;
}

static void finish_code(lx *c)
{
    tn_buf *o = &c->out;
    const char *s = c->cap.data ? c->cap.data : "";
    size_t n = c->cap.len;
    int safe = 1;
    for (size_t i = 0; i + 14 <= n && safe; i++) if (!memcmp(s + i, "\\end{verbatim}", 14)) safe = 0;
    if (safe) {
        tn_buf_puts(o, "\\begin{verbatim}\n");
        put_raw(o, s, n);
        if (n && s[n - 1] != '\n') tn_buf_putc(o, '\n');
        tn_buf_puts(o, "\\end{verbatim}\n\n");
    } else {
        /* The code contains the one thing verbatim cannot hold. Typeset it line by line. */
        tn_buf_puts(o, "{\\ttfamily\\noindent\n");
        size_t i = 0;
        while (i < n) {
            size_t j = i;
            while (j < n && s[j] != '\n') j++;
            tn_buf_puts(o, "\\mbox{}");
            for (size_t k = i; k < j; k++) {
                if (s[k] == ' ') tn_buf_puts(o, "\\ ");
                else if (s[k] == '\t') tn_buf_puts(o, "\\ \\ \\ \\ ");
                else put_text(c, s + k, 1);
            }
            tn_buf_puts(o, "\\\\\n");
            i = j < n ? j + 1 : j;
        }
        tn_buf_puts(o, "\\par}\n\n");
    }
    c->in_code = 0;
}

static int enter_block(MD_BLOCKTYPE type, void *detail, void *userdata)
{
    lx *c = userdata;
    tn_buf *o = &c->out;
    close_all_spans(c);
    int env = c->env_state;
    c->env_state = 0;
    int first = type != MD_BLOCK_DOC && c->blocks++ == 0;
    switch (type) {
    case MD_BLOCK_DOC: break;
    case MD_BLOCK_QUOTE:
        if (c->real < MAX_REAL) {
            c->env_quote_pos = o->len;
            c->env_state = 1;
            tn_buf_puts(o, "\\begin{quote}\n");
            env_push(c, EV_QUOTE, 0);
            c->real++;
        } else env_push(c, EV_FLAT, 0);
        break;
    case MD_BLOCK_UL:
        if (c->real < MAX_REAL) { tn_buf_puts(o, "\\begin{itemize}\n"); env_push(c, EV_UL, 0); c->real++; }
        else env_push(c, EV_FLAT, 0);
        break;
    case MD_BLOCK_OL: {
        const MD_BLOCK_OL_DETAIL *d = detail;
        if (c->real < MAX_REAL) {
            tn_buf_puts(o, "\\begin{enumerate}\n");
            if (d->start != 1 && c->ol_depth < 4) {
                char set[64];
                snprintf(set, sizeof set, "\\setcounter{%s}{%ld}\n", ENUM_COUNTER[c->ol_depth], (long)d->start - 1);
                tn_buf_puts(o, set);
            }
            env_push(c, EV_OL, 0);
            c->real++; c->ol_depth++;
        } else env_push(c, EV_FLAT, 0);
        break;
    }
    case MD_BLOCK_LI: {
        const MD_BLOCK_LI_DETAIL *d = detail;
        int top = env_top(c);
        if (top == EV_UL || top == EV_OL) {
            if (!d->is_task) tn_buf_puts(o, "\\item ");
            else tn_buf_puts(o, d->task_mark == 'x' || d->task_mark == 'X' ? "\\item[$\\boxtimes$] " : "\\item[$\\square$] ");
            c->guard_pos = o->len;
        } else {
            tn_buf_puts(o, "\\par\\textendash{} ");
        }
        break;
    }
    case MD_BLOCK_HR: tn_buf_puts(o, "\\par\\noindent\\rule{\\linewidth}{0.4pt}\\par\n\n"); break;
    case MD_BLOCK_H: {
        const MD_BLOCK_H_DETAIL *d = detail;
        int level = (int)d->level;
        c->heading_is_title = first && level == 1 && c->title_first;
        if (c->heading_is_title) tn_buf_puts(o, "\\title{");
        else {
            level -= c->shift;
            tn_buf_puts(o, level <= 1 ? "\\section{" : level == 2 ? "\\subsection{" :
                           level == 3 ? "\\subsubsection{" : "\\paragraph{");
        }
        c->in_heading = 1;
        tn_buf_clear(&c->htext);
        break;
    }
    case MD_BLOCK_CODE: c->in_code = 1; tn_buf_clear(&c->cap); break;
    case MD_BLOCK_HTML: break;
    case MD_BLOCK_P: if (env == 1) c->env_state = 2; break;
    case MD_BLOCK_TABLE: {
        const MD_BLOCK_TABLE_DETAIL *d = detail;
        tn_buf_puts(o, "\\begin{center}\n\\begin{tabular}{");
        c->table_spec_pos = o->len;
        c->table_cols = d->col_count;
        c->table_rows = 0;
        memset(c->aligns, 'l', sizeof c->aligns);
        tn_buf_puts(o, "}\n\\hline\n");
        break;
    }
    case MD_BLOCK_THEAD: case MD_BLOCK_TBODY: break;
    case MD_BLOCK_TR: c->cells_in_row = 0; break;
    case MD_BLOCK_TH:
    case MD_BLOCK_TD: {
        const MD_BLOCK_TD_DETAIL *d = detail;
        if (c->cells_in_row) tn_buf_puts(o, " & ");
        if (c->table_rows == 0 && c->cells_in_row < MAX_COLS)
            c->aligns[c->cells_in_row] = d->align == MD_ALIGN_CENTER ? 'c' : d->align == MD_ALIGN_RIGHT ? 'r' : 'l';
        c->cells_in_row++;
        c->in_cell = 1;
        break;
    }
    }
    return 0;
}

static int leave_block(MD_BLOCKTYPE type, void *detail, void *userdata)
{
    lx *c = userdata;
    tn_buf *o = &c->out;
    (void)detail;
    close_all_spans(c);
    c->env_state = 0;
    switch (type) {
    case MD_BLOCK_DOC: break;
    case MD_BLOCK_QUOTE:
    case MD_BLOCK_UL:
    case MD_BLOCK_OL: {
        if (c->envs.len < 2) break;
        int kind = c->envs.data[c->envs.len - 2], which = c->envs.data[c->envs.len - 1];
        c->envs.len -= 2;
        if (kind == EV_FLAT) break;
        c->real--;
        if (kind == EV_OL) c->ol_depth--;
        tn_buf_puts(o, "\\end{");
        tn_buf_puts(o, kind == EV_QUOTE ? "quote" : kind == EV_UL ? "itemize" : kind == EV_OL ? "enumerate" : TN_ENVS[which].id);
        tn_buf_puts(o, "}\n\n");
        break;
    }
    case MD_BLOCK_LI:   tn_buf_putc(o, '\n'); break;
    case MD_BLOCK_HR:   break;
    case MD_BLOCK_H:    finish_heading(c); break;
    case MD_BLOCK_CODE: finish_code(c); break;
    case MD_BLOCK_HTML: break;
    case MD_BLOCK_P:    tn_buf_puts(o, o->len == c->fresh_pos ? "\n" : "\n\n"); break;
    case MD_BLOCK_TABLE: tn_buf_puts(o, "\\hline\n\\end{tabular}\n\\end{center}\n\n"); break;
    case MD_BLOCK_THEAD: tn_buf_puts(o, "\\hline\n"); break;
    case MD_BLOCK_TBODY: break;
    case MD_BLOCK_TR:
        if (c->table_rows++ == 0) {
            /* The first row tells how each column is aligned: write the column list. */
            for (unsigned i = 0; i < c->table_cols; i++)
                tn_buf_insert(o, c->table_spec_pos + i, i < MAX_COLS ? &c->aligns[i] : "l", 1);
        }
        tn_buf_puts(o, " \\\\\n");
        c->guard_pos = o->len;
        break;
    case MD_BLOCK_TH:
    case MD_BLOCK_TD: c->in_cell = 0; break;
    }
    return 0;
}

/* ----------------------------------------------------------------- spans */

static int enter_span(MD_SPANTYPE type, void *detail, void *userdata)
{
    lx *c = userdata;
    tn_buf *o = &c->out;
    int env = c->env_state;
    c->env_state = 0;
    if (c->img_depth) { c->img_depth++; return 0; }
    if (c->span_depth == SPAN_STACK) { c->span_over++; return 0; }
    int close = SP_BRACE;
    switch (type) {
    case MD_SPAN_EM:     tn_buf_puts(o, "\\emph{"); break;
    case MD_SPAN_STRONG:
        if (env == 2 && c->span_depth == 0) { c->env_state = 3; c->env_len = 0; }
        tn_buf_puts(o, "\\textbf{");
        break;
    case MD_SPAN_DEL:    tn_buf_puts(o, "\\sout{"); break;
    case MD_SPAN_CODE:   tn_buf_puts(o, "\\texttt{"); break;
    case MD_SPAN_U:
    case MD_SPAN_WIKILINK: close = SP_NONE; break;
    case MD_SPAN_A: {
        const MD_SPAN_A_DETAIL *d = detail;
        tn_buf raw;
        tn_buf_init(&raw);
        attribute(&d->href, &raw);
        const char *r = raw.data ? raw.data : "";
        if (!raw.failed && tn_link_allowed(r, raw.len)) {
            tn_buf_puts(o, "\\href{"); put_url(o, r, raw.len); tn_buf_puts(o, "}{");
        } else {
            tn_buf_clear(&c->slug);
            if (!raw.failed && raw.len > 1 && r[0] == '#') tn_slug(&c->slug, r + 1, raw.len - 1);
            if (c->slug.len) {
                tn_buf_puts(o, "\\hyperref[tn-h-"); tn_buf_put(o, c->slug.data, c->slug.len); tn_buf_puts(o, "]{");
            } else close = SP_NONE;                  /* not kept: its text stays */
        }
        if (raw.failed) o->failed = 1;
        tn_buf_free(&raw);
        break;
    }
    case MD_SPAN_IMG: {
        const MD_SPAN_IMG_DETAIL *d = detail;
        c->img_depth = 1;
        tn_buf_clear(&c->cap); tn_buf_clear(&c->img_src);
        attribute(&d->src, &c->img_src);
        close = SP_IMG;
        break;
    }
    case MD_SPAN_LATEXMATH:
    case MD_SPAN_LATEXMATH_DISPLAY:
        c->math = (int)type + 1;
        tn_buf_clear(&c->cap);
        close = SP_MATH;
        break;
    }
    c->spans[c->span_depth].type = type;
    c->spans[c->span_depth].close = close;
    c->span_depth++;
    return 0;
}

static void close_span(lx *c)
{
    tn_buf *o = &c->out;
    lx_span sp = c->spans[--c->span_depth];
    switch (sp.close) {
    case SP_BRACE: tn_buf_putc(o, '}'); break;
    case SP_NONE:  break;
    case SP_MATH:  finish_math(c); c->math = 0; break;
    case SP_IMG: {
        const char *src = c->img_src.data ? c->img_src.data : "";
        int linked = tn_image_allowed(src, c->img_src.len);
        c->img_depth = 0;
        if (linked) { tn_buf_puts(o, "\\href{"); put_url(o, src, c->img_src.len); tn_buf_puts(o, "}{"); }
        tn_buf_puts(o, "{[}Image: ");
        put_text(c, c->cap.data ? c->cap.data : "", c->cap.len);
        tn_buf_putc(o, ']');
        if (linked) tn_buf_putc(o, '}');
        if (c->cap.failed || c->img_src.failed) o->failed = 1;
        break;
    }
    }
}

static void close_all_spans(lx *c)
{
    while (c->span_depth) close_span(c);
    c->span_over = 0;
    c->img_depth = 0;
    c->math = 0;
}

static int leave_span(MD_SPANTYPE type, void *detail, void *userdata)
{
    lx *c = userdata;
    (void)detail;
    if (c->img_depth > 1) { c->img_depth--; return 0; }
    if (c->span_over) { c->span_over--; return 0; }
    if (!c->span_depth || c->spans[c->span_depth - 1].type != type) { c->env_state = 0; return 0; }
    if (c->env_state == 3 && type == MD_SPAN_STRONG && env_top(c) == EV_QUOTE) {
        const char *name; size_t name_len;
        int kind = tn_env_head(c->env_raw, c->env_len, &name, &name_len);
        if (kind >= 0) {
            /* The block quote becomes the environment, and the bold run its head. */
            tn_buf *o = &c->out;
            o->len = c->env_quote_pos;
            tn_buf_puts(o, "\\begin{");
            tn_buf_puts(o, TN_ENVS[kind].id);
            tn_buf_putc(o, '}');
            if (name_len) {
                /* In braces, so that a ']' in the name does not end the argument. */
                tn_buf_puts(o, TN_ENVS[kind].numbered ? "[{" : "[{Proof (");
                put_text(c, name, name_len);
                tn_buf_puts(o, TN_ENVS[kind].numbered ? "}]" : ")}]");
            }
            tn_buf_putc(o, '\n');
            c->fresh_pos = o->len;
            c->envs.data[c->envs.len - 2] = EV_THEOREM;
            c->envs.data[c->envs.len - 1] = (char)kind;
            c->spans[c->span_depth - 1].close = SP_NONE;
        }
    }
    c->env_state = 0;
    close_span(c);
    return 0;
}

/* ------------------------------------------------------------------ text */

static int text(MD_TEXTTYPE type, const MD_CHAR *s, MD_SIZE size, void *userdata)
{
    lx *c = userdata;
    tn_buf *o = &c->out;
    size_t n = (size_t)size;

    if (c->env_state == 3 && type == MD_TEXT_NORMAL && !c->math && !c->img_depth &&
        c->env_len + n <= sizeof c->env_raw) {
        memcpy(c->env_raw + c->env_len, s, n);
        c->env_len += n;
    } else {
        c->env_state = 0;
    }

    if (c->math) {
        if (!in_source(c, s) && n == 1 && s[0] == ' ') tn_buf_putc(&c->cap, '\n');
        else tn_buf_put(&c->cap, s, n);
        return 0;
    }
    if (c->in_code) { tn_buf_put(&c->cap, s, n); return 0; }
    if (c->in_heading) {
        if (type == MD_TEXT_BR || type == MD_TEXT_SOFTBR) tn_buf_putc(&c->htext, ' ');
        else if (type != MD_TEXT_NULLCHAR) tn_buf_put(&c->htext, s, n);
    }
    if (c->img_depth) {                               /* the description, as plain text */
        if (type == MD_TEXT_BR || type == MD_TEXT_SOFTBR) tn_buf_putc(&c->cap, ' ');
        else if (type == MD_TEXT_ENTITY) decode_entity(&c->cap, s, n);
        else if (type != MD_TEXT_NULLCHAR) tn_buf_put(&c->cap, s, n);
        return 0;
    }
    switch (type) {
    case MD_TEXT_NULLCHAR: tn_buf_putc(o, '?'); break;
    case MD_TEXT_BR:
        if (c->in_heading || c->in_cell) tn_buf_putc(o, ' ');
        else { tn_buf_puts(o, "\\\\\n"); c->guard_pos = o->len; }
        break;
    case MD_TEXT_SOFTBR:
        if (o->len != c->fresh_pos) tn_buf_putc(o, c->in_heading || c->in_cell ? ' ' : '\n');
        break;
    case MD_TEXT_ENTITY: {
        tn_buf plain;
        tn_buf_init(&plain);
        decode_entity(&plain, s, n);
        put_text(c, plain.data ? plain.data : "", plain.len);
        if (plain.failed) o->failed = 1;
        tn_buf_free(&plain);
        break;
    }
    default:
        if (o->len == c->fresh_pos) while (n && s[0] == ' ') { s++; n--; }
        put_text(c, s, n);
        break;
    }
    return 0;
}

/* ---------------------------------------------------------- the first look */

/* Before rendering: does the note start with a level-1 heading, and is it the only one? */
typedef struct { int blocks, first_is_h1, h1, struck; } lx_scan;

static int scan_enter_block(MD_BLOCKTYPE type, void *detail, void *userdata)
{
    lx_scan *sc = userdata;
    if (type == MD_BLOCK_DOC) return 0;
    int is_h1 = type == MD_BLOCK_H && ((const MD_BLOCK_H_DETAIL *)detail)->level == 1;
    if (sc->blocks++ == 0) sc->first_is_h1 = is_h1;
    sc->h1 += is_h1;
    return 0;
}
static int scan_block(MD_BLOCKTYPE t, void *d, void *u) { (void)t; (void)d; (void)u; return 0; }
static int scan_span(MD_SPANTYPE t, void *d, void *u)
{
    (void)d;
    if (t == MD_SPAN_DEL) ((lx_scan *)u)->struck = 1;
    return 0;
}
static int scan_text(MD_TEXTTYPE t, const MD_CHAR *s, MD_SIZE n, void *u) { (void)t; (void)s; (void)n; (void)u; return 0; }

/* ------------------------------------------------------------------- API */

static const unsigned TN_MD_FLAGS = MD_FLAG_LATEXMATHSPANS | MD_FLAG_NOHTML | MD_FLAG_TABLES |
                                    MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS;

static const char PREAMBLE[] =
    "% Written by travel_notes " TN_VERSION " from a Markdown note.\n"
    "\\documentclass{amsart}\n"
    "\\usepackage[T1]{fontenc}\n"
    "\\usepackage{amssymb}\n";

static const char PREAMBLE_STRUCK[] =             /* only if the note strikes text through */
    "\\usepackage[normalem]{ulem}\n";

static const char PREAMBLE_REST[] =
    "\\usepackage{hyperref}\n"
    "\n"
    "\\newtheorem{theorem}{Theorem}\n"
    "\\newtheorem{lemma}[theorem]{Lemma}\n"
    "\\newtheorem{proposition}[theorem]{Proposition}\n"
    "\\newtheorem{corollary}[theorem]{Corollary}\n"
    "\\newtheorem{conjecture}[theorem]{Conjecture}\n"
    "\\newtheorem{claim}[theorem]{Claim}\n"
    "\\theoremstyle{definition}\n"
    "\\newtheorem{definition}[theorem]{Definition}\n"
    "\\newtheorem{example}[theorem]{Example}\n"
    "\\newtheorem{exercise}[theorem]{Exercise}\n"
    "\\theoremstyle{remark}\n"
    "\\newtheorem{remark}[theorem]{Remark}\n"
    "\n"
    "\\begin{document}\n"
    "\n";

int tn_latex(const char *md, size_t len, char **tex, size_t *tex_len)
{
    if (tex) *tex = NULL;
    if (tex_len) *tex_len = 0;
    if (!tex || (!md && len)) return TN_ERR_ARG;
    if (len > TN_MAX_INPUT) return TN_ERR_TOO_LARGE;

    tn_buf pre;                                       /* see tn_protect_math */
    tn_buf_init(&pre);
    if (tn_protect_math(&pre, md ? md : "", len)) {
        if (pre.failed) { tn_buf_free(&pre); return TN_ERR_NOMEM; }
        md = pre.data; len = pre.len;
    }

    MD_PARSER parser;
    memset(&parser, 0, sizeof parser);
    parser.abi_version = 0;
    parser.flags = TN_MD_FLAGS;

    lx_scan sc;
    memset(&sc, 0, sizeof sc);
    parser.enter_block = scan_enter_block;
    parser.leave_block = scan_block;
    parser.enter_span = scan_span;
    parser.leave_span = scan_span;
    parser.text = scan_text;
    if (md_parse(md ? md : "", (MD_SIZE)len, &parser, &sc) != 0) { tn_buf_free(&pre); return TN_ERR_PARSE; }

    lx c;
    memset(&c, 0, sizeof c);
    tn_buf_init(&c.out); tn_buf_init(&c.cap); tn_buf_init(&c.tex); tn_buf_init(&c.htext);
    tn_buf_init(&c.slug); tn_buf_init(&c.img_src); tn_buf_init(&c.envs);
    tn_slugs_init(&c.ids);
    c.src = md; c.src_len = len;
    c.title_first = sc.first_is_h1;
    c.shift = sc.first_is_h1 && sc.h1 == 1;
    c.guard_pos = c.fresh_pos = (size_t)-1;

    tn_buf_puts(&c.out, PREAMBLE);
    if (sc.struck) tn_buf_puts(&c.out, PREAMBLE_STRUCK);
    tn_buf_puts(&c.out, PREAMBLE_REST);
    parser.enter_block = enter_block;
    parser.leave_block = leave_block;
    parser.enter_span = enter_span;
    parser.leave_span = leave_span;
    parser.text = text;
    int rc = md_parse(md ? md : "", (MD_SIZE)len, &parser, &c);
    tn_buf_puts(&c.out, "\\end{document}\n");
    tn_buf_putc(&c.out, '\0');
    int failed = c.out.failed || c.cap.failed || c.tex.failed || c.envs.failed || c.ids.failed;

    tn_buf_free(&c.cap); tn_buf_free(&c.tex); tn_buf_free(&c.htext);
    tn_buf_free(&c.slug); tn_buf_free(&c.img_src); tn_buf_free(&c.envs);
    tn_buf_free(&pre);
    tn_slugs_free(&c.ids);
    if (rc != 0 || failed) {
        tn_buf_free(&c.out);
        return failed ? TN_ERR_NOMEM : TN_ERR_PARSE;
    }
    *tex = c.out.data;
    if (tex_len) *tex_len = c.out.len - 1;
    return TN_OK;
}

char *tn_latex_cstr(const char *md, size_t len)
{
    char *tex = NULL;
    return tn_latex(md, len, &tex, NULL) == TN_OK ? tex : NULL;
}
