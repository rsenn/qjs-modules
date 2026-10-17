#!/usr/bin/env node
/* describe-browser.js -- runs describe.js inside a headless browser and
 * prints what it printed: Chrome/Chromium over the DevTools protocol, Firefox
 * over WebDriver BiDi.
 *
 *   describe-browser.js [describe.js options] --global [name...]
 *   describe-browser.js [describe.js options] <url> [export...]
 *
 * <url> is a module the page can import (https://..., or a path relative to
 * about:blank's base, so absolute URLs); --global describes properties of
 * the page's globalThis ('navigator', 'Intl', 'CSS'). Without a name, window.
 *
 * Runs in Node.js, Bun or Deno (it needs their WebSocket). The browser:
 *   $DESCRIBE_BROWSER      the executable; a name with "firefox" in it is driven
 *                          over BiDi, any other as Chrome (else google-chrome,
 *                          chromium, chromium-browser, chrome, firefox on PATH)
 *   $DESCRIBE_BROWSER_URL  a running browser, used as it is: http://127.0.0.1:<port>
 *                          (Chrome --remote-debugging-port) or ws://127.0.0.1:<port>
 *                          (Firefox --remote-debugging-port)
 *   $DESCRIBE_BROWSER_ARGS extra command line arguments, split on spaces
 */
import { spawn } from "node:child_process";
import { readFileSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const args = typeof Deno !== "undefined" ? Deno.args : process.argv.slice(2);
const source = readFileSync(join(dirname(fileURLToPath(import.meta.url)), "..", "lib", "describe.js"), "utf8");
const env = typeof Deno !== "undefined" ? Deno.env.toObject() : process.env;

let child, profile;

/* stops the browser and removes its profile; the browser may still be writing into it, so a failure there is ignored. */
function cleanup() {
  if(child) child.kill();

  try {
    if(profile) rmSync(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 });
  } catch(e) {}
}

function die(message) {
  console.error("describe-browser.js: " + message);
  cleanup();
  process.exit(1);
}

const CHROME = ["google-chrome", "google-chrome-stable", "chromium", "chromium-browser", "chrome"];

/* starts `exe` headless with the debugging port open; resolves to its endpoint (http://... for Chrome, ws://... for Firefox), or null if it did not start. */
function launch(exe) {
  const firefox = /firefox/i.test(exe);
  const extra = (env.DESCRIBE_BROWSER_ARGS || "").split(" ").filter(Boolean);
  const flags = firefox
    ? ["--headless", "--remote-debugging-port=0", "--profile", profile, "--no-remote", ...extra, "about:blank"]
    : ["--headless=new", "--remote-debugging-port=0", "--user-data-dir=" + profile, "--no-first-run", "--disable-gpu", ...extra, "about:blank"];
  const proc = spawn(exe, flags, { stdio: ["ignore", "pipe", "pipe"] });

  return new Promise(resolve => {
    let text = "";
    const watch = d => {
      text += d;
      const m = firefox ? /WebDriver BiDi listening on (ws:\/\/[\d.]+:\d+)/.exec(text) : /DevTools listening on ws:\/\/([\d.]+:\d+)\//.exec(text);
      if(m) {
        child = proc;
        resolve(firefox ? m[1] : "http://" + m[1]);
      }
    };

    proc.on("error", () => resolve(null));
    proc.on("exit", () => resolve(null));
    proc.stdout.on("data", watch);
    proc.stderr.on("data", watch);
  });
}

/* the browser's endpoint: a running one's, or a headless one started here. */
async function endpoint() {
  if(env.DESCRIBE_BROWSER_URL) return env.DESCRIBE_BROWSER_URL.replace(/\/$/, "");

  profile = mkdtempSync(join(tmpdir(), "describe-browser-"));

  for(const exe of env.DESCRIBE_BROWSER ? [env.DESCRIBE_BROWSER] : [...CHROME, "firefox"]) {
    const url = await launch(exe);
    if(url) return url;
  }

  die("no browser found or none started (set DESCRIBE_BROWSER)");
}

const base = await endpoint();
let ws,
  id = 0;
const pending = new Map();

/* one request and its reply, JSON over the open WebSocket. */
const send = (method, params = {}) =>
  new Promise(resolve => {
    pending.set(++id, resolve);
    ws.send(JSON.stringify({ id, method, params }));
  });

async function connect(url) {
  ws = new WebSocket(url);
  ws.onmessage = e => {
    const m = JSON.parse(e.data);
    if(m.id && pending.has(m.id)) pending.get(m.id)(m);
  };

  await new Promise((resolve, reject) => {
    ws.onopen = resolve;
    ws.onerror = () => reject(new Error("cannot connect to " + url));
  }).catch(e => die(e.message));
}

/* evaluates `expression` (a promise of a string) in a page and returns the string: Chrome by Runtime.evaluate, Firefox by BiDi script.evaluate. */
async function evaluate(expression) {
  if(base.startsWith("ws:")) {
    await connect(base + "/session");
    await send("session.new", { capabilities: {} });

    const tree = await send("browsingContext.getTree");
    const res = await send("script.evaluate", { expression, target: { context: tree.result.contexts[0].context }, awaitPromise: true, resultOwnership: "none" });

    if(res.type !== "success" || res.result.type !== "success") die(JSON.stringify((res.result && res.result.exceptionDetails) || res.message || res).slice(0, 500));
    return res.result.result.value;
  }

  const url = base + "/json/list";
  let json,
    body = await (await fetch(url)).text();

  try {
    json = JSON.parse(body);
  } catch(e) {
    console.log("ERROR:", { url, body });
    //console.log(" body);
    throw e;
  }

  /* a web page or a blank one; not an extension's, devtools' or chrome://'s, whose origin would resolve a module URL somewhere odd. */
  const page = json.find(t => t.type === "page" && /^(https?:|about:blank)/.test(t.url)) || (await (await fetch(base + "/json/new?about:blank", { method: "PUT" })).json());

  await connect(page.webSocketDebuggerUrl);

  const res = await send("Runtime.evaluate", { expression, awaitPromise: true, returnByValue: true });

  if(res.result.exceptionDetails) die((res.result.exceptionDetails.exception || {}).description || res.result.exceptionDetails.text);
  return res.result.result.value;
}

/* describe.js as the body of a function: no `export`, `import.meta` or hashbang, so it needs no module loading, which a page's CSP or origin could refuse (`data:` imports). */
const body = source
  .replace(/^#!.*\n/, "")
  .replace(/^export /gm, "")
  .replace(/import\.meta/g, "({})");

/* the page runs it with the arguments, collects what it logs and returns that as JSON. */
const expression =
  "(async () => {\n  const run = (() => {\n'use strict';\n" +
  body +
  "\nreturn run;\n})();\n" +
  `
  const out = [], err = [];
  const log = console.log, error = console.error;

  globalThis.__describeArgs = ${JSON.stringify(args)};
  globalThis.__describeExit = 0;
  console.log = (...a) => out.push(a.join(' '));
  console.error = (...a) => err.push(a.join(' '));

  try { await run(); } finally { console.log = log; console.error = error; }

  return JSON.stringify({ out, err, code: globalThis.__describeExit });
})()`;

const { out, err, code } = JSON.parse(await evaluate(expression));

if(out.length) console.log(out.join("\n"));
if(err.length) console.error(err.join("\n"));

if(ws) ws.close();
cleanup();
process.exit(code || 0);
