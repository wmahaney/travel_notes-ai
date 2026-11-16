# Changelog

travel_notes is **pre-1.0**. Until 1.0 there is no compatibility promise: any release
may change the page, how drafts are stored in the browser, the command-line tool and the
C API.

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
