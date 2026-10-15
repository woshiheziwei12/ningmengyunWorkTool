// Isolated Chrome for Testing only. Website navigation is intercepted locally;
// no real account, personal browser profile, or live Lemon Cloud API is used.
import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { spawn } from "node:child_process";
import { mkdtemp, mkdir } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { extensionId } from "../login-sync.js";
const require = createRequire(import.meta.url);
const { chromium } = require("playwright");
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const work = path.join(root, "BuildCheck/UnifiedExtension/browser-e2e");
const extension = path.join(root, "LemonChromeExtension/unpacked");
const executable = path.join(root, "BuildCheck/BrowserLogin/Tests-Release/bin/BrowserLoginBridgeTests.exe");
await mkdir(work, { recursive: true });

async function fixture(testEnvironment) {
  const child = spawn(executable, ["--browser-fixture", ...(testEnvironment ? ["--test-environment"] : [])], {
    windowsHide: true, stdio: ["ignore", "pipe", "pipe"],
    env: { ...process.env, PATH: `D:\\app\\qt5_12\\5.12.0\\msvc2017\\bin;${process.env.PATH || ""}` }
  });
  const exit = new Promise(resolve => child.once("exit", resolve));
  const url = await new Promise((resolve, reject) => {
    let buffer = "";
    const timer = setTimeout(() => { child.kill(); reject(new Error("Fixture startup timeout")); }, 10000);
    child.once("error", e => { clearTimeout(timer); reject(e); });
    child.once("exit", () => { clearTimeout(timer); if (!buffer.includes("\n")) reject(new Error("Fixture exited before ready")); });
    child.stdout.on("data", chunk => {
      buffer += chunk;
      if (buffer.includes("\n")) { clearTimeout(timer); resolve(buffer.split("\n")[0].trim()); }
    });
  });
  return { child, exit, url };
}

