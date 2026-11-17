# travel_notes architecture

travel_notes (version 0.2.0) turns Markdown with LaTeX math into a typeset note, in the
browser, from static files. The same core also writes the note as a LaTeX document. This document says how it is put together, what each part
guarantees, and which choices were made where the brief left them open.

It is also the pattern for the owner's other small web tools: a C core with no user
interface, compiled natively for testing and to WebAssembly for the page, with a thin
front end on top.

## 1. Constraints

These come from where the tool is hosted (GitHub Pages) and are not up for trade.

1. **Static only.** No backend, account, database or API call.
2. **No third-party requests of our own.** Scripts, styles and MathJax are vendored and
   served from the same origin. The only outside requests the page can make are images
   whose `https` addresses the user wrote into their own note.
3. **Light on the visitor's machine.** No work while idle. The core and MathJax are
   loaded on the first Compile, not on page load.
4. **Public repository.** No secrets. Test fixtures are invented text.
5. **Small.** The deployed site is 2.5 MB; the limit is 10 MB.
6. **Single-threaded WebAssembly.** Pages cannot send the headers that threads need.

## 2. Layering

```
   cli/main.c  (tnotes: file in; HTML, LaTeX or outline out)   web/app.js  (the page)
                 \                                          /        \
                  v                                        v          v
              include/tnotes.h   -- the only header a front end uses   MathJax
          +----------------------------------------------------+   (typesets the
          | tn_render   md4c callbacks -> HTML, and the outline |    math spans)
          | tn_latex    md4c callbacks -> LaTeX                 |
          | tn_buf      growable buffer, escaping, address      |
          |             rules, and what both renderers share:   |
          |             math cleaning, heading slugs, theorem   |
          |             heads, protecting formulas              |
          +----------------------------------------------------+
          third_party/md4c   (the Markdown parser, unmodified)
```

Rules:

1. The core has no user interface, prints nothing and keeps no global state.
2. A front end never builds HTML from the note's text. The page inserts the core's output
   into the preview, and handles everything else as text.
3. Vendored code is not edited. md4c is compiled as it is.

## 3. The core

The API is three functions of one shape,
`int tn_render(const char *md, size_t len, char **out, size_t *out_len)` for HTML,
`tn_latex` for LaTeX and `tn_outline` for the list of headings; then `tn_free`,
`tn_version`, `tn_strerror`, and the one-value wrappers `tn_render_cstr` and
`tn_latex_cstr` that the web build calls.

md4c parses; `src/tn_render.c` writes the HTML from md4c's callbacks. md4c's own HTML
renderer (`md4c-html.c`) is not used, because the rules below differ from plain
Markdown-to-HTML.

md4c flags: math spans on, raw HTML off, tables, strikethrough and task lists on.

md4c reports a block before its text, and two things the HTML needs are known only
later: a block's source line (from its first text) and a heading's id (from all its
text). So `tn_render.c` remembers the offset of a tag it has written and inserts the
attribute when it is known. One function, `out_insert`, does every insertion and moves
the remembered offsets that lie after it.

### What the output guarantees

Whatever the input bytes are:

- **Tags come from a fixed list**, each with a fixed list of attributes, always written
  `name="value"`. There is no `style` attribute, no event handler, no `<script>`.
- **`id`, `data-line` and `class` hold no text from the note.** An id is `tn-h-` and a
  slug of a to z, 0 to 9 and hyphens; a line is a number; classes are the core's own
  words. The one exception is the language of a code block (`class="language-..."`),
  which is escaped.
- **Nothing from the input is written unescaped.** `& < > " '` become character
  references. A character reference written in the note (`&copy;`, `&#60;`) is passed
  through as written, after its syntax is checked.
- **Raw HTML is never passed through.** md4c is told not to recognise it, so it arrives
  as text and is escaped like any other text.
- **Links** keep an address only if it starts with `http://`, `https://` or `mailto:`
  (any case) and contains no control character or space. Otherwise the link text is kept
  inside `<span class="tn-blocked-link">`. A kept link gets
  `rel="noopener noreferrer" target="_blank"`, so following it never discards the note.
  Relative links are dropped too: in a one-page editor they lead nowhere useful.
  The one address kept besides those is `#name`: it becomes `href="#tn-h-` and the slug
  of the name, which is the id of the heading with that text.
