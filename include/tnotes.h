/* tnotes: Markdown with LaTeX math to a safe HTML fragment.
 *
 * The core of travel_notes. It has no user interface and no dependency beyond the C
 * library and md4c (vendored in third_party/). The command-line tool in cli/ and the
 * web page in web/ are thin front ends on top of it.
 *
 * What the HTML is, and is not:
 *   - Math written as $...$ or $$...$$ is not typeset here. It is passed through
 *     untouched by Markdown rules and emitted as
 *         <span class="tn-math tn-inline"  data-line="N">\(TeX\)</span>
 *         <span class="tn-math tn-display" data-line="N">\[TeX\]</span>
 *     with the TeX escaped for HTML, for MathJax to typeset later. N is the line of
 *     the source on which the expression starts (1-based). TeX comments (% to the end
 *     of the line) are removed, and line breaks inside the formula are kept.
 *   - Paragraphs, headings, list items, code blocks and table rows carry data-line too.
 *   - A heading has an id, "tn-h-" and a slug of its text, unique in the note. A link
 *     to "#slug" points at it.
 *   - A block quote that opens with a bold "Theorem.", "Lemma (name).", "Proof." and so
 *     on becomes a theorem-like block: class "tn-env tn-theorem", and a numbered head.
 *   - Raw HTML in the input is never passed through; it comes out as visible text.
 *   - Links keep only http, https and mailto addresses, and "#heading". Images keep only https
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

#define TN_VERSION "0.2.0"

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

/* The note's headings, one per line, in order:
 *     LEVEL <tab> LINE <tab> ID <tab> TEXT <newline>
 * LEVEL is 1 to 6. LINE is the heading's line in the source (0 if not known). ID is the
 * id the heading has in the HTML ("tn-h-..."). TEXT is the heading as written, math
 * between dollar signs, as valid UTF-8 without tabs or line breaks. Same results and
 * ownership as tn_render. */
int tn_outline(const char *md, size_t len, char **outline, size_t *outline_len);

/* The note as a LaTeX document (amsart). Prose is escaped; math is copied as written,
 * without its comments; \newcommand and \renewcommand at the start of a formula are
 * moved in front of it, so that they last as they do in the preview. Same results and
 * ownership as tn_render. */
int tn_latex(const char *md, size_t len, char **tex, size_t *tex_len);
char *tn_latex_cstr(const char *md, size_t len);

void tn_free(void *p);

const char *tn_version(void);
const char *tn_strerror(int code);

#ifdef __cplusplus
}
#endif

#endif
