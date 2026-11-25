/**
 * Off-main-thread WebAssembly Slicer Worker (C++20 + Clipper2 compiled via Emscripten).
 * Runs whenever Kasa native server is unavailable (GitHub Pages / offline PWA mode).
 */

let wasmMod = null;
let wasmInitPromise = null;

async function ensureWasm() {
  if (wasmMod) return wasmMod;
  if (!wasmInitPromise) {
    wasmInitPromise = (async () => {
      const modFactory = (await import('../wasm/katman_wasm.js')).default;
      wasmMod = await modFactory();
      return wasmMod;
    })();
  }
  return wasmInitPromise;
}

self.addEventListener('message', async (ev) => {
  const msg = ev.data;
  if (!msg) return;

  if (msg.type === 'init') {
    try {
      const mod = await ensureWasm();
      const pPtr = mod._katman_wasm_presets_ptr();
      const presetsJson = mod.UTF8ToString(pPtr);
      self.postMessage({ type: 'ready', presets: JSON.parse(presetsJson) });
    } catch (err) {
      self.postMessage({ type: 'error', id: msg.id || 0, error: String(err?.message || err) });
    }
    return;
  }

  if (msg.type === 'slice') {
    const { id, buffer, name, config } = msg;
    try {
      const mod = await ensureWasm();
      const u8 = new Uint8Array(buffer);
      const stlPtr = mod._malloc(u8.byteLength);
      mod.HEAPU8.set(u8, stlPtr);

      const cfgStr = JSON.stringify(config || {});
      const cfgBytes = mod.lengthBytesUTF8(cfgStr) + 1;
      const cfgPtr = mod._malloc(cfgBytes);
      mod.stringToUTF8(cfgStr, cfgPtr, cfgBytes);

      const nameStr = String(name || 'model.stl');
      const nameBytes = mod.lengthBytesUTF8(nameStr) + 1;
      const namePtr = mod._malloc(nameBytes);
      mod.stringToUTF8(nameStr, namePtr, nameBytes);

      const ok = mod._katman_wasm_slice(stlPtr, u8.byteLength, namePtr, cfgPtr);
      mod._free(stlPtr);
      mod._free(cfgPtr);
      mod._free(namePtr);

      const statsJson = mod.UTF8ToString(mod._katman_wasm_stats_ptr());
      if (!ok) {
        const parsedErr = JSON.parse(statsJson);
        mod._katman_wasm_clear();
        self.postMessage({ type: 'slice-error', id, error: parsedErr.error || 'WASM slicing failed' });
        return;
      }

      const ktpPtr = mod._katman_wasm_ktp_ptr();
      const ktpLen = mod._katman_wasm_ktp_len();
      const ktpCopy = mod.HEAPU8.slice(ktpPtr, ktpPtr + ktpLen).buffer;

      const gcPtr = mod._katman_wasm_gcode_ptr();
      const gcLen = mod._katman_wasm_gcode_len();
      const gcCopy = mod.HEAPU8.slice(gcPtr, gcPtr + gcLen).buffer;

      mod._katman_wasm_clear();

      self.postMessage(
        {
          type: 'slice-done',
          id,
          engine: 'wasm-cpp20',
          stats: JSON.parse(statsJson),
          ktpBuffer: ktpCopy,
          gcodeBuffer: gcCopy,
        },
        [ktpCopy, gcCopy]
      );
    } catch (err) {
      self.postMessage({ type: 'slice-error', id, error: String(err?.message || err) });
    }
  }
});
