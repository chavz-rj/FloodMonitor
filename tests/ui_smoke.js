// UI smoke test: loads the real frontend in headless Chromium against a MOCK backend and a MOCK weather service.
// It checks for JavaScript errors and takes screenshots. It does NOT test the C++ server (that needs Qt).
// Run: NODE_PATH=$(npm root -g) node tests/ui_smoke.js
const http = require("http"), fs = require("fs"), path = require("path");
const { chromium } = require("playwright");
const root = path.join(__dirname, "..", "frontend");
const TOKEN = "test-token";
const kv = {};
const seg = (id, name, level, status, label, msg) => ({ id, name, hasReading: level != null, level, levelText: level == null ? "no reading" : level.toFixed(2) + " m",
  alertLimit: 0.4, criticalLimit: 0.8, updatedText: "2026-10-19 14:00:00", ageText: "5 min ago", status, statusLabel: label, statusMessage: msg,
  oldReadingNote: false, trend: status === 2 ? 2 : 0, rate: 0.6, trendLine: status === 2 ? "TREND: RISING at about 0.60 m/h" : "", projectionLine: status === 2 ? "ESTIMATE: at this rate the CRITICAL limit could be reached in about 40 min." : "", text: "" });
const board = { ok: true, overall: 2, overallMessage: "OVERALL: ALERT - use caution and review the affected segment.", counts: { noData: 1, outdated: 0, safe: 1, alert: 1, critical: 0 },
  highestName: "JP Laurel - De La Salle Lipa Gate", highestMessage: "STATUS: [ALERT - DRIVE WITH CAUTION]", operator: "unknown", generatedText: "2026-10-19 14:05:00",
  segments: [seg(1, "JP Laurel - Robinsons Area", 0.2, 1, "SAFE", "STATUS: [SAFE - CLEAR PASSAGE]"), seg(2, "JP Laurel - De La Salle Lipa Gate", 0.35, 2, "ALERT", "STATUS: [ALERT - DRIVE WITH CAUTION]"),
    seg(3, "JP Laurel - Tambo Exit", null, 0, "NO DATA", "STATUS: [NO DATA - NOT YET MEASURED, TREAT AS UNKNOWN]")] };
const api = {
  startup: () => ({ ok: true, loadResult: 1, operator: "unknown", maxOperatorLength: 30, maxDepth: 10, outdatedAfterMinutes: 30 }),
  board: () => board,
  setOperator: (a) => (a.name ? { ok: true, operator: a.name } : { ok: false, message: "Invalid name." }),
  updateLevel: (a) => (Number(a.depth) > 0.9 && !a.confirmed ? { ok: false, needsConfirm: true, message: "The new value is a very big change. Is it correct?" }
    : { ok: true, message: "Updated.", warnings: [], segment: seg(1, "JP Laurel - Robinsons Area", Number(a.depth), 1, "SAFE", "STATUS: [SAFE - CLEAR PASSAGE]") }),
  history: () => ({ ok: true, available: true, limit: 10, lines: ["2026-10-19 14:00:00 | JP Laurel - Robinsons Area | 0.20 m | STATUS: [SAFE] | by Tester"] }),
  statistics: () => ({ ok: true, total: 3, segments: [{ id: 1, name: "A", readings: 3, warnings: 0, peakText: "0.30 m", peakTime: "2026-10-19 14:00:00" }, { id: 2, name: "B", readings: 0 }, { id: 3, name: "C", readings: 0 }] }),
  report: () => ({ ok: true, saved: true, fileName: "flood_report_x.txt", text: "REPORT\nline" }),
  locate: () => ({ ok: false, code: "denied", message: "Location permission is turned off. Search for a city instead." }),
  storageGet: (a) => ({ ok: true, value: kv[a.key] || "" }),
  storageSet: (a) => { kv[a.key] = a.value; return { ok: true }; },
};
const types = { ".html": "text/html", ".css": "text/css", ".js": "application/javascript" };
const server = http.createServer((req, res) => {
  if (req.method === "POST" && req.url.startsWith("/api/")) {
    if (req.headers["x-app-token"] !== TOKEN) { res.writeHead(403); return res.end("no"); }
    let body = ""; req.on("data", (d) => (body += d));
    req.on("end", () => { const fn = api[req.url.slice(5)]; res.writeHead(fn ? 200 : 404, { "Content-Type": "application/json" }); res.end(JSON.stringify(fn ? fn(JSON.parse(body || "{}")) : { error: "Unknown" })); });
    return;
  }
  const f = path.join(root, req.url === "/" ? "index.html" : req.url.split("?")[0]);
  if (!f.startsWith(root) || !fs.existsSync(f)) { res.writeHead(404); return res.end(); }
  let data = fs.readFileSync(f); if (f.endsWith("index.html")) data = Buffer.from(data.toString().replace("__APP_TOKEN__", TOKEN));
  res.writeHead(200, { "Content-Type": types[path.extname(f)] || "text/plain" }); res.end(data);
});

