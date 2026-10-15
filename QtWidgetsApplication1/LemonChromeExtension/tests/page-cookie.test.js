import test from "node:test";
import assert from "node:assert/strict";
import { readPageCookie } from "../page-cookie.js";

const url = "https://jproh5test2.ningmengyun.com/books";
const context = { tabId: 7, url, envKey: "test" };
test("Cookie comes from the exact page URL and tab's store, including HttpOnly", async () => {
  const reads = [];
  const browser = {
    cookies: {
      getAllCookieStores: async () => [{ id: "other", tabIds: [8] }, { id: "mine", tabIds: [7] }],
      get: async query => { reads.push(query); return { value: "synthetic-page-token", domain: ".ningmengyun.com", path: "/", httpOnly: true }; }
    },
    tabs: { get: async () => ({ url }) }
  };
  const result = await readPageCookie(browser, context);
  assert.deepEqual(reads, [{ url, name: "ningmengcookie", storeId: "mine" }]);
  assert.equal(result.value, "synthetic-page-token");
  assert.equal(result.host, "jproh5test2.ningmengyun.com");
  browser.tabs.get = async () => ({ url: "https://example.com/" });
  await assert.rejects(readPageCookie(browser, context), /无法读取/);
});
test("missing cookie does not fall back to another site, missing store cannot read", async () => {
  let reads = 0;
  const browser = {
    cookies: { getAllCookieStores: async () => [{ id: "mine", tabIds: [7] }], get: async () => { ++reads; return null; } },
    tabs: { get: async () => ({ url }) }
  };
  await assert.rejects(readPageCookie(browser, context), /当前页面没有 ningmengcookie/);
  assert.equal(reads, 1);
  browser.cookies.getAllCookieStores = async () => [{ id: "other", tabIds: [8] }];
  await assert.rejects(readPageCookie(browser, context), /无法读取/);
  assert.equal(reads, 1);
});
test("raw API errors containing credentials are not returned to the UI", async () => {
  const browser = { cookies: { getAllCookieStores: async () => { throw new Error("sensitive-token"); } } };
  await assert.rejects(readPageCookie(browser, context), error => !error.message.includes("sensitive-token"));
});
