# travel_notes architecture

travel_notes (version 0.1.0) turns Markdown with LaTeX math into a typeset note, in the
browser, from static files. This document says how it is put together, what each part
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
5. **Small.** The deployed site is 2.4 MB; the limit is 10 MB.
6. **Single-threaded WebAssembly.** Pages cannot send the headers that threads need.

## 2. Layering

```
   cli/main.c  (tnotes: file in, HTML fragment out)        web/app.js  (the page)
                 \                                          /        \
                  v                                        v          v
              include/tnotes.h   -- the only header a front end uses   MathJax
          +----------------------------------------------------+   (typesets the
          | tn_render   md4c callbacks -> HTML                  |    math spans)
          | tn_buf      growable buffer, escaping, address rules|
          +----------------------------------------------------+
          third_party/md4c   (the Markdown parser, unmodified)
```

Rules:

1. The core has no user interface, prints nothing and keeps no global state.
2. A front end never builds HTML from the note's text. The page inserts the core's output
   into the preview, and handles everything else as text.
3. Vendored code is not edited. md4c is compiled as it is.

## 3. The core

`int tn_render(const char *md, size_t len, char **html, size_t *html_len)` is the whole
API, apart from `tn_free`, `tn_version`, `tn_strerror` and the one-value wrapper
`tn_render_cstr` that the web build calls.

md4c parses; `src/tn_render.c` writes the HTML from md4c's callbacks. md4c's own HTML
renderer (`md4c-html.c`) is not used, because the rules below differ from plain
Markdown-to-HTML.

md4c flags: math spans on, raw HTML off, tables, strikethrough and task lists on.

### What the output guarantees

Whatever the input bytes are:

- **Tags come from a fixed list**, each with a fixed list of attributes, always written
  `name="value"`. There is no `style` attribute, no event handler, no `<script>`.
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
<span class="tn-math tn-inline"  data-line="12">\( TeX \)</span>
<span class="tn-math tn-display" data-line="14">\[ TeX \]</span>
```

- The TeX is the bytes between the dollar signs, escaped for HTML and otherwise
  untouched: md4c applies no Markdown rule inside a math span. md4c turns a line break
  inside the span into one space.
- `data-line` is the 1-based line of the source on which the expression starts. md4c
  hands the callbacks pointers into the source buffer, so the line is found by counting
  newlines up to the pointer (stepping back over the white space after an opening `$$`).
  It is present whenever md4c gives such a pointer, which in practice is always.
- A display formula is a `<span>` with `display: block`, not a `<div>`: md4c reports it
  as a span inside a paragraph, and a `<div>` inside `<p>` is not valid HTML.

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
| Page load | `index.html`, `style.css`, `theme.js`, `app.js`: 29 KB in all. The draft is restored. |
| First Compile | `tnotes.js` and `tnotes.wasm` (104 KB) and MathJax (2.3 MB) are fetched. |
| Every Compile | core → HTML → preview → MathJax → error list. Images load now. |
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

### Files, images, PDF

- **Open** reads a file with the File API. **Download .md** hands the browser a Blob.
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
| `tests/test_core.c` | Exact output for Markdown, math next to Markdown syntax (underscores, asterisks, backslashes, dollar signs in prose, lists, quotes, tables, code), line numbers, link and image rules, raw HTML, invalid UTF-8, NUL, the API, 400 KB lines, 2000-deep quotes |
| `tests/hostile/` | Files of raw HTML, script addresses in every spelling, attribute breakouts, hostile math, long lines, invalid bytes. Each must render without a crash, and its output must pass the checker |
| `tests/golden/` | Three notes with their exact expected HTML |
| `tests/fuzz.c` | Mutates the golden and hostile files (byte flips, deletions, copies, spliced tokens) and checks every output, under the sanitizers. `make fuzz-smoke` runs 20000; 300000 were run before release |
| `make asan` | The whole suite with the core and md4c under AddressSanitizer and UBSan |
| `tests/browser/check.mjs` | The built site in headless Chrome: first load under 300 KB, compile, numbering, the error log with its line, macro reset, file name, idle (no timers), theme, print, draft recovery, a failing store, no request to another origin except the note's image, no policy violation |

## 8. Build and deploy

One Makefile: `make`, `make test`, `make asan`, `make fuzz-smoke`, `make web`,
`make serve`, `make browser-test`. `make web` writes the site to `build/web/`.

The WebAssembly build is one `emcc` command over the core and md4c: `MODULARIZE`,
`ENVIRONMENT=web`, `FILESYSTEM=0`, `DYNAMIC_EXECUTION=0`, `ALLOW_MEMORY_GROWTH`, and four
exported functions.

`.github/workflows/pages.yml` runs the tests, builds, checks the size, runs the browser
check, and on `main` deploys. Every action is pinned to a commit. The workflow has
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

## 10. Not implemented

These are absent by design in 0.1; each explains a choice above.

- **Typesetting while typing.** Compile is explicit, which is what keeps the page idle.
- **A service worker.** The page needs the network on first load and on the first Compile.
- **Local image files.** They would have to be stored in the browser or embedded in the
  `.md`; the core accepts `https` addresses only.
- **A real TeX engine**, and LaTeX packages beyond MathJax's.
- **Accounts, sync, share links.** There is no backend.
- **Mobile polish.** Narrow screens get one pane above the other and nothing more.
