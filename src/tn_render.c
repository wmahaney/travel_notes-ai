/* The renderer: md4c parses, the callbacks here write the HTML.
 *
 * md4c's own HTML renderer is not used, because the output rules differ from plain
 * Markdown-to-HTML in three places: math spans carry their source line and their TeX
 * for MathJax, links and images are filtered by scheme, and nothing from the input is
 * ever written unescaped. See docs/ARCHITECTURE.md.
 */
#include "tnotes.h"
#include "tn_internal.h"

#include "md4c.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPAN_STACK 64

/* An open span that has written an opening tag. "link" tells an <a> from the <span>
 * that stands in for a link whose address is not kept. */
typedef struct { MD_SPANTYPE type; int link; } tn_span;

typedef struct {
    tn_buf      out;
    tn_buf      cap;          /* TeX of the math span, or alt text of the image, being read */
    tn_buf      img_src;      /* the image's address: as written, and escaped for output */
    tn_buf      img_src_out;
    tn_buf      img_title;    /* escaped */
    const char *src;          /* the Markdown source, for line numbers */
    size_t      src_len;
    size_t      scan_pos;     /* line counting resumes here ... */
    long        scan_line;    /* ... at this line (1-based) */
    int         math;         /* 0, or MD_SPAN_LATEXMATH / MD_SPAN_LATEXMATH_DISPLAY + 1 */
    long        math_line;    /* line of the math span being read; 0 if not known yet */
    int         img_depth;    /* > 0 inside an image: only its alt text is collected */
    tn_span     spans[SPAN_STACK];
    int         span_depth;
    int         span_over;    /* spans nested deeper than the stack: no tags written */
} tn_ctx;

/* The 1-based line of a pointer into the source. Offsets mostly increase, so counting
 * resumes from the previous answer. */
static long line_of(tn_ctx *c, const char *p)
{
    size_t off = (size_t)(p - c->src);
    if (off < c->scan_pos) { c->scan_pos = 0; c->scan_line = 1; }
    for (size_t i = c->scan_pos; i < off; i++)
        if (c->src[i] == '\n') c->scan_line++;
    c->scan_pos = off;
    return c->scan_line;
}

static int in_source(const tn_ctx *c, const char *p)
{
    return c->src && p >= c->src && p < c->src + c->src_len;
}

/* An attribute as md4c hands it over: pieces of ordinary text, entities and NUL
 * characters. "raw" receives it as written (for the scheme test), "esc" receives it
 * ready for a double-quoted attribute, as a URL or as text. Either may be NULL. */
static void attribute(const MD_ATTRIBUTE *a, tn_buf *raw, tn_buf *esc, int is_url)
{
    if (!a->text || !a->substr_offsets) return;
    for (int i = 0; a->substr_offsets[i] < a->size; i++) {
        MD_OFFSET from = a->substr_offsets[i], to = a->substr_offsets[i + 1];
        const char *s = a->text + from;
        size_t n = (size_t)(to - from);
        MD_TEXTTYPE type = a->substr_types[i];
        if (raw) tn_buf_put(raw, s, n);
        if (!esc) continue;
        if (type == MD_TEXT_NULLCHAR) tn_buf_puts(esc, is_url ? "%EF%BF%BD" : "\xEF\xBF\xBD");
        else if (type == MD_TEXT_ENTITY && tn_is_entity(s, n)) tn_buf_put(esc, s, n);
        else if (is_url) tn_put_url(esc, s, n);
        else tn_put_escaped(esc, s, n);
    }
}

static void put_long(tn_buf *b, long v)
{
    char tmp[32];
    snprintf(tmp, sizeof tmp, "%ld", v);
    tn_buf_puts(b, tmp);
}

/* ---------------------------------------------------------------- blocks */

static const char *align_class(MD_ALIGN a)
{
    switch (a) {
    case MD_ALIGN_LEFT:   return " class=\"tn-left\"";
    case MD_ALIGN_CENTER: return " class=\"tn-center\"";
    case MD_ALIGN_RIGHT:  return " class=\"tn-right\"";
    default:              return "";
    }
}

static void close_all_spans(tn_ctx *c);

