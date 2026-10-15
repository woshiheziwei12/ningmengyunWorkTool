(() => {
  if (window.top !== window || document.getElementById("lemon-cloud-tools-root")) return;
  const host = document.createElement("div");
  host.id = "lemon-cloud-tools-root";
  host.style.cssText = "position:fixed;inset:0;z-index:2147483646;pointer-events:none;";
  const shadow = host.attachShadow({ mode: "closed" });
  const style = document.createElement("style");
  style.textContent = `
    button{position:fixed;right:20px;bottom:26px;border:0;border-radius:24px;padding:13px 20px;background:#176d54;color:white;font:600 14px system-ui;box-shadow:0 5px 24px #0003;cursor:pointer;pointer-events:auto}
    button.separate{right:20px;bottom:79px;padding:7px 13px;font-size:11px;background:#fff;color:#416c58;border:1px solid #d7e5dc;box-shadow:0 2px 12px #0001}
    iframe{position:fixed;right:16px;top:16px;width:min(1000px,calc(100vw - 32px));height:calc(100vh - 32px);border:1px solid #d5e2dc;border-radius:18px;background:#f6f8f7;box-shadow:0 12px 60px #0004;pointer-events:auto}
    [hidden]{display:none!important}
  `;
  const button = document.createElement("button");
  button.type = "button";
  button.textContent = "◈ 账套工具";
  button.setAttribute("aria-label", "打开柠檬云账套工具");
  let frame;
  let opening = false;
  const separate = document.createElement("button");
  separate.type = "button";
  separate.className = "separate";
  separate.textContent = "独立工具页 ↗";
  separate.title = "如果网站阻止嵌入面板，可在独立标签页操作";
  separate.onclick = () => chrome.runtime.sendMessage({ type: "lemon-open-window" }).catch(() => { separate.textContent = "请刷新页面"; });
  async function toggle() {
    if (opening) return;
    if (frame) { frame.hidden = !frame.hidden; button.hidden = separate.hidden = !frame.hidden; return; }
    opening = true;
    try {
      const response = await chrome.runtime.sendMessage({ type: "lemon-open" });
      if (!response?.ok) throw new Error(response?.error || "请刷新页面重试");
      frame = document.createElement("iframe");
      frame.title = "柠檬云账套工具";
      frame.allow = "clipboard-write";
      frame.src = response.data.url;
      shadow.append(frame);
      button.hidden = separate.hidden = true;
    } catch { button.textContent = "扩展已更新，请刷新页面"; }
    finally { opening = false; }
  }
  button.addEventListener("click", toggle);
  chrome.runtime.onMessage.addListener(message => {
    if (message.type === "lemon-toggle") toggle();
    if (message.type === "lemon-hide" && frame) { frame.hidden = true; button.hidden = separate.hidden = false; }
  });
  shadow.append(style, separate, button);
  document.documentElement.append(host);
})();
