import { importData } from "./core.js";

const $ = id => document.getElementById(id);
const state = { session: null, selected: new Set(), statuses: new Map(), pending: new Map(), file: null, busy: false, stop: false };
let cookieTimer, cookieRequest = 0, cookieBusy = false;
function clearCookie() {
  ++cookieRequest;
  clearTimeout(cookieTimer);
  $("cookie-value").value = "";
  $("cookie-value").type = "password";
  $("show-cookie").textContent = "显示";
  $("cookie-note").textContent = "默认遮挡，60 秒后自动清除面板中的值；不会删除浏览器 Cookie。";
  controls();
}
async function readCookie(copy = false) {
  if (cookieBusy || state.busy) return;
  clearCookie();
  const request = cookieRequest;
  cookieBusy = true; controls();
  try {
    const result = await rpc("read-page-cookie");
    if (request !== cookieRequest) return;
    $("cookie-value").value = result.value;
    cookieTimer = setTimeout(clearCookie, 60000);
    $("cookie-note").textContent = `已读取 ${result.host} 的 ningmengcookie，60 秒后清除显示。`;
    if (copy) {
      try { await window.navigator.clipboard.writeText(result.value); }
      catch {
        // Clipboard policy can block the modern API in a website iframe.
        // clipboardWrite permits the extension document's user-initiated fallback.
        if (request !== cookieRequest) return;
        const field = document.createElement("textarea");
        field.value = result.value; field.style.cssText = "position:fixed;left:-9999px;";
        document.body.append(field);
        try { field.select(); if (!document.execCommand("copy")) throw new Error("copy_failed"); }
        finally { field.value = ""; field.remove(); }
      }
      if (request === cookieRequest) $("cookie-note").textContent = `已复制 ${result.host} 当前的 ningmengcookie。`;
    }
  } catch {
    if (request === cookieRequest) {
      clearCookie();
      $("cookie-note").textContent = copy
        ? "复制未完成，请确认当前网页已登录，重新获取后再试。"
        : "未能获取 Cookie，请确认当前网页已登录且扩展有网站访问权限，再重新获取。";
    }
  } finally { cookieBusy = false; controls(); }
}
async function rpc(type, payload = {}) {
  const response = await chrome.runtime.sendMessage({ type, sessionId: state.session?.id, ...payload });
  if (!response?.ok) throw new Error(response?.error || "扩展连接中断，请刷新页面后再试");
  return response.data;
}
function notice(text = "") { $("notice").textContent = text; $("notice").hidden = !text; }
function log(text, kind = "") {
  const item = document.createElement("li");
  const time = document.createElement("time");
  time.textContent = new Date().toLocaleTimeString("zh-CN", { hour12: false });
  item.append(time, document.createTextNode(text));
  item.className = kind;
  if ($("log").firstElementChild?.classList.contains("muted")) $("log").replaceChildren();
  $("log").prepend(item);
  while ($("log").children.length > 100) $("log").lastElementChild.remove();
}
function filteredAccounts() {
  const query = $("search").value.trim().toLowerCase();
  return (state.session?.accounts || []).filter(a => `${a.name} ${a.asId} ${a.serviceId} ${a.plan}`.toLowerCase().includes(query));
}
function controls() {
  $("read-cookie").disabled = state.busy || cookieBusy;
  for (const id of ["show-cookie", "copy-cookie", "clear-cookie"]) $(id).disabled = state.busy || cookieBusy || !$("cookie-value").value;
  for (const id of ["connect", "search", "select-all", "file"]) $(id).disabled = state.busy;
  $("backup").disabled = $("delete").disabled = state.busy || !state.selected.size;
  $("plan").disabled = state.busy || !state.session?.plans.length;
  $("upload").disabled = state.busy || !state.file || !$("plan").value;
  $("query").disabled = state.busy || !state.session || !$("import-id").value.trim();
  $("import-id").disabled = state.busy;
  $("stop").disabled = !state.busy || state.stop;
  $("selection").textContent = `已选 ${state.selected.size} 个账套`;
  const visible = filteredAccounts();
  $("select-all").checked = !!visible.length && visible.every(a => state.selected.has(a.key));
  $("select-all").indeterminate = visible.some(a => state.selected.has(a.key)) && !$("select-all").checked;
}
function render() {
  $("accounts").replaceChildren();
  for (const account of filteredAccounts()) {
    const row = document.createElement("tr");
    row.classList.toggle("selected", state.selected.has(account.key));
    const checkCell = document.createElement("td");
    const check = document.createElement("input");
    check.type = "checkbox";
    check.setAttribute("aria-label", `选择 ${account.name}（${account.asId}）`);
    check.checked = state.selected.has(account.key);
    check.disabled = state.busy;
    check.onchange = () => { check.checked ? state.selected.add(account.key) : state.selected.delete(account.key); render(); };
    checkCell.append(check); row.append(checkCell);
    for (const [main, sub] of [[account.name, `编号 ${account.asId}`], [account.start, ""], [account.plan, `套餐 ${account.serviceId}`]]) {
      const cell = document.createElement("td"); cell.textContent = main;
      if (sub) { const small = document.createElement("small"); small.textContent = sub; cell.append(small); }
      row.append(cell);
    }
    const status = document.createElement("td");
    const value = state.statuses.get(account.key);
    status.className = `row-state ${value?.kind || "muted"}`;
    status.textContent = value?.text || "待操作";
    row.append(status); $("accounts").append(row);
  }
  $("empty").hidden = filteredAccounts().length > 0;
  $("empty").querySelector("h3").textContent = state.session ? "没有匹配的账套" : "还没有账套列表";
  $("empty").querySelector("p").textContent = state.session ? "可尝试调整搜索条件，或重新读取账套。" : "点击「读取账套」，这里将显示当前账号可操作的账套。";
  $("account-count").textContent = state.session?.accounts.length || 0;
  controls();
}
function status(account, text, kind = "") { state.statuses.set(account.key, { text, kind }); render(); }
async function run(fn) {
  if (state.busy) return;
  state.busy = true; state.stop = false; render(); notice();
  try { await fn(); }
  catch (error) { notice(error.message); log(error.message, "error"); }
  finally { state.busy = false; render(); }
}
function stopped() {
  if (state.stop) throw new Error("已停止等待及后续任务；已提交的服务器操作或 Chrome 下载可能继续，请核对结果");
}
async function wait(ms) {
  const end = Date.now() + ms;
  while (Date.now() < end) { stopped(); await new Promise(resolve => setTimeout(resolve, Math.min(250, end - Date.now()))); }
  stopped();
}
async function confirmAction(title, description, rows, destructive = false) {
  $("confirm-title").textContent = title;
  $("confirm-description").textContent = description;
  $("confirm-list").replaceChildren(...rows.map(text => { const li = document.createElement("li"); li.textContent = text; return li; }));
  $("confirm-submit").className = destructive ? "danger" : "primary";
  $("confirm-submit").textContent = destructive ? "确认删除所选账套" : "确认上传";
  const dialog = $("confirm-dialog");
  dialog.returnValue = "cancel";
  dialog.showModal();
  return new Promise(resolve => dialog.addEventListener("close", () => resolve(dialog.returnValue === "confirm"), { once: true }));
}

