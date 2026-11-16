/* tnotes: Markdown with LaTeX math to a safe HTML fragment.
 *
 * The core of travel_notes. It has no user interface and no dependency beyond the C
 * library and md4c (vendored in third_party/). The command-line tool in cli/ and the
 * web page in web/ are thin front ends on top of it.
 *
 * What the output is, and is not:
 *   - Math written as $...$ or $$...$$ is not typeset here. It is passed through
 *     untouched by Markdown rules and emitted as
 *         <span class="tn-math tn-inline"  data-line="N">\( TeX \)</span>
 *         <span class="tn-math tn-display" data-line="N">\[ TeX \]</span>
 *     with the TeX escaped for HTML, for MathJax to typeset later. N is the line of
 *     the source on which the expression starts (1-based).
 *   - Raw HTML in the input is never passed through; it comes out as visible text.
 *   - Links keep only http, https and mailto addresses. Images keep only https
 *     addresses. Anything else becomes a visibly marked placeholder.
 *   - The output is valid UTF-8 whatever the input bytes were.
 *
 * Pre-1.0: this API may change in any 0.x release.
 */
#ifndef TNOTES_H
#define TNOTES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TN_VERSION "0.1.0"

/* Largest accepted input, in bytes. Larger input is refused before any work is done. */
#define TN_MAX_INPUT ((size_t)8 * 1024 * 1024)

enum {
    TN_OK = 0,
    TN_ERR_ARG,        /* a null pointer where one is not allowed */
    TN_ERR_TOO_LARGE,  /* input longer than TN_MAX_INPUT */
    TN_ERR_NOMEM,      /* out of memory */
    TN_ERR_PARSE       /* the Markdown parser failed */
};

/* Render len bytes of Markdown. On TN_OK, *html is a NUL-terminated buffer of
 * *html_len bytes (not counting the NUL), to be released with tn_free. On any other
 * result *html is NULL. html_len may be NULL. */
int tn_render(const char *md, size_t len, char **html, size_t *html_len);

/* The same, for callers that want one return value: the HTML, or NULL on failure. */
char *tn_render_cstr(const char *md, size_t len);

void tn_free(void *p);

const char *tn_version(void);
const char *tn_strerror(int code);

#ifdef __cplusplus
}
#endif

#endif
