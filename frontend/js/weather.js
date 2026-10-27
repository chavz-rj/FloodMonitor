/* weather.js - smartphone-style weather feature. Data: Open-Meteo (https://open-meteo.com, CC BY 4.0, no API key).
   Part 1 (WeatherCore) is pure logic and is unit-tested in Node. Part 2 (WeatherUI) builds the screen. */
"use strict";

const WeatherCore = (() => {
  // WMO weather codes used by Open-Meteo: [description, day icon, night icon]
  const CODES = {
    0: ["Clear sky", "☀️", "🌙"], 1: ["Mainly clear", "🌤️", "🌙"], 2: ["Partly cloudy", "⛅", "☁️"], 3: ["Overcast", "☁️", "☁️"],
    45: ["Fog", "🌫️", "🌫️"], 48: ["Freezing fog", "🌫️", "🌫️"],
    51: ["Light drizzle", "🌦️", "🌧️"], 53: ["Drizzle", "🌦️", "🌧️"], 55: ["Heavy drizzle", "🌧️", "🌧️"],
    56: ["Freezing drizzle", "🌧️", "🌧️"], 57: ["Heavy freezing drizzle", "🌧️", "🌧️"],
    61: ["Light rain", "🌦️", "🌧️"], 63: ["Rain", "🌧️", "🌧️"], 65: ["Heavy rain", "🌧️", "🌧️"],
    66: ["Freezing rain", "🌧️", "🌧️"], 67: ["Heavy freezing rain", "🌧️", "🌧️"],
    71: ["Light snow", "🌨️", "🌨️"], 73: ["Snow", "🌨️", "🌨️"], 75: ["Heavy snow", "❄️", "❄️"], 77: ["Snow grains", "🌨️", "🌨️"],
    80: ["Light rain showers", "🌦️", "🌧️"], 81: ["Rain showers", "🌧️", "🌧️"], 82: ["Violent rain showers", "⛈️", "⛈️"],
    85: ["Light snow showers", "🌨️", "🌨️"], 86: ["Heavy snow showers", "❄️", "❄️"],
    95: ["Thunderstorm", "⛈️", "⛈️"], 96: ["Thunderstorm with hail", "⛈️", "⛈️"], 99: ["Thunderstorm with heavy hail", "⛈️", "⛈️"],
  };
  function describe(code, isDay) {
    const e = CODES[code];
    if (!e) return { text: "Conditions unavailable", icon: "❔" };
    return { text: e[0], icon: isDay === 0 ? e[2] : e[1] };
  }

  const num = (v) => (typeof v === "number" && isFinite(v) ? v : null);

  function forecastUrl(lat, lon) {
    const p = new URLSearchParams({
      latitude: lat.toFixed(4), longitude: lon.toFixed(4),
      current: "temperature_2m,relative_humidity_2m,apparent_temperature,is_day,precipitation,weather_code,pressure_msl,wind_speed_10m,wind_direction_10m",
      hourly: "temperature_2m,precipitation_probability,precipitation,weather_code,visibility,uv_index,is_day",
      daily: "weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset,uv_index_max,precipitation_probability_max,precipitation_sum",
      timezone: "auto", forecast_days: "10",
    });
    return "https://api.open-meteo.com/v1/forecast?" + p.toString();
  }
  function geocodeUrl(query) {
    return "https://geocoding-api.open-meteo.com/v1/search?" +
      new URLSearchParams({ name: query, count: "6", language: "en", format: "json" }).toString();
  }

  function validate(raw) {
    if (raw && raw.error) throw new Error("The weather service refused the request" + (raw.reason ? ": " + raw.reason : "."));
    const ok = raw && typeof raw === "object" && raw.current && typeof raw.current.time === "string" &&
      raw.hourly && Array.isArray(raw.hourly.time) && raw.daily && Array.isArray(raw.daily.time) && raw.daily.time.length > 0;
    if (!ok) throw new Error("The weather service sent an unexpected response.");
    return raw;
  }

  // Raw provider JSON -> one consistent model. Missing values stay null (never invented). Units: °C, km/h, mm, m, hPa.
  function normalize(raw) {
    validate(raw);
    const c = raw.current, hr = raw.hourly, d = raw.daily;
    const at = (arr, i) => (Array.isArray(arr) ? num(arr[i]) : null);
    const str = (arr, i) => (Array.isArray(arr) && typeof arr[i] === "string" ? arr[i] : null);

    let i0 = hr.time.findIndex((t) => t.slice(0, 13) === c.time.slice(0, 13));   // the hour we are in now
    if (i0 < 0) i0 = 0;

    const hourly = [];
    for (let i = i0; i < Math.min(hr.time.length, i0 + 24); i++) {
      hourly.push({ time: hr.time[i], temp: at(hr.temperature_2m, i), pop: at(hr.precipitation_probability, i),
        precip: at(hr.precipitation, i), code: at(hr.weather_code, i), isDay: at(hr.is_day, i) });
    }
    const daily = d.time.map((t, i) => ({ date: t, code: at(d.weather_code, i), max: at(d.temperature_2m_max, i),
      min: at(d.temperature_2m_min, i), sunrise: str(d.sunrise, i), sunset: str(d.sunset, i), uvMax: at(d.uv_index_max, i),
      popMax: at(d.precipitation_probability_max, i), precipSum: at(d.precipitation_sum, i) }));

    const rainValues = hourly.map((x) => x.precip).filter((v) => v != null);
    return {
      timezone: typeof raw.timezone === "string" ? raw.timezone : null,
      current: { time: c.time, temp: num(c.temperature_2m), feels: num(c.apparent_temperature), humidity: num(c.relative_humidity_2m),
        isDay: num(c.is_day), precip: num(c.precipitation), code: num(c.weather_code), pressure: num(c.pressure_msl),
        windSpeed: num(c.wind_speed_10m), windDir: num(c.wind_direction_10m) },
      now: { visibilityM: at(hr.visibility, i0), uv: at(hr.uv_index, i0), pop: at(hr.precipitation_probability, i0) },
      hourly, daily,
      rain24: rainValues.length ? rainValues.reduce((a, b) => a + b, 0) : null,
    };
  }

  // ---- unit conversion and formatting ----
  const toTemp = (c, unit) => (c == null ? null : unit === "f" ? (c * 9) / 5 + 32 : c);
  const toWind = (kmh, unit) => (kmh == null ? null : { kmh: kmh, mph: kmh * 0.621371, ms: kmh / 3.6, kn: kmh * 0.539957 }[unit]);
  const WIND_LABEL = { kmh: "km/h", mph: "mph", ms: "m/s", kn: "kn" };
  function fmt(v, dec) {
    if (v == null) return "—";
    const s = v.toFixed(dec || 0);
    return Number(s) === 0 ? (0).toFixed(dec || 0) : s;   // avoid "-0"
  }
  const compass = (deg) => {
    if (deg == null) return "—";
    const pts = ["N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"];
    return pts[Math.round(((((deg % 360) + 360) % 360) / 22.5)) % 16];
  };
  function hhmm(iso, clock) {
    const m = /T(\d{2}):(\d{2})/.exec(iso || "");
    if (!m) return "—";
    if (clock !== "12") return m[1] + ":" + m[2];
    let hour = Number(m[1]);
    const suffix = hour >= 12 ? "PM" : "AM";
    hour = hour % 12 || 12;
    return hour + ":" + m[2] + " " + suffix;
  }
  function hourLabel(iso, clock) {
    const m = /T(\d{2})/.exec(iso || "");
    if (!m) return "—";
    if (clock !== "12") return m[1] + ":00";
    const hour = Number(m[1]);
    return (hour % 12 || 12) + " " + (hour >= 12 ? "PM" : "AM");
  }
  function dayLabel(dateStr, index) {
    if (index === 0) return "Today";
    return new Date(dateStr + "T00:00:00Z").toLocaleDateString(undefined, { weekday: "short", timeZone: "UTC" });
  }
  function uvLabel(v) {
    if (v == null) return "—";
    return fmt(v, 0) + " (" + (v < 3 ? "Low" : v < 6 ? "Moderate" : v < 8 ? "High" : v < 11 ? "Very high" : "Extreme") + ")";
  }
  function agoText(ms) {
    const min = Math.floor(ms / 60000);
    if (min < 1) return "just now";
    if (min < 60) return min + " min ago";
    return Math.floor(min / 60) + " h " + (min % 60) + " min ago";
  }
  const placeName = (p) => [p.name, p.admin1, p.country].filter(Boolean).join(", ");

  async function fetchJson(url, opts) {
    const timeoutMs = (opts && opts.timeoutMs) || 12000;
    const retries = opts && opts.retries != null ? opts.retries : 1;   // at most 2 attempts in total
    let lastErr;
    for (let attempt = 0; attempt <= retries; attempt++) {
      const ctl = new AbortController();
      const timer = setTimeout(() => ctl.abort(), timeoutMs);
      try {
        const res = await fetch(url, { signal: ctl.signal });
        clearTimeout(timer);
        if (res.status === 429) { const e = new Error("The weather service is busy. Try again in a minute."); e.noRetry = true; throw e; }
        if (!res.ok) throw new Error("Weather service error (" + res.status + ").");
        return await res.json();
      } catch (e) {
        clearTimeout(timer);
        lastErr = e;
        if (e.noRetry) break;
        if (attempt < retries) await new Promise((r) => setTimeout(r, 800));
      }
    }
    if (lastErr.name === "AbortError") throw new Error("The weather service took too long to respond.");
    if (lastErr instanceof TypeError) throw new Error("No internet connection.");
    throw lastErr;
  }

  return { describe, forecastUrl, geocodeUrl, validate, normalize, toTemp, toWind, WIND_LABEL, fmt, compass, hhmm, hourLabel,
    dayLabel, uvLabel, agoText, placeName, fetchJson };
})();

