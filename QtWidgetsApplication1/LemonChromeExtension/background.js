import { ENVIRONMENTS, pageEnvironment, jwtIdentity, selectPlans, accountRows, apiData, importResult, safeFilename, redact } from "./core.js";
import { Transport } from "./transport.js";
import { synchronize } from "./login-sync.js";
import { readPageCookie } from "./page-cookie.js";

const transport = new Transport();
const inFlight = new Set();
const panelPath = chrome.runtime.getURL("panel.html");
chrome.alarms.create("cleanup", { periodInMinutes: 1 });
chrome.alarms.onAlarm.addListener(() => transport.cleanup().catch(() => {}));
chrome.runtime.onStartup.addListener(() => transport.cleanup().catch(() => {}));

chrome.action.onClicked.addListener(async tab => {
  try {
    pageEnvironment(tab.url);
    await chrome.tabs.sendMessage(tab.id, { type: "lemon-toggle" });
  } catch {
    await chrome.tabs.create({ url: chrome.runtime.getURL("help.html") });
  }
});

chrome.tabs.onRemoved.addListener(async tabId => {
  const data = await chrome.storage.session.get(null);
  const keys = Object.keys(data).filter(k => (k.startsWith("session:") || k.startsWith("download:")) && data[k].tabId === tabId);
  // Downloads continue in Chrome even after the initiating page is closed.
  await chrome.storage.session.remove(keys.filter(k => k.startsWith("session:")));
});

chrome.downloads.onChanged.addListener(async delta => {
  if (!delta.state || !["complete", "interrupted"].includes(delta.state.current)) return;
  const key = `download:${delta.id}`;
  const entry = (await chrome.storage.session.get(key))[key];
  if (entry) await transport.release(entry.leaseId).catch(() => {});
});

async function panelContext(sender) {
  if (sender.id !== chrome.runtime.id || !sender.url?.startsWith(`${panelPath}?`)) throw new Error("仅扩展工具面板可执行此操作");
  const tabId = Number(new URL(sender.url).searchParams.get("tabId"));
  if (!Number.isInteger(tabId) || tabId < 0 || (sender.tab && sender.frameId !== 0 && sender.tab.id !== tabId)) throw new Error("页面上下文无效");
  const tab = await chrome.tabs.get(tabId);
  return { tabId, url: tab.url, envKey: pageEnvironment(tab.url) };
}

async function readLogin(context, source) {
  const env = ENVIRONMENTS[context.envKey];
  const stores = await chrome.cookies.getAllCookieStores();
  const store = stores.find(s => s.tabIds.includes(context.tabId));
  if (!store) throw new Error("无法确定当前页面的浏览器会话");
  for (const url of source ? [source] : [`${env.auth}/v2/api/jwt`, env.gate, context.url]) {
    const cookie = await chrome.cookies.get({ url, name: "ningmengcookie", storeId: store.id });
    if (cookie?.value) return { value: cookie.value, source: url, storeId: store.id };
  }
  throw new Error("未检测到登录状态，请先在当前浏览器登录柠檬云，再读取账套");
}

async function connect(context) {
  const key = `session:${context.tabId}`;
  await chrome.storage.session.remove(key);
  const env = ENVIRONMENTS[context.envKey];
  const login = await readLogin(context);
  const session = { ...context, id: crypto.randomUUID(), cookie: login.value, cookieSource: login.source, storeId: login.storeId };
  await chrome.storage.session.set({ [key]: session });
  const jwt = await transport.json(session, env.auth, "/v2/api/jwt", { method: "POST", body: {}, client: true });
  if (jwt.success !== true || typeof jwt.token !== "string" || !jwt.token) throw new Error("登录状态已失效，请重新登录柠檬云");
  Object.assign(session, jwtIdentity(jwt.token), { jwt: jwt.token });
  await chrome.storage.session.set({ [key]: session });
  const plans = [];
  const seen = new Set();
  // Follow every page, instead of silently dropping plans after the first ten.
  for (let page = 1; page <= 100; page++) {
    const body = await transport.json(session, env.gate, "/wb/plan", {
      query: { v: 0, currentPage: page, pageSize: 10 }, bearer: session.jwt, origin: env.web
    });
    const rows = body?.data?.data;
    if (!Array.isArray(rows)) throw new Error("套餐列表读取失败，请重新登录后再试");
    if (!rows.length) break;
    const signature = rows.map(p => p.id).join(",");
    if (seen.has(signature)) throw new Error("套餐分页重复，请检查工作台接口，未使用不完整列表");
    seen.add(signature);
    plans.push(...rows);
    if (rows.length < 10) break;
    if (page === 100) throw new Error("套餐数量超出本工具支持范围");
  }
  session.plans = selectPlans(plans, session.userId);
  session.accounts = [];
  const warnings = [];
  for (const plan of session.plans) {
    try { session.accounts.push(...await loadAccounts(session, plan)); }
    catch (error) { warnings.push(`套餐 ${plan.serviceId}：${redact(error.message, [session.cookie, session.jwt])}`); }
  }
  const current = await readLogin(context, session.cookieSource);
  if (current.value !== session.cookie || current.storeId !== session.storeId) throw new Error("读取期间登录账号发生变化，请重新读取");
  session.created = Date.now();
  await chrome.storage.session.set({ [key]: session });
  return { id: session.id, name: session.name, userId: session.userId, environment: env.label,
    plans: session.plans, accounts: session.accounts, warnings };
}

