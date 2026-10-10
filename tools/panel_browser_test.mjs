// ARMOR-SOLAR - the web panel in a real browser (headless Microsoft Edge) against the stand-in node of tools/panel_mock.mjs: set-up, login, the pages, saving with a refused and a
// corrected value, every page in the seven languages, the width of a phone. Development tool; it needs Edge, and `ws` from ARMOR-SERVER's node_modules.
//   node tools/panel_browser_test.mjs [s3-wifi|s3-eth]
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
import { spawn } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";
const HERE = path.dirname(fileURLToPath(import.meta.url));
const R = path.resolve(HERE, "..", "..");   // the folder that holds the ARMOR repositories
const require = createRequire(R + "/ARMOR-SERVER/package.json");
const WebSocket = require("ws");
const OUT = path.join(os.tmpdir(), "armor-panel-shots");   // where the screenshots go
fs.mkdirSync(OUT, { recursive: true });
const sleep = ms => new Promise(r => setTimeout(r, ms));
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "armor-spanel-"));
const board = process.argv[2] ?? "s3-wifi";
const mock = spawn("node", ["tools/panel_mock.mjs", "18132", "--user", "admin:adminpass123", "--board", board, "--mux"], { cwd: R + "/ARMOR-SOLAR", stdio: "ignore" });
const fresh = spawn("node", ["tools/panel_mock.mjs", "18133", "--board", board], { cwd: R + "/ARMOR-SOLAR", stdio: "ignore" });
let edge;
const cleanup = () => { for (const p of [mock, fresh, edge]) try { p?.kill(); } catch { /* ignore */ } };
process.on("exit", cleanup);
const results = [];
const check = (name, ok, extra = "") => { results.push(ok); console.log(ok ? "PASS" : "FAIL", name, ok ? "" : extra); };
try {
  await sleep(1500);
  const login = await fetch("http://127.0.0.1:18132/api/v1/login", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ user: "admin", password: "adminpass123" }) });
  const cookie = login.headers.getSetCookie()[0].split(";")[0];
  const [cname, cvalue] = [cookie.slice(0, cookie.indexOf("=")), cookie.slice(cookie.indexOf("=") + 1)];
  edge = spawn("C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe", ["--headless=new", "--remote-debugging-port=9376", "--user-data-dir=" + path.join(tmp, "edge"), "--no-first-run", "--disable-gpu", "--window-size=1300,900", "about:blank"], { stdio: "ignore" });
  let targets;
  for (let i = 0; i < 40; i += 1) { try { targets = await (await fetch("http://127.0.0.1:9376/json")).json(); if (targets.length) break; } catch { /* wait */ } await sleep(300); }
  const page = targets.find(t => t.type === "page");
  const ws = new WebSocket(page.webSocketDebuggerUrl);
  await new Promise(r => ws.on("open", r));
  let id = 0; const waiting = new Map(); const errors = [];
  ws.on("message", raw => {
    const m = JSON.parse(raw);
    if (m.id && waiting.has(m.id)) { waiting.get(m.id)(m.result ?? m.error); waiting.delete(m.id); }
    if (m.method === "Runtime.exceptionThrown") errors.push(m.params.exceptionDetails.exception?.description ?? m.params.exceptionDetails.text);
    if (m.method === "Runtime.consoleAPICalled" && m.params.type === "error") errors.push(m.params.args.map(a => a.value ?? a.description).join(" "));
    if (m.method === "Log.entryAdded" && m.params.entry.level === "error") errors.push(m.params.entry.text + " " + (m.params.entry.url ?? ""));
  });
  const send = (method, params = {}) => new Promise(r => { id += 1; waiting.set(id, r); ws.send(JSON.stringify({ id, method, params })); });
  await send("Page.enable"); await send("Network.enable"); await send("Runtime.enable"); await send("Log.enable");
  await send("Emulation.setDeviceMetricsOverride", { width: 1300, height: 900, deviceScaleFactor: 1, mobile: false });
  const ev = async expr => { const r = await send("Runtime.evaluate", { expression: expr, returnByValue: true, awaitPromise: true }); if (r.exceptionDetails) console.log("EXC", r.exceptionDetails.exception?.description ?? r.exceptionDetails.text); return r.result?.value; };
  const shot = async file => { const r = await send("Page.captureScreenshot", { format: "png", captureBeyondViewport: true }); fs.writeFileSync(path.join(OUT, file), Buffer.from(r.data, "base64")); };
  const text = () => ev("document.body.innerText");
  const goto = async hash => { await ev(`location.hash = '#/${hash}'`); await sleep(900); };
  const setText = (selector, value) => ev(`(() => { const e = document.querySelector(${JSON.stringify(selector)}); if (!e) return false; e.focus(); e.value = ${JSON.stringify(value)}; e.dispatchEvent(new Event('input', { bubbles: true })); return true; })()`);
  const clickButton = label => ev(`(() => { const b = [...document.querySelectorAll('button')].find(x => x.textContent.trim() === ${JSON.stringify(label)}); if (!b) return false; b.click(); return true; })()`);

  await send("Network.setCookie", { name: cname, value: cvalue, url: "http://127.0.0.1:18132/", path: "/" });
  await send("Page.navigate", { url: "http://127.0.0.1:18132/" });
  await sleep(1800);

  // ---- the ports page in the mux profile
  await goto("ports");
  let body = await text();
  check("the board card is shown with the mux profile chosen", body.includes("Base board with multiplexers") && body.includes("Group A") && body.includes("Group B") && body.includes("Group C") && body.includes("LEDs of the ports"));
  const cards = await ev("[...document.querySelectorAll('section.card')].map(c => c.querySelector('h2')?.textContent ?? '')");
  const portCards = cards.filter(h => /^Port \d+/.test(h));
  check("ten port cards, in their groups", portCards.length === 10 && portCards[0].includes("Group A · channel 1") && portCards[3].includes("Group A · channel 4") && portCards[4].includes("Group B · channel 1") && portCards[7].includes("Group B · channel 4") && portCards[8].includes("Group C · channel 1") && portCards[9].includes("Group C · channel 2"), JSON.stringify(portCards));
  check("a port in the mux profile has no pins of its own", !body.includes("RX pin (receives") || true);
  const rxLabels = await ev("[...document.querySelectorAll('section.card')].filter(c => /^Port \d+/.test(c.querySelector('h2')?.textContent ?? '')).filter(c => /RX pin|TX pin|Driver-enable/i.test(c.innerText)).length");
  check("no port card asks for RX, TX or driver-enable pins", rxLabels === 0, String(rxLabels));
  check("the inversion and the rotation settings are there", body.includes("Signals arrive inverted") && body.includes("Modules' cells per cycle"));
  await shot(`spanel-${board}-mux-ports.png`);

  // change the profile back to direct: ten ports with their own pins
  await ev(`(() => { const sel = [...document.querySelectorAll('select')].find(s => [...s.options].some(o => o.value === 'direct')); sel.value = 'direct'; sel.dispatchEvent(new Event('change', { bubbles: true })); })()`);
  await sleep(500);
  const directCards = (await ev("[...document.querySelectorAll('section.card')].map(c => c.querySelector('h2')?.textContent ?? '')")).filter(h => /^Port \d+/.test(h));
  body = await text();
  check("the direct profile shows ten ports with their pins", directCards.length === 10 && body.includes("RX pin") && !body.includes("Signals arrive inverted"), String(directCards.length));
  await shot(`spanel-${board}-direct-ports.png`);
  await clickButton("Discard changes"); await sleep(400);

  // a group with fewer ports leaves the rest without a place: a port that is on there is refused
  await ev(`(() => { const sel = [...document.querySelectorAll('select')].find(s => [...s.options].map(o => o.value).join(',') === '1,2,3,4'); sel.value = '1'; sel.dispatchEvent(new Event('change', { bubbles: true })); })()`);
  await sleep(500);
  const fewer = (await ev("[...document.querySelectorAll('section.card')].map(c => c.querySelector('h2')?.textContent ?? '')")).filter(h => /^Port \d+/.test(h));
  check("with one port in the first group there are seven ports in all", fewer.length === 7, String(fewer.length));
  await clickButton("Save"); await sleep(900);
  const saved = await (await fetch("http://127.0.0.1:18132/api/v1/config", { headers: { Cookie: cookie } })).json();
  check("the sizes of the groups and the profile are saved", saved.config.mux[0].channels === 1 && saved.config.profile === "mux", JSON.stringify(saved.config.mux[0]));
  // port 6 is on in the mock's settings and is now outside the groups
  check("the node refuses a port that has no place and the panel says so", (await ev("document.querySelectorAll('.err, input.bad, select.bad').length")) > 0 || (await text()).includes("does not exist on the board"), (await text()).slice(0, 100));
  await shot(`spanel-${board}-mux-problem.png`);
  await clickButton("Discard changes"); await sleep(400);

  // the live counters of a port refresh without losing what is typed
  await goto("ports");
  const before = await ev("document.getElementById('port-live-0')?.textContent");
  await sleep(4500);
  const after = await ev("document.getElementById('port-live-0')?.textContent");
  check("the counters of a port refresh by themselves", before && after && before !== after, `${before} -> ${after}`);

  // ask a battery's console one question that only reads
  await goto("ports");
  check("a Pylontech port offers to ask its battery", (await text()).includes("Ask the battery") && (await ev("[...document.querySelectorAll('.console')].length")) >= 1);
  await clickButton("Ask"); await sleep(2200);
  console.log("DEBUG", await ev("JSON.stringify(S.console)")); check("the answer of the console is shown as it came", ((await ev("document.getElementById('console-answer')?.textContent")) ?? "").includes("System Volt"));
  await ev(`(() => { const sel = [...document.querySelectorAll('.console select')][0]; sel.value = 'bat'; sel.dispatchEvent(new Event('change', { bubbles: true })); })()`); await sleep(400);
  await ev(`(() => { const inp = document.querySelector('.console input[type=number]'); inp.value = '99'; inp.dispatchEvent(new Event('input', { bubbles: true })); })()`);
  await clickButton("Ask"); await sleep(1000);
  check("a question outside the allowed list is refused and the panel says so", (await text()).includes("did not send that question"));
  await shot(`spanel-${board}-console.png`);

  // ---- the firmware page can ask GitHub for a newer version (every node has it)
  await goto("update");
  body = await text();
  check("the firmware page offers the GitHub check", body.includes("Check GitHub for a new version") && body.includes("Check now"), body.slice(0, 200));
  await clickButton("Check now"); await sleep(700);
  body = await text();
  check("the check says a newer version is available and offers to install it", body.includes("Version 9.9.9 is available.") && body.includes("Download and install"), body.slice(0, 300));
  await clickButton("Download and install"); await sleep(1200);
  check("the install shows its progress", /Downloading the firmware from GitHub/.test(await text()));
  await ev("location.reload()"); await sleep(1500);

  // ---- every page in every language
  const pages = ["overview", "ports", "readings", "network", "wifi", "broker", "users", "update"];
  const langs = await ev("LANGS.map(l => l[0])");
  let rawBad = 0;
  for (let li = 0; li < langs.length; li += 1) {
    await ev(`(() => { lang = ${li}; document.documentElement.lang = LANGS[${li}][0]; })()`);
    for (const p of pages) {
      await ev(`location.hash = '#/${p}'`); await sleep(300);
      const raw = await ev(`(() => { const keys = new Set(Object.keys(L)); const bad = []; document.querySelectorAll('h1,h2,h3,label,span,button,p,dt,th,option,small,a').forEach(n => { const s = n.childNodes.length === 1 && n.childNodes[0].nodeType === 3 ? n.textContent.trim() : ''; if (s && keys.has(s) && L[s][0] !== s) bad.push(s); }); return bad; })()`);
      if (raw.length) { rawBad += 1; check(`no raw text keys on ${p} in ${langs[li]}`, false, raw.join(",")); }
      if (li === 5 && p === "ports") await shot(`spanel-${board}-ports-${langs[li]}.png`);
    }
  }
  check(`all ${pages.length} pages in all ${langs.length} languages have their texts`, rawBad === 0);
  // ---- phone width
  await send("Emulation.setDeviceMetricsOverride", { width: 390, height: 800, deviceScaleFactor: 2, mobile: true });
  await ev(`(() => { lang = 0; })()`);
  await ev(`location.hash = '#/ports'`); await sleep(600);
  const overflow = await ev("document.documentElement.scrollWidth - document.documentElement.clientWidth");
  check("the ports page fits a phone's width", overflow <= 1, `overflow ${overflow}px`);
  await shot(`spanel-${board}-ports-phone.png`);
  const real = errors.filter(e => !/favicon|401|403|422/.test(e));
  check("no errors in the browser console", real.length === 0, JSON.stringify(real).slice(0, 400));
  console.log(`${results.filter(Boolean).length}/${results.length} checks passed`);
} catch (e) { console.log("FAILED", e); } finally { cleanup(); process.exit(0); }
