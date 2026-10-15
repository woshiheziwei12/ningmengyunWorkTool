import { cookieHeader, redact } from "./core.js";

const REQUEST_NAMES = {
  "/v2/api/jwt": "获取登录令牌",
  "/wb/plan": "读取套餐列表",
  "/api/AccountSetOnlyAuth/TopInfoV2": "读取账套列表",
  "/api/Backup/ProcessBackup": "发起备份",
  "/api/Backup/GetFileRecord": "查询备份进度",
  "/api/AccountSetOnlyAuth/DropAccountSetToRecycleBinV2": "删除账套",
  "/api/AccImport/GetImportResultV2": "查询导入结果"
};

// A unique URL and a session-only header rule keep package selection out of the
// browser cookie jar. No chrome.cookies.set/remove and no persistent secrets.
export class Transport {
  constructor(api = chrome, fetcher = fetch) {
    this.chrome = api;
    // Native Chromium fetch validates its Window/WorkerGlobalScope receiver.
    // Calling an unbound reference as this.fetcher() passes the Transport
    // instance instead, throwing before any HTTP request is sent.
    this.fetcher = fetcher.bind(globalThis);
  }

  async prepare(session, base, path, { query = {}, serviceId, bearer, origin, client = false, download = false } = {}) {
    const url = new URL(path, base);
    for (const [key, value] of Object.entries(query)) url.searchParams.set(key, value);
    url.searchParams.set("_lemonTool", crypto.randomUUID());
    const id = crypto.getRandomValues(new Uint32Array(1))[0] % 2000000000 + 1;
    const requestHeaders = [{ header: "Cookie", operation: "set", value: cookieHeader(session, serviceId) }];
    if (query.appasid) requestHeaders.push({ header: "appasid", operation: "set", value: String(query.appasid) });
    if (bearer) requestHeaders.push({ header: "Authorization", operation: "set", value: `Bearer ${bearer}` });
    if (client) requestHeaders.push({ header: "User-Agent", operation: "set", value: "lemonClient" });
    if (origin) requestHeaders.push(
      { header: "Origin", operation: "set", value: origin },
      { header: "Referer", operation: "set", value: `${origin}/` }
    );
    const condition = { urlFilter: `|${url.href}|`, isUrlFilterCaseSensitive: true };
    // Download API requests may not carry an initiator. Their unguessable URL
    // can only match this one request; ordinary tab traffic is excluded.
    if (download) {
      condition.tabIds = [-1];
      condition.resourceTypes = ["main_frame", "sub_frame", "other", "xmlhttprequest"];
    }
    else condition.initiatorDomains = [this.chrome.runtime.id];
    await this.chrome.storage.session.set({ [`lease:${id}`]: { expires: Date.now() + 15 * 60 * 1000, tabId: session.tabId } });
    try {
      await this.chrome.declarativeNetRequest.updateSessionRules({ addRules: [{
        id, priority: 1, condition,
        action: { type: "modifyHeaders", requestHeaders,
          responseHeaders: [{ header: "Set-Cookie", operation: "remove" }] }
      }] });
    } catch (error) {
      await this.chrome.storage.session.remove(`lease:${id}`);
      throw error;
    }
    return { id, url: url.href };
  }

  async release(id) {
    await this.chrome.declarativeNetRequest.updateSessionRules({ removeRuleIds: [id] });
    await this.chrome.storage.session.remove(`lease:${id}`);
  }

  async json(session, base, path, options = {}) {
    const endpoint = new URL(path, base);
    const label = `${REQUEST_NAMES[endpoint.pathname] || "请求接口"}（${endpoint.origin}${endpoint.pathname}）`;
    const mutation = ["/api/Backup/ProcessBackup", "/api/AccountSetOnlyAuth/DropAccountSetToRecycleBinV2"].includes(endpoint.pathname);
    const uncertain = mutation ? "；服务端可能已受理，请先核对结果再重试" : "";
    let lease;
    try {
      lease = await this.prepare(session, base, path, options);
      const response = await this.fetcher(lease.url, {
        method: options.method || "GET", credentials: "omit", redirect: "error", cache: "no-store",
        headers: { Accept: "application/json", ...(options.body !== undefined ? { "Content-Type": "application/json" } : {}) },
        body: options.body === undefined ? undefined : JSON.stringify(options.body),
        signal: AbortSignal.timeout(25000)
      });
      if ([401, 403].includes(response.status)) throw new Error("登录已失效或无操作权限，请重新登录并读取账套");
      if (!response.ok) throw new Error(`请求失败（HTTP ${response.status}）`);
      try { return await response.json(); }
      catch { throw new Error("接口未返回 JSON，请检查登录状态和环境"); }
    } catch (error) {
      let message = error.message;
      if (["TimeoutError", "AbortError"].includes(error.name)) message = `请求超时${uncertain}`;
      else if (error instanceof TypeError) {
        message = /Illegal invocation|incompatible receiver/i.test(error.message)
          ? "扩展内部请求调用异常（Illegal invocation），请重新加载新版扩展"
          : `网络请求失败，请检查该接口的站点授权、网络连接或重定向${uncertain}`;
      }
      throw new Error(`${label}：${redact(message, [session.cookie, session.jwt])}`);
    } finally { if (lease) await this.release(lease.id); }
  }

  async cleanup() {
    const data = await this.chrome.storage.session.get(null);
    const rules = await this.chrome.declarativeNetRequest.getSessionRules();
    const expired = rules.filter(r => !data[`lease:${r.id}`] || data[`lease:${r.id}`].expires < Date.now());
    if (expired.length) {
      await this.chrome.declarativeNetRequest.updateSessionRules({ removeRuleIds: expired.map(r => r.id) });
      await this.chrome.storage.session.remove(expired.map(r => `lease:${r.id}`));
    }
  }
}
