import { mkdir, readFile, writeFile } from "node:fs/promises";
import { zipSync } from "../vendor/fflate.js";
const root = new URL("../", import.meta.url);
const files = ["manifest.json", "background.js", "core.js", "transport.js", "content.js", "login-sync.js", "login-content.js", "page-cookie.js", "panel.html", "panel.css", "panel.js", "help.html", "zip.js", "zip-worker.js", "vendor/fflate.js", "vendor/fflate-LICENSE.txt", "README.md"];
const zip = {};
for (const name of files) {
  const bytes = new Uint8Array(await readFile(new URL(name, root)));
  const destination = new URL(`unpacked/${name}`, root);
  await mkdir(new URL("./", destination), { recursive: true });
  await writeFile(destination, bytes);
  zip[`LemonChromeExtension/${name}`] = bytes;
}
await writeFile(new URL("LemonChromeExtension.zip", root), zipSync(zip, { level: 6 }));
console.log("已生成 unpacked/（Chrome 加载此目录）和 LemonChromeExtension.zip");