static int enter_block(MD_BLOCKTYPE type, void *detail, void *userdata)
{
    tn_ctx *c = userdata;
    tn_buf *o = &c->out;
    close_all_spans(c);
    switch (type) {
    case MD_BLOCK_DOC:   break;
    case MD_BLOCK_QUOTE: tn_buf_puts(o, "<blockquote>\n"); break;
    case MD_BLOCK_UL:    tn_buf_puts(o, "<ul>\n"); break;
    case MD_BLOCK_OL: {
        const MD_BLOCK_OL_DETAIL *d = detail;
        if (d->start == 1) tn_buf_puts(o, "<ol>\n");
        else { tn_buf_puts(o, "<ol start=\""); put_long(o, (long)d->start); tn_buf_puts(o, "\">\n"); }
        break;
    }
    case MD_BLOCK_LI: {
        const MD_BLOCK_LI_DETAIL *d = detail;
        if (!d->is_task) tn_buf_puts(o, "<li>");
        else {
            tn_buf_puts(o, "<li class=\"tn-task\"><input type=\"checkbox\" disabled=\"\"");
            if (d->task_mark == 'x' || d->task_mark == 'X') tn_buf_puts(o, " checked=\"\"");
            tn_buf_puts(o, ">");
        }
        break;
    }
    case MD_BLOCK_HR: tn_buf_puts(o, "<hr>\n"); break;
    case MD_BLOCK_H: {
        const MD_BLOCK_H_DETAIL *d = detail;
        unsigned level = d->level >= 1 && d->level <= 6 ? d->level : 6;
        tn_buf_puts(o, "<h"); tn_buf_putc(o, (char)('0' + level)); tn_buf_putc(o, '>');
        break;
    }
    case MD_BLOCK_CODE: {
        const MD_BLOCK_CODE_DETAIL *d = detail;
        tn_buf_puts(o, "<pre><code");
        if (d->lang.text && d->lang.size) {
            tn_buf_puts(o, " class=\"language-");
            attribute(&d->lang, NULL, o, 0);
            tn_buf_putc(o, '"');
        }
        tn_buf_putc(o, '>');
        break;
    }
    case MD_BLOCK_HTML:  break;        /* raw HTML is disabled; nothing is passed through */
    case MD_BLOCK_P:     tn_buf_puts(o, "<p>"); break;
    case MD_BLOCK_TABLE: tn_buf_puts(o, "<table>\n"); break;
    case MD_BLOCK_THEAD: tn_buf_puts(o, "<thead>\n"); break;
    case MD_BLOCK_TBODY: tn_buf_puts(o, "<tbody>\n"); break;
    case MD_BLOCK_TR:    tn_buf_puts(o, "<tr>"); break;
    case MD_BLOCK_TH:
    case MD_BLOCK_TD: {
        const MD_BLOCK_TD_DETAIL *d = detail;
        tn_buf_puts(o, type == MD_BLOCK_TH ? "<th" : "<td");
        tn_buf_puts(o, align_class(d->align));
        tn_buf_putc(o, '>');
        break;
    }
    }
    return 0;
}

static int leave_block(MD_BLOCKTYPE type, void *detail, void *userdata)
{
    tn_ctx *c = userdata;
    tn_buf *o = &c->out;
    close_all_spans(c);
    switch (type) {
    case MD_BLOCK_DOC:   break;
    case MD_BLOCK_QUOTE: tn_buf_puts(o, "</blockquote>\n"); break;
    case MD_BLOCK_UL:    tn_buf_puts(o, "</ul>\n"); break;
    case MD_BLOCK_OL:    tn_buf_puts(o, "</ol>\n"); break;
    case MD_BLOCK_LI:    tn_buf_puts(o, "</li>\n"); break;
    case MD_BLOCK_HR:    break;
    case MD_BLOCK_H: {
        const MD_BLOCK_H_DETAIL *d = detail;
        unsigned level = d->level >= 1 && d->level <= 6 ? d->level : 6;
        tn_buf_puts(o, "</h"); tn_buf_putc(o, (char)('0' + level)); tn_buf_puts(o, ">\n");
        break;
    }
    case MD_BLOCK_CODE:  tn_buf_puts(o, "</code></pre>\n"); break;
    case MD_BLOCK_HTML:  break;
    case MD_BLOCK_P:     tn_buf_puts(o, "</p>\n"); break;
    case MD_BLOCK_TABLE: tn_buf_puts(o, "</table>\n"); break;
    case MD_BLOCK_THEAD: tn_buf_puts(o, "</thead>\n"); break;
    case MD_BLOCK_TBODY: tn_buf_puts(o, "</tbody>\n"); break;
    case MD_BLOCK_TR:    tn_buf_puts(o, "</tr>\n"); break;
    case MD_BLOCK_TH:    tn_buf_puts(o, "</th>"); break;
    case MD_BLOCK_TD:    tn_buf_puts(o, "</td>"); break;
    }
    return 0;
}

