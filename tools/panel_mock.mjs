#!/usr/bin/env node
/**
 * ARMOR-SOLAR - a stand-in for a solar node, to work on the web panel without a board.
 * Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
 *
 *   node tools/panel_mock.mjs [port]          (default 8090)     open http://127.0.0.1:8090/
 *
 * It serves the panel from panel/ (text.js is joined in front of app.js, as tools/pack_panel.py does) and answers /api/v1 like the firmware
 * does, from memory: a fresh mock is in set-up (code TESTCODE); `--user admin:adminpass123` starts it with an administrator. It is a development
 * tool: it checks far less than the firmware, and its numbers are made up.
 */
import { createServer } from "node:http";
import { readFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const panel = path.join(here, "..", "panel");
const port = Number(process.argv.find(a => /^\d+$/.test(a)) ?? 8090);
const seeded = process.argv.includes("--user") ? process.argv[process.argv.indexOf("--user") + 1] : "";

const users = new Map();
if (seeded) { const [name, password] = seeded.split(":"); users.set(name, { password, role: "admin" }); }
const sessions = new Map();
const started = Date.now();
let rebootAt = 0;
let logText = "I (1200) armor-node: A.R.M.O.R. node armor-a1b2c3, firmware 0.2.3\nI (1500) armor-net: link up\nI (2600) armor-net: address 192.168.0.181, gateway 192.168.0.1, netmask 255.255.255.0 (wire)\nW (9000) armor-radar: radar 2: no data (RX not connected, no power or the wrong pin) (0 bytes, 0 frames, 0 bad)\n";

const config = {
  v: 1, node: { id: "solar-a1b2c3", name: "Solar house", hostname: "" },
  ap: { enabled: true, ssid: "ARMOR-SOLAR-A1B2C3", security: "wpa2", password_set: true, channel: 0, hidden: false, max_clients: 8, tx_power_dbm: 15, bandwidth_mhz: 20, country: "ES" },
  sta: { enabled: true, ssid: "HomeRouter", password_set: true },
  mqtt: { enabled: true, uri: "mqtt://192.168.0.180:18883", username: "solar-node-solar-a1b2c3", password_set: true, heartbeat_s: 10, ntp: "pool.ntp.org" },
  ports: [
    { enabled: true, kind: "voltronic", name: "axpert-1", baud: 0, rx: 16, tx: 15, de: -1, poll_s: 0, modules: 0 },
    { enabled: true, kind: "pylontech", name: "us3000-1", baud: 0, rx: 18, tx: 17, de: 8, poll_s: 0, modules: 0 },
    { enabled: false, kind: "raw", name: "port3", baud: 0, rx: 4, tx: 5, de: 6, poll_s: 0, modules: 0 },
    { enabled: true, kind: "voltronic", name: "axpert-2", baud: 2400, rx: 9, tx: 10, de: -1, poll_s: 0, modules: 0 },
    { enabled: true, kind: "raw", name: "monitor-5", baud: 9600, rx: 11, tx: -1, de: -1, poll_s: 0, modules: 0 },
    { enabled: true, kind: "ant", name: "ant-6", baud: 0, rx: 13, tx: 14, de: -1, poll_s: 0, modules: 0 },
    { enabled: false, kind: "voltronic", name: "port7", baud: 0, rx: 21, tx: 47, de: -1, poll_s: 0, modules: 0 },
    { enabled: false, kind: "voltronic", name: "port8", baud: 0, rx: 38, tx: 39, de: -1, poll_s: 0, modules: 0 },
    { enabled: false, kind: "voltronic", name: "port9", baud: 0, rx: 40, tx: 41, de: -1, poll_s: 0, modules: 0 },
    { enabled: false, kind: "voltronic", name: "port10", baud: 0, rx: 42, tx: 1, de: -1, poll_s: 0, modules: 0 },
  ],
  web: { mode: "both" },
  ui: { language: "en" },
};

const catalog = [];
for (let gpio = 0; gpio <= 48; ++gpio) {
  if (gpio >= 22 && gpio <= 25) continue;
  let use = "free", reason = "", note = "", header = true;
  if (gpio >= 26 && gpio <= 32) { use = "reserved"; reason = "flash"; header = false; }
  else if (gpio >= 33 && gpio <= 37) { use = "reserved"; reason = "psram"; header = gpio >= 35; }
  else if (gpio === 19 || gpio === 20) { use = "reserved"; reason = "usb"; }
  else if (gpio === 0) { use = "caution"; note = "boot"; }
  else if ([3, 45, 46].includes(gpio)) { use = "caution"; note = "strapping"; }
  else if (gpio === 43 || gpio === 44) { use = "caution"; note = "usb_serial"; }
  else if (gpio === 48) { use = "caution"; note = "led"; }
  catalog.push({ gpio, use, reason, note, on_header: header });
}
const cells = (base, salt) => Array.from({ length: 15 }, (_, i) => Number((base + ((i * 7 + salt) % 5) * 0.0015 + (i === 9 ? 0.02 : 0)).toFixed(3)));
const readings = () => [
  { kind: "inverter", node_id: config.node.id, device: "axpert-1", timestamp_ms: Date.now(), mode: "line", grid_v: 231.4, grid_hz: 50, out_v: 230, out_hz: 50, out_va: 1450, out_w: 1180, load_percent: 24, battery_v: 52.4, battery_a: 14, battery_percent: 81, pv_v: 180, pv_a: 10.9, pv_w: 1960, heatsink_c: 41, ac_charging: false, pv_charging: true, load_on: true, warnings: [] },
  { kind: "battery", node_id: config.node.id, device: "us3000-1", timestamp_ms: Date.now(), modules: 2, model: "US3000C", state: "charging", voltage_v: 51.3, current_a: 19.2, temperature_min_c: 21, temperature_max_c: 23, cell_min_v: 3.399, cell_max_v: 3.444, soc_percent: 79, alarm: false, capacity_ah: 116.9, full_capacity_ah: 148, energy_kwh: 6, cycles: 181,
    stack: [{ n: 1, present: true, voltage_v: 51.3, current_a: 9.6, temperature_c: 22, soc_percent: 79, state: "Charge", capacity_ah: 58.5, full_capacity_ah: 74, cycles: 181, cells_v: cells(3.4, 1) }, { n: 2, present: true, voltage_v: 51.3, current_a: 9.6, temperature_c: 22.5, soc_percent: 79, state: "Charge", capacity_ah: 58.4, full_capacity_ah: 74, cycles: 180, cells_v: cells(3.402, 2) }, { n: 3, present: false }] },
  { kind: "battery", node_id: config.node.id, device: "ant-6", timestamp_ms: Date.now(), modules: 1, model: "ANT-BMS", state: "idle", voltage_v: 52.84, current_a: 0.3, temperature_min_c: 1, temperature_max_c: 7, cell_min_v: 3.3, cell_max_v: 3.305, soc_percent: 91, alarm: false, capacity_ah: 252.6, full_capacity_ah: 280, energy_kwh: 13.35, cycles: 17,
    stack: [{ n: 1, present: true, voltage_v: 52.84, current_a: 0.3, temperature_c: 3, soc_percent: 91, state: "Idle", capacity_ah: 252.6, full_capacity_ah: 280, cycles: 17, cells_v: cells(3.3, 3).slice(0, 16), temperatures_c: [1, 2, 2, 7] }] },
];
const ports = () => config.ports.map((p, i) => {
  const state = !p.enabled ? "disabled" : i === 3 ? "silent" : i === 4 ? "listening" : "reporting";
  const on = p.enabled && state !== "silent";
  return { port: i + 1, enabled: p.enabled, soft: i >= 3, kind: p.kind, name: p.name, baud: p.baud || (p.kind === "pylontech" ? 115200 : p.kind === "ant" ? 19200 : p.kind === "raw" ? 9600 : 2400), rx: p.rx, tx: p.tx, de: p.de, poll_s: 5, state, error: i === 3 ? "timeout" : "",
    bytes_rx: on ? 48000 + Math.floor((Date.now() - started) / 100) : 0, bytes_tx: p.enabled && p.kind !== "raw" ? 900 : 0, replies_ok: on && p.kind !== "raw" ? 410 : 0, replies_bad: 1, timeouts: i === 3 ? 42 : 2, readings: on && p.kind !== "raw" ? 136 : 0, overruns: 0, framing_errors: 0, ...(p.kind === "ant" && on ? { detail: "new charge=1 discharge=1 balancer=0 cells=16" } : {}) };
});
const rawText = "48 65 6C 6C 6F 2C 20 69 6E 76 65 72 74 65 72 0D  |Hello, inverter.|\n0A                                               |.|\n";

const json = (response, code, body, headers = {}) => { response.writeHead(code, { "Content-Type": "application/json", "Cache-Control": "no-store", ...headers }); response.end(JSON.stringify(body)); };
const bodyOf = request => new Promise(resolve => { const chunks = []; request.on("data", c => chunks.push(c)); request.on("end", () => resolve(Buffer.concat(chunks))); });
const tokenOf = request => /armor_session=([0-9a-f]+)/.exec(request.headers.cookie ?? "")?.[1] ?? "";

function status() {
  return {
    node_id: config.node.id, name: config.node.name, version: "0.0.3", uptime_s: Math.floor((Date.now() - started) / 1000) + 5400, reset_reason: "power_on", heap_free: 182000, heap_min: 151000, psram_free: 7400000, partition: "ota_0",
    network: { layout: "wifi-station+ap", link_up: true, has_ip: true, ip: "192.168.0.181", netmask: "255.255.255.0", gateway: "192.168.0.1", dns: "192.168.0.1", mac: "34:85:18:a1:b2:c3",
      ap_active: config.ap.enabled, ap_setup: users.size === 0, ap_ssid: users.size === 0 ? "ARMOR-SETUP-A1B2C3" : config.ap.ssid, ap_channel: 6, ap_clients: 1, sta_connected: true, sta_ssid: config.sta.ssid, sta_rssi: -52 },
    mqtt: { enabled: config.mqtt.enabled, connected: true, clock_set: true, published: 400 + Math.floor((Date.now() - started) / 5000), dropped: 0 },
    web: { mode: config.web.mode, https: config.web.mode !== "http", cert_sha256: "a3f1c07d9e2b4c58a7106f3de9b2c4815d6e7f80a1b2c3d4e5f60718293a4b5c" },
    ports: ports(),
  };
}

const problems = doc => {
  const list = [];
  if (doc.ap?.enabled && !String(doc.ap.ssid ?? "").trim()) list.push({ path: "ap.ssid", code: "required" });
  if (doc.ap?.enabled && doc.ap.security !== "open" && doc.ap.password !== undefined && doc.ap.password.length > 0 && doc.ap.password.length < 8) list.push({ path: "ap.password", code: "invalid_key" });
  if (doc.mqtt?.enabled && !/^mqtts?:\/\/.+/.test(doc.mqtt.uri ?? "")) list.push({ path: "mqtt.uri", code: "invalid" });
  (doc.ports ?? []).forEach((p, i) => { if (i >= 3 && p.enabled && p.baud > 19200) list.push({ path: `ports.${i}.baud`, code: "too_fast_for_emulated" }); });
  return list;
};

const server = createServer(async (request, response) => {
  const url = new URL(request.url, "http://x");
  if (request.method === "GET" && ["/", "/index.html"].includes(url.pathname)) { response.writeHead(200, { "Content-Type": "text/html" }); return response.end(readFileSync(path.join(panel, "index.html"))); }
  if (request.method === "GET" && url.pathname === "/style.css") { response.writeHead(200, { "Content-Type": "text/css" }); return response.end(readFileSync(path.join(panel, "style.css"))); }
  if (request.method === "GET" && url.pathname === "/app.js") { response.writeHead(200, { "Content-Type": "text/javascript" }); return response.end(readFileSync(path.join(panel, "text.js"), "utf8") + "\n" + readFileSync(path.join(panel, "app.js"), "utf8")); }
  if (!url.pathname.startsWith("/api/v1/")) return json(response, 404, { error: "not_found" });

  const route = url.pathname.slice(8), method = request.method;
  const raw = await bodyOf(request);
  let body = {};
  if (raw.length && !url.pathname.endsWith("/ota")) { try { body = JSON.parse(raw.toString("utf8")); } catch { return json(response, 400, { error: "not_json" }); } }
  const session = sessions.get(tokenOf(request));
  const setup = users.size === 0;

  if (method === "GET" && route === "session") return json(response, 200, { setup, authenticated: !!session, user: session?.user ?? "", role: session?.role ?? "", node_id: config.node.id, language: config.ui.language, version: "0.2.3", setup_ssid: setup ? "ARMOR-SETUP-A1B2C3" : "", mac: "34:85:18:a1:b2:c3" });
  if (method === "POST" && route === "setup") {
    if (!setup) return json(response, 403, { error: "forbidden" });
    if (body.code !== "TESTCODE") return json(response, 403, { error: "wrong_code" });
    if (!/^[a-z0-9_.-]{3,32}$/.test(body.user ?? "")) return json(response, 422, { error: "invalid_name" });
    if ((body.password ?? "").length < 8) return json(response, 422, { error: "weak_password" });
    users.set(body.user, { password: body.password, role: "admin" });
    return json(response, 200, { ok: true, restart_required: true });
  }
  if (method === "POST" && route === "login") {
    const user = users.get(body.user);
    if (!user || user.password !== body.password) return json(response, 401, { error: "wrong_credentials" });
    const token = [...Array(48)].map(() => "0123456789abcdef"[Math.floor(Math.random() * 16)]).join("");
    sessions.set(token, { user: body.user, role: user.role });
    return json(response, 200, { ok: true, restart_required: false }, { "Set-Cookie": `armor_session=${token}; Path=/; HttpOnly; SameSite=Strict` });
  }
  if (method === "POST" && route === "logout") { sessions.delete(tokenOf(request)); return json(response, 200, { ok: true }); }
  if (setup) return json(response, 403, { error: "setup_required" });
  if (!session) return json(response, 401, { error: "unauthorized" });
  if (method !== "GET" && request.headers["x-requested-with"] !== "armor") return json(response, 403, { error: "forbidden" });
  const admin = session.role === "admin";
  const needAdmin = () => { if (!admin) { json(response, 403, { error: "forbidden" }); return false; } return true; };

  if (method === "GET" && route === "status") return json(response, 200, status());
  if (method === "GET" && route === "wifi/scan") { if (!needAdmin()) return; return json(response, 200, { networks: [{ ssid: "HomeRouter", rssi: -48, channel: 6, security: "wpa2" }, { ssid: "Neighbour", rssi: -71, channel: 11, security: "wpa2wpa3" }, { ssid: "CafeOpen", rssi: -80, channel: 1, security: "open" }] }); }
  if (method === "GET" && route === "config") return json(response, 200, { config, channel_auto: 6, firmware: "0.2.3" });
  if (method === "PUT" && route === "config") {
    if (!needAdmin()) return;
    const list = problems(body);
    if (list.length) return json(response, 422, { error: "invalid", problems: list });
    for (const key of Object.keys(body)) {
      const value = body[key];
      if (value && typeof value === "object" && !Array.isArray(value) && config[key] && typeof config[key] === "object") Object.assign(config[key], value);
      else config[key] = value;
    }
    for (const section of [config.ap, config.sta, config.mqtt]) if (typeof section.password === "string" && section.password) { section.password_set = true; delete section.password; } else delete section.password;
    return json(response, 200, { ok: true, restart_required: true });
  }
  if (method === "GET" && route === "ports") return json(response, 200, { ports: ports(), catalog });
  if (method === "GET" && route === "readings") return json(response, 200, readings());
  if (method === "GET" && route === "ports/raw") return json(response, 200, { port: Number(url.searchParams.get("port")), text: rawText });
  if (method === "GET" && route === "users") { if (!needAdmin()) return; return json(response, 200, [...users].map(([name, u]) => ({ name, role: u.role }))); }
  if (method === "POST" && route === "users") {
    if (!needAdmin()) return;
    if (!/^[a-z0-9_.-]{3,32}$/.test(body.name ?? "")) return json(response, 422, { error: "invalid_name" });
    if ((body.password ?? "").length < 8) return json(response, 422, { error: "weak_password" });
    if (users.has(body.name)) return json(response, 409, { error: "exists" });
    users.set(body.name, { password: body.password, role: body.role === "admin" ? "admin" : "viewer" });
    return json(response, 200, { ok: true, restart_required: false });
  }
  if (method === "PUT" && route.startsWith("users/")) {
    if (!needAdmin()) return;
    const user = users.get(route.slice(6));
    if (!user) return json(response, 404, { error: "not_found" });
    if (body.role) user.role = body.role;
    if (body.password) user.password = body.password;
    return json(response, 200, { ok: true, restart_required: false });
  }
  if (method === "DELETE" && route.startsWith("users/")) {
    if (!needAdmin()) return;
    const name = route.slice(6);
    if (users.get(name)?.role === "admin" && [...users.values()].filter(u => u.role === "admin").length === 1) return json(response, 409, { error: "last_admin" });
    users.delete(name);
    return json(response, users.has(name) ? 500 : 200, { ok: true, restart_required: false });
  }
  if (method === "PUT" && route === "account") {
    const user = users.get(session.user);
    if (user.password !== body.current) return json(response, 403, { error: "wrong_password" });
    if ((body.password ?? "").length < 8) return json(response, 422, { error: "weak_password" });
    user.password = body.password;
    return json(response, 200, { ok: true, restart_required: false });
  }
  if (method === "GET" && route === "log") return json(response, 200, { next: logText.length, text: logText.slice(Number(url.searchParams.get("from") ?? 0)) });
  if (method === "POST" && route === "reboot") { if (!needAdmin()) return; rebootAt = Date.now(); return json(response, 200, { ok: true, restart_required: false }); }
  if (method === "POST" && route === "factory-reset") { if (!needAdmin()) return; if (body.confirm !== "RESET") return json(response, 422, { error: "confirm_required" }); users.clear(); return json(response, 200, { ok: true, restart_required: true }); }
  if (method === "POST" && route === "ota") { if (!needAdmin()) return; return json(response, 200, { ok: true, restart_required: true, version: "0.2.4", bytes: raw.length, sha256: "0".repeat(64) }); }
  return json(response, 404, { error: "not_found" });
});
server.listen(port, "127.0.0.1", () => console.log(`ARMOR node panel mock on http://127.0.0.1:${port}/ (${users.size ? "signed-in users: " + [...users.keys()].join(", ") : "set-up code TESTCODE"})`));
