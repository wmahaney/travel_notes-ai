// A headless-browser check of the built site.
//     node tests/browser/check.mjs build/web
// Needs Node 18+, Chrome or Chromium, and the package puppeteer-core:
//     npm install --no-save puppeteer-core
// Set CHROME to the browser's path if it is not found. This is a development tool:
// the site itself uses nothing from npm.
import http from "node:http";
import { readFile, writeFile, mkdir } from "node:fs/promises";
import { existsSync } from "node:fs";
import path from "node:path";

const root = path.resolve(process.argv[2] || "build/web");
const puppeteer = (await import("puppeteer-core")).default;
const chrome = process.env.CHROME || ["/usr/bin/google-chrome", "/usr/bin/chromium", "/usr/bin/chromium-browser",
  "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"].find((p) => existsSync(p));
if (!chrome) { console.error("no Chrome found; set CHROME"); process.exit(2); }

const TYPES = { ".html": "text/html", ".js": "text/javascript", ".css": "text/css", ".wasm": "application/wasm", ".txt": "text/plain" };
const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, "http://x");
  // Served under a sub-path, as on GitHub Pages, to prove every address is relative.
  if (!url.pathname.startsWith("/some/path/")) { res.writeHead(404).end(); return; }
  let file = path.join(root, decodeURIComponent(url.pathname.slice("/some/path/".length)) || "index.html");
  if (file.endsWith(path.sep)) file += "index.html";
  try {
    const body = await readFile(file);
    res.writeHead(200, { "content-type": TYPES[path.extname(file)] || "application/octet-stream" }).end(body);
  } catch { res.writeHead(404).end(); }
});
await new Promise((r) => server.listen(0, "127.0.0.1", r));
const base = `http://127.0.0.1:${server.address().port}/some/path/`;