- **Images** keep an address only if it starts with `https://`. A kept image is
  `<img src alt [title] loading="lazy" decoding="async" referrerpolicy="no-referrer">`,
  with the description as `alt`. Anything else becomes
  `<span class="tn-blocked-image">description</span>`.
- **Addresses are percent-encoded** outside the usual URL characters, so a quote in an
  address cannot end the attribute.
- **The output is valid UTF-8.** Invalid sequences, and control characters other than
  tab, LF and CR, become U+FFFD.
- **Tags are properly nested and all closed**, even when md4c reports the end of a span it
  never opened. (md4c 0.5.2 does that on some malformed nested links; the fuzzer found
  it. The renderer keeps its own stack of open spans, ignores a closing that does not
  match, and closes whatever is open at each block boundary.)

`tests/tn_check.h` is a checker for exactly these rules. The unit tests and the fuzzer
run every output through it.

### Math

A math span is emitted as

```html
<span class="tn-math tn-inline"  data-line="12">\(TeX\)</span>
<span class="tn-math tn-display" data-line="14">\[TeX\]</span>
```

- The TeX is the bytes between the dollar signs, escaped for HTML: md4c applies no
  Markdown rule inside a math span. Two things are changed (`tn_math_clean`). A TeX
  comment is removed: an unescaped `%`, the rest of its line, the line break and the
  white space that starts the next line, which is how TeX reads it. And white space at
  the two ends goes, except a final space that a backslash makes a control space.
- Line breaks inside the formula are kept. md4c hands a line break in a math span over
  as a single space that does not point into the source; that is how the renderer tells
  it from a space the author typed, and it is what lets a comment end where its line
  ends.
- `data-line` is the 1-based line of the source on which the expression starts. md4c
  hands the callbacks pointers into the source buffer, so the line is found by counting
  newlines up to the pointer (stepping back over the white space after an opening `$$`).
  It is present whenever md4c gives such a pointer, which in practice is always.
- A display formula is a `<span>` with `display: block`, not a `<div>`: md4c reports it
  as a span inside a paragraph, and a `<div>` inside `<p>` is not valid HTML.

### Formulas of several lines

Markdown decides what a line is before it looks for math. Inside

```
$$
a = b
+ c
$$
```

the line `+ c` would start a list and cut the formula in two; `- c`, `# c`, `===` and
`1. c` do the same in their ways. `tn_protect_math` runs before the parser. It finds
formulas written like this (a line that is just `$$`, lines, a line that is just `$$`,
no blank line between, not in a fenced code block) and, in a copy of the source,
indents by four spaces each line in between that could start a block. Markdown reads an
indented line after a paragraph line as more of the paragraph, and drops the
indentation. No line break is added or removed, so line numbers are the same in the
copy. It is deliberately narrow: an opening `$$` indented by four spaces or more is left
alone, because that may be code, and a blank line inside a formula still ends it.

### Source lines

Paragraphs, headings, list items, code blocks and table rows carry `data-line`, the line
on which their first text stands (for a fenced code block, the line of the fence). A
block with no text has none. The line is that of the innermost block's own text, so in
`<li><p>` both carry it. At most sixteen blocks wait for a line at once; nested deeper,
the outermost go without.

### Headings and the outline

A heading's id is `tn-h-` and a slug of its text (`tn_slug`): ASCII letters, lowered, and
digits, with one hyphen for each run of anything else, at most 64 bytes. Math counts as
its TeX. An empty slug is `section`. A slug that is taken gets `-2`, `-3`, and so on; the
set of used slugs is a hash table that keeps a counter per slug, so a note of twenty
thousand equal headings is not slow. The prefix keeps a heading called "Status" from
taking the id of an element of the page.

`tn_outline` returns one line per heading: level, source line, id, and the text as
written. It is the same pass as the HTML, returning a different buffer. The LaTeX
writer makes the same slugs in the same order, so `\label` and `id` agree.

### Theorem-like blocks

