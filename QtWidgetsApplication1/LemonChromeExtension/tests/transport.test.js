import test from "node:test";
import assert from "node:assert/strict";
import { Transport } from "../transport.js";
import { ENVIRONMENTS } from "../core.js";

function fakeChrome() {
  const rules = new Map();
  return {
    rules,
    runtime: { id: "abcdefghijklmnopabcdefghijklmnop" },
    storage: { session: { async set() {}, async remove() {} } },
    declarativeNetRequest: {
      async updateSessionRules({ addRules = [], removeRuleIds = [] }) {
        for (const rule of addRules) rules.set(rule.id, rule);
        for (const id of removeRuleIds) rules.delete(id);
      }
    }
  };
}

test("测试套餐请求在 HSTS 升级后仍匹配携带凭据的单次规则", async () => {
  const api = fakeChrome();
  const transport = new Transport(api, async url => {
    // The live etest HTTPS endpoint advertises HSTS includeSubDomains. Model
    // the upgraded network URL rather than matching the original HTTP input.
    const networkUrl = new URL(url);
    if (networkUrl.hostname === "etest.ningmengyun.com") networkUrl.protocol = "https:";
    const rule = [...api.rules.values()].find(r => r.condition.urlFilter === `|${networkUrl.href}|`);
    if (!rule) throw new TypeError("Failed to fetch");
    assert.ok(rule.action.requestHeaders.some(h => h.header === "Authorization" && h.value === "Bearer fixture-jwt"));
    return Response.json({ data: { data: [] } });
  });
  const result = await transport.json({ cookie: "fixture-cookie", tabId: 7 }, ENVIRONMENTS.test.gate, "/wb/plan", {
    query: { v: 0, currentPage: 1, pageSize: 10 }, bearer: "fixture-jwt", origin: ENVIRONMENTS.test.web
  });
  assert.deepEqual(result, { data: { data: [] } });
  assert.equal(api.rules.size, 0);
});

test("请求封装保留 WorkerGlobalScope 接收者，不在发送前抛 Illegal invocation", async () => {
  const api = fakeChrome();
  let sent = 0;
  // Unlike the existing arrow-function mocks, model the browser's native
  // WebIDL receiver check at the exact production call site.
  function browserFetch() {
    if (this !== globalThis) throw new TypeError("Failed to execute 'fetch' on 'WorkerGlobalScope': Illegal invocation");
    sent++;
    return Promise.resolve(Response.json({ success: true }));
  }
  const transport = new Transport(api, browserFetch);
  const result = await transport.json({ cookie: "fixture-cookie", tabId: 7 }, "https://auth.ningmengyun.com", "/v2/api/jwt", { method: "POST", body: {} });
  assert.deepEqual(result, { success: true });
  assert.equal(sent, 1);
  assert.equal(api.rules.size, 0);
});

test("网络失败指出接口和阶段，不泄露 Cookie 或把只读查询说成已提交操作", async () => {
  const api = fakeChrome();
  const transport = new Transport(api, async () => { throw new TypeError("Failed to fetch"); });
  await assert.rejects(transport.json({ cookie: "fixture-secret", tabId: 7 }, "https://auth.ningmengyun.com", "/v2/api/jwt", { method: "POST", body: {} }), error => {
    assert.match(error.message, /获取登录令牌/);
    assert.match(error.message, /auth\.ningmengyun\.com\/v2\/api\/jwt/);
    assert.doesNotMatch(error.message, /fixture-secret|已提交的操作|_lemonTool/);
    return true;
  });
  assert.equal(api.rules.size, 0);
});
