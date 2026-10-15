export const extensionId = "nmahgegihmcfiendmbakhelgokloehgi";

export function parseHandoff(value) {
  const url = new URL(value);
  if (url.protocol !== "http:" || url.hostname !== "127.0.0.1" || !url.port
      || url.username || url.password || url.search || url.pathname !== "/lemon-login/v1/"
      || !/^#[0-9a-f]{64}$/.test(url.hash)) throw new Error("invalid_handoff");
  return { origin: url.origin, ticket: url.hash.slice(1) };
}

export function validatePayload(data) {
  if (!data || data.protocol !== 1 || typeof data.url !== "string"
      || typeof data.cookie !== "string" || !data.cookie || data.cookie.length > 16384
      || /[\x00-\x20\x7f-\uffff";,\\]/.test(data.cookie)) throw new Error("invalid_payload");
  const url = new URL(data.url);
  if (url.protocol !== "https:" || !["wbgate.ningmengyun.com", "etest.ningmengyun.com"].includes(url.hostname)
      || url.port || url.username || url.password || url.hash
      || url.pathname !== "/wb/redirect/menu" || url.search !== "?productType=acc") throw new Error("invalid_payload");
  return url;
}

export async function synchronize(browser, fetcher, sender) {
  if (sender.id !== extensionId || sender.frameId !== 0 || !Number.isInteger(sender.tab?.id)
      || sender.tab.incognito) throw new Error("invalid_sender");
  const handoff = parseHandoff(sender.url);
  // Derive the local endpoint from Chrome's sender metadata, never a message-supplied URL.
  const request = (action) => fetcher(`${handoff.origin}/lemon-login/v1/${action}`, {
    method: "POST", credentials: "omit", cache: "no-store", redirect: "error",
    headers: { Authorization: `Bearer ${handoff.ticket}`, "X-Lemon-Extension": extensionId },
    signal: AbortSignal.timeout(10000)
  });
  let claimed = false;
  let stage = "connect";
  try {
    const response = await request("claim");
    if (!response.ok) throw new Error("claim_failed");
    claimed = true;
    const payload = await response.json();
    const target = validatePayload(payload);
    stage = "cookie";
    const stores = await browser.cookies.getAllCookieStores();
    const store = stores.find(item => item.tabIds.includes(sender.tab.id));
    if (!store) throw new Error("store_not_found");
    const beforeWrite = await browser.tabs.get(sender.tab.id);
    if (beforeWrite.url !== sender.url) throw new Error("tab_changed");
    // Keep a short-lived in-memory snapshot so a failed write can restore the prior login.
    const conflicts = await browser.cookies.getAll({ url: target.href, name: "ningmengcookie", storeId: store.id });
    const affected = conflicts.filter(old => {
      const domain = old.domain.replace(/^\./, "");
      return !old.partitionKey && (domain === "ningmengyun.com" || domain === target.hostname);
    });
    try {
      // Chrome's remove may remove overlapping domain/host copies together. Clear
      // the old applicable name before writing, or it can remove the freshly set value.
      for (const old of affected) {
        await browser.cookies.remove({
          url: `https://${old.domain.replace(/^\./, "")}${old.path}`, name: "ningmengcookie", storeId: store.id
        });
      }
      const written = await browser.cookies.set({
        url: `${target.origin}/`, domain: ".ningmengyun.com", path: "/", name: "ningmengcookie",
        value: payload.cookie, secure: true, httpOnly: false, sameSite: "lax", storeId: store.id
        // Session cookie: do not invent or extend the server's token lifetime.
      });
      if (!written) throw new Error("cookie_not_written");
      const effective = await browser.cookies.get({ url: target.href, name: "ningmengcookie", storeId: store.id });
      if (!effective || effective.value !== payload.cookie) throw new Error("cookie_shadowed");
    } catch (error) {
      for (const old of affected) {
        if (old.expirationDate && old.expirationDate <= Date.now() / 1000) continue;
        const restore = {
          url: `https://${old.domain.replace(/^\./, "")}${old.path}`, name: old.name || "ningmengcookie",
          value: old.value, path: old.path, secure: old.secure, httpOnly: old.httpOnly,
          sameSite: old.sameSite, storeId: store.id
        };
        if (!old.hostOnly) restore.domain = old.domain;
        if (old.expirationDate) restore.expirationDate = old.expirationDate;
        try { await browser.cookies.set(restore); } catch { /* surface failure, never log credentials */ }
      }
      throw error;
    }
    payload.cookie = "";
    stage = "navigate";
    // Do not redirect a tab the user has navigated away from during the exchange.
    const current = await browser.tabs.get(sender.tab.id);
    if (current.url !== sender.url) throw new Error("tab_changed");
    await browser.tabs.update(sender.tab.id, { url: target.href });
    // Cookie + navigation already succeeded; a lost receipt must not retry the credential transfer.
    try { await request("complete"); } catch { /* short-lived listener may already have expired */ }
    return { ok: true };
  } catch {
    if (claimed) { try { await request("failed"); } catch { /* no credential retry */ } }
    const messages = {
      connect: "连接已失效或桌面工具未运行，请回到工具重新点击“打开官网”。",
      cookie: "登录同步未完成，已尝试保留原登录。请检查扩展的网站权限和浏览器登录状态后重试。",
      navigate: "登录 Cookie 已同步，但页面未能打开。请回到工具重新点击“打开官网”。"
    };
    return { ok: false, error: messages[stage] };
  }
}
