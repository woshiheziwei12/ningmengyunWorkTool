import { unzipSync, zipSync, strToU8 } from "./vendor/fflate.js";
import { positiveId } from "./core.js";

export const MAX_ZIP_SIZE = 128 * 1024 * 1024;
const MAX_EXPANDED_SIZE = 256 * 1024 * 1024;

// Patch the original numeric tokens, not a parse/stringify round trip: financial
// exports may contain 64-bit IDs or decimal amounts JS cannot represent exactly.
export function patchAccountSet(text, serviceId) {
  JSON.parse(text); // Validate syntax; the parsed values are deliberately unused.
  const values = { AccountSetType: 1, AAId: 0, ProServiceId: positiveId(serviceId) };
  const edits = [];
  const found = new Set();
  for (let i = 0; i < text.length; i++) {
    if (text[i] !== '"') continue;
    const start = i++;
    for (; i < text.length; i++) {
      if (text[i] === "\\") { i++; continue; }
      if (text[i] === '"') break;
    }
    const key = JSON.parse(text.slice(start, i + 1));
    if (!Object.hasOwn(values, key)) continue;
    let offset = i + 1;
    while (/\s/.test(text[offset] || "x")) offset++;
    if (text[offset] !== ":") continue;
    offset++;
    while (/\s/.test(text[offset] || "x")) offset++;
    const match = /^-?\d+(?=\s*[,}])/.exec(text.slice(offset));
    if (!match) throw new Error(`${key} 不是整数字段，未修改压缩包`);
    if (found.has(key)) throw new Error(`${key} 出现多次，无法确定目标，未修改压缩包`);
    found.add(key);
    edits.push({ start: offset, end: offset + match[0].length, value: String(values[key]) });
  }
  if (found.size !== 3) throw new Error("AccountSet.json 缺少 AccountSetType、AAId 或 ProServiceId");
  for (const edit of edits.sort((a, b) => b.start - a.start)) text = text.slice(0, edit.start) + edit.value + text.slice(edit.end);
  return text;
}

export function rewritePackage(bytes, serviceId) {
  if (!bytes.length || bytes.length > MAX_ZIP_SIZE) throw new Error("请选择不超过 128 MB 的 ZIP；更大的文件请使用桌面工具");
  let expanded = 0;
  const names = new Set();
  const files = unzipSync(bytes, { filter(entry) {
    expanded += entry.originalSize;
    if (expanded > MAX_EXPANDED_SIZE) throw new Error("ZIP 解压后超过 256 MB，请使用桌面工具");
    if (names.has(entry.name)) throw new Error("ZIP 存在同名条目，未修改压缩包");
    if (entry.name.split(/[\\/]/).includes("..") || /^[\\/]/.test(entry.name)) throw new Error("ZIP 包含无效文件路径");
    names.add(entry.name);
    return true;
  } });
  const candidates = Object.keys(files).filter(name => /(^|[\\/])AccountSet\.json$/i.test(name));
  if (candidates.length !== 1) throw new Error("ZIP 中必须恰好有一个 AccountSet.json");
  const name = candidates[0];
  const text = new TextDecoder("utf-8", { fatal: true }).decode(files[name]);
  files[name] = strToU8(patchAccountSet(text, serviceId));
  return zipSync(files, { level: 1 });
}
