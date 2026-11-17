/* Internal to the tnotes core. */
#ifndef TN_INTERNAL_H
#define TN_INTERNAL_H

#include <stddef.h>

/* A growable byte buffer. After a failed allocation "failed" is set and every later
 * append is a no-op, so callers check once at the end. */
typedef struct {
    char  *data;
    size_t len, cap;
    int    failed;
} tn_buf;

void tn_buf_init(tn_buf *b);
void tn_buf_free(tn_buf *b);
void tn_buf_clear(tn_buf *b);
void tn_buf_put(tn_buf *b, const char *s, size_t n);
void tn_buf_puts(tn_buf *b, const char *s);
void tn_buf_putc(tn_buf *b, char c);
/* Insert n bytes at offset pos (pos <= len). */
void tn_buf_insert(tn_buf *b, size_t pos, const char *s, size_t n);

/* Append text escaped for HTML (element content or a double-quoted attribute): & < > "
 * and ' become character references, invalid UTF-8 and control characters other than
 * tab, line feed and carriage return become U+FFFD. */
void tn_put_escaped(tn_buf *b, const char *s, size_t n);

/* Append text as plain UTF-8, unescaped: invalid sequences become U+FFFD, and tab, line
 * breaks and other control characters become one space. For the outline. */
void tn_put_plain(tn_buf *b, const char *s, size_t n);

/* Append a URL for a double-quoted attribute: characters outside the usual URL set
 * are percent-encoded, & is written &amp;. */
void tn_put_url(tn_buf *b, const char *s, size_t n);

/* Is s[0..n) a syntactically valid character reference (&name; &#123; &#x1F;)? */
int tn_is_entity(const char *s, size_t n);

/* Does the address start with one of the schemes we keep? */
int tn_link_allowed(const char *s, size_t n);    /* http://, https://, mailto: */
int tn_image_allowed(const char *s, size_t n);   /* https:// only */

/* ---- math ------------------------------------------------------------------------ */

/* Append the TeX of a math span, cleaned: a TeX comment (an unescaped % up to and
 * including the end of its line, and the white space that starts the next line) is
 * removed, and so is white space at both ends. Nothing else changes. */
void tn_math_clean(tn_buf *dst, const char *s, size_t n);

/* Markdown decides what a line is before it looks for math, so inside
 *     $$
 *     a = b
 *     + c
 *     $$
 * the line "+ c" would start a list and cut the formula in two. This finds formulas
 * written that way (a line that is just $$, then lines, then a line that is just $$,
 * with no blank line between and not in a fenced code block), and indents by four
 * spaces each line in between that could start a block. Markdown then reads it as a
 * continuation of the paragraph, and drops the indentation again.
 * Returns 1 and fills dst if anything had to change; 0 (dst untouched) otherwise. No
 * line break is added or removed, so line numbers are the same in both texts. */
int tn_protect_math(tn_buf *dst, const char *md, size_t len);

/* ---- headings -------------------------------------------------------------------- */

/* Append the slug of a heading's text: ASCII letters (lowercased) and digits, with
 * every run of anything else as one hyphen, none at the ends, at most 64 bytes. It
 * can be empty. The result uses only a-z, 0-9 and '-'. */
void tn_slug(tn_buf *b, const char *s, size_t n);

/* The set of slugs used so far in one document, to keep heading ids unique. */
typedef struct {
    tn_buf  pool;             /* the keys, each NUL-terminated */
    size_t *slot;             /* open addressing: offset into pool + 1, or 0 */
    size_t *count;            /* per slot: how many times the key was asked for */
    size_t  cap, used;
    int     failed;
} tn_slugs;

void tn_slugs_init(tn_slugs *t);
void tn_slugs_free(tn_slugs *t);
/* Make the slug in b (all of b) unique in t by appending -2, -3, ... if needed, and
 * record it. An empty slug becomes "section" first. */
void tn_slugs_unique(tn_slugs *t, tn_buf *b);

/* ---- theorem-like blocks --------------------------------------------------------- */

typedef struct {
    const char *name;         /* "Theorem" */
    const char *id;           /* "theorem": the CSS class suffix and the LaTeX environment */
    int         numbered;
} tn_env_kind;

extern const tn_env_kind TN_ENVS[];

/* Is the text of a bold run the head of a theorem-like block: a kind's name, an
 * optional "(name)", and an optional final '.' or ':'? Returns the index into TN_ENVS
 * or -1. *name and *name_len receive the text inside the brackets (length 0 if none). */
int tn_env_head(const char *s, size_t n, const char **name, size_t *name_len);

#endif