/* ----------------------------------------------------------------- spans */

static int enter_span(MD_SPANTYPE type, void *detail, void *userdata)
{
    tn_ctx *c = userdata;
    tn_buf *o = &c->out;

    if (c->img_depth) {                 /* inside an image only the alt text matters */
        c->img_depth++;
        return 0;
    }
    if (c->span_depth == SPAN_STACK) { c->span_over++; return 0; }
    int link = 0;
    switch (type) {
    case MD_SPAN_EM:     tn_buf_puts(o, "<em>"); break;
    case MD_SPAN_STRONG: tn_buf_puts(o, "<strong>"); break;
    case MD_SPAN_DEL:    tn_buf_puts(o, "<del>"); break;
    case MD_SPAN_CODE:   tn_buf_puts(o, "<code>"); break;
    case MD_SPAN_U:      break;         /* not enabled */
    case MD_SPAN_WIKILINK: break;       /* not enabled */
    case MD_SPAN_A: {
        const MD_SPAN_A_DETAIL *d = detail;
        tn_buf raw, esc;
        tn_buf_init(&raw); tn_buf_init(&esc);
        attribute(&d->href, &raw, &esc, 1);
        int ok = !raw.failed && !esc.failed && tn_link_allowed(raw.data ? raw.data : "", raw.len);
        if (ok) {
            tn_buf_puts(o, "<a href=\"");
            tn_buf_put(o, esc.data, esc.len);
            tn_buf_putc(o, '"');
            if (d->title.text && d->title.size) {
                tn_buf_puts(o, " title=\"");
                attribute(&d->title, NULL, o, 0);
                tn_buf_putc(o, '"');
            }
            /* A link opens in a new tab, so following one never discards the note. */
            tn_buf_puts(o, " rel=\"noopener noreferrer\" target=\"_blank\">");
        } else {
            tn_buf_puts(o, "<span class=\"tn-blocked-link\" title=\"Link removed: only http, https and mailto links are kept\">");
        }
        if (raw.failed || esc.failed) o->failed = 1;
        tn_buf_free(&raw); tn_buf_free(&esc);
        link = ok;
        break;
    }
    case MD_SPAN_IMG: {
        const MD_SPAN_IMG_DETAIL *d = detail;
        c->img_depth = 1;
        tn_buf_clear(&c->cap);
        tn_buf_clear(&c->img_src); tn_buf_clear(&c->img_src_out); tn_buf_clear(&c->img_title);
        attribute(&d->src, &c->img_src, &c->img_src_out, 1);
        attribute(&d->title, NULL, &c->img_title, 0);
        break;
    }
    case MD_SPAN_LATEXMATH:
    case MD_SPAN_LATEXMATH_DISPLAY:
        c->math = (int)type + 1;
        c->math_line = 0;
        tn_buf_clear(&c->cap);
        break;
    }
    c->spans[c->span_depth].type = type;
    c->spans[c->span_depth].link = link;
    c->span_depth++;
    return 0;
}