const forecast = { timezone: "Asia/Manila", current: { time: "2026-10-19T14:15", temperature_2m: 31.4, apparent_temperature: 36.2, relative_humidity_2m: 70, is_day: 1, weather_code: 63, pressure_msl: 1004.6, wind_speed_10m: 18, wind_direction_10m: 225 },
  hourly: { time: Array.from({ length: 48 }, (_, i) => `2026-10-${19 + Math.floor(i / 24)}T${String(i % 24).padStart(2, "0")}:00`), temperature_2m: Array(48).fill(30), precipitation_probability: Array(48).fill(40), precipitation: Array(48).fill(0.5), weather_code: Array(48).fill(61), visibility: Array(48).fill(9000), uv_index: Array(48).fill(7.2), is_day: Array(48).fill(1) },
  daily: { time: ["2026-10-19", "2026-10-20", "2026-10-21"], weather_code: [63, 3, 0], temperature_2m_max: [33, 32, 34], temperature_2m_min: [25, 24, 25], sunrise: Array(3).fill("2026-10-19T05:42"), sunset: Array(3).fill("2026-10-19T17:30"), precipitation_probability_max: [80, 20, 5] } };

(async () => {
  await new Promise((r) => server.listen(0, "127.0.0.1", r));
  const base = "http://127.0.0.1:" + server.address().port + "/";
  const browser = await chromium.launch();
  const page = await (await browser.newContext({ viewport: { width: 360, height: 780 }, deviceScaleFactor: 2 })).newPage();
  const errors = [];
  page.on("pageerror", (e) => errors.push("pageerror: " + e.message));
  page.on("console", (m) => { if (m.type() === "error") errors.push("console: " + m.text()); });
  await page.route("https://geocoding-api.open-meteo.com/**", (r) => r.fulfill({ contentType: "application/json", headers: { "access-control-allow-origin": "*" },
    body: JSON.stringify({ results: [{ name: "Lipa City", admin1: "Batangas", country: "Philippines", country_code: "PH", latitude: 13.94, longitude: 121.16 }] }) }));
  let fcCalls = 0;
  await page.route("https://api.open-meteo.com/**", (r) => { fcCalls++; r.fulfill({ contentType: "application/json", headers: { "access-control-allow-origin": "*" }, body: JSON.stringify(forecast) }); });
  const ok = (cond, msg) => { if (!cond) errors.push("check failed: " + msg); };
  const shot = (n) => page.screenshot({ path: path.join("/tmp", "shot_" + n + ".png") });

  await page.goto(base);
  await page.waitForSelector("#dlg-operator:not([hidden])");
  await page.fill("#op-input", "Enforcer 07"); await page.click("#op-form button[type=submit]");
  await page.waitForSelector("#dlg-operator", { state: "hidden" });
  ok((await page.locator("#board-list .card").count()) === 3, "3 segment cards"); await shot("board");

  await page.click('.tabbar [data-view="update"]');
  await page.selectOption("#upd-segment", "1"); await page.fill("#upd-depth", "0,25"); await page.click("#upd-submit");
  await page.waitForSelector("#upd-result:not([hidden]) .ok-text"); await shot("update");
  await page.fill("#upd-depth", "0.95"); await page.click("#upd-submit");
  await page.waitForSelector("#dlg-confirm:not([hidden])"); await page.click("#confirm-yes");
  await page.waitForSelector("#upd-result .ok-text");

  await page.click('.tabbar [data-view="weather"]');
  await page.waitForSelector(".wx-hero"); ok((await page.textContent(".wx-temp")).trim() === "31°", "temp shows 31°");
  ok(/Lipa City/.test(await page.textContent(".wx-place")), "default place Lipa City"); await shot("weather");
  await page.click("#wx-locate"); await page.waitForSelector("#wx-banner:not([hidden])");
  ok(/permission/i.test(await page.textContent("#wx-banner")), "denied message shown"); ok(fcCalls === 1, "forecast fetched once (cache/no loop)");
  await page.click("#btn-settings"); await page.selectOption("#set-temp", "f"); await page.click("#set-close");
  ok((await page.textContent(".wx-temp")).trim() === "89°", "°F conversion applied without refetch"); ok(fcCalls === 1, "unit change does not refetch");
  await page.selectOption("#set-theme", "dark").catch(() => {});

  await page.click('.tabbar [data-view="insights"]'); await page.click('[data-insight="history"]'); await page.waitForSelector("#pane-history li");
  await page.click('[data-insight="statistics"]'); await page.waitForSelector("#pane-statistics .card");
  await page.click('.tabbar [data-view="report"]'); await page.click("#rep-btn"); await page.waitForSelector("#rep-text");
  ok(await page.evaluate(() => window.appBack()) === true, "Back leaves Report for Board"); ok(await page.evaluate(() => window.appBack()) === false, "Back on Board lets the app exit");

  await page.setViewportSize({ width: 740, height: 360 }); await shot("landscape");
  const overflow = await page.evaluate(() => document.documentElement.scrollWidth > document.documentElement.clientWidth);
  ok(!overflow, "no horizontal page scroll in landscape");
  await browser.close(); server.close();
  console.log(errors.length ? "UI smoke test FAILED:\n" + errors.join("\n") : "UI smoke test passed (mock backend)");
  process.exit(errors.length ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
