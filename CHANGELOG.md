# Changelog

travel_notes is **pre-1.0**. Until 1.0 there is no compatibility promise: any release
may change the page, how drafts are stored in the browser, the command-line tool and the
C API.

## 0.2.0 (2026-11-16)

The HTML the core writes has changed: blocks carry `data-line`, headings carry `id`, and
line breaks inside a formula are kept. Notes need no change.

- **TeX comments.** In math, an unescaped `%` and the rest of its line are dropped, with
  the line break and the white space that starts the next line, as in TeX. `\%` is kept.
  Line breaks inside a formula are no longer turned into spaces, and white space at the
  ends of a formula is removed.
- **Formulas of several lines.** Between two lines that are just `$$`, a line that starts
  with `+`, `-`, `*`, `=`, `#`, `>`, `|`, a fence or a list number no longer cuts the
  formula in two.
- **Source lines on blocks.** Paragraphs, headings, list items, code blocks and table
  rows carry `data-line`, like math. In the page, double-clicking a block selects its
  line in the source, and **Locate** (`Ctrl+J`) shows the cursor's line in the preview.
- **Heading ids and links.** Each heading has an id made from its text, unique in the
  note. `[text](#the-heading)` links to it. Before, such a link was removed.
- **Outline.** `tn_outline` and `tnotes --outline` list the headings with level, line and
  id. The page has an Outline panel.
- **Theorem-like blocks.** A block quote that starts with a bold `Theorem.`, `Lemma
  (name).`, `Proof.` and so on becomes a numbered, styled block.
- **LaTeX.** `tn_latex` and `tnotes --latex` write the note as an `amsart` document. The
  page has **Download .tex**.
- Tests: 959 unit checks (were 443); golden LaTeX and outlines next to the golden HTML;
  the fuzzer also runs the LaTeX and outline writers; `make latex-test` compiles the
  LaTeX of the golden notes with pdflatex; the browser check covers the new features.
- The line of a display formula inside a block quote is now the line of its `$$`, not
  the line after it.

## 0.1.0 (2026-11-16)

- Core (`libtnotes`, C11): Markdown with `$...$` and `$$...$$` math to an HTML fragment,
  on top of md4c. Math is passed through untouched and marked with its source line. Raw
  HTML is never passed through; links keep `http`, `https` and `mailto` only; images keep
  `https` only; output is valid UTF-8 for any input.
- Command-line tool `tnotes`.
- Web page: source and preview side by side, compile on request, MathJax (TeX input, SVG
  output, AMS environments, equation numbers), an error log with line numbers and a
  `.log` download, open and download `.md`, PDF through the print dialog, draft recovery,
  light and dark themes.
- Tests: unit tests, golden files, a hostile-input set, a mutation fuzzer, the suite
  under AddressSanitizer and UBSan, and a headless-browser check of the built site.
- GitHub Actions workflow that builds with Emscripten and deploys to GitHub Pages.