for (const testEnvironment of [false, true]) {
  const host = testEnvironment ? "etest.ningmengyun.com" : "wbgate.ningmengyun.com";
  const profile = await mkdtemp(path.join(work, "profile-"));
  const context = await chromium.launchPersistentContext(profile, {
    channel: "chromium", headless: true,
    args: [`--disable-extensions-except=${extension}`, `--load-extension=${extension}`]
  });
  let server;
  try {
    let requestCookie = "";
    await context.route("**/*", async route => {
      const url = new URL(route.request().url());
      if (url.protocol === "chrome-extension:") return route.continue();
      if (url.hostname === "127.0.0.1") return route.continue();
      if (url.hostname === host && url.pathname === "/wb/redirect/menu") {
        requestCookie = (await route.request().allHeaders()).cookie || "";
        return route.fulfill({ status: 200, contentType: "text/html", body: "<h1>Local website fixture</h1>" });
      }
      return route.abort();
    });
    const worker = context.serviceWorkers()[0] || await context.waitForEvent("serviceworker");
    assert.equal(new URL(worker.url()).hostname, extensionId);
    if (process.env.LOGIN_DIAGNOSTICS === "1") await worker.evaluate(() => {
      globalThis.testDiagnostics = [];
      for (const [api, names] of [[chrome.cookies, ["getAllCookieStores", "getAll", "set", "remove", "get"]], [chrome.tabs, ["get", "update"]]]) {
        for (const name of names) {
          const original = api[name].bind(api);
          api[name] = async (...args) => {
            try {
              const result = await original(...args);
              globalThis.testDiagnostics.push({ api: name, result: JSON.stringify(result, (k, v) => k === "value" || k === "url" ? "[redacted]" : v) });
              return result;
            } catch (error) {
              globalThis.testDiagnostics.push({ api: name, error: error.message }); throw error;
            }
          };
        }
      }
    });
    await context.addCookies([
      { name: "ningmengcookie", value: "old-domain", domain: ".ningmengyun.com", path: "/", secure: true },
      { name: "ningmengcookie", value: "old-host", url: `https://${host}/` },
      { name: "unrelated", value: "preserved", url: `https://${host}/` }
    ]);
    server = await fixture(testEnvironment);
    const page = context.pages()[0] || await context.newPage();
    await page.goto(server.url);
    try {
      await page.waitForURL(`https://${host}/wb/redirect/menu?productType=acc`, { timeout: 20000 });
    } catch (error) {
      console.error("Handoff page status:", await page.locator("#status").textContent().catch(() => "unavailable"));
      console.error("Synthetic fixture API diagnostics:", await worker.evaluate(() => globalThis.testDiagnostics));
      await page.screenshot({ path: path.join(work, `failure-${testEnvironment ? "test" : "pro"}.png`) });
      throw error;
    }
    await page.waitForLoadState();
    assert.ok(requestCookie.includes("ningmengcookie=synthetic-test-token"));
    assert.ok(!requestCookie.includes("old-host") && !requestCookie.includes("old-domain"));
    assert.ok(requestCookie.includes("unrelated=preserved"));
    const cookies = await context.cookies(`https://${host}/`);
    const login = cookies.find(c => c.name === "ningmengcookie");
    assert.equal(login.domain, ".ningmengyun.com");
    assert.equal(login.secure, true);
    assert.equal(login.expires, -1);
    assert.equal(await Promise.race([server.exit, new Promise(resolve => setTimeout(() => resolve("timeout"), 5000))]), 0);
    // Open the actual in-page extension iframe; no account-list request is needed.
    await worker.evaluate(async host => {
      const [tab] = await chrome.tabs.query({ url: `https://${host}/*` });
      await chrome.tabs.sendMessage(tab.id, { type: "lemon-toggle" });
    }, host);
    const frameReady = async () => {
      for (let i = 0; i < 100; ++i) {
        const frame = page.frames().find(f => f.url().startsWith(`chrome-extension://${extensionId}/panel.html?`));
        if (frame) return frame;
        await new Promise(resolve => setTimeout(resolve, 50));
      }
      throw new Error("Tool iframe did not open");
    };
    const panel = await frameReady();
    await panel.locator('[data-tab="cookie"]').click();
    await panel.locator("#read-cookie").click();
    await panel.waitForFunction(() => document.getElementById("cookie-value").value === "synthetic-test-token");
    assert.equal(await panel.locator("#cookie-value").getAttribute("type"), "password");
    assert.ok(!(await page.locator("body").textContent()).includes("synthetic-test-token"));
    assert.ok(!(await panel.locator("#log").textContent()).includes("synthetic-test-token"));
    // Keep the user's system clipboard untouched; exercise copy through a test sink.
    await panel.evaluate(() => Object.defineProperty(navigator, "clipboard", {
      configurable: true, value: { writeText: async value => { globalThis.copiedFixtureCookie = value; } }
    }));
    await context.addCookies([{ name: "ningmengcookie", value: "changed-fixture-token", domain: ".ningmengyun.com", path: "/", secure: true }]);
    await panel.locator("#copy-cookie").click();
    await panel.waitForFunction(() => globalThis.copiedFixtureCookie === "changed-fixture-token");
    await panel.locator("#show-cookie").click();
    assert.equal(await panel.locator("#cookie-value").getAttribute("type"), "text");
    await panel.locator("#show-cookie").click();
    await panel.locator("body").screenshot({ path: path.join(work, `cookie-panel-${testEnvironment ? "test" : "pro"}.png`) });
    await panel.locator('[data-tab="accounts"]').click();
    assert.equal(await panel.locator("#cookie-value").inputValue(), "");
    await panel.locator('[data-tab="cookie"]').click();
    await context.clearCookies({ name: "ningmengcookie" });
    await panel.locator("#read-cookie").click();
    await panel.waitForFunction(() => document.getElementById("cookie-note").textContent.includes("未能获取"));
    assert.equal(await panel.locator("#copy-cookie").isDisabled(), true);
    console.log(`PASS ${testEnvironment ? "test" : "production"}: unified extension + Qt sync + actual page cookie + refreshed copy + no-login handling`);
  } finally {
    if (server && server.child.exitCode === null) server.child.kill();
    await context.close();
  }
}
