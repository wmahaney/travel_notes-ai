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

/* Append text escaped for HTML (element content or a double-quoted attribute): & < > "
 * and ' become character references, invalid UTF-8 and control characters other than
 * tab, line feed and carriage return become U+FFFD. */
void tn_put_escaped(tn_buf *b, const char *s, size_t n);

/* Append a URL for a double-quoted attribute: characters outside the usual URL set
 * are percent-encoded, & is written &amp;. */
void tn_put_url(tn_buf *b, const char *s, size_t n);

/* Is s[0..n) a syntactically valid character reference (&name; &#123; &#x1F;)? */
int tn_is_entity(const char *s, size_t n);

/* Does the address start with one of the schemes we keep? */
int tn_link_allowed(const char *s, size_t n);    /* http://, https://, mailto: */
int tn_image_allowed(const char *s, size_t n);   /* https:// only */

#endif
