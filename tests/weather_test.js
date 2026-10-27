// Run: node tests/weather_test.js     (tests the pure weather logic; fixtures below are TEST DATA only, never shown in the app)
const assert = require("assert");
const W = require("../frontend/js/weather.js");

// A response shaped like Open-Meteo's, with some fields missing on purpose.
const raw = {
  timezone: "Asia/Manila",
  current: { time: "2026-10-19T14:15", temperature_2m: 31.4, apparent_temperature: 36.2, relative_humidity_2m: 70, is_day: 1,
    weather_code: 63, pressure_msl: 1004.6, wind_speed_10m: 18, wind_direction_10m: 225 },
  hourly: { time: Array.from({ length: 48 }, (_, i) => "2026-10-" + (19 + Math.floor(i / 24)) + "T" + String(i % 24).padStart(2, "0") + ":00"),
    temperature_2m: Array(48).fill(30), precipitation_probability: Array(48).fill(40), precipitation: Array(48).fill(0.5),
    weather_code: Array(48).fill(61), visibility: Array(48).fill(9000), uv_index: Array(48).fill(7.2), is_day: Array(48).fill(1) },
  daily: { time: ["2026-10-19", "2026-10-20"], weather_code: [63, 3], temperature_2m_max: [33, 32], temperature_2m_min: [25, 24],
    sunrise: ["2026-10-19T05:42", "2026-10-20T05:42"], sunset: ["2026-10-19T17:30", "2026-10-20T17:29"] /* uv/pop missing */ },
};

const m = W.normalize(raw);
assert.strictEqual(m.hourly[0].time, "2026-10-19T14:00");            // starts at the current hour
assert.strictEqual(m.hourly.length, 24);
assert.strictEqual(m.rain24, 12);                                      // 24 x 0.5 mm
assert.strictEqual(m.now.visibilityM, 9000);
assert.strictEqual(m.daily[0].uvMax, null);                            // missing field stays null, not invented
assert.strictEqual(m.current.pressure, 1004.6);
assert.strictEqual(m.timezone, "Asia/Manila");

// bad / partial responses are rejected, not displayed
assert.throws(() => W.normalize(null));
assert.throws(() => W.normalize({}));
assert.throws(() => W.normalize({ error: true, reason: "Too many requests" }), /Too many requests/);
assert.throws(() => W.normalize({ current: {}, hourly: {}, daily: {} }));
const noNums = JSON.parse(JSON.stringify(raw)); noNums.current.temperature_2m = "hot";
assert.strictEqual(W.normalize(noNums).current.temp, null);           // wrong type -> null

// units
assert.strictEqual(W.fmt(W.toTemp(0, "f")), "32");
assert.strictEqual(W.fmt(W.toTemp(-0.2, "c")), "0");                  // no "-0"
assert.strictEqual(W.fmt(W.toWind(36, "ms")), "10");
assert.strictEqual(W.fmt(W.toWind(100, "mph")), "62");
assert.strictEqual(W.fmt(null), "—");
assert.strictEqual(W.compass(225), "SW"); assert.strictEqual(W.compass(359), "N"); assert.strictEqual(W.compass(null), "—");

// time and labels
assert.strictEqual(W.hhmm("2026-10-19T17:30", "24"), "17:30");
assert.strictEqual(W.hhmm("2026-10-19T17:30", "12"), "5:30 PM");
assert.strictEqual(W.hhmm("2026-10-19T00:05", "12"), "12:05 AM");
assert.strictEqual(W.hhmm(null, "24"), "—");
assert.strictEqual(W.hourLabel("2026-10-19T13:00", "12"), "1 PM");
assert.strictEqual(W.dayLabel("2026-10-19", 0), "Today");
assert.strictEqual(W.dayLabel("2026-10-20", 1, "en"), new Date("2026-10-20T00:00:00Z").toLocaleDateString(undefined, { weekday: "short", timeZone: "UTC" }));
assert.strictEqual(W.uvLabel(7.2), "7 (High)"); assert.strictEqual(W.uvLabel(null), "—");
assert.strictEqual(W.agoText(30000), "just now"); assert.strictEqual(W.agoText(125 * 60000), "2 h 5 min ago");
assert.strictEqual(W.describe(63, 1).icon, "🌧️"); assert.strictEqual(W.describe(0, 0).icon, "🌙"); assert.strictEqual(W.describe(12345, 1).text, "Conditions unavailable");

// URLs
const u = new URL(W.forecastUrl(13.9411, 121.1631));
assert.strictEqual(u.hostname, "api.open-meteo.com"); assert.strictEqual(u.searchParams.get("timezone"), "auto");
assert.ok(u.searchParams.get("hourly").includes("precipitation_probability"));
assert.ok(W.geocodeUrl("São Paulo & co").includes("S%C3%A3o+Paulo+%26+co"));

// fetchJson: retry once on network failure, never more than 2 attempts; rate limit is not retried
(async () => {
  let calls = 0;
  global.fetch = async () => { calls++; throw new TypeError("network"); };
  await assert.rejects(W.fetchJson("https://x", { retries: 1 }), /No internet connection/);
  assert.strictEqual(calls, 2);
  calls = 0;
  global.fetch = async () => { calls++; return { status: 429, ok: false }; };
  await assert.rejects(W.fetchJson("https://x", { retries: 1 }), /busy/);
  assert.strictEqual(calls, 1);
  global.fetch = async () => ({ status: 200, ok: true, json: async () => ({ a: 1 }) });
  assert.deepStrictEqual(await W.fetchJson("https://x"), { a: 1 });
  console.log("weather tests passed");
})().catch((e) => { console.error(e); process.exit(1); });
