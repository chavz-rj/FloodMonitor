/* export.js - adds a "Save files to phone" button.
   Tapping it asks the C++ backend to copy the saved data files (flood_data.txt, flood_log.txt, reports)
   into the phone's Downloads/FloodMonitor folder, where the Files app can open them.
   Needs backend.js (Backend, h) to be loaded first. */
"use strict";

(() => {
  // Move the button here if it covers something in the interface (distances are from the screen edges).
  const BOTTOM = "88px";
  const RIGHT = "16px";

  const note = h("div", {
    id: "export-note", role: "status",
    style: "position:fixed;right:" + RIGHT + ";bottom:calc(" + BOTTOM + " + 56px);max-width:80vw;padding:10px 14px;" +
           "border-radius:10px;background:#111c33;color:#fff;font:14px sans-serif;border:1px solid #2b3a5c;display:none;z-index:9999",
  });

  const button = h("button", {
    type: "button", id: "export-button",
    style: "position:fixed;right:" + RIGHT + ";bottom:" + BOTTOM + ";padding:12px 16px;border:0;border-radius:24px;" +
           "background:#1d4ed8;color:#fff;font:600 14px sans-serif;box-shadow:0 2px 8px rgba(0,0,0,.4);z-index:9999",
  }, "Save files to phone");

  let hideTimer = null;
  function say(text) {
    note.textContent = text;
    note.style.display = "block";
    clearTimeout(hideTimer);
    hideTimer = setTimeout(() => { note.style.display = "none"; }, 6000);
  }

  button.addEventListener("click", async () => {
    button.disabled = true;
    say("Saving...");
    try {
      const r = await Backend.call("exportFiles");
      say(r.ok ? r.message : (r.message || "The files could not be saved."));
    } catch (e) {
      say(e.message);
    }
    button.disabled = false;
  });

  document.body.append(note, button);
})();