static void finish_image(tn_ctx *c)
{
    tn_buf *o = &c->out;
    if (tn_image_allowed(c->img_src.data ? c->img_src.data : "", c->img_src.len)) {
        tn_buf_puts(o, "<img src=\"");
        tn_buf_put(o, c->img_src_out.data, c->img_src_out.len);
        tn_buf_puts(o, "\" alt=\"");
        tn_buf_put(o, c->cap.data, c->cap.len);
        tn_buf_putc(o, '"');
        if (c->img_title.len) {
            tn_buf_puts(o, " title=\"");
            tn_buf_put(o, c->img_title.data, c->img_title.len);
            tn_buf_putc(o, '"');
        }
        tn_buf_puts(o, " loading=\"lazy\" decoding=\"async\" referrerpolicy=\"no-referrer\">");
    } else {
        /* Not an https address: show the description in a marked box instead. */
        tn_buf_puts(o, "<span class=\"tn-blocked-image\" title=\"Image not shown: only https addresses are loaded\">");
        tn_buf_put(o, c->cap.data, c->cap.len);
        tn_buf_puts(o, "</span>");
    }
    if (c->cap.failed || c->img_src.failed || c->img_src_out.failed || c->img_title.failed) o->failed = 1;
}

static void finish_math(tn_ctx *c)
{
    tn_buf *o = &c->out;
    int display = c->math == (int)MD_SPAN_LATEXMATH_DISPLAY + 1;
    tn_buf_puts(o, display ? "<span class=\"tn-math tn-display\"" : "<span class=\"tn-math tn-inline\"");
    if (c->math_line > 0) { tn_buf_puts(o, " data-line=\""); put_long(o, c->math_line); tn_buf_putc(o, '"'); }
    tn_buf_puts(o, display ? ">\\[" : ">\\(");
    tn_put_escaped(o, c->cap.data, c->cap.len);
    tn_buf_puts(o, display ? "\\]</span>" : "\\)</span>");
    if (c->cap.failed) o->failed = 1;
}

/* The closing tag of the innermost open span. */
static void close_span(tn_ctx *c)
{
    tn_buf *o = &c->out;
    tn_span sp = c->spans[--c->span_depth];
    switch (sp.type) {
    case MD_SPAN_EM:     tn_buf_puts(o, "</em>"); break;
    case MD_SPAN_STRONG: tn_buf_puts(o, "</strong>"); break;
    case MD_SPAN_DEL:    tn_buf_puts(o, "</del>"); break;
    case MD_SPAN_CODE:   tn_buf_puts(o, "</code>"); break;
    case MD_SPAN_A:      tn_buf_puts(o, sp.link ? "</a>" : "</span>"); break;
    case MD_SPAN_IMG:    finish_image(c); c->img_depth = 0; break;
    case MD_SPAN_LATEXMATH:
    case MD_SPAN_LATEXMATH_DISPLAY:
        finish_math(c);
        c->math = 0;
        break;
    case MD_SPAN_U:
    case MD_SPAN_WIKILINK:
        break;
    }
}

/* Spans never cross a block boundary. Whatever is still open when a block starts or
 * ends is closed here, so the output stays well nested even if the parser reports an
 * opening without its closing. */
static void close_all_spans(tn_ctx *c)
{
    while (c->span_depth) close_span(c);
    c->span_over = 0;
    c->img_depth = 0;
    c->math = 0;
}

static int leave_span(MD_SPANTYPE type, void *detail, void *userdata)
{
    tn_ctx *c = userdata;
    (void)detail;

    if (c->img_depth > 1) { c->img_depth--; return 0; }      /* a span inside an image */
    if (c->span_over) { c->span_over--; return 0; }
    /* md4c can report the end of a span it never opened (seen on malformed nested
     * links). A closing that does not match the innermost open span is ignored. */
    if (!c->span_depth || c->spans[c->span_depth - 1].type != type) return 0;
    close_span(c);
    return 0;
}

/* ------------------------------------------------------------------ text */