A block quote becomes a theorem-like block when its first paragraph starts with a bold
run that is exactly a kind's name, an optional `(name)`, and an optional final `.` or
`:` (`tn_env_head`). The renderer follows three states: quote opened, its first paragraph
opened, reading the bold run that opens it. Anything else in between cancels. On a
match the bold run is rewritten as `<strong class="tn-env-head">Theorem 3 (name).</strong>`
and the block quote gets `class="tn-env tn-theorem"`. All kinds but Proof share one
counter. The bold run must be plain text: a head with emphasis or math inside it, or
with a number written by hand, is left as an ordinary quotation.

### LaTeX

`src/tn_latex.c` is a second set of callbacks on the same parser, writing an `amsart`
document.

- **Prose is escaped** character by character (`put_text`), so no text of a note becomes
  a TeX command. Addresses are percent-encoded down to a small set and then written for
  `\href`. Invalid UTF-8 becomes `?` and control characters are dropped.
- **Math is the author's TeX**, copied after `tn_math_clean`. It is not checked: a note
  can say in math whatever TeX can do, as any `.tex` file can. Someone who compiles a
  note they did not write is compiling TeX they did not write.
- **Definitions are moved out of formulas.** `\newcommand` and `\renewcommand` at the
  start of a formula are written in front of it. MathJax keeps a definition after the
  formula ends and LaTeX does not; moving it makes the two agree.
- **A display formula that is itself an environment** (`align`, `gather`, `equation`,
  `multline`, `flalign`, `alignat`, `eqnarray`, starred or not) is written without
  `\[ \]`. Display math is written with no blank line around it, because a blank line
  is a paragraph break in LaTeX.
- **Title and levels.** A level-1 heading that is the first block is the `\title`. If it
  is the only level-1 heading, the rest move up a level. A first, small pass over the
  note finds this out, and whether `ulem` is needed for strikethrough.
- **Nesting.** LaTeX allows four levels of a list. Deeper lists and quotations are
  flattened: items become paragraphs that start with a dash.
- **Brackets.** A `[` right after `\item` or `\\` would be read as an optional argument;
  it is written `{[}` there.
- **Code** is `verbatim`, unless it contains `\end{verbatim}`; then it is typeset line by
  line in the typewriter font, escaped.
- **Images** cannot be fetched by TeX. An image is `[Image: description]`, linked to its
  address if the preview would load it.

`make latex-test` compiles the LaTeX of three golden notes with pdflatex.

### Limits

Input larger than 8 MB (`TN_MAX_INPUT`) is refused before any work. Buffers grow by
doubling and a failed allocation ends the render with `TN_ERR_NOMEM`. Open spans are
tracked to a depth of 64; deeper ones write no tags.

## 4. The page

`web/` is plain HTML, CSS and JavaScript: no framework, no bundler, nothing from npm.

```
index.html   the layout: toolbar, source, divider, preview, error list, status bar
style.css    colours, layout, the rendered note, the print stylesheet
theme.js     applies the remembered theme before the first paint (a few lines)
app.js       everything else
```

### When things run

| When | What |
|---|---|
| Page load | `index.html`, `style.css`, `theme.js`, `app.js`: 37 KB in all. The draft is restored. |
| First Compile | `tnotes.js` and `tnotes.wasm` (126 KB) and MathJax (2.3 MB) are fetched. |
| Every Compile | core → HTML → preview → MathJax → error list and outline. Images load now. |
| Download .tex | core → LaTeX. The core is loaded if it is not yet; MathJax is not needed. |
| Typing | One debounced timer (800 ms) that saves the draft. Nothing is typeset. |
| Idle | Nothing: no timer, no polling, no animation frame. The browser check measures this. |

### Compile

1. The source goes through the core (WebAssembly) and comes back as HTML.
2. `MathJax.typesetClear([preview])` releases what the previous compile left in MathJax.
3. `MathJax.startup.getComponents()` builds a fresh TeX input processor and
   `MathJax.texReset()` resets equation numbers. A fresh processor has no macros from
   earlier compiles, so the result depends only on the text. The browser check proves
   it: a macro defined in one compile is an error in the next.
4. `preview.innerHTML = html`. This is the only place HTML is inserted.
5. `MathJax.typesetPromise([preview])` typesets the note.
6. Errors are collected.

**MathJax finds math through the delimiters the core wrote.** It is configured to ignore
the preview (`ignoreHtmlClass: "tn-doc"`) except inside the math spans
(`processHtmlClass: "tn-math"`), with `\( \)` and `\[ \]` as the only delimiters. So
text and code that merely look like TeX are left alone. The whole note is typeset as one
MathJax document, rather than one expression at a time, because that is what makes
equation numbers and forward `\eqref` references come out right.

