// travel_notes: the front end. Plain JavaScript, no framework and no build step.
//
// What runs when:
//   - On load: restore the draft and wire the buttons. Nothing else is downloaded.
//   - On the first Compile: load the core (tnotes.js + tnotes.wasm) and MathJax.
//   - On every Compile: the core turns the Markdown into HTML, the HTML goes into the
//     preview, MathJax typesets the math, and math errors are collected.
//   - While idle: nothing. The only timer is the debounced autosave after typing.
//
// The only HTML ever inserted into the page is the output of the core (see compile()).
// Text from the note is otherwise handled as text (textContent), never as markup.
"use strict";

(() => {
  const KEY = "travel_notes:";          // every localStorage key of this page starts with this
  const SAVE_DELAY = 800;               // ms after the last keystroke before the draft is saved
  const IMAGE_WAIT = 4000;              // ms to wait for images before printing

  const $ = (id) => document.getElementById(id);
  const source = $("source"), preview = $("preview"), statusEl = $("status");
  const errorsPanel = $("errors"), errorList = $("error-list"), errorsButton = $("btn-errors");

  const WELCOME = [
    "# A first note",
    "",
    "Write Markdown on the left and press **Compile** (Ctrl+Enter) to typeset it on the right.",
    "",
    "Math goes between dollar signs: $e^{i\\pi} + 1 = 0$ in a line, or on its own:",
    "",
    "$$",
    "\\int_0^1 x^2 \\, dx = \\frac{1}{3}",
    "$$",
    "",
    "Write `\\$` for a dollar sign that is not math.",
    "",
  ].join("\n");

  // ---- storage: never let a full or blocked store break the editor ---------------
  const store = {
    get(name) { try { return localStorage.getItem(KEY + name); } catch { return null; } },
    set(name, value) { try { localStorage.setItem(KEY + name, value); return true; } catch { return false; } },
    remove(name) { try { localStorage.removeItem(KEY + name); return true; } catch { return false; } },
  };

  // ---- state -----------------------------------------------------------------------
  let compiledSource = null;            // the text of the last successful compile
  let compiling = false;
  let errors = [];                      // { line, tex, message }
  let compiledAt = null;
  let saveTimer = 0;
  let unsaved = false;                  // typed since the last successful save of the draft
  let storageOk = true;

  function setStatus(text, stale = false) {
    statusEl.textContent = text;
    statusEl.classList.toggle("stale", stale);
  }
  function showFreshness() {
    if (compiling) return;
    if (compiledSource === null) setStatus("Not compiled yet. Press Compile (Ctrl+Enter).");
    else if (source.value !== compiledSource) setStatus("The preview is out of date. Press Compile (Ctrl+Enter).", true);
    else setStatus(errors.length ? "Compiled, with math errors." : "Compiled.");
  }

  // ---- loading the core and MathJax, once, on the first compile ------------------
  function loadScript(src) {
    return new Promise((resolve, reject) => {
      const s = document.createElement("script");
      s.src = src;
      s.onload = resolve;
      s.onerror = () => reject(new Error("could not load " + src));
      document.head.appendChild(s);
    });
  }

  let corePromise = null;
  function loadCore() {
    if (!corePromise) {
      corePromise = loadScript("tnotes.js").then(() => window.createTnotes())
        .catch((e) => { corePromise = null; throw e; });
    }
    return corePromise;
  }

  let mathPromise = null;
  function loadMathJax() {
    if (!mathPromise) {
      // The core marks math as \( ... \) and \[ ... \] inside <span class="tn-math">.
      // MathJax looks only inside those spans: the rest of the preview is ignored.
      window.MathJax = {
        tex: {
          inlineMath: [["\\(", "\\)"]],
          displayMath: [["\\[", "\\]"]],
          processEscapes: false,
          processEnvironments: false,
          tags: "ams",
          // Without "noundefined" an unknown command is an error, reported in the
          // log, instead of red text in the output.
          packages: { "[-]": ["noundefined"] },
        },
        svg: { fontCache: "local" },
        options: {
          ignoreHtmlClass: "tn-doc",
          processHtmlClass: "tn-math",
          enableMenu: false,
        },
        startup: { typeset: false },
      };
      mathPromise = loadScript("vendor/mathjax/tex-svg-full.js").then(() => window.MathJax.startup.promise)
        .catch((e) => { mathPromise = null; throw e; });
    }
    return mathPromise;
  }

  // Markdown to HTML, through the WebAssembly core.
  function render(core, markdown) {
    const size = core.lengthBytesUTF8(markdown);
    const input = core._malloc(size + 1);
    if (!input) throw new Error("out of memory");
    core.stringToUTF8(markdown, input, size + 1);
    const output = core._tn_render_cstr(input, size);
    core._free(input);
    if (!output) throw new Error("the note could not be rendered (it may be larger than 8 MB)");
    const html = core.UTF8ToString(output);
    core._tn_free(output);
    return html;
  }

  // ---- compile -------------------------------------------------------------------
  async function compile() {
    if (compiling) return;
    compiling = true;
    $("btn-compile").disabled = true;
    setStatus("Compiling.");
    const text = source.value;
    try {
      const [core] = await Promise.all([loadCore(), loadMathJax()]);
      const html = render(core, text);
      const MJ = window.MathJax;

      // Release what the previous compile left in MathJax, then start from a clean
      // TeX state: a fresh input processor has no macros and no equation numbers
      // from earlier compiles, so the result depends only on the text.
      MJ.typesetClear([preview]);
      MJ.startup.getComponents();
      MJ.texReset();

      preview.innerHTML = html;         // the core's output: the only HTML inserted
      watchImages();

      // Remember each expression's TeX before MathJax replaces it.
      const items = [];
      for (const el of preview.querySelectorAll(".tn-math")) {
        const raw = el.textContent;
        items.push({ el, line: Number(el.dataset.line) || 0, tex: raw.slice(2, -2).trim() });
      }
      let failure = null;
      try { await MJ.typesetPromise([preview]); } catch (e) { failure = e; }

      errors = [];
      for (const item of items) {
        const bad = item.el.querySelector("[data-mjx-error]");
        if (!bad) continue;
        const message = bad.getAttribute("data-mjx-error") || "TeX error";
        errors.push({ line: item.line, tex: item.tex, message });
        item.el.classList.add("tn-math-error");
        item.el.title = message;
      }
      if (failure) errors.push({ line: 0, tex: "", message: "MathJax stopped: " + (failure.message || failure) });

      compiledSource = text;
      compiledAt = new Date();
      showErrors();
    } catch (e) {
      setStatus("Compile failed: " + (e && e.message ? e.message : e), true);
      compiling = false;
      $("btn-compile").disabled = false;
      return false;
    }
    compiling = false;
    $("btn-compile").disabled = false;
    showFreshness();
    return true;
  }

  // ---- the error log ----------------------------------------------------------------
  function showErrors() {
    errorList.replaceChildren();
    for (const err of errors) {
      const li = document.createElement("li");
      if (err.line) {
        const go = document.createElement("button");
        go.type = "button";
        go.textContent = "line " + err.line;
        go.title = "Go to this line in the source";
        go.addEventListener("click", () => gotoLine(err.line));
        li.appendChild(go);
      }
      li.appendChild(document.createTextNode(err.message));
      if (err.tex) {
        const code = document.createElement("code");
        code.textContent = err.tex;
        li.appendChild(code);
      }
      errorList.appendChild(li);
    }
    const n = errors.length;
    errorsButton.hidden = n === 0;
    errorsButton.textContent = n === 1 ? "1 math error" : n + " math errors";
    $("errors-title").textContent = n === 1 ? "1 math error" : n + " math errors";
    if (n === 0) errorsPanel.hidden = true;
  }

  function logText() {
    const lines = [
      "travel_notes math log",
      "compiled: " + (compiledAt ? compiledAt.toISOString() : "never"),
      errors.length === 1 ? "1 error" : errors.length + " errors",
      "",
    ];
    for (const err of errors) {
      lines.push((err.line ? "line " + err.line + ": " : "") + err.message);
      if (err.tex) lines.push("    " + err.tex.replace(/\s+/g, " "));
      lines.push("");
    }
    return lines.join("\n");
  }

  function gotoLine(line) {
    const text = source.value;
    let start = 0;
    for (let i = 1; i < line; i++) {
      const next = text.indexOf("\n", start);
      if (next < 0) break;
      start = next + 1;
    }
    let end = text.indexOf("\n", start);
    if (end < 0) end = text.length;
    source.focus();
    source.setSelectionRange(start, end);
    // Scroll the line into view: roughly its share of the full height.
    const total = text.split("\n").length || 1;
    source.scrollTop = Math.max(0, ((line - 1) / total) * source.scrollHeight - source.clientHeight / 3);
  }

  // ---- images ----------------------------------------------------------------------
  // The core emits <img> for https addresses only. If one fails to load, show its
  // description in a marked box instead of a broken-image icon.
  function replaceImage(img) {
    const box = document.createElement("span");
    box.className = "tn-failed-image";
    box.textContent = img.alt || "(no description)";
    img.replaceWith(box);
  }
  function watchImages() {
    for (const img of preview.querySelectorAll("img")) {
      img.addEventListener("error", () => replaceImage(img), { once: true });
    }
  }
  // Before printing: ask every image to load now, and wait for them, but not for long.
  function imagesReady() {
    const pending = [...preview.querySelectorAll("img")].filter((img) => { img.loading = "eager"; return !img.complete; });
    if (!pending.length) return Promise.resolve();
    return new Promise((resolve) => {
      let left = pending.length;
      const timer = setTimeout(() => {              // too slow: print the description instead
        for (const img of pending) if (!img.complete && img.isConnected) replaceImage(img);
        resolve();
      }, IMAGE_WAIT);
      const done = () => { if (--left === 0) { clearTimeout(timer); resolve(); } };
      for (const img of pending) {
        img.addEventListener("load", done, { once: true });
        img.addEventListener("error", done, { once: true });
      }
    });
  }

  // ---- files -----------------------------------------------------------------------
  // The file name comes from the first heading; "note" if there is none.
  function fileStem() {
    const m = /^ {0,3}#{1,6}[ \t]+(.+?)[ \t]*#*[ \t]*$/m.exec(source.value);
    let stem = m ? m[1] : "";
    stem = stem.normalize("NFKD").replace(/[^\p{L}\p{N}]+/gu, "-")
      .replace(/^-+|-+$/g, "").slice(0, 60).replace(/-+$/, "");
    return stem || "note";
  }
  function download(name, text, type) {
    const url = URL.createObjectURL(new Blob([text], { type }));
    const a = document.createElement("a");
    a.href = url;
    a.download = name;
    document.body.appendChild(a);
    a.click();
    a.remove();
    URL.revokeObjectURL(url);
  }

  function setSource(text) {
    source.value = text;
    source.setSelectionRange(0, 0);
    source.scrollTop = 0;
    saveDraft();
    showFreshness();
  }

  // ---- the draft -------------------------------------------------------------------
  function saveDraft() {
    clearTimeout(saveTimer);
    saveTimer = 0;
    storageOk = source.value ? store.set("draft", source.value) : store.remove("draft");
    unsaved = !storageOk;
    if (!storageOk) setStatus("The draft could not be saved in this browser (storage is full or blocked). Download the .md to keep it.", true);
  }
  function scheduleSave() {
    unsaved = true;
    clearTimeout(saveTimer);
    saveTimer = setTimeout(saveDraft, SAVE_DELAY);
  }

  // ---- theme -----------------------------------------------------------------------
  const darkQuery = window.matchMedia("(prefers-color-scheme: dark)");
  function isDark() {
    const chosen = document.documentElement.dataset.theme;
    return chosen ? chosen === "dark" : darkQuery.matches;
  }
  function showTheme() {
    const btn = $("btn-theme");
    btn.textContent = isDark() ? "Light" : "Dark";
    btn.setAttribute("aria-label", isDark() ? "Switch to the light theme" : "Switch to the dark theme");
  }

  // ---- wiring ------------------------------------------------------------------------
  $("btn-compile").addEventListener("click", compile);
  $("btn-toggle").addEventListener("click", togglePreview);
  function togglePreview() {
    const hidden = document.body.classList.toggle("no-preview");
    $("btn-toggle").setAttribute("aria-pressed", String(!hidden));
  }

  $("btn-new").addEventListener("click", () => {
    if (source.value && !window.confirm("Start a new note? The current text will be discarded.")) return;
    setSource("");
    source.focus();
  });

  $("btn-open").addEventListener("click", () => $("file").click());
  $("file").addEventListener("change", async (ev) => {
    const file = ev.target.files[0];
    ev.target.value = "";
    if (!file) return;
    if (source.value && !window.confirm("Open " + file.name + "? The current text will be discarded.")) return;
    try { setSource(await file.text()); }
    catch { setStatus("That file could not be read.", true); }
  });

  $("btn-save").addEventListener("click", () => download(fileStem() + ".md", source.value, "text/markdown;charset=utf-8"));

  $("btn-pdf").addEventListener("click", async () => {
    if (compiledSource === null || source.value !== compiledSource) {
      if (!(await compile())) return;
    }
    document.body.classList.remove("no-preview");
    $("btn-toggle").setAttribute("aria-pressed", "true");
    setStatus("Waiting for images.");
    await imagesReady();
    setStatus("In the print dialog, choose “Save as PDF” as the printer.");
    window.print();
  });

  errorsButton.addEventListener("click", () => { errorsPanel.hidden = !errorsPanel.hidden; });
  $("btn-errors-close").addEventListener("click", () => { errorsPanel.hidden = true; });
  $("btn-log").addEventListener("click", () => download(fileStem() + ".log", logText(), "text/plain;charset=utf-8"));

  $("btn-theme").addEventListener("click", () => {
    const next = isDark() ? "light" : "dark";
    document.documentElement.dataset.theme = next;
    store.set("theme", next);
    showTheme();
  });
  darkQuery.addEventListener("change", showTheme);

  source.addEventListener("input", () => { scheduleSave(); showFreshness(); });

  document.addEventListener("keydown", (ev) => {
    if (!(ev.ctrlKey || ev.metaKey) || ev.altKey) return;
    if (ev.key === "Enter") { ev.preventDefault(); compile(); }
    else if (ev.key === "\\") { ev.preventDefault(); togglePreview(); }
  });

  // Save at once when the page is hidden or closed, and warn if the draft is not stored.
  document.addEventListener("visibilitychange", () => { if (document.hidden && saveTimer) saveDraft(); });
  window.addEventListener("pagehide", () => { if (saveTimer) saveDraft(); });
  window.addEventListener("beforeunload", (ev) => {
    if (saveTimer) saveDraft();
    if (unsaved && source.value) { ev.preventDefault(); ev.returnValue = ""; }
  });

  // The divider: drag it, or use the arrow keys.
  const divider = $("divider"), panes = $("panes");
  function setSplit(percent) {
    const p = Math.min(80, Math.max(20, percent));
    panes.style.setProperty("--split", p + "%");
    divider.setAttribute("aria-valuenow", String(Math.round(p)));
  }
  divider.addEventListener("pointerdown", (ev) => {
    divider.setPointerCapture(ev.pointerId);
    const move = (e) => { const box = panes.getBoundingClientRect(); setSplit(((e.clientX - box.left) / box.width) * 100); };
    const stop = () => { divider.removeEventListener("pointermove", move); divider.removeEventListener("pointerup", stop); divider.removeEventListener("pointercancel", stop); };
    divider.addEventListener("pointermove", move);
    divider.addEventListener("pointerup", stop);
    divider.addEventListener("pointercancel", stop);
    ev.preventDefault();
  });
  divider.addEventListener("keydown", (ev) => {
    const now = parseFloat(panes.style.getPropertyValue("--split")) || 50;
    if (ev.key === "ArrowLeft") { setSplit(now - 2); ev.preventDefault(); }
    else if (ev.key === "ArrowRight") { setSplit(now + 2); ev.preventDefault(); }
  });

  // ---- start -----------------------------------------------------------------------
  const draft = store.get("draft");
  source.value = draft !== null ? draft : WELCOME;
  showTheme();
  showFreshness();
  if (draft !== null) setStatus("Draft restored. Press Compile (Ctrl+Enter).");
})();
