# travel_notes

travel_notes is a Markdown note editor with LaTeX math that runs entirely in the
browser. You write Markdown on the left, press Compile, and read the typeset note on
the right. You take the result away as a `.md` file, a LaTeX file or a PDF.

There is no account, no server and no upload. Everything you type stays in your browser.

This tool was built with Claude; the `-ai` suffix in the repository's name marks that.

> **Version 0.2.0. No compatibility promise before 1.0.** Any 0.x release may change the
> page, the storage format of drafts and the C API. See [CHANGELOG.md](CHANGELOG.md).

## What it does

- **Markdown:** headings, emphasis, lists, task lists, block quotes, tables, code blocks,
  links, strikethrough.
- **Math:** `$...$` in a line and `$$...$$` on its own, typeset by MathJax as SVG. The AMS
  environments (`align`, `gather`, `cases`, ...) work, equations are numbered, and
  `\label`, `\ref` and `\eqref` work. `\newcommand` works within a note. TeX comments
  work: `%` and the rest of its line are dropped.
- **Theorem-like blocks.** A block quote that starts with a bold `**Theorem.**`,
  `**Lemma (name).**`, `**Definition.**`, `**Proof.**` and so on becomes a styled block
  with a number. See [Theorems](#theorems).
- **Headings you can link to.** `[see the proof](#the-proof)` goes to the heading
  "The proof". **Outline** lists the headings of the compiled note.
- **Source and preview stay together.** Double-click a paragraph, a heading, a list item
  or a formula in the preview to select its line in the source. **Locate** (`Ctrl+J`)
  shows the line under the cursor in the preview.
- **Compile on request.** Press **Compile** or `Ctrl+Enter`. Nothing is typeset while you
  type; the status bar says when the preview is out of date.
- **An error log.** Each math error is listed with its line, the TeX that failed and
  MathJax's message. The status bar shows the count, and the list can be downloaded as a
  `.log` file. A failed expression stays marked in the preview.
- **Files.** Open a `.md` file from your computer; download the source as `.md`. The file
  name comes from the first heading.
- **LaTeX.** **Download .tex** writes the note as a LaTeX document (`amsart`), with the
  math as you wrote it. See [LaTeX](#latex).
- **PDF.** **Download PDF** opens the browser's print dialog with only the note on the
  page. Choose "Save as PDF" as the printer.
- **Draft recovery.** The source is saved in the browser a moment after you stop typing,
  and restored when you come back.
- **Light and dark.** Follows the system setting; the toolbar button overrides it and is
  remembered.

| Shortcut | Action |
|---|---|
| `Ctrl+Enter` | Compile |
| `Ctrl+\` | Show or hide the preview |
| `Ctrl+J` | Locate: show the cursor's line in the preview |

## What it does not do

- **It is not a TeX engine.** Math is typeset by MathJax 3. LaTeX packages beyond what
  MathJax ships, and TikZ, are not available. An unknown command is an error.
- **The PDF comes from the browser's print dialog**, not from a PDF library. Page breaks,
  headers and footers are the browser's.
- **No raw HTML.** HTML written in a note is shown as text.
- **Images are loaded from `https` addresses only**: `![description](https://...)`. A local
  file, an `http` address or a `data:` address is shown as its description in a marked
  box. Local image files cannot be uploaded or embedded.
- **Links keep `http`, `https` and `mailto` addresses only**, and open in a new tab. A
  link to `#a-heading` stays in the note. Any other link is shown as its text.
- **One draft per browser.** There is no sync, no sharing and no list of notes.
- **Notes up to 8 MB.**
- **Made for desktop browsers.** It works on a phone, one pane above the other, without
  further polish.

Five details of writing math:

- A display formula must not contain a blank line: Markdown ends the paragraph there.
- Write a formula of several lines with `$$` on a line of its own before and after it.
  Then a line inside it may start with `+`, `-`, `=`, `#` or a number; otherwise Markdown
  takes such a line for a list or a heading and cuts the formula in two.
- A comment runs from `%` to the end of its line. Write `\%` for a percent sign.
- A `$` that can close a formula does. Write `\$` for a dollar sign in prose
  (`it costs \$5`).
- References are math too: write `$\eqref{eq:name}$`, inside dollar signs.

## Theorems

A block quote whose first words are a bold kind becomes a theorem-like block:

```markdown
> **Theorem (Pizer).** The graph $G_p(\ell)$ is Ramanujan.

> **Proof.** Omitted.
```

The kinds are Theorem, Lemma, Proposition, Corollary, Conjecture, Claim, Definition,
Example, Exercise, Remark and Proof, in any letter case. A name in brackets is optional,
and so is the full stop or colon. All kinds but Proof share one counter, in the order
they appear: Theorem 1, Lemma 2, Definition 3. A proof ends with a square.

A head with a number written by hand (`**Theorem 7.**`) is left as you wrote it, in an
ordinary block quote. Blocks cannot be referred to by number.

## Headings and links

Every heading gets a name made from its text: lower-case letters and digits, with a
hyphen for everything in between. "The proof" is `the-proof`, and "Step 2: $x^2$" is
`step-2-x-2`. Letters outside a to z are left out. If two headings would get the same
name, the later ones get `-2`, `-3`. A heading with no letters or digits is `section`.

`[text](#the-proof)` links to that heading. The Outline panel shows the headings in
order; choosing one shows it in the preview and selects it in the source.

## LaTeX

**Download .tex** writes an `amsart` document:

- A level-1 heading at the very start is the title. If it is the only level-1 heading,
  the other headings move up one level, so `##` is a `\section`.
- Math is copied as you wrote it, without its comments. A display formula that is one
  `align`, `gather`, `equation`, `multline`, `flalign`, `alignat` or `eqnarray`
  environment is written without `\[ \]`.
- `\newcommand` and `\renewcommand` at the start of a formula are moved in front of it.
  In LaTeX a definition inside a formula ends with the formula; in the preview it lasts.
  Put definitions first in their formula and both agree.
- Theorem-like blocks become `theorem`, `lemma`, ..., `proof` environments, on one
  counter, as in the preview.
- Prose is escaped: `\`, `{`, `$`, `%`, `_` and the like in your text come out as those
  characters, never as TeX commands.
- Links to headings become `\hyperref`; web links become `\href`. An image becomes its
  description, linked to the image's address. Tables become `tabular`.
- Lists nested more than four deep are flattened, because LaTeX stops there.

The document needs `amsart`, `amssymb`, `hyperref` and T1 fonts, and `ulem` if the note
strikes text through. Characters that pdfLaTeX does not know (such as ∀ typed in prose,
or non-Latin scripts) need XeLaTeX or LuaLaTeX, or a package of your choice. Commands
that only MathJax has do not exist in LaTeX.

## Privacy

- **Drafts live only in this browser.** They are kept in the browser's local storage for
  this site, under keys that start with `travel_notes:`. The browser can evict them, and
  clearing site data deletes them. Download the `.md` to keep a note.
- **A note with web images makes your browser contact those image hosts** when you
  compile, and they can see your IP address. Images are requested only on Compile, never
  while typing, and without a referrer. A note without web images contacts nobody.
- The page loads nothing from other sites: its scripts, styles and MathJax are served
  from the same place as the page.

## Building

You need a C11 compiler and `make`. The Markdown parser (md4c) and MathJax are vendored
in `third_party/`, so nothing else is downloaded.

```sh
make               # build/libtnotes.a and the command-line tool build/tnotes
make test          # unit tests, golden files, hostile inputs
make asan          # the same under AddressSanitizer and UBSan
make fuzz-smoke    # a short mutation fuzz run of the renderer
make latex-test    # compile the LaTeX of the test notes (needs pdflatex)
```

The command-line tool renders a file to an HTML fragment, to LaTeX, or to its outline:

```sh
build/tnotes note.md -o note.html
build/tnotes --latex note.md -o note.tex
build/tnotes --outline note.md          # level, line, id and text of each heading
```

The site needs [Emscripten](https://emscripten.org) (`emcc`):

```sh
make web           # the site, in build/web/
make serve         # build it and serve it at http://localhost:8000
make browser-test  # load it in headless Chrome (needs Node and: npm install --no-save puppeteer-core)
```

## Publishing

`.github/workflows/pages.yml` tests the core, compiles its LaTeX output, builds the site
with a pinned Emscripten version, checks it in a headless browser, and on pushes to `main` deploys `build/web/` to
GitHub Pages. One manual step is needed once: in the repository, **Settings → Pages →
Source: GitHub Actions**.

Every address in the site is relative, so it works unchanged under any repository name
or at the root of a domain.

## Layout

```
include/tnotes.h    the public API of the core
src/                the core: Markdown with math to safe HTML, and to LaTeX (C11, prefix tn_)
cli/main.c          the command-line tool
web/                the page: index.html, style.css, app.js, theme.js
tests/              unit tests, golden files, hostile inputs, fuzzer, browser check
third_party/        md4c and the MathJax component, with their licenses
docs/               ARCHITECTURE.md
```

[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) covers the design: what the core
guarantees about its output, how the page uses it, the security policy, and the choices
made along the way.

## License

travel_notes is dedicated to the public domain under [CC0 1.0](LICENSE). The vendored
software keeps its own licenses: md4c is MIT, MathJax is Apache 2.0. See
[third_party/NOTICE.txt](third_party/NOTICE.txt).
