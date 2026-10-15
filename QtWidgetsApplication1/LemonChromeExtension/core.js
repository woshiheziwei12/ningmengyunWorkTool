export const ENVIRONMENTS = Object.freeze({
  pro: {
    label: "正式环境", auth: "https://auth.ningmengyun.com", web: "https://www.ningmengyun.com",
    gate: "https://wbgate.ningmengyun.com", api: "https://jproapi.ningmengyun.com",
    origin: "https://jpro6.ningmengyun.com", importer: "https://prodaozhangapi.ningmengyun.com"
  },
  test: {
    label: "测试环境", auth: "http://authtest.ningmengyun.com", web: "http://wwwtest2.ningmengyun.com",
    // etest advertises HSTS. Start with HTTPS so the actual network URL still
    // matches the exact per-request authentication rule after browser upgrades.
    gate: "https://etest.ningmengyun.com", api: "https://jproapitest2.ningmengyun.com",
    origin: "https://jproh5test2.ningmengyun.com", importer: "http://proimportertest2.ningmengyun.com"
  }
});

export function pageEnvironment(url) {
  const u = new URL(url);
  if (!["http:", "https:"].includes(u.protocol) ||
      !(u.hostname === "ningmengyun.com" || u.hostname.endsWith(".ningmengyun.com"))) {
    throw new Error("请在已登录的柠檬云页面打开工具");
  }
  return /test/i.test(u.hostname) ? "test" : "pro";
}

export function positiveId(value) {
  const id = Number(value);
  if (!Number.isSafeInteger(id) || id <= 0) throw new Error("账套或套餐编号无效，请重新读取列表");
  return id;
}

export function jwtIdentity(token) {
  try {
    const body = token.split(".")[1].replace(/-/g, "+").replace(/_/g, "/");
    const data = JSON.parse(new TextDecoder().decode(Uint8Array.from(atob(body), c => c.charCodeAt(0))));
    return { userId: positiveId(data.sub), name: data["http://schemas.xmlsoap.org/ws/2005/05/identity/claims/name"] || data.name || "" };
  } catch { throw new Error("登录接口返回的用户信息无效，请重新登录"); }
}

export function cookieHeader(session, serviceId) {
  if (!session.cookie || /[\r\n;]/.test(session.cookie)) throw new Error("登录 Cookie 无效");
  const parts = [`ningmengcookie=${session.cookie}`];
  if (session.userId) parts.push(`auth=${positiveId(session.userId)}`);
  if (session.name) parts.push(`authname=${encodeURIComponent(session.name)}`);
  if (serviceId) parts.push(`service-id=${positiveId(serviceId)}`);
  return parts.join("; ");
}

export function selectPlans(plans, userId) {
  const valid = plans.filter(p => p.productType === "acc" && p.isValid !== false && !p.expired && !p.Closed &&
    Number(p.id) > 0 && p.lastAccountSet?.appAsid);
  const owned = valid.filter(p => !p.administrator || Number(p.administrator) === userId);
  return [...new Map((owned.length ? owned : valid).map(p => [Number(p.id), {
    serviceId: positiveId(p.id), appAsId: String(p.lastAccountSet.appAsid),
    label: String(p.productTypeDescription || "财务套餐")
  }])).values()];
}

export function accountRows(data, plan) {
  if (!Array.isArray(data?.accountList)) throw new Error("账套列表格式不正确");
  return data.accountList.map(a => {
    if (!a.appAsId) throw new Error("账套缺少 appAsId，已停止加载");
    return {
      key: `${plan.serviceId}:${positiveId(a.asid)}`, asId: positiveId(a.asid),
      appAsId: String(a.appAsId), name: String(a.asname || a.asid),
      start: `${a.asStartYear || ""}-${String(a.asStartMonth || "").padStart(2, "0")}`,
      serviceId: plan.serviceId, plan: plan.label
    };
  });
}

export function apiData(body) {
  if (body?.state !== 1000) throw new Error(String(body?.msg || `接口返回失败（state=${body?.state ?? "未知"}）`));
  return body.data;
}

export function importData(body) {
  if (body?.Code !== 0) throw new Error(String(body?.Message || `导账接口返回失败（Code=${body?.Code ?? "未知"}）`));
  return body.Data;
}

export function importResult(body, importId) {
  const data = importData(body);
  if (!Array.isArray(data)) return data ?? null;
  // Never display another task's result as the requested task's result.
  return data.find(item => String(item.Id) === importId)?.Data ?? null;
}

export function safeFilename(name) {
  return String(name).replace(/[<>:"/\\|?*\x00-\x1f]/g, "_").replace(/[. ]+$/g, "").slice(0, 120) || "账套备份";
}

export function redact(message, secrets = []) {
  let text = String(message);
  for (const secret of secrets.filter(Boolean)) text = text.split(secret).join("[已隐藏]");
  return text.replace(/Bearer\s+[^\s"<>]+/gi, "Bearer [已隐藏]").slice(0, 500);
}
