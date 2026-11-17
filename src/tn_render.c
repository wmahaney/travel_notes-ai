/* The HTML renderer: md4c parses, the callbacks here write the HTML.
 *
 * md4c's own HTML renderer is not used, because the output rules differ from plain
 * Markdown-to-HTML: math spans carry their TeX for MathJax, blocks carry their source
 * line, headings get ids, links and images are filtered by scheme, a block quote that
 * opens with "**Theorem.**" becomes a numbered block, and nothing from the input is
 * ever written unescaped. See docs/ARCHITECTURE.md.
 *
 * md4c reports a block before its text, and a heading's id depends on its text. So a
 * few attributes are inserted into tags already written: the tag's offset in the output
 * is remembered, and out_insert() keeps the remembered offsets right.
 */
#include "tnotes.h"
#include "tn_internal.h"

#include "md4c.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPAN_STACK 64
#define LINE_STACK 16         /* blocks waiting for their source line */
#define ENV_HEAD   160        /* longest "Theorem (name)." looked at, in bytes */

/* An open span that has written an opening tag. "link" tells an <a> from the <span>
 * that stands in for a link whose address is not kept. */
typedef struct { MD_SPANTYPE type; int link; } tn_span;

typedef struct {
    tn_buf      out;
    tn_buf      cap;          /* TeX of the math span, or alt text of the image, being read */
    tn_buf      img_src;      /* the image's address: as written, and escaped for output */
    tn_buf      img_src_out;
    tn_buf      img_title;    /* escaped */
    tn_buf      tex;          /* the math span's TeX, cleaned */
    tn_buf      htext;        /* the text of the heading being read, as written */
    tn_buf      slug;
    tn_buf      outline;      /* one line per heading: level, line, id, text */
    tn_slugs    ids;
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

    /* Blocks that take data-line: p, h1-h6, li, pre, tr. Each waits here, with the
     * offset just after its tag name, for the first text that points into the source. */
    struct { size_t pos; int depth; } wait[LINE_STACK];
    int         nwait;
    int         line_depth;   /* how many such blocks are open */
    int         fenced;       /* in a fenced code block that has no line yet */

    int         in_heading;
    unsigned    h_level;
    size_t      h_pos;        /* offset just after "<hN" */
    long        h_line;

    /* A block quote that opens with a bold "Theorem." and the like. 1: the quote has
     * just opened; 2: so has its first paragraph; 3: reading the bold run that opens it. */
    int         env_state;
    size_t      env_quote_pos;   /* offset just after "<blockquote" */
    size_t      env_strong_pos;  /* offset of "<strong>" */
    char        env_raw[ENV_HEAD];
    size_t      env_len;
    long        env_count;
} tn_ctx;

/* Insert into the output and move the remembered offsets that lie after the place. */
static void out_insert(tn_ctx *c, size_t pos, const char *s)
{
    size_t n = strlen(s), before = c->out.len;
    tn_buf_insert(&c->out, pos, s, n);
    if (c->out.len == before) return;
    for (int i = 0; i < c->nwait; i++) if (c->wait[i].pos > pos) c->wait[i].pos += n;
    if (c->h_pos > pos) c->h_pos += n;
    if (c->env_quote_pos > pos) c->env_quote_pos += n;
    if (c->env_strong_pos > pos) c->env_strong_pos += n;
}

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

/* A block that takes data-line has just written its tag name. */
static void wait_for_line(tn_ctx *c)
{
    c->line_depth++;
    if (c->nwait == LINE_STACK) {             /* nested too deep: the outermost goes without */
        memmove(c->wait, c->wait + 1, (LINE_STACK - 1) * sizeof c->wait[0]);
        c->nwait--;
    }
    c->wait[c->nwait].pos = c->out.len;
    c->wait[c->nwait].depth = c->line_depth;
    c->nwait++;
}

/* The block is closing. If it never saw text from the source it has no line. */
static void done_waiting(tn_ctx *c)
{
    if (c->nwait && c->wait[c->nwait - 1].depth == c->line_depth) c->nwait--;
    if (c->line_depth) c->line_depth--;
}

