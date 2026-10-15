(() => {
  if (!document.querySelector('meta[name="lemon-login-bridge"][content="1"]')) return;
  if (!/^#[0-9a-f]{64}$/.test(location.hash)) return;
  const status = document.getElementById("status");
  if (!status) return;
  status.textContent = "已连接扩展，正在同步登录账号…";
  chrome.runtime.sendMessage({ type: "lemon-login-v1" }).then(result => {
    if (!result?.ok) status.textContent = result?.error || "同步失败，请从桌面工具重新打开官网。";
  }).catch(() => { status.textContent = "扩展连接中断，请从桌面工具重新打开官网。"; });
})();
