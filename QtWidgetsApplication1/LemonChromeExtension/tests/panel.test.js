import test from "node:test";
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { JSDOM } from "jsdom";

test("离线 DOM 界面：选择、搜索、取消删除、删除范围、取消备份和上传结果", async t => {
  const previous = Object.fromEntries(["window", "document", "Option", "chrome", "Worker", "fetch"].map(k => [k, globalThis[k]]));
  const html = await readFile(new URL("../panel.html", import.meta.url), "utf8");
  const dom = new JSDOM(html, { url: "http://127.0.0.1/panel.html?tabId=7", runScripts: "outside-only" });
  const win = dom.window;
  const $ = id => win.document.getElementById(id);
  const tick = () => new Promise(resolve => setTimeout(resolve, 10));
  const messages = [];
  win.HTMLDialogElement.prototype.showModal = function () { this.open = true; };
  win.HTMLDialogElement.prototype.close = function (value) { this.returnValue = value; this.open = false; this.dispatchEvent(new win.Event("close")); };
  const rows = [
    { key: "10:1", asId: 1, name: "公司甲", appAsId: "a", serviceId: 10, plan: "专业版", start: "2026-01" },
    { key: "20:2", asId: 2, name: "公司乙 <img src=x>", appAsId: "b", serviceId: 20, plan: "专业版", start: "2026-02" }
  ];
  let progress = 0;
  let pageCookie = "synthetic-page-cookie", copiedCookie = "";
  Object.defineProperty(win.navigator, "clipboard", { value: { writeText: async value => { copiedCookie = value; } } });
  try {
    Object.assign(globalThis, { window: win, document: win.document, Option: win.Option,
      chrome: { runtime: { async sendMessage(message) {
        messages.push(message); let data;
        switch (message.type) {
          case "context": data = { environment: "离线测试" }; break;
          case "read-page-cookie":
            if (!pageCookie) return { ok: false, error: "没有 Cookie" };
            data = { value: pageCookie, host: "jpro6.ningmengyun.com" }; break;
          case "connect": data = { id: "s", name: "测试账号", userId: 9, environment: "离线测试", accounts: rows.slice(), plans: [{ serviceId: 10, label: "专业版" }, { serviceId: 20, label: "专业版" }], warnings: [] }; break;
          case "delete": data = true; break;
          case "backup-start": data = { fileName: "fixture.zip" }; break;
          case "backup-progress": data = { progress }; break;
          case "backup-download": data = { downloadId: 3 }; break;
          case "download-status": data = { state: "complete" }; break;
          case "upload-prepare": data = { id: 81, url: "https://fixture.invalid/upload" }; break;
          case "import-result": data = { result: "模拟导入完成" }; break;
          case "release": data = true; break;
          default: data = true;
        }
        return { ok: true, data };
      } } },
      Worker: class {
        postMessage() { queueMicrotask(() => this.onmessage({ data: { ok: true, buffer: new ArrayBuffer(10) } })); }
        terminate() {}
      },
      fetch: async () => Response.json({ Code: 0, Data: "upload-task" })
    });
    await import(`../panel.js?test=${Date.now()}`);
    await tick();
    await t.test("无需读取账套即可获取 Cookie，默认遮挡，复制时重新读取，不进入日志", async () => {
      win.document.querySelector('[data-tab="cookie"]').click();
      assert.equal($("cookie-view").hidden, false);
      assert.equal($("copy-cookie").disabled, true);
      $("read-cookie").click(); await tick();
      assert.equal($("cookie-value").value, pageCookie);
      assert.equal($("cookie-value").type, "password");
      assert.equal(messages.some(m => m.type === "connect"), false);
      $("show-cookie").click(); assert.equal($("cookie-value").type, "text");
      pageCookie = "new-page-cookie";
      $("copy-cookie").click(); await tick();
      assert.equal(copiedCookie, "new-page-cookie");
      assert.equal($("cookie-value").type, "password");
      assert.ok(!$("log").textContent.includes("page-cookie"));
      win.document.querySelector('[data-tab="accounts"]').click();
      assert.equal($("cookie-value").value, "");
    });
    await t.test("重新读取失败清除旧值，不能继续复制陈旧登录", async () => {
      win.document.querySelector('[data-tab="cookie"]').click();
      $("read-cookie").click(); await tick();
      pageCookie = ""; $("read-cookie").click(); await tick();
      assert.equal($("cookie-value").value, "");
      assert.equal($("copy-cookie").disabled, true);
      assert.match($("cookie-note").textContent, /未能获取/);
      pageCookie = "synthetic-page-cookie";
      win.document.querySelector('[data-tab="accounts"]').click();
    });
    await t.test("初始操作禁用，读取后账号与账套展示，名称用文本渲染", async () => {
      assert.equal($("backup").disabled, true); assert.equal($("upload").disabled, true);
      $("connect").click(); await tick();
      assert.equal($("accounts").children.length, 2);
      assert.equal($("accounts").querySelector("img"), null);
      assert.match($("identity").textContent, /测试账号/);
    });
    await t.test("搜索后的全选只影响可见项，取消删除不发送请求", async () => {
      $("search").value = "公司甲"; $("search").dispatchEvent(new win.Event("input"));
      $("select-all").click(); assert.equal($("selection").textContent, "已选 1 个账套");
      $("delete").click(); await tick();
      assert.equal($("confirm-dialog").open, true);
      assert.match($("confirm-list").textContent, /公司甲/); assert.doesNotMatch($("confirm-list").textContent, /公司乙/);
      $("confirm-dialog").close("cancel"); await tick();
      assert.equal(messages.some(m => m.type === "delete"), false);
    });
    await t.test("停止备份停止后续轮询，再次操作延续已发起的任务", async () => {
      $("backup").click(); await tick();
      assert.equal($("connect").disabled, true); assert.equal($("delete").disabled, true);
      $("stop").click(); await new Promise(resolve => setTimeout(resolve, 280));
      assert.equal($("connect").disabled, false);
      const polls = messages.filter(m => m.type === "backup-progress").length;
      await tick(); assert.equal(messages.filter(m => m.type === "backup-progress").length, polls);
      progress = 100; $("backup").click(); await tick();
      assert.equal(messages.filter(m => m.type === "backup-start").length, 1);
      assert.match($("accounts").textContent, /备份已下载/);
    });
    await t.test("确认删除只发送选中键，成功后移出清单", async () => {
      $("delete").click(); await tick(); $("confirm-dialog").close("confirm"); await tick();
      const deletes = messages.filter(m => m.type === "delete");
      assert.deepEqual(deletes.map(m => m.key), ["10:1"]); assert.equal(deletes[0].confirmed, true);
      $("search").value = ""; $("search").dispatchEvent(new win.Event("input"));
      assert.equal($("accounts").children.length, 1); assert.match($("accounts").textContent, /公司乙/);
    });
    await t.test("文件和套餐未选择时禁用上传，上传确认取消不提交", async () => {
      win.document.querySelector('[data-tab="upload"]').click();
      assert.equal($("upload-view").hidden, false);
      $("plan").value = "20"; $("plan").dispatchEvent(new win.Event("change"));
      const file = { name: "fixture.zip", size: 30, arrayBuffer: async () => new ArrayBuffer(30) };
      Object.defineProperty($("file"), "files", { value: [file] }); $("file").dispatchEvent(new win.Event("change"));
      assert.equal($("upload").disabled, false);
      $("upload").click(); await tick(); $("confirm-dialog").close("cancel"); await tick();
      assert.equal(messages.some(m => m.type === "upload-prepare"), false);
      $("upload").click(); await tick(); $("confirm-dialog").close("confirm"); await tick();
      assert.equal(messages.find(m => m.type === "upload-prepare").serviceId, 20);
      assert.equal($("import-id").value, "upload-task"); assert.equal($("import-result").textContent, "模拟导入完成");
      assert.ok(messages.some(m => m.type === "release" && m.id === 81));
    });
  } finally { dom.window.close(); Object.assign(globalThis, previous); }
});