$("connect").onclick = () => run(async () => {
  clearCookie();
  state.session = null; state.selected.clear(); state.statuses.clear(); state.pending.clear();
  $("plan").replaceChildren(new Option("请先读取账套", ""));
  $("identity").textContent = "正在读取当前登录账号…";
  $("session-note").textContent = "正在读取套餐和账套列表，请稍候。";
  try {
    const result = await rpc("connect");
    state.session = result;
    $("identity").textContent = `${result.name || "当前账号"} · ${result.userId}`;
    $("session-note").textContent = `${result.plans.length} 个可用套餐 · ${result.accounts.length} 个账套 · 已同步浏览器登录状态`;
    $("environment").textContent = result.environment;
    $("plan").replaceChildren(new Option("请选择目标套餐", ""), ...result.plans.map(p => new Option(`${p.label} · 套餐 ${p.serviceId}`, p.serviceId)));
    if (result.plans.length === 1) $("plan").value = result.plans[0].serviceId;
    notice(result.warnings.join("\n"));
    log(`已读取 ${result.accounts.length} 个账套${result.warnings.length ? `，${result.warnings.length} 个套餐读取失败` : ""}`);
    $("connect").textContent = "重新读取 ↻";
  } catch (error) {
    $("identity").textContent = "未能读取登录信息";
    $("session-note").textContent = "请确认网页已登录，再点击读取账套。";
    throw error;
  }
});
$("search").oninput = render;
$("select-all").onchange = event => { for (const a of filteredAccounts()) event.target.checked ? state.selected.add(a.key) : state.selected.delete(a.key); render(); };
$("stop").onclick = () => { state.stop = true; controls(); log("已请求停止，当前请求结束后不再继续后续任务。"); };
if (window.top === window) $("hide").textContent = "返回柠檬云 ↗";
$("hide").onclick = () => { clearCookie(); rpc(window.top === window ? "focus-source" : "hide").catch(error => notice(error.message)); };
$("read-cookie").onclick = () => readCookie();
$("copy-cookie").onclick = () => readCookie(true);
$("clear-cookie").onclick = clearCookie;
$("show-cookie").onclick = () => {
  const visible = $("cookie-value").type === "password";
  $("cookie-value").type = visible ? "text" : "password";
  $("show-cookie").textContent = visible ? "隐藏" : "显示";
};
window.addEventListener("pagehide", clearCookie);
document.addEventListener("visibilitychange", () => { if (document.hidden) clearCookie(); });
for (const tab of document.querySelectorAll(".tab")) tab.onclick = () => {
  if (tab.dataset.tab !== "cookie") clearCookie();
  for (const button of document.querySelectorAll(".tab")) {
    const active = button === tab; button.classList.toggle("active", active); button.setAttribute("aria-pressed", active);
  }
  $("accounts-view").hidden = tab.dataset.tab !== "accounts";
  $("upload-view").hidden = tab.dataset.tab !== "upload";
  $("cookie-view").hidden = tab.dataset.tab !== "cookie";
};