**Configuration:** TeX input, SVG output, `tags: "ams"`. The package `noundefined` is
removed, so an unknown command is an error in the log and not red text in the output.
`fontCache: "local"` keeps each formula's SVG self-contained, which prints reliably.
The MathJax menu is off.

### The error log

MathJax has no `.log` file, so the page builds one. Before typesetting, it records each
math span's TeX and line. After typesetting, a span whose output contains an element with
`data-mjx-error` has failed: its message, TeX and line go into the list, and the span is
marked in the preview. The status bar shows the count; the panel lists the errors, each
with a button that selects its line in the source; **Download .log** saves the list as
plain text.

### Outline, Locate, links to headings

These three use only what the core wrote into the HTML.

- **Outline** reads the headings with an id from the preview after each compile. An
  entry shows the heading and selects its line in the source.
- **Locate** (`Ctrl+J`) finds the last block whose `data-line` is at or before the
  cursor's line and shows it, outlined. The outline stays until the next Locate or
  Compile: removing it after a moment would need a timer.
- **Double-clicking** a block in the preview selects its line in the source.
- **A link to a heading** scrolls the preview to it. The click is handled by the page,
  so the address bar and the history do not change.

### Files, images, PDF

- **Open** reads a file with the File API. **Download .md** hands the browser a Blob, and
  so does **Download .tex**, with the core's LaTeX in it.
  The file name is the first ATX heading, reduced to letters and digits joined by
  hyphens, or `note`.
- **Images** are in the HTML only after Compile, so nothing is fetched while typing. An
  image that fails to load is replaced by its description in a marked box.
- **Download PDF** compiles if the preview is stale, asks every image to load now, waits
  for them for at most 4 seconds (a missing one prints as its description), and calls
  `window.print()`. The print stylesheet shows only the note, in the light palette on
  white, with no page break inside a formula or straight after a heading. This is a
  deliberate trade for version 0.1: no TeX engine and no PDF library.

### Storage

All of `wmahaney.github.io` shares one storage origin, so every key starts with
`travel_notes:`.

| Key | Holds |
|---|---|
| `travel_notes:draft` | the source text |
| `travel_notes:theme` | `light` or `dark`, once the toggle has been used |

Every access is wrapped: if storage is full or blocked, the editor keeps its text, the
status bar says the draft could not be saved, and the browser asks before the tab is
closed. The draft is also saved when the tab is hidden or closed.

### Addresses

Every address in the page is relative (`style.css`, `tnotes.js`,
`vendor/mathjax/tex-svg-full.js`). The repository's name appears nowhere in the site.
The browser check serves the site from a sub-path to prove it. The one absolute address
is the link to the owner's page, `https://wmahaney.github.io/code/`.

## 5. Security

The page carries this policy in a `<meta>` tag:

```
default-src 'none';
script-src 'self' 'wasm-unsafe-eval';
connect-src 'self';
style-src 'self' 'unsafe-inline';
font-src 'self' data:;
img-src 'self' data: https:;
base-uri 'none'; form-action 'none'
```

- `script-src 'self'`: no inline script, no `eval`. `'wasm-unsafe-eval'` is what lets the
  browser compile the WebAssembly module, and nothing more. The Emscripten glue is built
  with `-sDYNAMIC_EXECUTION=0`, so it contains no `eval` either.
- `connect-src 'self'`: needed to fetch `tnotes.wasm`.
- `img-src https:`: for the images a note asks for. The core emits no other kind.
- **The one loosening: `style-src 'unsafe-inline'`.** MathJax's SVG output writes a
  `<style>` element and `style` attributes on the formulas it draws (for vertical
  alignment), and there is no way to give those a nonce from a static page. It allows
  inline styles only, not inline scripts. The core never emits a `style` attribute, so
  the note's own text cannot use it.
- Because the page is static, `frame-ancestors` cannot be set (it is ignored in a
  `<meta>` policy).

Besides the policy, the page sends no referrer (`<meta name="referrer">`, and
`referrerpolicy` on every image).

## 6. Look

The colours are those of the owner's site, https://wmahaney.github.io, taken from its
theme files as they are.

