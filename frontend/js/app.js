/* app.js - screens for the original flood-monitoring features (menu 1-6), navigation and lifecycle.
   All flood logic lives in C++; this file only shows results and sends the user's input. */
"use strict";

const STATUS_ICON = { 0: "❔", 1: "✅", 2: "⚠️", 3: "⛔", 4: "🕒" };            // shape + text, never colour alone
const TREND_ICON = { 0: "", 1: "▬", 2: "▲", 3: "▼" };
const OVERALL_CLASS = { 0: "safe", 1: "none", 2: "alert", 3: "crit" };

const App = (() => {
  let info = null;            // result of backend "startup"
  let boardData = null;
  let view = "board";
  let pendingConfirm = null;  // depth waiting for the user's "yes, it is correct"
  let boardTimer = null;

  /* ------------------------------------------------------------ navigation ------------------------------------------------------------ */
  function showView(name) {
    view = name;
    $$(".view").forEach((v) => v.classList.toggle("active", v.id === "view-" + name));
    $$(".tabbar button").forEach((b) => {
      const on = b.dataset.view === name;
      b.classList.toggle("active", on);
      b.setAttribute("aria-current", on ? "page" : "false");
    });
    $("#views").scrollTop = 0;
    if (name === "board") refreshBoard();
    if (name === "update") prepareUpdate();
    if (name === "weather") WeatherUI.show();
    if (name === "insights") showInsight(currentInsight);
  }

  function openModal(id) { $("#" + id).hidden = false; const f = $("#" + id + " input, #" + id + " select, #" + id + " button"); if (f) f.focus(); }
  function closeModal(id) { $("#" + id).hidden = true; }
  function anyModalOpen() { return $$(".modal").find((m) => !m.hidden) || null; }

  // Called by the native Back button (see qml/Main.qml). Returns true when the page handled it.
  window.appBack = function () {
    const m = anyModalOpen();
    if (m) { if (m.id === "dlg-operator") skipOperator(); else m.hidden = true; return true; }
    const results = $("#wx-results");
    if (results && !results.hidden) { results.hidden = true; return true; }
    if (view !== "board") { showView("board"); return true; }
    return false;   // nothing left to go back to -> the app closes
  };

  /* ------------------------------------------------------------ board (menu 1 + 3) ------------------------------------------------------------ */
  async function refreshBoard() {
    try {
      boardData = await Backend.call("board");
      renderBoard();
      if (view === "insights" && currentInsight === "summary") renderSummary();
    } catch (e) {
      setChildren($("#board-list"), errorBox(e.message, refreshBoard));
    }
  }

  function errorBox(message, retry) {
    return h("div", { class: "empty" }, h("p", { class: "error-text" }, message), h("button", { class: "btn", type: "button", onclick: retry }, "Try again"));
  }

  function badge(status, label) { return h("span", { class: "badge s" + status }, STATUS_ICON[status] + " " + label); }

  function renderBoard() {
    const b = boardData;
    const cls = OVERALL_CLASS[b.overall];
    setChildren($("#board-overall"), h("div", { class: "overall " + cls }, h("strong", null, b.overallMessage)));
    setChildren($("#board-list"), b.segments.map(segmentCard));
    $("#board-time").textContent = "Checked " + b.generatedText;
  }

  function segmentCard(s) {
    const pctOf = (v) => Math.max(0, Math.min(100, (v / s.criticalLimit) * 100));
    const gauge = h("div", { class: "gauge", role: "img",
      "aria-label": "Water level " + s.levelText + ", alert at " + s.alertLimit.toFixed(2) + " m, critical at " + s.criticalLimit.toFixed(2) + " m" },
      h("div", { class: "gauge-fill s" + s.status, style: "width:" + (s.hasReading ? pctOf(s.level) : 0) + "%" }),
      h("div", { class: "gauge-mark", style: "left:" + pctOf(s.alertLimit) + "%" }));
    return h("article", { class: "card seg s" + s.status },
      h("div", { class: "seg-head" }, h("h3", null, s.name), badge(s.status, s.statusLabel)),
      h("div", { class: "seg-level" }, h("span", { class: "big" }, s.hasReading ? s.level.toFixed(2) : "—"), h("span", { class: "muted" }, s.hasReading ? " m" : " no reading"),
        s.trend ? h("span", { class: "trend t" + s.trend, "aria-hidden": "true" }, " " + TREND_ICON[s.trend]) : null),
      gauge,
      h("p", { class: "muted small" }, "Alert " + s.alertLimit.toFixed(2) + " m · Critical " + s.criticalLimit.toFixed(2) + " m"),
      h("p", { class: "small" }, s.hasReading ? "Updated " + s.updatedText + " (" + s.ageText + ")" : "Updated: never"),
      h("p", { class: "status-msg" }, s.statusMessage),
      s.oldReadingNote ? h("p", { class: "note" }, "NOTE: this reading is older than " + info.outdatedAfterMinutes + " minutes. Please verify and update it.") : null,
      s.trendLine ? h("p", { class: "small" }, s.trendLine) : null,
      s.projectionLine ? h("p", { class: "small projection" }, s.projectionLine) : null,
      h("button", { class: "btn secondary", type: "button", onclick: () => { $("#upd-segment").value = String(s.id); showView("update"); $("#upd-segment").value = String(s.id); } }, "Update this segment"));
  }

  /* ------------------------------------------------------------ update (menu 2) ------------------------------------------------------------ */
  function prepareUpdate() {
    const sel = $("#upd-segment");
    const keep = sel.value;
    if (boardData) {
      setChildren(sel, boardData.segments.map((s) => h("option", { value: String(s.id) }, s.id + ". " + s.name + " (now: " + s.levelText + ")")));
      if (keep) sel.value = keep;
    }
    $("#upd-hint").textContent = "Depth in meters, 0.00 to " + info.maxDepth.toFixed(2) + " (example: 0.45)";
    $("#upd-operator").textContent = info.operator === "unknown" ? "Operator not set" : "Operator: " + info.operator;
  }

  async function submitUpdate(confirmed) {
    const btn = $("#upd-submit");
    const out = $("#upd-result");
    if (info.operator === "unknown") { openModal("dlg-operator"); return; }

    let depth = $("#upd-depth").value.trim();
    if (/^\d*,\d+$/.test(depth)) depth = depth.replace(",", ".");   // phone keyboards may offer a decimal comma
    if (depth === "") { showResult(out, "Enter the new water depth.", "error"); return; }

    btn.disabled = true;
    try {
      const r = await Backend.call("updateLevel", { segmentId: Number($("#upd-segment").value), depth, confirmed: !!confirmed });
      if (r.needsConfirm) {
        pendingConfirm = true;
        $("#confirm-text").textContent = r.message;
        openModal("dlg-confirm");
      } else if (!r.ok) {
        showResult(out, r.message, "error");
      } else {
        $("#upd-depth").value = "";
        const s = r.segment;
        const lines = [h("p", { class: "ok-text" }, r.message), h("p", { class: "status-msg" }, s.statusMessage)];
        if (s.trendLine) lines.push(h("p", { class: "small" }, s.trendLine));
        if (s.projectionLine) lines.push(h("p", { class: "small projection" }, s.projectionLine));
        r.warnings.forEach((w) => lines.push(h("p", { class: "note" }, "WARNING: " + w)));
        setChildren(out, lines);
        out.className = "result-box";
        out.hidden = false;
        refreshBoard();
      }
    } catch (e) {
      showResult(out, e.message, "error");
    } finally {
      btn.disabled = false;
    }
  }
  function showResult(box, text, kind) { setChildren(box, h("p", { class: kind === "error" ? "error-text" : "" }, text)); box.className = "result-box"; box.hidden = false; }

  /* ------------------------------------------------------------ insights (menu 3, 4, 5) ------------------------------------------------------------ */
  let currentInsight = "summary";
  function showInsight(name) {
    currentInsight = name;
    $$("#insight-tabs button").forEach((b) => b.setAttribute("aria-selected", String(b.dataset.insight === name)));
    $$(".insight-pane").forEach((p) => (p.hidden = p.id !== "pane-" + name));
    if (name === "summary") { if (boardData) renderSummary(); else refreshBoard(); }
    if (name === "history") loadHistory();
    if (name === "statistics") loadStatistics();
  }

  function renderSummary() {
    const b = boardData, c = b.counts;
    const row = (label, v) => h("div", { class: "stat" }, h("span", null, label), h("strong", null, String(v)));
    setChildren($("#pane-summary"),
      h("div", { class: "card" }, row("Segments with no reading", c.noData), row("Outdated readings", c.outdated), row("Recent and safe", c.safe),
        row("At alert level", c.alert), row("At critical level", c.critical)),
      h("div", { class: "card" }, h("p", { class: "muted small" }, "Highest measured risk"),
        h("strong", null, b.highestName || "none (no segments measured yet)"), b.highestMessage ? h("p", null, b.highestMessage) : null),
      h("div", { class: "overall " + OVERALL_CLASS[b.overall] }, h("strong", null, b.overallMessage)));
  }

  async function loadHistory() {
    const pane = $("#pane-history");
    setChildren(pane, h("p", { class: "muted pad" }, "Loading…"));
    try {
      const r = await Backend.call("history");
      if (!r.available) setChildren(pane, h("p", { class: "empty" }, "No update history is available yet."));
      else if (r.lines.length === 0) setChildren(pane, h("p", { class: "empty" }, "The update history file is empty."));
      else setChildren(pane, h("p", { class: "muted small" }, "Latest " + r.limit + " updates"),
        h("ol", { class: "history" }, r.lines.map((l) => h("li", { class: "card small" }, l))));
    } catch (e) { setChildren(pane, errorBox(e.message, loadHistory)); }
  }

  async function loadStatistics() {
    const pane = $("#pane-statistics");
    setChildren(pane, h("p", { class: "muted pad" }, "Loading…"));
    try {
      const r = await Backend.call("statistics");
      if (r.total <= 0) { setChildren(pane, h("p", { class: "empty" }, "No readings have been recorded yet.")); return; }
      setChildren(pane, r.segments.map((s) => h("div", { class: "card" }, h("h3", null, "Segment " + s.id + ": " + s.name),
        s.readings === 0 ? h("p", { class: "muted" }, "No readings recorded.") : h("div", null,
          h("p", { class: "small" }, "Readings recorded: " + s.readings),
          h("p", { class: "small" }, "Highest depth: " + s.peakText + " (" + s.peakTime + ")"),
          h("p", { class: "small" }, "Readings at ALERT or CRITICAL level: " + s.warnings)))),
        h("p", { class: "muted small" }, "Total readings recorded: " + r.total));
    } catch (e) { setChildren(pane, errorBox(e.message, loadStatistics)); }
  }

  /* ------------------------------------------------------------ report (menu 6) ------------------------------------------------------------ */
  async function exportReport() {
    const btn = $("#rep-btn"), out = $("#rep-out");
    btn.disabled = true;
    try {
      const r = await Backend.call("report");
      setChildren(out, h("p", { class: r.saved ? "ok-text" : "error-text" }, r.saved ? "Situation report saved in the app's storage as " + r.fileName : "The report could not be saved."),
        h("pre", { id: "rep-text", class: "report" }, r.text),
        h("button", { class: "btn secondary", type: "button", onclick: () => copyText(r.text) }, "Copy report text"));
    } catch (e) { setChildren(out, errorBox(e.message, exportReport)); }
    finally { btn.disabled = false; }
  }
  async function copyText(text) {
    try { await navigator.clipboard.writeText(text); toast("Copied"); }
    catch (e) {
      const range = document.createRange(); range.selectNodeContents($("#rep-text"));
      const sel = getSelection(); sel.removeAllRanges(); sel.addRange(range);
      toast("Select and copy the highlighted text");
    }
  }
  function toast(text) { const t = $("#toast"); t.textContent = text; t.hidden = false; setTimeout(() => (t.hidden = true), 2200); }

  /* ------------------------------------------------------------ operator + settings ------------------------------------------------------------ */
  async function saveOperator() {
    const input = $("#op-input"), err = $("#op-error");
    try {
      const r = await Backend.call("setOperator", { name: input.value });
      if (!r.ok) { err.textContent = r.message; err.hidden = false; return; }
      err.hidden = true;
      info.operator = r.operator;
      Backend.setJson("operator", r.operator);
      $("#op-chip").textContent = "👤 " + r.operator;
      closeModal("dlg-operator");
      if (view === "update") prepareUpdate();
    } catch (e) { err.textContent = e.message; err.hidden = false; }
  }
  function skipOperator() { closeModal("dlg-operator"); }   // stays "unknown"; the Update screen asks again

  function openSettings() {
    const s = Settings.get();
    $("#set-theme").value = s.theme; $("#set-temp").value = s.tempUnit; $("#set-wind").value = s.windUnit; $("#set-clock").value = s.clock;
    openModal("dlg-settings");
  }

  /* ------------------------------------------------------------ startup ------------------------------------------------------------ */
  async function start() {
    await Settings.load();
    $$(".tabbar button").forEach((b) => b.addEventListener("click", () => showView(b.dataset.view)));
    $$("#insight-tabs button").forEach((b) => b.addEventListener("click", () => showInsight(b.dataset.insight)));
    $("#upd-form").addEventListener("submit", (e) => { e.preventDefault(); submitUpdate(false); });
    $("#confirm-yes").addEventListener("click", () => { closeModal("dlg-confirm"); submitUpdate(true); });
    $("#confirm-no").addEventListener("click", () => { closeModal("dlg-confirm"); showResult($("#upd-result"), "Cancelled. Nothing was changed.", ""); });
    $("#op-form").addEventListener("submit", (e) => { e.preventDefault(); saveOperator(); });
    $("#op-skip").addEventListener("click", skipOperator);
    $("#op-chip").addEventListener("click", () => { $("#op-error").hidden = true; openModal("dlg-operator"); });
    $("#btn-settings").addEventListener("click", openSettings);
    $("#set-close").addEventListener("click", () => closeModal("dlg-settings"));
    $("#rep-btn").addEventListener("click", exportReport);
    $("#board-refresh").addEventListener("click", refreshBoard);
    [["set-theme", "theme"], ["set-temp", "tempUnit"], ["set-wind", "windUnit"], ["set-clock", "clock"]].forEach(([id, key]) =>
      $("#" + id).addEventListener("change", (e) => Settings.update({ [key]: e.target.value }).then(() => WeatherUI.rerender())));
    $$("input").forEach((i) => i.addEventListener("focus", () => setTimeout(() => i.scrollIntoView({ block: "center" }), 300)));   // keep field above the keyboard
    WeatherUI.init();

    try {
      info = await Backend.call("startup");
    } catch (e) {
      setChildren($("#board-list"), errorBox(e.message, () => location.reload()));
      return;
    }
    if (info.loadResult === 1) showStartupNote("No saved data found. All segments start as “no reading”. Data will be saved when you update a level.");
    if (info.loadResult === 2) showStartupNote("The saved data file is incomplete, damaged, or from an older version. All segments are set to “no reading”.", true);

    const saved = await Backend.getJson("operator", "");
    $("#op-input").value = typeof saved === "string" ? saved : "";
    $("#op-input").maxLength = info.maxOperatorLength;
    $("#op-chip").textContent = "👤 not set";
    await refreshBoard();
    openModal("dlg-operator");   // like the console version: ask for name / badge number at start

    // Status can become "outdated" as time passes, so re-check once a minute while the board is visible (local call, no network).
    boardTimer = setInterval(() => { if (!document.hidden && view === "board") refreshBoard(); }, 60000);
    document.addEventListener("visibilitychange", () => { if (!document.hidden) { if (view === "board") refreshBoard(); if (view === "weather") WeatherUI.show(); } });
  }
  function showStartupNote(text, warn) { const n = $("#startup-note"); n.textContent = text; n.className = "banner " + (warn ? "warn" : ""); n.hidden = false; }

  return { start };
})();

document.addEventListener("DOMContentLoaded", App.start);
