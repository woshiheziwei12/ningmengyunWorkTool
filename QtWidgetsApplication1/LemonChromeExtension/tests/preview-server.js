// Offline UI fixture only. Does not load background.js or call Lemon Cloud.
import http from "node:http";
import { readFile } from "node:fs/promises";
const root = new URL("../", import.meta.url);
const allowed = new Set(["panel.html", "panel.js", "panel.css", "core.js", "zip.js", "zip-worker.js", "vendor/fflate.js", "tests/mock-browser.js"]);
const mime = { js: "text/javascript", html: "text/html", css: "text/css" };
const server = http.createServer(async (req, res) => {
  const pathname = new URL(req.url, "http://127.0.0.1").pathname.slice(1);
  if (pathname === "mock-upload" && req.method === "POST") {
    req.resume(); req.on("end", () => { res.writeHead(200, { "Content-Type": "application/json" }); res.end(JSON.stringify({ Code: 0, Data: "fixture-import-001" })); }); return;
  }
  if (!allowed.has(pathname)) { res.writeHead(404); res.end(); return; }
  try {
    let content = await readFile(new URL(pathname, root));
    if (pathname === "panel.html") content = content.toString().replace('<script type="module" src="panel.js">', '<script src="tests/mock-browser.js"></script><script type="module" src="panel.js">');
    res.writeHead(200, { "Content-Type": `${mime[pathname.split(".").at(-1)] || "text/plain"}; charset=utf-8`, "Cache-Control": "no-store" }); res.end(content);
  } catch { res.writeHead(500); res.end(); }
});
server.listen(18765, "127.0.0.1", () => console.log("Offline extension UI: http://127.0.0.1:18765/panel.html?tabId=7"));