/* The first text of the waiting blocks is on this line. */
static void stamp_line(tn_ctx *c, long line)
{
    char attr[48];
    if (c->fenced) { c->fenced = 0; if (line > 1) line--; }   /* the fence is one line up */
    snprintf(attr, sizeof attr, " data-line=\"%ld\"", line);
    while (c->nwait) {
        c->nwait--;
        out_insert(c, c->wait[c->nwait].pos, attr);
    }
    if (c->in_heading && !c->h_line) c->h_line = line;
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

/* The heading's text is known now: give it an id, and add it to the outline. */
static void finish_heading(tn_ctx *c)
{
    const char *t = c->htext.data ? c->htext.data : "";
    size_t n = c->htext.len;
    tn_buf attr;
    char num[64];

    tn_buf_clear(&c->slug);
    tn_slug(&c->slug, t, n);
    tn_slugs_unique(&c->ids, &c->slug);

    snprintf(num, sizeof num, "%u\t%ld\ttn-h-", c->h_level, c->h_line);
    tn_buf_puts(&c->outline, num);
    tn_buf_put(&c->outline, c->slug.data, c->slug.len);
    tn_buf_putc(&c->outline, '\t');
    while (n && (t[0] == ' ' || t[0] == '\t')) { t++; n--; }
    while (n && (t[n - 1] == ' ' || t[n - 1] == '\t')) n--;
    tn_put_plain(&c->outline, t, n);
    tn_buf_putc(&c->outline, '\n');

    tn_buf_init(&attr);
    tn_buf_puts(&attr, " id=\"tn-h-");
    tn_buf_put(&attr, c->slug.data, c->slug.len);
    tn_buf_put(&attr, "\"", 2);                       /* the quote and a NUL */
    if (attr.failed || c->slug.failed || c->htext.failed) c->out.failed = 1;
    else out_insert(c, c->h_pos, attr.data);
    tn_buf_free(&attr);
    c->in_heading = 0;
}

static int enter_block(MD_BLOCKTYPE type, void *detail, void *userdata)
{
    tn_ctx *c = userdata;
    tn_buf *o = &c->out;
    close_all_spans(c);
    int env = c->env_state;
    c->env_state = 0;
    switch (type) {
    case MD_BLOCK_DOC:   break;
    case MD_BLOCK_QUOTE:
        tn_buf_puts(o, "<blockquote");
        c->env_quote_pos = o->len;
        c->env_state = 1;
        tn_buf_puts(o, ">\n");
        break;
    case MD_BLOCK_UL:    tn_buf_puts(o, "<ul>\n"); break;
    case MD_BLOCK_OL: {
        const MD_BLOCK_OL_DETAIL *d = detail;
        if (d->start == 1) tn_buf_puts(o, "<ol>\n");
        else { tn_buf_puts(o, "<ol start=\""); put_long(o, (long)d->start); tn_buf_puts(o, "\">\n"); }
        break;
    }
    case MD_BLOCK_LI: {
        const MD_BLOCK_LI_DETAIL *d = detail;
        tn_buf_puts(o, "<li");
        wait_for_line(c);
        if (!d->is_task) tn_buf_putc(o, '>');
        else {
            tn_buf_puts(o, " class=\"tn-task\"><input type=\"checkbox\" disabled=\"\"");
            if (d->task_mark == 'x' || d->task_mark == 'X') tn_buf_puts(o, " checked=\"\"");
            tn_buf_puts(o, ">");
        }
        break;
    }
    case MD_BLOCK_HR: tn_buf_puts(o, "<hr>\n"); break;
    case MD_BLOCK_H: {
        const MD_BLOCK_H_DETAIL *d = detail;
        unsigned level = d->level >= 1 && d->level <= 6 ? d->level : 6;
        tn_buf_puts(o, "<h"); tn_buf_putc(o, (char)('0' + level));
        c->h_pos = o->len;
        wait_for_line(c);
        tn_buf_putc(o, '>');
        c->in_heading = 1; c->h_level = level; c->h_line = 0;
        tn_buf_clear(&c->htext);
        break;
    }
    case MD_BLOCK_CODE: {
        const MD_BLOCK_CODE_DETAIL *d = detail;
        tn_buf_puts(o, "<pre");
        wait_for_line(c);
        c->fenced = d->fence_char != 0;
        tn_buf_puts(o, "><code");
        if (d->lang.text && d->lang.size) {
            tn_buf_puts(o, " class=\"language-");
            attribute(&d->lang, NULL, o, 0);
            tn_buf_putc(o, '"');
        }
        tn_buf_putc(o, '>');
        break;
    }
    case MD_BLOCK_HTML:  break;        /* raw HTML is disabled; nothing is passed through */
    case MD_BLOCK_P:
        tn_buf_puts(o, "<p");
        wait_for_line(c);
        tn_buf_putc(o, '>');
        if (env == 1) c->env_state = 2;
        break;
    case MD_BLOCK_TABLE: tn_buf_puts(o, "<table>\n"); break;
    case MD_BLOCK_THEAD: tn_buf_puts(o, "<thead>\n"); break;
    case MD_BLOCK_TBODY: tn_buf_puts(o, "<tbody>\n"); break;
    case MD_BLOCK_TR:
        tn_buf_puts(o, "<tr");
        wait_for_line(c);
        tn_buf_putc(o, '>');
        break;
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
    c->env_state = 0;
    switch (type) {
    case MD_BLOCK_DOC:   break;
    case MD_BLOCK_QUOTE: tn_buf_puts(o, "</blockquote>\n"); break;
    case MD_BLOCK_UL:    tn_buf_puts(o, "</ul>\n"); break;
    case MD_BLOCK_OL:    tn_buf_puts(o, "</ol>\n"); break;
    case MD_BLOCK_LI:    done_waiting(c); tn_buf_puts(o, "</li>\n"); break;
    case MD_BLOCK_HR:    break;
    case MD_BLOCK_H: {
        const MD_BLOCK_H_DETAIL *d = detail;
        unsigned level = d->level >= 1 && d->level <= 6 ? d->level : 6;
        done_waiting(c);
        finish_heading(c);
        tn_buf_puts(o, "</h"); tn_buf_putc(o, (char)('0' + level)); tn_buf_puts(o, ">\n");
        break;
    }
    case MD_BLOCK_CODE:  done_waiting(c); c->fenced = 0; tn_buf_puts(o, "</code></pre>\n"); break;
    case MD_BLOCK_HTML:  break;
    case MD_BLOCK_P:     done_waiting(c); tn_buf_puts(o, "</p>\n"); break;
    case MD_BLOCK_TABLE: tn_buf_puts(o, "</table>\n"); break;
    case MD_BLOCK_THEAD: tn_buf_puts(o, "</thead>\n"); break;
    case MD_BLOCK_TBODY: tn_buf_puts(o, "</tbody>\n"); break;
    case MD_BLOCK_TR:    done_waiting(c); tn_buf_puts(o, "</tr>\n"); break;
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

    int env = c->env_state;
    c->env_state = 0;
    if (c->img_depth) {                 /* inside an image only the alt text matters */
        c->img_depth++;
        return 0;
    }
    if (c->span_depth == SPAN_STACK) { c->span_over++; return 0; }
    int link = 0;
    if (type == MD_SPAN_STRONG && env == 2 && c->span_depth == 0) {
        c->env_state = 3;               /* perhaps "**Theorem.**": see leave_span */
        c->env_strong_pos = o->len;
        c->env_len = 0;
    }
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
        /* "#name" points at a heading of this note, by its id. */
        if (!ok && !raw.failed && raw.len > 1 && raw.data[0] == '#') {
            tn_buf_clear(&c->slug);
            tn_slug(&c->slug, raw.data + 1, raw.len - 1);
            if (c->slug.len && !c->slug.failed) {
                tn_buf_puts(o, "<a href=\"#tn-h-");
                tn_buf_put(o, c->slug.data, c->slug.len);
                tn_buf_puts(o, "\">");
                tn_buf_free(&raw); tn_buf_free(&esc);
                link = 1;
                break;
            }
        }
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
            tn_buf_puts(o, "<span class=\"tn-blocked-link\" title=\"Link removed: only http, https, mailto and #heading links are kept\">");
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
    tn_buf_clear(&c->tex);
    tn_math_clean(&c->tex, c->cap.data ? c->cap.data : "", c->cap.len);
    tn_put_escaped(o, c->tex.data ? c->tex.data : "", c->tex.len);
    tn_buf_puts(o, display ? "\\]</span>" : "\\)</span>");
    if (c->in_heading) {                /* in the outline, math is shown as written */
        tn_buf_putc(&c->htext, '$');
        tn_buf_put(&c->htext, c->tex.data, c->tex.len);
        tn_buf_putc(&c->htext, '$');
    }
    if (c->cap.failed || c->tex.failed) o->failed = 1;
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
    if (!c->span_depth || c->spans[c->span_depth - 1].type != type) { c->env_state = 0; return 0; }
    if (c->env_state == 3 && type == MD_SPAN_STRONG) {
        const char *name; size_t name_len;
        int kind = tn_env_head(c->env_raw, c->env_len, &name, &name_len);
        if (kind >= 0) {
            /* Rewrite the bold run as the block's head, and mark the block quote. */
            tn_buf *o = &c->out;
            char cls[64];
            o->len = c->env_strong_pos;
            tn_buf_puts(o, "<strong class=\"tn-env-head\">");
            tn_buf_puts(o, TN_ENVS[kind].name);
            if (TN_ENVS[kind].numbered) { tn_buf_putc(o, ' '); put_long(o, ++c->env_count); }
            if (name_len) {
                tn_buf_puts(o, " (");
                tn_put_escaped(o, name, name_len);
                tn_buf_putc(o, ')');
            }
            tn_buf_putc(o, '.');
            snprintf(cls, sizeof cls, " class=\"tn-env tn-%s\"", TN_ENVS[kind].id);
            out_insert(c, c->env_quote_pos, cls);
        }
    }
    c->env_state = 0;
    close_span(c);
    return 0;
}

/* ------------------------------------------------------------------ text */

static int text(MD_TEXTTYPE type, const MD_CHAR *s, MD_SIZE size, void *userdata)
{
    tn_ctx *c = userdata;
    size_t n = (size_t)size;
    int from_source = in_source(c, s);

    /* The first piece that points into the source locates a math span, and every
     * block still waiting for its line. */
    if (from_source && ((c->math && !c->math_line) || c->nwait || (c->in_heading && !c->h_line))) {
        const char *q = s;
        /* In math, step back over the white space after the opening $ or $$, so that
         * the line is the one the expression starts on, not the one its text starts on. */
        if (c->math) {
            while (q > c->src && (q[-1] == ' ' || q[-1] == '\t' || q[-1] == '\n' || q[-1] == '\r')) q--;
            /* In a block quote the lines between start with '>': step over those too,
             * if that leads back to the dollar signs. */
            const char *r = q;
            while (r > c->src && (r[-1] == ' ' || r[-1] == '\t' || r[-1] == '\n' || r[-1] == '\r' || r[-1] == '>')) r--;
            if (r > c->src && r[-1] == '$') q = r;
        }
        long line = line_of(c, q);
        if (c->math && !c->math_line) c->math_line = line;
        if (c->nwait || (c->in_heading && !c->h_line)) stamp_line(c, line);
    }

    if (c->env_state == 3 && type == MD_TEXT_NORMAL && !c->math && !c->img_depth &&
        c->env_len + n <= sizeof c->env_raw) {
        memcpy(c->env_raw + c->env_len, s, n);
        c->env_len += n;
    } else {
        c->env_state = 0;
    }

    if (c->math) {
        /* TeX: kept byte for byte. md4c hands a line break inside the span over as one
         * space that is not part of the source; it is a line break again here, which
         * is what lets a % comment end where its line ends. */
        if (!from_source && n == 1 && s[0] == ' ') tn_buf_putc(&c->cap, '\n');
        else tn_buf_put(&c->cap, s, n);
        return 0;
    }
    if (c->in_heading) {                /* the heading's text, for its id and the outline */
        if (type == MD_TEXT_BR || type == MD_TEXT_SOFTBR) tn_buf_putc(&c->htext, ' ');
        else if (type != MD_TEXT_NULLCHAR) tn_buf_put(&c->htext, s, n);
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

/* One pass gives both the HTML and the outline; "want_outline" says which to return. */
static int render(const char *md, size_t len, char **html, size_t *html_len, int want_outline)
{
    if (html) *html = NULL;
    if (html_len) *html_len = 0;
    if (!html || (!md && len)) return TN_ERR_ARG;
    if (len > TN_MAX_INPUT) return TN_ERR_TOO_LARGE;

    tn_ctx c;
    memset(&c, 0, sizeof c);
    tn_buf_init(&c.out); tn_buf_init(&c.cap);
    tn_buf_init(&c.img_src); tn_buf_init(&c.img_src_out); tn_buf_init(&c.img_title);
    tn_buf_init(&c.tex); tn_buf_init(&c.htext); tn_buf_init(&c.slug); tn_buf_init(&c.outline);
    tn_slugs_init(&c.ids);
    /* Formulas whose lines Markdown would misread are protected in a copy. */
    tn_buf pre;
    tn_buf_init(&pre);
    if (tn_protect_math(&pre, md ? md : "", len)) {
        if (pre.failed) { tn_buf_free(&pre); return TN_ERR_NOMEM; }
        md = pre.data; len = pre.len;
    }
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
    tn_buf *res = want_outline ? &c.outline : &c.out;
    tn_buf_putc(res, '\0');
    int failed = c.out.failed || c.cap.failed || c.outline.failed || c.ids.failed;

    tn_buf_free(&c.cap);
    tn_buf_free(&c.img_src); tn_buf_free(&c.img_src_out); tn_buf_free(&c.img_title);
    tn_buf_free(&c.tex); tn_buf_free(&c.htext); tn_buf_free(&c.slug);
    tn_buf_free(&pre);
    tn_slugs_free(&c.ids);
    tn_buf_free(want_outline ? &c.out : &c.outline);
    if (rc != 0 || failed) {
        tn_buf_free(res);
        return failed ? TN_ERR_NOMEM : TN_ERR_PARSE;
    }
    *html = res->data;
    if (html_len) *html_len = res->len - 1;
    return TN_OK;
}

int tn_render(const char *md, size_t len, char **html, size_t *html_len)
{
    return render(md, len, html, html_len, 0);
}

int tn_outline(const char *md, size_t len, char **outline, size_t *outline_len)
{
    return render(md, len, outline, outline_len, 1);
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
