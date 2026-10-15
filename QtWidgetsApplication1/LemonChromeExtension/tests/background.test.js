import test from "node:test";
import assert from "node:assert/strict";
import { Transport } from "../transport.js";

const event = () => ({ handlers: [], addListener(fn) { this.handlers.push(fn); } });
const data = {}, rules = new Map(), downloads = new Map(), requests = [];
let cookie = "fixture-cookie", failedPath = "", forbidden = false, renamed = false;
let pageUrl = "https://jpro6.ningmengyun.com/";
const jwt = `x.${Buffer.from(JSON.stringify({ sub: "99", name: "离线测试" })).toString("base64url")}.x`;
const clone = value => JSON.parse(JSON.stringify(value));
globalThis.chrome = {
  runtime: { id: "abcdefghijklmnopabcdefghijklmnop", onMessage: event(), onStartup: event(), getURL: path => `chrome-extension://abcdefghijklmnopabcdefghijklmnop/${path}` },
  alarms: { create() {}, onAlarm: event() }, action: { onClicked: event() },
  tabs: { onRemoved: event(), get: async () => ({ id: 7, url: pageUrl }), sendMessage: async () => {}, create: async () => {} },
  cookies: { getAllCookieStores: async () => [{ id: "0", tabIds: [7] }], get: async () => cookie ? { value: cookie } : null },
  storage: { session: {
    async get(key) { if (key === null) return clone(data); return Object.fromEntries([key].flat().filter(k => k in data).map(k => [k, clone(data[k])])); },
    async set(values) { Object.assign(data, clone(values)); },
    async remove(keys) { for (const key of [keys].flat()) delete data[key]; }
  } },
  declarativeNetRequest: {
    async updateSessionRules({ addRules = [], removeRuleIds = [] }) { for (const id of removeRuleIds) rules.delete(id); for (const rule of addRules) rules.set(rule.id, clone(rule)); },
    async getSessionRules() { return [...rules.values()]; }
  },
  downloads: { onChanged: event(), async download(options) { downloads.set(11, { id: 11, state: "in_progress", ...options }); return 11; }, async search({ id }) { return downloads.has(id) ? [downloads.get(id)] : []; } }
};
const plan = id => ({ id, productType: "acc", administrator: 99, lastAccountSet: { appAsid: `plan-${id}` }, productTypeDescription: "专业版" });
globalThis.fetch = async (url, options) => {
  const u = new URL(url);
  const rule = [...rules.values()].find(r => r.condition.urlFilter === `|${url}|`);
  assert.ok(rule, "每个请求必须有精确 URL 的会话规则");
  assert.equal(options.credentials, "omit"); assert.equal(options.redirect, "error");
  requests.push({ url: u, options, rule });
  if (u.pathname === failedPath) throw new TypeError("offline");
  if (forbidden) return new Response("", { status: 401 });
  let body;
  switch (u.pathname) {
    case "/v2/api/jwt": body = { success: true, token: jwt }; break;
    case "/wb/plan": body = { data: { data: u.searchParams.get("currentPage") === "1" ? Array.from({ length: 10 }, (_, i) => plan(i + 1)) : [plan(11)] } }; break;
    case "/api/AccountSetOnlyAuth/TopInfoV2": body = { state: 1000, data: { accountList: [{ asid: 101, asname: renamed ? "改名" : "测试账套", appAsId: `account-${u.searchParams.get("serviceId")}`, asStartYear: 2026, asStartMonth: 9 }] } }; break;
    case "/api/Backup/ProcessBackup": body = { state: 1000, data: { fileName: "备份 & 文件.zip" } }; break;
    case "/api/Backup/GetFileRecord": body = { state: 1000, data: { progress: 100 } }; break;
    case "/api/AccountSetOnlyAuth/DropAccountSetToRecycleBinV2": body = { state: 1000, data: true }; break;
    case "/api/AccImport/GetImportResultV2": body = { Code: 0, Data: [{ Id: "mine", Data: "完成" }] }; break;
    default: throw new Error(`未知接口 ${u.pathname}`);
  }
  return Response.json(body);
};
await import("../background.js");
const sender = { id: chrome.runtime.id, url: chrome.runtime.getURL("panel.html?tabId=7"), tab: { id: 7 }, frameId: 1 };
function call(type, extra = {}, from = sender) {
  return new Promise(resolve => chrome.runtime.onMessage.handlers[0]({ type, ...extra }, from, resolve));
}
test("认证、分页、备份下载、上传授权、删除核验完整流程（模拟 Chrome 与 HTTP）", async t => {
  let sessionId;
  await t.test("拒绝普通网页伪造操作，未登录不能读取", async () => {
    const denied = await call("connect", {}, { ...sender, url: pageUrl });
    assert.equal(denied.ok, false); assert.equal(requests.length, 0);
    cookie = ""; assert.equal((await call("connect")).ok, false); cookie = "fixture-cookie";
  });
  await t.test("换 JWT 并读取全部分页，不向面板暴露凭据", async () => {
    const before = requests.length;
    assert.equal((await call("read-page-cookie", {}, { ...sender, url: pageUrl })).ok, false);
    const direct = await call("read-page-cookie");
    assert.equal(direct.ok, true); assert.equal(direct.data.value, cookie);
    assert.equal(direct.data.host, "jpro6.ningmengyun.com");
    assert.equal(requests.length, before);
    assert.equal(data["session:7"], undefined);
    cookie = ""; assert.equal((await call("read-page-cookie")).ok, false); cookie = "fixture-cookie";
    const result = await call("connect"); assert.equal(result.ok, true, result.error);
    sessionId = result.data.id;
    assert.equal(result.data.plans.length, 11); assert.equal(result.data.accounts.length, 11);
    assert.equal(JSON.stringify(result).includes("fixture-cookie"), false);
    assert.equal(JSON.stringify(result).includes(jwt), false);
    assert.equal(rules.size, 0);
    const list = requests.find(r => r.url.pathname.endsWith("TopInfoV2"));
    assert.ok(list.rule.action.requestHeaders.some(h => h.header === "appasid" && h.value === "plan-1"));
  });
  await t.test("备份使用所选套餐，编码特殊文件名，下载完成才报完成", async () => {
    assert.equal((await call("backup-start", { sessionId, key: "11:101" })).ok, true);
    assert.equal((await call("backup-progress", { sessionId, key: "11:101" })).data.progress, 100);
    const request = requests.at(-1);
    assert.equal(request.url.searchParams.get("filename"), "备份 & 文件.zip");
    assert.ok(request.rule.action.requestHeaders.find(h => h.header === "Cookie").value.includes("service-id=11"));
    const download = await call("backup-download", { sessionId, key: "11:101" }); assert.equal(download.ok, true);
    assert.equal((await call("download-status", { sessionId, downloadId: 11 })).data.state, "in_progress");
    downloads.get(11).state = "complete";
    await chrome.downloads.onChanged.handlers[0]({ id: 11, state: { current: "complete" } });
    assert.equal((await call("download-status", { sessionId, downloadId: 11 })).data.state, "complete"); assert.equal(rules.size, 0);
  });
  await t.test("上传沿用桌面端 ningmengcookie Bearer，只授权专业版接口", async () => {
    const upload = await call("upload-prepare", { sessionId, serviceId: 11 });
    assert.equal(upload.ok, true);
    const rule = rules.get(upload.data.id);
    assert.ok(rule.action.requestHeaders.some(h => h.header === "Authorization" && h.value === "Bearer fixture-cookie"));
    assert.ok(upload.data.url.startsWith("https://prodaozhangapi.ningmengyun.com/api/AccImport/ImportAccountSetFileAsyncV2?"));
    assert.deepEqual(rule.condition.initiatorDomains, [chrome.runtime.id]);
    await call("release", { id: upload.data.id }); assert.equal(rules.size, 0);
    assert.equal((await call("upload-prepare", { sessionId, serviceId: 999 })).ok, false);
    assert.equal((await call("import-result", { sessionId, importId: "mine" })).data.result, "完成");
  });
  await t.test("删除需要确认、已读取目标和身份复核，改名后拒绝删除", async () => {
    assert.equal((await call("delete", { sessionId, key: "1:101" })).ok, false);
    assert.equal((await call("delete", { sessionId, key: "999:101", confirmed: true })).ok, false);
    renamed = true;
    assert.equal((await call("delete", { sessionId, key: "1:101", confirmed: true })).ok, false);
    assert.equal(requests.some(r => r.url.pathname.includes("DropAccount")), false);
    renamed = false;
    assert.equal((await call("delete", { sessionId, key: "1:101", confirmed: true })).ok, true);
    assert.equal((await call("delete", { sessionId, key: "1:101", confirmed: true })).ok, false);
    assert.equal(requests.filter(r => r.url.pathname.includes("DropAccount")).length, 1);
  });
  await t.test("网络失败和 401 都清理临时规则，切换账号阻止旧会话操作", async () => {
    failedPath = "/api/Backup/ProcessBackup";
    assert.equal((await call("backup-start", { sessionId, key: "2:101" })).ok, false);
    assert.equal(rules.size, 0); failedPath = "";
    forbidden = true; assert.equal((await call("backup-start", { sessionId, key: "2:101" })).ok, false); forbidden = false;
    assert.equal(rules.size, 0);
    const count = requests.length;
    cookie = "another-user";
    assert.equal((await call("delete", { sessionId, key: "2:101", confirmed: true })).ok, false);
    assert.equal(requests.length, count); cookie = "fixture-cookie";
  });
  await t.test("测试站点只使用测试接口，正式站点切换后旧会话失效", async () => {
    let result;
    for (const host of ["jproh5test2", "jproh5test", "jproh5graytest"]) {
      pageUrl = `https://${host}.ningmengyun.com/`;
      const start = requests.length;
      result = await call("connect"); assert.equal(result.ok, true);
      assert.ok(requests.slice(start).every(r => /test/.test(r.url.hostname)));
      assert.ok(requests.slice(start).filter(r => r.url.pathname === "/wb/plan").every(r => r.url.origin === "https://etest.ningmengyun.com"));
    }
    pageUrl = "https://jpro6.ningmengyun.com/";
    assert.equal((await call("upload-prepare", { sessionId: result.data.id, serviceId: 1 })).ok, false);
  });
  await t.test("过期或失去元数据的规则会被清理", async () => {
    rules.set(91, { id: 91 }); rules.set(92, { id: 92 });
    data["lease:92"] = { expires: 1 };
    await new Transport().cleanup(); assert.equal(rules.size, 0);
  });
});