$("backup").onclick = () => run(async () => {
  const accounts = state.session.accounts.filter(a => state.selected.has(a.key));
  for (const account of accounts) {
    stopped();
    try {
      if (!state.pending.has(account.key)) {
        status(account, "正在发起备份…");
        await rpc("backup-start", { key: account.key });
        state.pending.set(account.key, {});
        log(`${account.name}：已发起备份`);
      }
      const job = state.pending.get(account.key);
      if (!job.downloadId) {
        const deadline = Date.now() + 15 * 60 * 1000;
        while (true) {
          stopped();
          const { progress } = await rpc("backup-progress", { key: account.key });
          status(account, `备份进度 ${Math.min(progress, 100)}%`);
          if (progress >= 100) break;
          if (Date.now() > deadline) throw new Error("备份等待超过 15 分钟，再次点击备份可继续查询");
          await wait(3000);
        }
        stopped();
        const result = await rpc("backup-download", { key: account.key });
        job.downloadId = result.downloadId;
      }
      status(account, "文件下载中…");
      const deadline = Date.now() + 15 * 60 * 1000;
      while (true) {
        stopped();
        const result = await rpc("download-status", { downloadId: job.downloadId });
        if (result.state === "complete") break;
        if (["interrupted", "unknown"].includes(result.state)) { delete job.downloadId; throw new Error(`下载中断（${result.error || result.state}），可再次点击备份重试下载`); }
        if (Date.now() > deadline) throw new Error("下载仍在进行，请查看 Chrome 下载列表");
        await wait(1500);
      }
      state.pending.delete(account.key);
      status(account, "备份已下载", "success"); log(`${account.name}：备份文件已下载`, "success");
    } catch (error) {
      status(account, error.message, "error"); log(`${account.name}：${error.message}`, "error");
      if (state.stop) throw error;
    }
  }
});

$("delete").onclick = () => run(async () => {
  const accounts = state.session.accounts.filter(a => state.selected.has(a.key));
  if (!await confirmAction(`删除 ${accounts.length} 个账套`, `${state.session.environment} · ${state.session.name || state.session.userId}。将所选账套移入回收站；请确认已保存需要的备份。`, accounts.map(a => `${a.name}（编号 ${a.asId}，套餐 ${a.serviceId}）`), true)) return;
  for (const account of accounts) {
    stopped();
    status(account, "正在核对并删除…");
    try {
      await rpc("delete", { key: account.key, confirmed: true });
      state.selected.delete(account.key);
      state.session.accounts = state.session.accounts.filter(a => a.key !== account.key);
      log(`${account.name}（${account.asId}）：已移入回收站`, "success"); render();
    } catch (error) {
      status(account, error.message, "error");
      // Stop after an uncertain deletion: never automatically repeat a mutation.
      throw error;
    }
  }
});

