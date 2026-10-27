/* backend.js - bridge to the C++ backend + tiny DOM helpers.
   JavaScript never touches files or flood logic directly: it can only call the named
   functions that LocalApiServer allows (startup, board, updateLevel, ...). */
"use strict";

const Backend = (() => {
  const token = document.body.dataset.token;

  async function call(name, args) {
    let res;
    try {
      res = await fetch("/api/" + name, {
        method: "POST",
        headers: { "Content-Type": "application/json", "X-App-Token": token },
        body: JSON.stringify(args || {}),
      });
    } catch (e) {
      throw new Error("Cannot reach the app backend. Please restart the app.");
    }
    let data;
    try { data = await res.json(); } catch (e) { throw new Error("The backend sent an unreadable answer."); }
    if (!res.ok) throw new Error(data.error || "Backend error " + res.status);
    return data;
  }

  // Small JSON storage kept by C++ (survives restarts; allow-listed keys only).
  async function getJson(key, fallback) {
    try {
      const r = await call("storageGet", { key });
      return r.value ? JSON.parse(r.value) : fallback;
    } catch (e) { return fallback; }
  }
  async function setJson(key, value) {
    try { await call("storageSet", { key, value: JSON.stringify(value) }); } catch (e) { /* non-fatal */ }
  }

  return { call, getJson, setJson };
})();

/* ---- DOM helpers (always use textContent-style insertion; never innerHTML with data) ---- */
function h(tag, attrs, ...kids) {
  const el = document.createElement(tag);
  if (attrs) {
    for (const [k, v] of Object.entries(attrs)) {
      if (v == null || v === false) continue;
      if (k === "class") el.className = v;
      else if (k.startsWith("on")) el.addEventListener(k.slice(2), v);
      else el.setAttribute(k, v === true ? "" : v);
    }
  }
  for (const kid of kids.flat()) {
    if (kid == null || kid === false) continue;
    el.append(kid.nodeType ? kid : document.createTextNode(String(kid)));
  }
  return el;
}
const $ = (sel, root) => (root || document).querySelector(sel);
const $$ = (sel, root) => Array.from((root || document).querySelectorAll(sel));
function setChildren(el, ...kids) { el.replaceChildren(...kids.flat().filter((k) => k != null && k !== false)); }