| Property | Light | Dark | Source in `wmahaney/wmahaney.github.io` |
|---|---|---|---|
| `--bg` | `#FBF5E6` | `#15171C` | `--wm-bg` in `assets/theme-light.scss`, `assets/theme-dark.scss` |
| `--panel` | `#FFFDF7` | `#1E2128` | `--wm-surface`, same files |
| `--text` | `#1E2430` | `#F2EDE0` | `--wm-ink` |
| `--muted` | `#4A5160` | `#B3B8C5` | `--wm-muted` |
| `--border` | `#E3D8BA` | `#343947` | `--wm-line` |
| `--accent` | `#2B4FB8` | `#9DB3F7` | `--wm-accent` |
| `--accent-text` | `#FFFFFF` | `#15171C` | `--wm-accent-ink` |
| `--tint` | `#F2E9CF` | `#262A33` | `--wm-tint` (code blocks, table headings) |
| `--danger` | `#B93A2B` | `#F08D7C` | `--wm-red` (errors, blocked links and images) |

The last three are not among the six names the tools share, but come from the same two
files. Printing uses the light values on a white page.

Fonts: the system UI stack for the interface, the system monospace for the editor, and
the site's reading stack (`Georgia, "Times New Roman", Times, serif`, from
`$font-family-base`) for the note. The site's menu font, Inter, is fetched from Google
and is not used here. Toolbar 44 px, status bar 28 px, 6 px corners, no shadows, and no
animation other than a 120 ms colour transition.

Dark mode follows `prefers-color-scheme` until the toggle is used; the choice is then
remembered. MathJax's SVG uses `currentColor`, so math follows the text colour.

## 7. Tests

| Test | What it covers |
|---|---|
| `tests/test_core.c` | 959 checks. Exact HTML for Markdown, math next to Markdown syntax, comments, formulas of several lines, source lines, heading ids and links, the outline, theorem-like blocks, link and image rules, raw HTML, invalid UTF-8, NUL, the API, 400 KB lines, 2000-deep quotes. Exact LaTeX for the same ground: escaping, titles and levels, math, definitions, links, lists, nesting, tables, code |
| `tests/hostile/` | Files of raw HTML, script addresses in every spelling, attribute breakouts, hostile math, long lines, invalid bytes. Each must render without a crash, its HTML must pass the checker, and its LaTeX and outline must be clean UTF-8 |
| `tests/golden/` | Four notes with their exact expected HTML, LaTeX and outline |
| `tests/fuzz.c` | Mutates the golden and hostile files (byte flips, deletions, copies, spliced tokens) and checks the HTML of every one, and that the LaTeX and the outline are clean UTF-8, under the sanitizers. `make fuzz-smoke` runs 20000; 300000 were run before release |
| `make latex-test` | pdflatex compiles the LaTeX of the golden notes `math`, `images` and `theorems`, twice each. `basics` is compared but not compiled: its prose has characters pdfLaTeX does not know |
| `make asan` | The whole suite with the core and md4c under AddressSanitizer and UBSan |
| `tests/browser/check.mjs` | The built site in headless Chrome: first load under 300 KB, compile, numbering, theorem-like blocks, heading ids, comments, source lines, the outline, a link to a heading, double-click, Locate, the `.tex` download, the error log with its line, macro reset, file name, idle (no timers), theme, print, draft recovery, a failing store, no request to another origin except the note's image, no policy violation |

## 8. Build and deploy

One Makefile: `make`, `make test`, `make asan`, `make fuzz-smoke`, `make latex-test`,
`make web`, `make serve`, `make browser-test`. `make web` writes the site to `build/web/`.

The WebAssembly build is one `emcc` command over the core and md4c: `MODULARIZE`,
`ENVIRONMENT=web`, `FILESYSTEM=0`, `DYNAMIC_EXECUTION=0`, `ALLOW_MEMORY_GROWTH`, and five
exported functions.

`.github/workflows/pages.yml` runs the tests, installs TeX Live and compiles the LaTeX,
builds, checks the size, runs the browser check, and on `main` deploys. Every action is pinned to a commit. The workflow has
`contents: read`; only the deploy job has `pages: write` and `id-token: write`.

