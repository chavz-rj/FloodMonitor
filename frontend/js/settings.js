/* settings.js - user settings (theme, units, clock), saved by the C++ side. */
"use strict";

const Settings = (() => {
  const defaults = { theme: "auto", tempUnit: "c", windUnit: "kmh", clock: "24" };
  const allowed = {
    theme: ["auto", "light", "dark"],
    tempUnit: ["c", "f"],
    windUnit: ["kmh", "mph", "ms", "kn"],
    clock: ["12", "24"],
  };
  let current = Object.assign({}, defaults);
  const listeners = [];

  function clean(obj) {
    const out = Object.assign({}, defaults);
    for (const k of Object.keys(allowed)) {
      if (obj && allowed[k].includes(obj[k])) out[k] = obj[k];
    }
    return out;
  }
  function apply() { document.documentElement.dataset.theme = current.theme; }

  async function load() {
    current = clean(await Backend.getJson("settings", {}));
    apply();
    return current;
  }
  async function update(patch) {
    current = clean(Object.assign({}, current, patch));
    apply();
    await Backend.setJson("settings", current);
    listeners.forEach((fn) => fn(current));
  }
  return { load, update, get: () => current, onChange: (fn) => listeners.push(fn), allowed };
})();
