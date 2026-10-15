// Injected only by preview-server.js, never referenced by the extension manifest.
window.chrome ??= {};
const fixtureRows = [
  { key: "12001:101", asId: 101, appAsId: "fixture-a", name: "杭州示例科技有限公司", start: "2026-01", serviceId: 12001, plan: "企业专业版" },
  { key: "12001:102", asId: 102, appAsId: "fixture-b", name: "示例商贸 · 华东分公司", start: "2025-06", serviceId: 12001, plan: "企业专业版" },
  { key: "12002:103", asId: 103, appAsId: "fixture-c", name: "测试账套 <img onerror=alert(1)>", start: "2024-03", serviceId: 12002, plan: "企业专业版" }
];
const fixtureProgress = new Map();
window.chrome.runtime = { async sendMessage(message) {
  let data;
  switch (message.type) {
    case "read-page-cookie": data = { value: "offline-demo-cookie-not-real", host: "离线演示" }; break;
    case "context": data = { environment: "离线演示 · 模拟数据" }; break;
    case "connect": data = { id: "fixture-session", name: "离线演示账号", userId: 99, environment: "离线演示 · 模拟数据", plans: [{ serviceId: 12001, label: "企业专业版", appAsId: "fixture-a" }, { serviceId: 12002, label: "企业专业版", appAsId: "fixture-c" }], accounts: fixtureRows.slice(), warnings: [] }; break;
    case "backup-start": fixtureProgress.set(message.key, 0); data = { fileName: "fixture.zip" }; break;
    case "backup-progress": fixtureProgress.set(message.key, (fixtureProgress.get(message.key) || 0) + 50); data = { progress: fixtureProgress.get(message.key) }; break;
    case "backup-download": data = { downloadId: 1 }; break;
    case "download-status": data = { state: "complete" }; break;
    case "delete": data = true; break;
    case "upload-prepare": data = { id: 1, url: "/mock-upload" }; break;
    case "import-result": data = { result: { message: "离线模拟导入完成", importId: message.importId } }; break;
    case "hide": case "release": data = true; break;
    default: return { ok: false, error: "未知模拟操作" };
  }
  return { ok: true, data };
} };