async function getSession(context, id) {
  const session = (await chrome.storage.session.get(`session:${context.tabId}`))[`session:${context.tabId}`];
  if (!session || session.id !== id || session.envKey !== context.envKey) throw new Error("工具会话已变化，请重新读取账套");
  const login = await readLogin(context, session.cookieSource);
  if (login.value !== session.cookie || login.storeId !== session.storeId) {
    await chrome.storage.session.remove(`session:${context.tabId}`);
    throw new Error("检测到退出登录或切换账号，请重新读取账套");
  }
  return session;
}

async function loadAccounts(session, plan) {
  const env = ENVIRONMENTS[session.envKey];
  const body = await transport.json(session, env.api, "/api/AccountSetOnlyAuth/TopInfoV2", {
    serviceId: plan.serviceId, query: { serviceId: plan.serviceId, appasid: plan.appAsId }, origin: env.origin
  });
  return accountRows(apiData(body), plan);
}

function findAccount(session, key) {
  const account = session.accounts.find(a => a.key === key);
  if (!account) throw new Error("账套不在当前读取的列表中，请重新读取");
  return account;
}

async function dispatch(message, sender) {
  if (message.type === "lemon-login-v1") {
    const result = await synchronize(chrome, globalThis.fetch.bind(globalThis), sender);
    if (!result.ok) throw new Error(result.error);
    return true;
  }
  if (["lemon-open", "lemon-open-window"].includes(message.type)) {
    if (sender.id !== chrome.runtime.id || sender.frameId !== 0 || !sender.tab || sender.url?.startsWith("chrome-extension:")) throw new Error("页面无效");
    pageEnvironment(sender.url);
    const url = `${panelPath}?tabId=${sender.tab.id}`;
    if (message.type === "lemon-open-window") await chrome.tabs.create({ url });
    return { url };
  }
  const context = await panelContext(sender);
  if (message.type === "read-page-cookie") return readPageCookie(chrome, context);
  if (message.type === "context") return { environment: ENVIRONMENTS[context.envKey].label };
  if (message.type === "hide") {
    await chrome.tabs.sendMessage(context.tabId, { type: "lemon-hide" });
    return true;
  }
  if (message.type === "focus-source") {
    await chrome.tabs.update(context.tabId, { active: true });
    return true;
  }
  if (message.type === "connect") return connect(context);
  if (message.type === "release") {
    const lease = (await chrome.storage.session.get(`lease:${message.id}`))[`lease:${message.id}`];
    if (lease?.tabId === context.tabId) await transport.release(message.id);
    return true;
  }
  const session = await getSession(context, message.sessionId);
  const env = ENVIRONMENTS[session.envKey];
  if (message.type === "upload-prepare") {
    const plan = session.plans.find(p => p.serviceId === message.serviceId);
    if (!plan) throw new Error("请选择已读取的目标套餐");
    return transport.prepare(session, env.importer, "/api/AccImport/ImportAccountSetFileAsyncV2", {
      serviceId: plan.serviceId, bearer: session.cookie, client: true
    });
  }
  if (message.type === "import-result") {
    if (typeof message.importId !== "string" || !message.importId.trim() || message.importId.length > 200) throw new Error("请输入有效的导入任务 ID");
    const body = await transport.json(session, env.importer, "/api/AccImport/GetImportResultV2", {
      method: "POST", body: [message.importId], bearer: session.cookie, client: true
    });
    return { result: importResult(body, message.importId) };
  }
  if (message.type === "download-status") {
    const entry = (await chrome.storage.session.get(`download:${message.downloadId}`))[`download:${message.downloadId}`];
    if (!entry || entry.sessionId !== session.id) throw new Error("下载记录已过期，请查看 Chrome 下载列表");
    const [download] = await chrome.downloads.search({ id: message.downloadId });
    if (download?.state === "complete" && /(?:text\/|json|xml)/i.test(download.mime || "")) {
      return { state: "interrupted", error: "服务端返回了网页或错误信息，请重新登录；不要将该下载文件作为备份" };
    }
    return { state: download?.state || "unknown", error: download?.error || "", bytesReceived: download?.bytesReceived, totalBytes: download?.totalBytes };
  }
  const account = findAccount(session, message.key);
  const options = { serviceId: account.serviceId, origin: env.origin, query: { appasid: account.appAsId } };
  if (message.type === "backup-start") {
    const result = apiData(await transport.json(session, env.api, "/api/Backup/ProcessBackup", {
      ...options, query: { ...options.query, allowTemporaryFile: 1, isProcess: 1 }, method: "POST", body: {}
    }));
    if (typeof result?.fileName !== "string" || !result.fileName) throw new Error("备份接口未返回文件名；请核对备份记录后再试");
    session.backups ??= {};
    session.backups[account.key] = result.fileName;
    await chrome.storage.session.set({ [`session:${session.tabId}`]: session });
    return { fileName: result.fileName };
  }
  const fileName = session.backups?.[account.key];
  if (message.type === "backup-progress") {
    if (!fileName) throw new Error("请先发起备份");
    const result = apiData(await transport.json(session, env.api, "/api/Backup/GetFileRecord", {
      ...options, query: { ...options.query, filename: fileName }, method: "POST", body: {}
    }));
    const progress = Number(result?.progress);
    if (!Number.isFinite(progress) || progress < 0) throw new Error("备份进度无效，请核对网页备份记录");
    return { progress };
  }
  if (message.type === "backup-download") {
    if (!fileName) throw new Error("请先发起备份");
    const lease = await transport.prepare(session, env.api, "/api/Backup/DownloadFile", {
      ...options, query: { ...options.query, fileName, fileType: 0 }, download: true
    });
    try {
      const id = await chrome.downloads.download({ url: lease.url,
        filename: `柠檬云备份/${safeFilename(account.name)}_${account.asId}_${new Date().toISOString().replace(/[:.]/g, "-")}.zip`,
        conflictAction: "uniquify", saveAs: false });
      await chrome.storage.session.set({ [`download:${id}`]: { tabId: session.tabId, sessionId: session.id, leaseId: lease.id } });
      return { downloadId: id };
    } catch (error) { await transport.release(lease.id); throw error; }
  }
  if (message.type === "delete") {
    if (message.confirmed !== true) throw new Error("请先确认删除清单");
    const plan = session.plans.find(p => p.serviceId === account.serviceId);
    const fresh = await loadAccounts(session, plan);
    if (!fresh.some(a => a.key === account.key && a.appAsId === account.appAsId && a.name === account.name)) throw new Error("账套信息已变化，请重新读取后确认");
    // Check the browser identity again immediately before a destructive request.
    await getSession(context, message.sessionId);
    const result = apiData(await transport.json(session, env.api, "/api/AccountSetOnlyAuth/DropAccountSetToRecycleBinV2", {
      ...options, query: { ...options.query, asId: account.asId, serviceid: account.serviceId }, method: "POST"
    }));
    if (result !== true) throw new Error("服务端未确认删除成功，请刷新网页核对");
    session.accounts = session.accounts.filter(a => a.key !== account.key);
    await chrome.storage.session.set({ [`session:${session.tabId}`]: session });
    return true;
  }
  throw new Error("不支持的操作");
}

chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  const lock = sender.tab?.id ?? sender.url;
  const serial = !["context", "hide", "lemon-open", "release", "download-status"].includes(message?.type);
  if (serial && inFlight.has(lock)) { sendResponse({ ok: false, error: "当前页面仍有请求进行中，请稍后重试" }); return false; }
  if (serial) inFlight.add(lock);
  (async () => {
    try { sendResponse({ ok: true, data: await dispatch(message, sender) }); }
    catch (error) {
      const data = await chrome.storage.session.get(null);
      const secrets = Object.entries(data).filter(([k]) => k.startsWith("session:")).flatMap(([, s]) => [s.cookie, s.jwt]);
      sendResponse({ ok: false, error: redact(error.message, secrets) });
    } finally { if (serial) inFlight.delete(lock); }
  })();
  return true;
});