let failures = 0;
const check = (ok, what) => { console.log((ok ? "ok   " : "FAIL ") + what); if (!ok) failures++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// A 1x1 PNG, served in place of the note's https image (the test has no network).
const PNG = Buffer.from("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==", "base64");

const NOTE = `# Test note

Inline $a^2 + b^2 = c^2$ and a macro $\\newcommand{\\R}{\\mathbb{R}} f\\colon \\R \\to \\R$.

$$
\\int_0^1 x^2 \\, dx = \\frac{1}{3}
$$

$$
\\begin{align}
a &= b + c \\label{eq:one} \\\\
d &= e
\\end{align}
$$

See \\eqref{eq:one}? No: references are math too, $\\eqref{eq:one}$.

| x | y |
|---|---|
| 1 | $2$ |

\`\`\`python
print("$not math$")
\`\`\`

![A picture](https://example.org/picture.png)

![Not loaded](http://example.org/plain-http.png)
`;

const browser = await puppeteer.launch({ executablePath: chrome, headless: "new",
  args: ["--no-sandbox", "--disable-gpu", "--no-proxy-server"] });
try {
  const page = await browser.newPage();
  await page.setViewport({ width: 1280, height: 800 });
  const requests = [], problems = [];
  await page.setRequestInterception(true);
  page.on("request", (req) => {
    const url = req.url();
    requests.push(url);
    if (url.startsWith("https://example.org/")) req.respond({ status: 200, contentType: "image/png", body: PNG });
    else if (url.startsWith(base) || url.startsWith("data:") || url.startsWith("blob:")) req.continue();
    else req.abort();
  });
  page.on("pageerror", (e) => problems.push("page error: " + e));
  page.on("console", (m) => { if (m.type() === "error") problems.push("console: " + m.text()); });
  page.on("dialog", (d) => d.accept());
  await page.evaluateOnNewDocument(() => {
    window.__timers = 0; window.__csp = [];
    for (const name of ["setTimeout", "setInterval", "requestAnimationFrame"]) {
      const real = window[name].bind(window);
      window[name] = (...a) => { window.__timers++; return real(...a); };
    }
    document.addEventListener("securitypolicyviolation", (e) => window.__csp.push(e.violatedDirective + " " + e.blockedURI));
  });
  const sizes = new Map();
  page.on("response", async (res) => { try { sizes.set(res.url(), (await res.buffer()).length); } catch { /* aborted */ } });

  // ---- first load ----
  await page.goto(base, { waitUntil: "networkidle0" });
  await sleep(300);
  const first = requests.filter((u) => u.startsWith(base)).map((u) => u.slice(base.length) || "index.html");
  check(first.join(",") === "index.html,style.css,theme.js,app.js", "first load fetches the page, its style and its two scripts only: " + first.join(", "));
  const firstBytes = [...sizes.values()].reduce((a, b) => a + b, 0);
  check(firstBytes < 300 * 1024, `first load is under 300 KB (${firstBytes} bytes)`);
  check((await page.$eval("#source", (t) => t.value)).includes("A first note"), "a first visit shows the starter note");

  // ---- compile ----
  const setNote = async (text) => {
    await page.$eval("#source", (t, v) => { t.value = v; t.dispatchEvent(new Event("input")); }, text);
  };
  const compile = async () => {
    await page.click("#btn-compile");
    await page.waitForFunction(() => !document.querySelector("#btn-compile").disabled && /^Compiled|failed/.test(document.querySelector("#status").textContent), { timeout: 60000 });
  };
  await setNote(NOTE);
  check(!requests.some((u) => u.includes("example.org")), "no image is requested while typing");
  check((await page.$eval("#status", (s) => s.textContent)).includes("Not compiled"), "status says the note is not compiled yet");
  await compile();
  const after = await page.evaluate(() => ({
    status: document.querySelector("#status").textContent,
    svgs: document.querySelectorAll("#preview .tn-math > mjx-container > svg").length,
    maths: document.querySelectorAll("#preview .tn-math").length,
    text: document.querySelector("#preview").textContent,
    table: !!document.querySelector("#preview table td"),
    code: document.querySelector("#preview pre code")?.textContent,
    img: document.querySelector("#preview img")?.getAttribute("src"),
    blocked: document.querySelector("#preview .tn-blocked-image")?.textContent,
    errorsHidden: document.querySelector("#btn-errors").hidden,
    numbered: [...document.querySelectorAll("#preview svg [data-mml-node='mtd'], #preview svg g[data-mml-node='mlabeledtr']")].length,
  }));
  check(after.status === "Compiled.", "status after compiling: " + after.status);
  check(after.maths === 6 && after.svgs === 6, `all ${after.maths} expressions are typeset as SVG (${after.svgs})`);
  check(after.table && after.code === 'print("$not math$")\n', "table and code block are rendered, code keeps its dollars");
  check(after.img === "https://example.org/picture.png", "the https image is in the preview");
  check(after.blocked === "Not loaded", "the http image is a marked placeholder");
  check(after.errorsHidden, "no math errors reported");
  check(after.numbered > 0, "the align environment is numbered");
  check(requests.some((u) => u === "https://example.org/picture.png"), "the image is requested after Compile");
  check(!requests.some((u) => u.includes("plain-http")), "the http image is never requested");
  const fetched = [...new Set(requests.filter((u) => u.startsWith(base)).map((u) => u.slice(base.length) || "index.html"))];
  check(fetched.includes("tnotes.js") && fetched.includes("tnotes.wasm") && fetched.includes("vendor/mathjax/tex-svg-full.js"), "the core and MathJax load on the first Compile");
  await mkdir("build", { recursive: true });
  await page.screenshot({ path: "build/browser-light.png" });

  // ---- lines, headings, theorem-like blocks, comments, LaTeX ----
  const NOTE2 = `# Title of the note

Go to [the end](#the-end).

> **Theorem (Euler).** For all $x$:
> $$
> e^{ix} = \\cos x % the real part
> + i \\sin x
> $$

> **Proof.** Omitted.

## The end

Last paragraph with ~~struck~~ text.
`;
  await setNote(NOTE2);
  await compile();
  const two = await page.evaluate(() => ({
    status: document.querySelector("#status").textContent,
    svgs: document.querySelectorAll("#preview .tn-math > mjx-container > svg").length,
    heads: [...document.querySelectorAll("#preview blockquote.tn-env .tn-env-head")].map((e) => e.textContent),
    theorem: !!document.querySelector("#preview blockquote.tn-env.tn-theorem"),
    ids: [...document.querySelectorAll("#preview h1, #preview h2")].map((h) => h.id),
    tex: document.querySelector("#preview .tn-display").getAttribute("data-tex"),
    lists: document.querySelectorAll("#preview li").length,
    lines: [...document.querySelectorAll("#preview > p, #preview > h1, #preview > h2")].map((e) => e.dataset.line),
  }));
  check(two.status === "Compiled." && two.svgs === 2, "the second note compiles without math errors: " + two.status);
  check(two.theorem && two.heads.join("|") === "Theorem 1 (Euler).|Proof.", "theorem-like blocks get their heads: " + two.heads.join(" | "));
  check(two.ids.join(",") === "tn-h-title-of-the-note,tn-h-the-end", "headings have ids: " + two.ids.join(", "));
  check(!two.tex.includes("real part") && two.tex.includes("+ i \\sin x") && two.lists === 0, "a TeX comment is dropped and a line starting with + stays in the formula: " + two.tex.replace(/\s+/g, " "));
  check(two.lines.join(",") === "1,3,13,15", "blocks carry their source lines: " + two.lines.join(","));

  const selected = () => page.$eval("#source", (t) => t.value.slice(t.selectionStart, t.selectionEnd));
  await page.click("#btn-outline");
  const outline = await page.$$eval("#outline-list button", (bs) => bs.map((b) => b.textContent));
  check(outline.join("|") === "Title of the note|The end", "the outline lists the headings: " + outline.join(" | "));
  await page.click("#outline-list li:nth-child(2) button");
  check((await selected()) === "## The end", "an outline entry selects its heading in the source: " + (await selected()));
  await page.click("#btn-outline-close");

  await page.click("#preview a[href='#tn-h-the-end']");
  check(await page.evaluate(() => document.querySelector("#tn-h-the-end").classList.contains("tn-located") && location.hash === ""),
    "a link to a heading goes to it and leaves the address alone");

  await page.$eval("#preview > p:last-of-type", (p) => p.dispatchEvent(new MouseEvent("dblclick", { bubbles: true, cancelable: true })));
  check((await selected()) === "Last paragraph with ~~struck~~ text.", "double-clicking a paragraph selects its line in the source: " + (await selected()));

  await page.$eval("#source", (t) => { const at = t.value.indexOf("e^{ix}"); t.setSelectionRange(at, at); });
  await page.click("#btn-locate");
  check(await page.evaluate(() => { const el = document.querySelector("#preview .tn-located"); return !!el && el.classList.contains("tn-display"); }),
    "Locate marks the block that the cursor's line belongs to");

  const texFile = await page.evaluate(async () => {
    const realClick = HTMLAnchorElement.prototype.click; let name = null, blob = null;
    const real = URL.createObjectURL;
    URL.createObjectURL = (b) => { blob = b; return real.call(URL, b); };
    HTMLAnchorElement.prototype.click = function () { name = this.download; };
    document.querySelector("#btn-tex").click();
    for (let i = 0; i < 400 && !blob; i++) await new Promise((r) => setTimeout(r, 25));
    HTMLAnchorElement.prototype.click = realClick; URL.createObjectURL = real;
    return { name, text: blob ? await blob.text() : "" };
  });
  check(texFile.name === "Title-of-the-note.tex", "the .tex is named after the first heading: " + texFile.name);
  check(["\\documentclass{amsart}", "\\title{Title of the note}", "\\begin{theorem}[{Euler}]", "\\begin{proof}", "\\hyperref[tn-h-the-end]{the end}",
    "\\section{The end}\\label{tn-h-the-end}", "\\usepackage[normalem]{ulem}", "\\sout{struck}", "e^{ix} = \\cos x + i \\sin x"].every((t) => texFile.text.includes(t)) &&
    !texFile.text.includes("real part"), "the .tex download is the note as LaTeX");

  // ---- out of date, then a deliberate error ----
  await setNote(NOTE + "\nline with an error: $\\undefinedmacro + 1$\n");
  check((await page.$eval("#status", (s) => s.textContent)).includes("out of date"), "status says the preview is out of date after typing");
  await compile();
  const errorLine = (NOTE + "\nline").split("\n").length;
  const err = await page.evaluate(() => ({
    button: document.querySelector("#btn-errors").textContent, hidden: document.querySelector("#btn-errors").hidden,
    marked: document.querySelectorAll("#preview .tn-math-error").length,
  }));
  check(!err.hidden && err.button === "1 math error", "the status bar counts the error: " + err.button);
  check(err.marked === 1, "the failed expression stays marked in the preview");
  await page.click("#btn-errors");
  const listed = await page.$eval("#error-list", (l) => l.textContent);
  check(listed.includes("line " + errorLine) && /undefinedmacro/i.test(listed), "the panel lists the error with its line: " + listed.slice(0, 90));
  // the downloaded log: capture the text handed to the download
  const log = await page.evaluate(async () => {
    const real = URL.createObjectURL; let blob = null;
    URL.createObjectURL = (b) => { blob = b; return real.call(URL, b); };
    document.querySelector("#btn-log").click();
    URL.createObjectURL = real;
    return blob ? await blob.text() : "";
  });
  check(log.includes("line " + errorLine + ":") && log.includes("\\undefinedmacro + 1") && log.includes("1 error"), "the downloaded .log has the line, the TeX and the message");

  // ---- macros and numbers do not survive a compile ----
  await setNote("Uses a macro from the earlier note: $\\R$\n\n$$\\begin{equation} x = 1 \\end{equation}$$\n");
  await compile();
  const reset = await page.evaluate(() => ({
    errors: document.querySelectorAll("#preview .tn-math-error").length,
    text: document.querySelector("#error-list").textContent,
  }));
  check(reset.errors === 1 && /\\R/.test(reset.text), "a macro defined in an earlier compile is gone: " + reset.text.slice(0, 60));

  // ---- file name and .md download ----
  await setNote("Some text first\n\n## Notes on *Hecke* operators & $T_\\ell$\n\nbody\n");
  const saved = await page.evaluate(async () => {
    const realClick = HTMLAnchorElement.prototype.click; let name = null, blob = null;
    const real = URL.createObjectURL;
    URL.createObjectURL = (b) => { blob = b; return real.call(URL, b); };
    HTMLAnchorElement.prototype.click = function () { name = this.download; };
    document.querySelector("#btn-save").click();
    HTMLAnchorElement.prototype.click = realClick; URL.createObjectURL = real;
    return { name, text: await blob.text() };
  });
  check(saved.name === "Notes-on-Hecke-operators-T-ell.md", "the .md is named after the first heading: " + saved.name);
  check(saved.text.includes("## Notes on *Hecke* operators"), "the .md download is the source");

  // ---- idle: no timers, no requests ----
  await setNote(NOTE); await compile(); await sleep(1500);
  const t0 = await page.evaluate(() => window.__timers), r0 = requests.length;
  await sleep(2500);
  const t1 = await page.evaluate(() => window.__timers);
  check(t1 === t0 && requests.length === r0, `idle for 2.5 s: ${t1 - t0} timers or frames, ${requests.length - r0} requests`);

  // ---- theme ----
  await page.click("#btn-theme");
  check(await page.evaluate(() => document.documentElement.dataset.theme === "dark" && localStorage.getItem("travel_notes:theme") === "dark"), "the theme toggle is remembered under a travel_notes: key");
  const colours = await page.evaluate(() => ({ bg: getComputedStyle(document.body).backgroundColor, svg: getComputedStyle(document.querySelector("#preview svg")).color, text: getComputedStyle(document.querySelector("#preview")).color }));
  check(colours.bg === "rgb(21, 23, 28)" && colours.svg === colours.text, "dark theme: page background " + colours.bg + ", math takes the text colour");
  await page.screenshot({ path: "build/browser-dark.png" });

  // ---- PDF (print) ----
  await page.pdf({ path: "build/browser-print.pdf", format: "A4", printBackground: true });
  await page.emulateMediaType("print");
  const print = await page.evaluate(() => ({
    toolbar: getComputedStyle(document.querySelector(".toolbar")).display, editor: getComputedStyle(document.querySelector(".editor")).display,
    status: getComputedStyle(document.querySelector(".status")).display, bg: getComputedStyle(document.body).backgroundColor,
    text: getComputedStyle(document.querySelector("#preview")).color,
  }));
  check(print.toolbar === "none" && print.editor === "none" && print.status === "none", "print shows only the note");
  check(print.bg === "rgb(255, 255, 255)" && print.text === "rgb(30, 36, 48)", "print uses light colours even in the dark theme: " + print.bg + " / " + print.text);
  await page.emulateMediaType("screen");

  // ---- the draft survives a reload ----
  await setNote("# Draft to keep\n\n$x$\n"); await sleep(1200);
  check(await page.evaluate(() => localStorage.getItem("travel_notes:draft")?.startsWith("# Draft to keep")), "the draft is saved after typing stops");
  check(await page.evaluate(() => Object.keys(localStorage).every((k) => k.startsWith("travel_notes:"))), "every storage key starts with travel_notes:");
  await page.reload({ waitUntil: "networkidle0" });
  check((await page.$eval("#source", (t) => t.value)) === "# Draft to keep\n\n$x$\n", "closing and reopening restores the draft");
  check(await page.evaluate(() => document.documentElement.dataset.theme === "dark"), "the theme is restored too");

  // ---- storage that fails does not lose the text ----
  await page.evaluate(() => { Storage.prototype.setItem = () => { throw new DOMException("full", "QuotaExceededError"); }; });
  await setNote("text typed while storage is full"); await sleep(1200);
  const full = await page.evaluate(() => ({ text: document.querySelector("#source").value, status: document.querySelector("#status").textContent }));
  check(full.text === "text typed while storage is full" && /could not be saved/.test(full.status), "a full store keeps the editor's text and says so");

  // ---- nothing went elsewhere, nothing was blocked ----
  const outside = [...new Set(requests.filter((u) => !u.startsWith(base) && !u.startsWith("data:") && !u.startsWith("blob:")))];
  check(outside.every((u) => u === "https://example.org/picture.png"), "requests to other origins: only the note's own image (" + outside.join(", ") + ")");
  const csp = await page.evaluate(() => window.__csp);
  check(csp.length === 0, "no Content-Security-Policy violation" + (csp.length ? ": " + csp.join("; ") : ""));
  check(problems.length === 0, "no script errors" + (problems.length ? ": " + problems.join("; ") : ""));
} finally {
  await browser.close();
  server.close();
}
console.log(failures ? `${failures} check(s) failed` : "browser check ok");
process.exit(failures ? 1 : 0);
