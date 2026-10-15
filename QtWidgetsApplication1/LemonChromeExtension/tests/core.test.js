import test from "node:test";
import assert from "node:assert/strict";
import { pageEnvironment, jwtIdentity, cookieHeader, selectPlans, accountRows, apiData, importResult, safeFilename } from "../core.js";
import { patchAccountSet, rewritePackage } from "../zip.js";
import { zipSync, unzipSync, strToU8, strFromU8 } from "../vendor/fflate.js";

test("环境识别拒绝相似域名与非 HTTP 协议", () => {
  assert.equal(pageEnvironment("https://jpro6.ningmengyun.com/account"), "pro");
  assert.equal(pageEnvironment("https://jproh5test2.ningmengyun.com"), "test");
  assert.equal(pageEnvironment("https://jproh5test.ningmengyun.com/Default/Default?serviceID=35617&appasid=fixture"), "test");
  assert.equal(pageEnvironment("https://jproh5graytest.ningmengyun.com/Default/Default?serviceID=35617&appasid=fixture"), "test");
  for (const url of ["https://ningmengyun.com.evil.com", "https://evilningmengyun.com", "file:///a.html"]) assert.throws(() => pageEnvironment(url));
});
test("Cookie 按套餐构造，中文用户名编码，不允许请求头注入", () => {
  assert.equal(cookieHeader({ cookie: "fixture-token", userId: 9, name: "测试用户" }, 12), "ningmengcookie=fixture-token; auth=9; authname=%E6%B5%8B%E8%AF%95%E7%94%A8%E6%88%B7; service-id=12");
  assert.throws(() => cookieHeader({ cookie: "x;evil=1" }));
  assert.throws(() => cookieHeader({ cookie: "x" }, -1));
  const token = `x.${Buffer.from(JSON.stringify({ sub: "9", name: "测试用户" })).toString("base64url")}.x`;
  assert.deepEqual(jwtIdentity(token), { userId: 9, name: "测试用户" });
});
test("套餐与账套筛选沿用桌面端规则，缺少身份字段拒绝操作", () => {
  const make = (id, administrator, extra = {}) => ({ id, administrator, productType: "acc", lastAccountSet: { appAsid: "app" }, ...extra });
  const plans = selectPlans([make(1, 9), make(2, 8), make(3, 9, { expired: true }), make(4, 9, { isValid: false })], 9);
  assert.deepEqual(plans.map(p => p.serviceId), [1]);
  assert.equal(selectPlans([make(2, 8)], 9).length, 1);
  assert.throws(() => accountRows({ accountList: [{ asid: 1 }] }, plans[0]));
  const rows = accountRows({ accountList: [{ asid: 1, appAsId: "app2", asname: "公司" }] }, plans[0]);
  assert.equal(rows[0].key, "1:1");
});
test("API 业务失败不会报成功，导入查询只匹配请求任务", () => {
  assert.throws(() => apiData({ state: 500, msg: "无权限" }), /无权限/);
  assert.equal(importResult({ Code: 0, Data: [{ Id: "other", Data: "done" }] }, "mine"), null);
  assert.equal(importResult({ Code: 0, Data: [{ Id: "mine", Data: false }] }, "mine"), false);
  assert.equal(safeFilename("a/b:c"), "a_b_c");
});
const json = '{"AccountSetType":2,"AAId":9007199254740995,"ProServiceId":3,"VoucherId":9223372036854775807,"Amount":123456789.123456789,"Note":"\\\"ProServiceId\\\": 888"}';
test("修改导账元数据保留 64 位整数、金额精度、转义字符串原文", () => {
  const result = patchAccountSet(json, 123);
  assert.equal(result, json.replace('"AccountSetType":2', '"AccountSetType":1').replace('"AAId":9007199254740995', '"AAId":0').replace('"ProServiceId":3', '"ProServiceId":123'));
  assert.throws(() => patchAccountSet('{"AAId":1}', 1), /缺少/);
  assert.throws(() => patchAccountSet(json.replace('"ProServiceId":3', '"ProServiceId":"3"'), 1), /整数/);
  assert.throws(() => patchAccountSet(json.replace('"VoucherId":', '"ProServiceId":'), 1), /多次/);
});
test("ZIP 只替换目标 JSON 内容，其他文件不变且源 ZIP 不变", () => {
  const source = zipSync({ "folder/AccountSet.json": strToU8(json), "attachments/凭证.txt": strToU8("附件原文") });
  const copy = source.slice();
  const result = unzipSync(rewritePackage(source, 42));
  assert.deepEqual(source, copy);
  assert.equal(strFromU8(result["folder/AccountSet.json"]), patchAccountSet(json, 42));
  assert.equal(strFromU8(result["attachments/凭证.txt"]), "附件原文");
  assert.throws(() => rewritePackage(zipSync({ "a.txt": strToU8("x") }), 1), /恰好/);
  assert.throws(() => rewritePackage(zipSync({ "a/AccountSet.json": strToU8(json), "b/AccountSet.json": strToU8(json) }), 1), /恰好/);
  assert.throws(() => rewritePackage(new Uint8Array([1, 2, 3]), 1));
});
