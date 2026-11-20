(() => {
  if (window.top !== window || document.getElementById("lemon-cloud-tools-root")) return;
  const host = document.createElement("div");
  host.id = "lemon-cloud-tools-root";
  host.style.cssText = "position:fixed;inset:0;z-index:2147483646;pointer-events:none;";
  const shadow = host.attachShadow({ mode: "closed" });
  const style = document.createElement("style");
  style.textContent = `
    .launcher{position:fixed;right:20px;bottom:26px;display:flex;flex-direction:column;align-items:flex-end;gap:9px;width:max-content;max-width:calc(100vw - 16px);pointer-events:auto}
    button{border:0;border-radius:24px;padding:13px 20px;background:#176d54;color:white;font:600 14px system-ui;box-shadow:0 5px 24px #0003;cursor:pointer}
    button.separate{padding:7px 13px;font-size:11px;background:#fff;color:#416c58;border:1px solid #d7e5dc;box-shadow:0 2px 12px #0001}
    .panel{position:fixed;right:16px;top:16px;width:min(1000px,calc(100vw - 32px));height:min(820px,calc(100vh - 32px));display:flex;flex-direction:column;overflow:hidden;border:1px solid #d5e2dc;border-radius:18px;background:#f6f8f7;box-shadow:0 12px 60px #0004;pointer-events:auto;box-sizing:border-box}
    .drag-handle{flex:none;padding:7px 14px;background:#e8f0eb;color:#416c58;font:12px system-ui;text-align:center}
    .draggable{cursor:grab;touch-action:none;user-select:none}
    .dragging,.dragging *{cursor:grabbing!important}
    .panel.dragging iframe{pointer-events:none}
    iframe{display:block;flex:1;min-height:0;width:100%;border:0;background:#f6f8f7}
    [hidden]{display:none!important}
  `;
  const button = document.createElement("button");
  button.type = "button";
  button.textContent = "◈ 账套工具";
  button.setAttribute("aria-label", "打开柠檬云账套工具");
  button.className = "draggable";
  button.title = "点击打开，按住拖动位置";
  const launcher = document.createElement("div");
  launcher.className = "launcher";
  let frame;
  let panel;
  let opening = false;
  // Keep each surface inside the viewport, including after a window resize.
  function place(element, left, top) {
    const rect = element.getBoundingClientRect();
    const margin = 8;
    element.style.left = `${Math.max(margin, Math.min(left, window.innerWidth - rect.width - margin))}px`;
    element.style.top = `${Math.max(margin, Math.min(top, window.innerHeight - rect.height - margin))}px`;
    element.style.right = element.style.bottom = "auto";
  }
  function keepVisible(element) {
    if (!element || element.hidden) return;
    const rect = element.getBoundingClientRect();
    place(element, rect.left, rect.top);
  }
  function draggable(handle, element) {
    let drag;
    let suppressClick = false;
    handle.addEventListener("pointerdown", event => {
      if (!event.isPrimary || event.button !== 0 || drag) return;
      suppressClick = false;
      const rect = element.getBoundingClientRect();
      drag = { id: event.pointerId, x: event.clientX, y: event.clientY, left: rect.left, top: rect.top, moved: false };
      handle.setPointerCapture(event.pointerId);
    });
    handle.addEventListener("pointermove", event => {
      if (!drag || event.pointerId !== drag.id) return;
      const dx = event.clientX - drag.x, dy = event.clientY - drag.y;
      if (!drag.moved && Math.hypot(dx, dy) < 5) return;
      drag.moved = true;
      suppressClick = true;
      element.classList.add("dragging");
      place(element, drag.left + dx, drag.top + dy);
      event.preventDefault();
    });
    const finish = event => {
      if (!drag || event.pointerId !== drag.id) return;
      drag = undefined;
      element.classList.remove("dragging");
      if (handle.hasPointerCapture(event.pointerId)) handle.releasePointerCapture(event.pointerId);
    };
    for (const type of ["pointerup", "pointercancel", "lostpointercapture"]) handle.addEventListener(type, finish);
    handle.addEventListener("click", event => {
      if (suppressClick && event.detail !== 0) {
        suppressClick = false;
        event.preventDefault();
        event.stopImmediatePropagation();
      }
    }, true);
  }
  draggable(button, launcher);
  window.addEventListener("resize", () => { keepVisible(launcher); keepVisible(panel); });
  const separate = document.createElement("button");
  separate.type = "button";
  separate.className = "separate";
  separate.textContent = "独立工具页 ↗";
  separate.title = "如果网站阻止嵌入面板，可在独立标签页操作";
  separate.onclick = () => chrome.runtime.sendMessage({ type: "lemon-open-window" }).catch(() => { separate.textContent = "请刷新页面"; });
  async function toggle() {
    if (opening) return;
    if (panel) {
      panel.hidden = !panel.hidden;
      launcher.hidden = !panel.hidden;
      keepVisible(panel.hidden ? launcher : panel);
      return;
    }
    opening = true;
    try {
      const response = await chrome.runtime.sendMessage({ type: "lemon-open" });
      if (!response?.ok) throw new Error(response?.error || "请刷新页面重试");
      frame = document.createElement("iframe");
      frame.title = "柠檬云账套工具";
      frame.allow = "clipboard-write";
      frame.src = response.data.url;
      panel = document.createElement("div");
      panel.className = "panel";
      const handle = document.createElement("div");
      handle.className = "drag-handle draggable";
      handle.textContent = "⠿ 按住此处拖动面板";
      draggable(handle, panel);
      panel.append(handle, frame);
      shadow.append(panel);
      launcher.hidden = true;
    } catch { button.textContent = "扩展已更新，请刷新页面"; }
    finally { opening = false; }
  }
  button.addEventListener("click", toggle);
  chrome.runtime.onMessage.addListener(message => {
    if (message.type === "lemon-toggle") toggle();
    if (message.type === "lemon-hide" && panel) { panel.hidden = true; launcher.hidden = false; keepVisible(launcher); }
  });
  launcher.append(separate, button);
  shadow.append(style, launcher);
  document.documentElement.append(host);
})();