function selectFile(file) {
  if (state.busy || !file) return;
  if (!/\.zip$/i.test(file.name) || !file.size || file.size > 128 * 1024 * 1024) {
    state.file = null;
    $("file-name").textContent = "请重新选择 ZIP 文件";
    $("file-detail").textContent = "单个 ZIP 最大 128 MB，解压后最大 256 MB";
    notice("请选择非空且不超过 128 MB 的 ZIP 文件"); controls(); return;
  }
  state.file = file; notice();
  $("file-name").textContent = file.name;
  $("file-detail").textContent = `${(file.size / 1024 / 1024).toFixed(2)} MB · 仅处理上传副本`;
  controls();
}
$("file").onchange = event => selectFile(event.target.files[0]);
$("dropzone").onkeydown = event => { if (["Enter", " "].includes(event.key) && !state.busy) { event.preventDefault(); $("file").click(); } };
for (const name of ["dragenter", "dragover"]) $("dropzone").addEventListener(name, event => { event.preventDefault(); $("dropzone").classList.add("dragover"); });
$("dropzone").ondragleave = () => $("dropzone").classList.remove("dragover");
$("dropzone").ondrop = event => { event.preventDefault(); $("dropzone").classList.remove("dragover"); if (event.dataTransfer.files.length !== 1) notice("请一次选择一个 ZIP 文件"); else selectFile(event.dataTransfer.files[0]); };
$("plan").onchange = controls;
$("import-id").oninput = controls;

async function patchZip(file, serviceId) {
  const buffer = await file.arrayBuffer(); stopped();
  return new Promise((resolve, reject) => {
    const worker = new Worker(new URL("zip-worker.js", import.meta.url), { type: "module" });
    const started = Date.now();
    const cleanup = () => { clearInterval(timer); worker.terminate(); };
    const timer = setInterval(() => {
      if (state.stop || Date.now() - started > 120000) { cleanup(); reject(new Error(state.stop ? "已停止压缩包处理，未上传" : "压缩包处理超时，请使用桌面工具")); }
    }, 250);
    worker.onerror = () => { cleanup(); reject(new Error("压缩包处理失败，请检查文件格式")); };
    worker.onmessage = event => { cleanup(); event.data.ok ? resolve(new Blob([event.data.buffer], { type: "application/zip" })) : reject(new Error(event.data.error)); };
    worker.postMessage({ buffer, serviceId }, [buffer]);
  });
}
async function queryImport(importId) {
  const deadline = Date.now() + 15 * 60 * 1000;
  while (true) {
    stopped();
    const { result } = await rpc("import-result", { importId });
    if (result !== null) {
      $("import-result").textContent = typeof result === "string" ? result : JSON.stringify(result, null, 2);
      log(`导入任务 ${importId}：已返回结果，请查看结果内容`); return;
    }
    $("import-result").textContent = `任务 ${importId} 正在处理中，10 秒后继续查询…`;
    if (Date.now() > deadline) throw new Error("导入结果等待超过 15 分钟，可使用任务 ID 稍后再次查询");
    await wait(10000);
  }
}
$("upload").onclick = () => run(async () => {
  const file = state.file;
  const serviceId = Number($("plan").value);
  const plan = state.session.plans.find(p => p.serviceId === serviceId);
  if (!file || !plan) throw new Error("请选择文件和目标套餐");
  if (!await confirmAction("上传并创建导入任务", `${state.session.environment} · ${state.session.name || state.session.userId}。确认后将修改上传副本中的套餐信息并提交专业版导账服务。`, [`文件：${file.name}`, `目标：${plan.label}（套餐 ${serviceId}）`])) return;
  log(`正在处理上传副本：${file.name}`);
  const blob = await patchZip(file, serviceId); stopped();
  const lease = await rpc("upload-prepare", { serviceId });
  try {
    stopped();
    const name = file.name.replace(/\.zip$/i, "");
    const form = new FormData(); form.append(name, blob, file.name);
    log(`正在上传 ${file.name}，目标套餐 ${serviceId}。`);
    const response = await fetch(lease.url, { method: "POST", body: form, credentials: "omit", redirect: "error", signal: AbortSignal.timeout(120000) });
    if (!response.ok) throw new Error(`上传返回 HTTP ${response.status}；请核对导入记录后再重试`);
    const id = importData(await response.json());
    if (typeof id !== "string" || !id) throw new Error("上传响应缺少任务 ID，请先核对导入记录，避免重复上传");
    $("import-id").value = id;
    $("import-result").textContent = `已提交任务 ${id}，正在等待导入结果。`;
    log(`上传已提交，任务 ID：${id}`, "success");
  } catch (error) {
    if (["TimeoutError", "AbortError", "TypeError", "SyntaxError"].includes(error.name)) throw new Error("上传连接中断、超时或响应无法解析；服务端可能已接收，请先核对导入记录，勿直接重复上传");
    throw error;
  } finally { await rpc("release", { id: lease.id }); }
  await queryImport($("import-id").value);
});
$("query").onclick = () => run(() => queryImport($("import-id").value.trim()));
window.addEventListener("beforeunload", event => { if (state.busy) { event.preventDefault(); event.returnValue = ""; } });
rpc("context").then(context => { $("environment").textContent = context.environment; }).catch(error => notice(error.message));
render();
