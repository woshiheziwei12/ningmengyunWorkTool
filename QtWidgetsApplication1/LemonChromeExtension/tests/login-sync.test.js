import test from "node:test";
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { createHash } from "node:crypto";
import { extensionId, parseHandoff, validatePayload, synchronize } from "../login-sync.js";

const ticket = "a".repeat(64);
const local = `http://127.0.0.1:23456/lemon-login/v1/#${ticket}`;
const website = "https://wbgate.ningmengyun.com/wb/redirect/menu?productType=acc";
const sender = { id: extensionId, frameId: 0, url: local, tab: { id: 7, incognito: false } };
function fixture(overrides = {}) {
  const calls = [];
  const payload = { protocol: 1, cookie: "synthetic-test-token", url: website, ...overrides };
  const browser = {
    cookies: {
      getAllCookieStores: async () => [{ id: "wrong", tabIds: [99] }, { id: "selected", tabIds: [7] }],
      getAll: async (x) => { calls.push(["getAll", x]); return []; },
      get: async () => ({ value: payload.cookie }),
      remove: async (x) => { calls.push(["remove", x]); return {}; },
      set: async (x) => { calls.push(["set", x]); return { ...x }; }
    },
    tabs: {
      get: async () => ({ url: local }),
      update: async (id, change) => { calls.push(["navigate", { id, ...change }]); }
    }
  };
  const fetcher = async (url, options) => {
    calls.push([url.split("/").at(-1), options]);
    return { ok: true, json: async () => ({ ...payload }) };
  };
  return { calls, payload, browser, fetcher };
}
test("manifest public key pins the desktop's extension ID; minimal permissions", async () => {
  const m = JSON.parse(await readFile(new URL("../manifest.json", import.meta.url)));
  const derived = createHash("sha256").update(Buffer.from(m.key, "base64")).digest("hex").slice(0, 32)
    .replace(/[0-9a-f]/g, x => String.fromCharCode(97 + parseInt(x, 16)));
  assert.equal(derived, extensionId);
  assert.deepEqual(m.permissions, ["cookies", "storage", "downloads", "alarms", "declarativeNetRequestWithHostAccess", "clipboardWrite"]);
  assert.equal(m.externally_connectable, undefined);
  const cpp = await readFile(new URL("../../QtWidgetsApplication1/Common/BrowserLoginBridge.cpp", import.meta.url), "utf8");
  assert.ok(cpp.includes(extensionId));
});
test("handoff rejects remote endpoints, credentials, alternate paths and malformed tickets", () => {
  assert.equal(parseHandoff(local).ticket, ticket);
  for (const value of [local.replace("127.0.0.1", "example.com"), local.replace("http:", "https:"),
    local.replace("127.0.0.1", "user@127.0.0.1"), local.replace("/v1/", "/other/"),
    local.replace("#", "?x=1#"), local.slice(0, -1), "file:///tmp/x"]) {
    assert.throws(() => parseHandoff(value));
  }
});
test("payload allows only the two HTTPS gateways and cookie-safe values", () => {
  for (const host of ["wbgate", "etest"]) {
    assert.equal(validatePayload({ protocol: 1, url: website.replace("wbgate", host), cookie: "test-token" }).hostname,
      `${host}.ningmengyun.com`);
  }
  for (const patch of [{ cookie: "" }, { cookie: "a;b" }, { cookie: "a\r\nb" }, { cookie: "a b" },
    { cookie: "中" }, { cookie: "x".repeat(16385) }, { protocol: 2 }, { url: website.replace("https:", "http:") },
    { url: website.replace("wbgate", "evil") }, { url: `${website}&redirect=https://example.com` }]) {
    assert.throws(() => validatePayload({ protocol: 1, url: website, cookie: "test", ...patch }));
  }
});
for (const host of ["wbgate", "etest"]) test(`${host}: claim, correct cookie store, write then navigate and receipt`, async () => {
  const f = fixture({ url: website.replace("wbgate", host) });
  assert.deepEqual(await synchronize(f.browser, f.fetcher, sender), { ok: true });
  const names = f.calls.map(x => x[0]);
  assert.ok(names.indexOf("claim") < names.indexOf("set"));
  assert.ok(names.indexOf("set") < names.indexOf("navigate"));
  assert.ok(names.indexOf("navigate") < names.indexOf("complete"));
  const cookie = f.calls.find(x => x[0] === "set")[1];
  assert.equal(cookie.value, "synthetic-test-token");
  assert.equal(cookie.storeId, "selected");
  assert.equal(cookie.secure, true);
  assert.equal(cookie.domain, ".ningmengyun.com");
  assert.equal(cookie.expirationDate, undefined);
  const claim = f.calls[0][1];
  assert.equal(claim.headers.Authorization, `Bearer ${ticket}`);
  assert.equal(claim.redirect, "error");
  assert.equal(claim.credentials, "omit");
  assert.ok(!f.calls.find(x => x[0] === "navigate")[1].url.includes("token"));
});
test("invalid sender, subframe or incognito cannot request credentials", async () => {
  for (const patch of [{ id: "wrong" }, { frameId: 1 }, { tab: { id: 7, incognito: true } },
    { url: "https://example.com" }, { tab: {} }]) {
    const f = fixture();
    await assert.rejects(synchronize(f.browser, f.fetcher, { ...sender, ...patch }));
    assert.equal(f.calls.length, 0);
  }
});
test("expired ticket or connection failure cannot write cookies", async () => {
  for (const fetcher of [async () => ({ ok: false }), async () => { throw new Error("network"); }]) {
    const f = fixture();
    const result = await synchronize(f.browser, fetcher, sender);
    assert.equal(result.ok, false);
    assert.equal(f.calls.length, 0);
    assert.ok(!result.error.includes(ticket));
  }
});
test("reject substituted destination without writing a cookie or redirecting", async () => {
  const f = fixture({ url: "https://example.com/" });
  assert.equal((await synchronize(f.browser, f.fetcher, sender)).ok, false);
  assert.deepEqual(f.calls.map(x => x[0]), ["claim", "failed"]);
});
test("cookie failure never opens the website and does not expose raw exception", async () => {
  const f = fixture();
  f.browser.cookies.set = async () => { throw new Error("sensitive-raw-cookie"); };
  const result = await synchronize(f.browser, f.fetcher, sender);
  assert.equal(result.ok, false);
  assert.ok(!result.error.includes("sensitive"));
  assert.ok(!f.calls.some(x => x[0] === "navigate"));
  assert.equal(f.calls.at(-1)[0], "failed");
});
test("replace only applicable same-name cookies in this store", async () => {
  const f = fixture();
  f.browser.cookies.getAll = async () => [
    { domain: "wbgate.ningmengyun.com", path: "/", hostOnly: true },
    { domain: ".ningmengyun.com", path: "/wb" },
    { domain: ".ningmengyun.com", path: "/" },
    { domain: "other.ningmengyun.com", path: "/" },
    { domain: "wbgate.ningmengyun.com", path: "/", partitionKey: { topLevelSite: "https://example.com" } }
  ];
  assert.equal((await synchronize(f.browser, f.fetcher, sender)).ok, true);
  assert.deepEqual(f.calls.filter(x => x[0] === "remove").map(x => x[1]), [
    { url: "https://wbgate.ningmengyun.com/", name: "ningmengcookie", storeId: "selected" },
    { url: "https://ningmengyun.com/wb", name: "ningmengcookie", storeId: "selected" },
    { url: "https://ningmengyun.com/", name: "ningmengcookie", storeId: "selected" }
  ]);
});
test("overlapping cookies are removed before writing, effective value is verified", async () => {
  const f = fixture();
  f.browser.cookies.getAll = async () => [{ domain: "wbgate.ningmengyun.com", path: "/", hostOnly: true }];
  assert.equal((await synchronize(f.browser, f.fetcher, sender)).ok, true);
  assert.ok(f.calls.some(x => x[0] === "remove"));
  assert.ok(f.calls.findIndex(x => x[0] === "remove") < f.calls.findIndex(x => x[0] === "set"));
  assert.ok(f.calls.some(x => x[0] === "navigate"));
});
test("write failure restores the previous cookie attributes without exposing it", async () => {
  const f = fixture();
  const old = { name: "ningmengcookie", value: "old-token", domain: ".ningmengyun.com", path: "/", secure: true,
    httpOnly: true, sameSite: "lax", hostOnly: false, expirationDate: Date.now() / 1000 + 3600 };
  f.browser.cookies.getAll = async () => [old];
  const restored = [];
  f.browser.cookies.set = async x => {
    if (x.value === "synthetic-test-token") throw new Error("write failed");
    restored.push(x); return x;
  };
  assert.equal((await synchronize(f.browser, f.fetcher, sender)).ok, false);
  assert.equal(restored.length, 1);
  assert.equal(restored[0].value, old.value);
  assert.equal(restored[0].expirationDate, old.expirationDate);
  assert.equal(restored[0].httpOnly, true);
  assert.ok(!f.calls.some(x => x[0] === "navigate"));
});
test("cookie still shadowed after cleanup cannot navigate or report success", async () => {
  const f = fixture();
  f.browser.cookies.get = async () => ({ value: "wrong-token" });
  assert.equal((await synchronize(f.browser, f.fetcher, sender)).ok, false);
  assert.ok(!f.calls.some(x => x[0] === "navigate"));
});
test("tab navigated by user is not redirected", async () => {
  const f = fixture();
  f.browser.tabs.get = async () => ({ url: "https://example.com/" });
  assert.equal((await synchronize(f.browser, f.fetcher, sender)).ok, false);
  assert.ok(!f.calls.some(x => x[0] === "navigate"));
});
test("lost receipt does not retry or claim synchronization failed", async () => {
  const f = fixture();
  const fetcher = async (url, options) => {
    if (url.endsWith("complete")) throw new Error("listener expired");
    return f.fetcher(url, options);
  };
  assert.equal((await synchronize(f.browser, fetcher, sender)).ok, true);
  assert.equal(f.calls.filter(x => x[0] === "claim").length, 1);
});
