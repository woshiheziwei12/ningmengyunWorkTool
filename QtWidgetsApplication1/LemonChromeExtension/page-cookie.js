// Only the extension panel may invoke this through background.js/panelContext.
// Read the source page itself, not a different environment or a cached business session.
export async function readPageCookie(browser, context) {
  let cookie;
  try {
    const stores = await browser.cookies.getAllCookieStores();
    const store = stores.find(item => item.tabIds.includes(context.tabId));
    if (!store) throw new Error("no_store");
    cookie = await browser.cookies.get({ url: context.url, name: "ningmengcookie", storeId: store.id });
    const current = await browser.tabs.get(context.tabId);
    if (current.url !== context.url) throw new Error("page_changed");
  } catch {
    throw new Error("无法读取当前页面 Cookie，请检查扩展的网站权限，刷新页面后重试。");
  }
  if (!cookie?.value) throw new Error("当前页面没有 ningmengcookie，请先在这个浏览器页面登录柠檬云。");
  return { value: cookie.value, host: new URL(context.url).hostname, domain: cookie.domain, path: cookie.path };
}
