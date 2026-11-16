// Applies the remembered theme before the page is drawn, so it does not flash.
// (Kept in its own small file because the page allows no inline script.)
(function () {
  try {
    var theme = localStorage.getItem("travel_notes:theme");
    if (theme === "light" || theme === "dark") document.documentElement.dataset.theme = theme;
  } catch (e) { /* storage unavailable: follow the system setting */ }
})();