**Emscripten is pinned to 3.1.6.** That is the version this release was built and tested
with (it is what Ubuntu 24.04 packages). Newer releases should work, but none was
available to test against when this was written. To move: change `EMSDK_VERSION` in the
workflow, push a branch, and let the browser check decide.

## 9. Choices made

Where the brief left a choice open, the simpler option was taken.

| Choice | Taken | Why |
|---|---|---|
| HTML renderer | Our own callbacks on md4c's parser | The output rules (math spans, address filtering, nesting) are ours; md4c's renderer would need editing |
| MathJax version | 3.2.2 | One self-contained file per configuration. Version 4 ships its fonts as separate packages loaded on demand, which is more to vendor and more requests |
| MathJax component | `tex-svg-full.js` (2.3 MB) | The only file needed. `tex-svg.js` is 0.2 MB smaller but loads TeX extensions from other files on demand |
| How MathJax finds math | Delimiters written by the core, one document per compile | Equation numbers and forward references need the document pass |
| Resetting TeX state | A fresh input processor per compile | The documented way to drop macros; cheap next to typesetting |
| Unknown TeX commands | Errors | The brief asks for an error log; red text in the output is easy to miss |
| Links | Open in a new tab; relative links dropped | Following a link must not lose the draft |
| Character references in the note | Passed through as written | No entity table to carry; the browser decodes them |
| Preview divider | Draggable, with arrow keys; position not remembered | Optional in the brief; remembering it would be one more stored key |
| First visit | A short starter note | Shows the math syntax without a manual |
| Favicon | An empty `data:` icon | Otherwise the browser asks the server for `/favicon.ico` |
| `theme.js` | A separate small file | The policy allows no inline script, and the theme must apply before the first paint |
| PDF | The browser's print dialog | No TeX engine, no PDF library |
| Math delimiters | Dollar signs only | The owner's choice. `\( \)` and a bare `\begin{align}` are not read as math |
| TeX comments | `%` to the end of the line, the line break, and the next line's leading white space | What TeX does. A comment on a line of its own leaves nothing behind |
| Line breaks in math | Kept | Comments need them, and the LaTeX output reads better |
| Formulas of several lines | Protected only between two `$$` lines | The one form that can be found without parsing Markdown twice; see "Formulas of several lines" |
| Which blocks carry a line | Paragraphs, headings, list items, code blocks, table rows | The innermost block under the pointer is what a double-click finds |
| Heading ids | `tn-h-` and an ASCII slug | Stable when the note is edited elsewhere; safe as an attribute and as an address; cannot take an id of the page |
| Outline in the page | Read from the preview's headings | The HTML already carries ids and lines; `tn_outline` serves the command line and other front ends |
| Theorem syntax | A block quote that opens with a bold kind | It is ordinary Markdown: the note reads the same anywhere else |
| Theorem numbers | One counter for all kinds but Proof | The simplest scheme that is common in papers; it is also what the LaTeX output declares |
| LaTeX class | `amsart` | The owner's default for anything that is not a talk |
| Definitions in math | Moved in front of their formula in LaTeX | A definition inside a formula is local in LaTeX and lasting in MathJax |
| `ulem` | Loaded only if the note strikes text through | One package fewer for most notes |
| Locate's mark | Stays until the next Locate or Compile | Fading it would need a timer, and the page has none while idle |

## 10. Not implemented

These are absent by design in 0.1; each explains a choice above.

- **Typesetting while typing.** Compile is explicit, which is what keeps the page idle.
- **A service worker.** The page needs the network on first load and on the first Compile.
- **Local image files.** They would have to be stored in the browser or embedded in the
  `.md`; the core accepts `https` addresses only.
- **A real TeX engine**, and LaTeX packages beyond MathJax's.
- **Accounts, sync, share links.** There is no backend.
- **Mobile polish.** Narrow screens get one pane above the other and nothing more.
- **`\( \)` and `\[ \]` as delimiters.** Dollar signs are the one way to write math.
- **Footnotes.** md4c has no footnote syntax.
- **References to theorem numbers**, and numbers written into a head by hand. A head with
  a number is left as the author wrote it.
- **Letters outside a to z in heading ids.** A heading written in another script gets
  `section`, `section-2`, and so on.
- **Protecting a formula that does not stand between two `$$` lines**, or that contains a
  blank line. Markdown still cuts those.