static int text(MD_TEXTTYPE type, const MD_CHAR *s, MD_SIZE size, void *userdata)
{
    tn_ctx *c = userdata;
    size_t n = (size_t)size;

    if (c->math) {
        /* TeX: kept byte for byte (md4c turns a line break inside the span into one
         * space). The first piece that points into the source locates the span. */
        if (!c->math_line && in_source(c, s)) {
            /* Step back over the white space after the opening $ or $$, so that the
             * line is the one the expression starts on, not the one its text starts on. */
            const char *q = s;
            while (q > c->src && (q[-1] == ' ' || q[-1] == '\t' || q[-1] == '\n' || q[-1] == '\r')) q--;
            c->math_line = line_of(c, q);
        }
        tn_buf_put(&c->cap, s, n);
        return 0;
    }
    if (c->img_depth) {                 /* alt text: plain, already escaped */
        if (type == MD_TEXT_BR || type == MD_TEXT_SOFTBR) tn_buf_putc(&c->cap, ' ');
        else if (type == MD_TEXT_NULLCHAR) tn_buf_puts(&c->cap, "\xEF\xBF\xBD");
        else if (type == MD_TEXT_ENTITY && tn_is_entity(s, n)) tn_buf_put(&c->cap, s, n);
        else tn_put_escaped(&c->cap, s, n);
        return 0;
    }
    tn_buf *o = &c->out;
    switch (type) {
    case MD_TEXT_NULLCHAR: tn_buf_puts(o, "\xEF\xBF\xBD"); break;
    case MD_TEXT_BR:       tn_buf_puts(o, "<br>\n"); break;
    case MD_TEXT_SOFTBR:   tn_buf_putc(o, '\n'); break;
    case MD_TEXT_ENTITY:
        if (tn_is_entity(s, n)) tn_buf_put(o, s, n);
        else tn_put_escaped(o, s, n);
        break;
    case MD_TEXT_NORMAL:
    case MD_TEXT_CODE:
    case MD_TEXT_HTML:                  /* cannot occur with raw HTML disabled; escaped anyway */
    case MD_TEXT_LATEXMATH:
    default:
        tn_put_escaped(o, s, n);
        break;
    }
    return 0;
}

/* ------------------------------------------------------------------- API */

int tn_render(const char *md, size_t len, char **html, size_t *html_len)
{
    if (html) *html = NULL;
    if (html_len) *html_len = 0;
    if (!html || (!md && len)) return TN_ERR_ARG;
    if (len > TN_MAX_INPUT) return TN_ERR_TOO_LARGE;

    tn_ctx c;
    memset(&c, 0, sizeof c);
    tn_buf_init(&c.out); tn_buf_init(&c.cap);
    tn_buf_init(&c.img_src); tn_buf_init(&c.img_src_out); tn_buf_init(&c.img_title);
    c.src = md; c.src_len = len; c.scan_line = 1;

    MD_PARSER parser;
    memset(&parser, 0, sizeof parser);
    parser.abi_version = 0;
    /* Math spans on; raw HTML off; tables, strikethrough and task lists on. */
    parser.flags = MD_FLAG_LATEXMATHSPANS | MD_FLAG_NOHTML | MD_FLAG_TABLES |
                   MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS;
    parser.enter_block = enter_block;
    parser.leave_block = leave_block;
    parser.enter_span = enter_span;
    parser.leave_span = leave_span;
    parser.text = text;

    int rc = md_parse(md ? md : "", (MD_SIZE)len, &parser, &c);
    tn_buf_putc(&c.out, '\0');
    int failed = c.out.failed || c.cap.failed;

    tn_buf_free(&c.cap);
    tn_buf_free(&c.img_src); tn_buf_free(&c.img_src_out); tn_buf_free(&c.img_title);
    if (rc != 0 || failed) {
        tn_buf_free(&c.out);
        return failed ? TN_ERR_NOMEM : TN_ERR_PARSE;
    }
    *html = c.out.data;
    if (html_len) *html_len = c.out.len - 1;
    return TN_OK;
}

char *tn_render_cstr(const char *md, size_t len)
{
    char *html = NULL;
    return tn_render(md, len, &html, NULL) == TN_OK ? html : NULL;
}

void tn_free(void *p) { free(p); }

const char *tn_version(void) { return TN_VERSION; }

const char *tn_strerror(int code)
{
    switch (code) {
    case TN_OK:            return "ok";
    case TN_ERR_ARG:       return "bad argument";
    case TN_ERR_TOO_LARGE: return "input is larger than 8 MB";
    case TN_ERR_NOMEM:     return "out of memory";
    case TN_ERR_PARSE:     return "the Markdown parser failed";
    default:               return "unknown error";
    }
}
