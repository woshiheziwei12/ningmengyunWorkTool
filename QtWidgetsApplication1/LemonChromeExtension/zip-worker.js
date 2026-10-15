import { rewritePackage } from "./zip.js";
self.onmessage = event => {
  try {
    const result = rewritePackage(new Uint8Array(event.data.buffer), event.data.serviceId);
    self.postMessage({ ok: true, buffer: result.buffer }, [result.buffer]);
  } catch (error) { self.postMessage({ ok: false, error: error.message }); }
};