if (typeof module !== "undefined") module.exports = WeatherCore;   // for tests/weather_test.js

/* ------------------------------------------------------------------ UI ------------------------------------------------------------------ */
const WeatherUI = (() => {
  const W = WeatherCore;
  const CACHE_MAX_AGE_MS = 10 * 60 * 1000;   // fresh enough: don't hit the network again
  const MAX_RECENTS = 6;
  let place = null;          // {name, admin1, country, lat, lon, gps}
  let recents = [];
  let model = null, fetchedAt = 0, fromCache = false, loadedOnce = false, busy = false, searchTimer = null, searchSeq = 0;

  const root = () => $("#view-weather");

  function init() {
    $("#wx-search").addEventListener("input", onSearchInput);
    $("#wx-locate").addEventListener("click", useMyLocation);
    $("#wx-refresh").addEventListener("click", () => load(true));
    Settings.onChange(() => { if (model) render(); });
  }

  // Called each time the Weather tab opens.
  async function show() {
    if (!loadedOnce) {
      const saved = await Backend.getJson("cities", {});
      place = saved.current || null;
      recents = Array.isArray(saved.recents) ? saved.recents : [];
      renderRecents();
      if (!place) place = await findDefaultPlace();
      loadedOnce = true;
      if (place) await load(false);
      else showMessage("Search for a city to see its weather.");
    } else if (place && Date.now() - fetchedAt > CACHE_MAX_AGE_MS && !busy) {
      load(false);
    }
  }

  async function findDefaultPlace() {   // first run only: the project area (Lipa City); user can change it any time
    try {
      const r = await W.fetchJson(W.geocodeUrl("Lipa City"), { retries: 0 });
      const list = Array.isArray(r.results) ? r.results : [];
      const hit = list.find((x) => x.country_code === "PH") || list[0];
      return hit ? { name: hit.name, admin1: hit.admin1, country: hit.country, lat: hit.latitude, lon: hit.longitude } : null;
    } catch (e) { return null; }
  }

  function persist() { return Backend.setJson("cities", { current: place, recents }); }

  function setPlace(p, remember) {
    place = p;
    if (remember && !p.gps) {
      recents = [p].concat(recents.filter((r) => !(Math.abs(r.lat - p.lat) < 0.01 && Math.abs(r.lon - p.lon) < 0.01))).slice(0, MAX_RECENTS);
    }
    persist();
    renderRecents();
    $("#wx-results").hidden = true;
    $("#wx-search").value = "";
    return load(true);
  }

  function renderRecents() {
    const box = $("#wx-recents");
    setChildren(box, recents.map((r) => h("button", { class: "chip-btn", type: "button", onclick: () => setPlace(r, true) }, r.name)));
    box.hidden = recents.length === 0;
  }

  /* ---- search / geocoding ---- */
  function onSearchInput(e) {
    clearTimeout(searchTimer);
    const q = e.target.value.trim();
    const box = $("#wx-results");
    if (q.length < 2) { box.hidden = true; return; }
    searchTimer = setTimeout(() => runSearch(q), 350);   // wait for the user to stop typing
  }
  async function runSearch(q) {
    const mySeq = ++searchSeq;
    const box = $("#wx-results");
    box.hidden = false;
    setChildren(box, h("p", { class: "muted pad" }, "Searching…"));
    try {
      const r = await W.fetchJson(W.geocodeUrl(q), { retries: 0 });
      if (mySeq !== searchSeq) return;   // an older search finished late
      const list = Array.isArray(r.results) ? r.results : [];
      if (!list.length) { setChildren(box, h("p", { class: "muted pad" }, "No city found for “" + q + "”.")); return; }
      setChildren(box, list.map((x) => h("button", { class: "result", type: "button",
        onclick: () => setPlace({ name: x.name, admin1: x.admin1, country: x.country, lat: x.latitude, lon: x.longitude }, true) },
        h("strong", null, x.name), h("span", { class: "muted" }, [x.admin1, x.country].filter(Boolean).join(", ")))));
    } catch (err) {
      if (mySeq === searchSeq) setChildren(box, h("p", { class: "error-text pad" }, err.message));
    }
  }

  /* ---- device location (asked only when the user taps the button) ---- */
  async function useMyLocation() {
    const btn = $("#wx-locate");
    btn.disabled = true;
    const old = btn.textContent;
    btn.textContent = "Locating…";
    try {
      const r = await Backend.call("locate");
      if (r.ok) {
        await setPlace({ name: "My location", admin1: r.lat.toFixed(2) + ", " + r.lon.toFixed(2), lat: r.lat, lon: r.lon, gps: true }, false);
      } else {
        showBanner(r.message || "Could not get your location.", "warn");
      }
    } catch (e) {
      showBanner(e.message, "warn");
    } finally {
      btn.disabled = false;
      btn.textContent = old;
    }
  }

  /* ---- loading, caching ---- */
  async function load(force) {
    if (!place || busy) return;
    busy = true;
    $("#wx-refresh").disabled = true;
    const cache = await Backend.getJson("weatherCache", null);
    const sameSpot = cache && Math.abs(cache.lat - place.lat) < 0.01 && Math.abs(cache.lon - place.lon) < 0.01;
    try {
      if (!force && sameSpot && Date.now() - cache.fetchedAt < CACHE_MAX_AGE_MS) {
        model = W.normalize(cache.raw); fetchedAt = cache.fetchedAt; fromCache = false;   // still fresh: no network needed
      } else {
        if (!model || !sameSpot) showLoading();
        const raw = await W.fetchJson(W.forecastUrl(place.lat, place.lon), { retries: 1 });
        model = W.normalize(raw);
        fetchedAt = Date.now(); fromCache = false;
        Backend.setJson("weatherCache", { lat: place.lat, lon: place.lon, fetchedAt, raw });
      }
      render();
    } catch (err) {
      if (sameSpot) {   // network failed: show the old data, but say clearly that it is old
        try {
          model = W.normalize(cache.raw); fetchedAt = cache.fetchedAt; fromCache = true;
          render();
          showBanner(err.message + " Showing saved data from " + W.agoText(Date.now() - fetchedAt) + ".", "warn");
        } catch (e2) { showError(err.message); }
      } else {
        showError(err.message);
      }
    } finally {
      busy = false;
      $("#wx-refresh").disabled = false;
    }
  }

  /* ---- rendering ---- */
  function showLoading() { setChildren($("#wx-body"), h("div", { class: "skeleton", role: "status" }, "Loading weather…")); showBanner(""); }
  function showMessage(text) { setChildren($("#wx-body"), h("p", { class: "empty" }, text)); }
  function showError(text) {
    setChildren($("#wx-body"), h("div", { class: "empty" }, h("p", { class: "error-text" }, text),
      h("button", { class: "btn", type: "button", onclick: () => load(true) }, "Try again")));
  }
  function showBanner(text, kind) {
    const b = $("#wx-banner");
    b.hidden = !text;
    b.className = "banner " + (kind || "");
    b.textContent = text || "";
  }

  function stat(label, value) { return h("div", { class: "stat" }, h("span", { class: "muted" }, label), h("strong", null, value)); }

  function render() {
    const s = Settings.get(), u = s.tempUnit, wu = s.windUnit;
    const t = (c) => W.fmt(W.toTemp(c, u), 0) + "°";
    const c = model.current, today = model.daily[0];
    const cond = W.describe(c.code, c.isDay);
    const imperial = u === "f";
    const vis = model.now.visibilityM == null ? null : model.now.visibilityM / (imperial ? 1609.34 : 1000);

    showBanner(fromCache ? "Offline: showing saved data." : "", fromCache ? "warn" : "");

    const hero = h("section", { class: "wx-hero", "data-night": c.isDay === 0 ? "1" : "0" },
      h("p", { class: "wx-place" }, place.gps ? "📍 My location (" + place.admin1 + ")" : W.placeName(place)),
      h("p", { class: "wx-time muted" }, "Local time " + W.hhmm(c.time, s.clock) + (model.timezone ? " · " + model.timezone : "")),
      h("div", { class: "wx-main" }, h("span", { class: "wx-icon", "aria-hidden": "true" }, cond.icon), h("span", { class: "wx-temp" }, t(c.temp))),
      h("p", { class: "wx-cond" }, cond.text),
      h("p", { class: "muted" }, "Feels like " + t(c.feels) + " · H " + t(today.max) + " · L " + t(today.min)));

    const details = h("details", { class: "card" }, h("summary", null, "Details"),
      h("div", { class: "stats" },
        stat("Humidity", c.humidity == null ? "—" : W.fmt(c.humidity) + "%"),
        stat("Wind", c.windSpeed == null ? "—" : W.fmt(W.toWind(c.windSpeed, wu)) + " " + W.WIND_LABEL[wu] + " " + W.compass(c.windDir)),
        stat("Chance of rain", model.now.pop == null ? "—" : W.fmt(model.now.pop) + "%"),
        stat("Rain next 24 h", model.rain24 == null ? "—" : W.fmt(model.rain24, 1) + " mm"),
        stat("Pressure", c.pressure == null ? "—" : W.fmt(c.pressure) + " hPa"),
        stat("Visibility", vis == null ? "—" : W.fmt(vis, 1) + (imperial ? " mi" : " km")),
        stat("UV index", W.uvLabel(model.now.uv)),
        stat("Sunrise", W.hhmm(today.sunrise, s.clock)),
        stat("Sunset", W.hhmm(today.sunset, s.clock))));
    details.open = true;

    const hourly = h("section", { class: "card" }, h("h3", null, "Next 24 hours"),
      h("div", { class: "hourly", role: "list" }, model.hourly.map((x, i) => h("div", { class: "hour", role: "listitem" },
        h("span", { class: "muted" }, i === 0 ? "Now" : W.hourLabel(x.time, s.clock)),
        h("span", { class: "hour-icon", "aria-hidden": "true" }, W.describe(x.code, x.isDay).icon),
        h("strong", null, t(x.temp)),
        h("span", { class: "muted small" }, x.pop == null ? "" : W.fmt(x.pop) + "%")))));

    const daily = h("section", { class: "card" }, h("h3", null, model.daily.length + "-day forecast"),
      model.daily.map((d, i) => h("div", { class: "day" },
        h("span", { class: "day-name" }, W.dayLabel(d.date, i)),
        h("span", { class: "hour-icon", "aria-hidden": "true" }, W.describe(d.code, 1).icon),
        h("span", { class: "muted small day-pop" }, d.popMax == null ? "" : "💧" + W.fmt(d.popMax) + "%"),
        h("span", { class: "day-temps" }, h("strong", null, t(d.max)), " ", h("span", { class: "muted" }, t(d.min))))));

    const foot = h("p", { class: "muted small foot" },
      "Updated " + new Date(fetchedAt).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", hour12: s.clock === "12" }) +
      " (" + W.agoText(Date.now() - fetchedAt) + "). " +
      "Forecast data are model forecasts, not measurements. Weather data by Open-Meteo.com (CC BY 4.0). " +
      "This source gives no severe-weather alerts; check your national weather agency for official warnings.");

    setChildren($("#wx-body"), hero, details, hourly, daily, foot);
  }

  return { init, show, rerender: () => { if (model) render(); } };
})();
