/**
 * Smart Dual-Engine Slicer Client:
 *   1. Native 4-Core C++20 Backend (Kasa Docker POST /api/slice -> ~30-90 ms!)
 *   2. Automatic Offline WebAssembly Worker fallback (exact same C++20 + Clipper2 core!)
 */

export class SlicerClient {
  constructor() {
    this.serverMode = false;
    this.worker = null;
    this.workerReady = false;
    this.reqSeq = 0;
    this.pending = new Map();
    this.lastJobId = null;
    this.lastLocalGcodeBlob = null;
    this.lastEngine = 'detecting';
  }

  async detectBackend() {
    try {
      const resp = await fetch('./api/health', { cache: 'no-store' });
      if (resp.ok) {
        const j = await resp.json();
        if (j && j.ok && j.nativeSlicer) {
          this.serverMode = true;
          this.lastEngine = 'native-cpp20';
          return { serverMode: true, health: j };
        }
      }
    } catch {}
    this.serverMode = false;
    this.lastEngine = 'wasm-cpp20';
    return { serverMode: false, health: null };
  }

  async loadPresets() {
    if (this.serverMode) {
      try {
        const r = await fetch('./api/presets');
        if (r.ok) return await r.json();
      } catch {}
    }
    await this._ensureWorker();
    return this._wasmPresets || null;
  }

  async _ensureWorker() {
    if (this.workerReady && this.worker) return;
    if (this._workerBootPromise) return this._workerBootPromise;

    this._workerBootPromise = new Promise((resolve, reject) => {
      try {
        this.worker = new Worker('./js/slicer_worker.js', { type: 'module' });
        this.worker.addEventListener('message', (ev) => {
          const d = ev.data;
          if (!d) return;
          if (d.type === 'ready') {
            this.workerReady = true;
            this._wasmPresets = d.presets;
            resolve();
            return;
          }
          const cb = this.pending.get(d.id);
          if (!cb) return;
          this.pending.delete(d.id);
          if (d.type === 'slice-done') cb.resolve(d);
          else cb.reject(new Error(d.error || 'WASM error'));
        });
        this.worker.addEventListener('error', (e) => {
          reject(new Error(e.message || 'WASM Worker failed'));
        });
        this.worker.postMessage({ type: 'init' });
      } catch (err) {
        reject(err);
      }
    });
    return this._workerBootPromise;
  }

  /**
   * Slice an STL ArrayBuffer (or Kasa archive reference!) using native C++20 backend or WASM fallback.
   * Returns { engine, stats, ktpBuffer, jobId, gcodeBlob? }
   */
  async slice({ buffer, name, config, libraryRef = null }) {
    this.lastJobId = null;
    this.lastLocalGcodeBlob = null;

    // 1. Try Kasa Native 4-Core C++20 Server first!
    if (this.serverMode) {
      try {
        let resp;
        if (libraryRef && !buffer) {
          resp = await fetch('./api/slice/library', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ root: libraryRef.root, path: libraryRef.relPath, config }),
          });
        } else {
          resp = await fetch('./api/slice', {
            method: 'POST',
            headers: {
              'Content-Type': 'application/octet-stream',
              'X-Filename': encodeURIComponent(name || 'model.stl'),
              'X-Slicer-Config': encodeURIComponent(JSON.stringify(config || {})),
            },
            body: buffer,
          });
        }

        if (resp.ok) {
          const jobId = resp.headers.get('x-job-id') || null;
          const engine = resp.headers.get('x-engine') || 'native-cpp20';
          const ab = await resp.arrayBuffer();
          const dv = new DataView(ab);
          const jsonLen = dv.getUint32(0, true);
          const jsonBytes = new Uint8Array(ab, 4, jsonLen);
          const stats = JSON.parse(new TextDecoder().decode(jsonBytes));
          const ktpBuffer = ab.slice(4 + jsonLen);
          this.lastJobId = jobId;
          this.lastEngine = engine;
          return { engine, stats, ktpBuffer, jobId };
        }
        const errJson = await resp.json().catch(() => ({}));
        throw new Error(errJson.error || `Native slice HTTP ${resp.status}`);
      } catch (err) {
        // Only fall back to WASM on network/unavailable errors, rethrow real model errors
        if (buffer && /Failed to fetch|NetworkError|502|503/i.test(String(err?.message || ''))) {
          this.serverMode = false;
        } else {
          throw err;
        }
      }
    }

    // 2. Offline / GitHub Pages WASM Worker (exact same C++20 + Clipper2 engine!)
    if (!buffer) throw new Error('STL buffer required for offline WASM slicing');
    await this._ensureWorker();
    const id = ++this.reqSeq;
    const copy = buffer.slice(0);
    const res = await new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.worker.postMessage({ type: 'slice', id, buffer: copy, name, config }, [copy]);
    });
    this.lastEngine = 'wasm-cpp20';
    this.lastLocalGcodeBlob = new Blob([res.gcodeBuffer], { type: 'text/plain;charset=utf-8' });
    return {
      engine: 'wasm-cpp20',
      stats: res.stats,
      ktpBuffer: res.ktpBuffer,
      jobId: null,
      gcodeBlob: this.lastLocalGcodeBlob,
    };
  }

  async fetchGcodeText(maxBytes = 512 * 1024) {
    if (this.lastLocalGcodeBlob) {
      const slice = this.lastLocalGcodeBlob.slice(0, maxBytes);
      return { text: await slice.text(), truncated: this.lastLocalGcodeBlob.size > maxBytes, totalBytes: this.lastLocalGcodeBlob.size };
    }
    if (this.lastJobId) {
      const r = await fetch(`./api/gcode/${this.lastJobId}?preview=${maxBytes}`);
      if (!r.ok) throw new Error('G-code preview expired');
      const totalBytes = Number(r.headers.get('x-total-bytes') || 0);
      const text = await r.text();
      return { text, truncated: totalBytes > text.length, totalBytes };
    }
    throw new Error('No sliced G-code available yet');
  }

  async downloadGcode(modelName = 'model') {
    const cleanBase = String(modelName || 'model').replace(/\.(stl|obj)$/i, '').replace(/[^\w.-]+/g, '_');
    const fname = `${cleanBase}.gcode`;
    if (this.lastLocalGcodeBlob) {
      const url = URL.createObjectURL(this.lastLocalGcodeBlob);
      const a = document.createElement('a');
      a.href = url;
      a.download = fname;
      a.click();
      setTimeout(() => URL.revokeObjectURL(url), 5000);
      return;
    }
    if (this.lastJobId) {
      const a = document.createElement('a');
      a.href = `./api/gcode/${this.lastJobId}?download=1&name=${encodeURIComponent(fname)}`;
      a.download = fname;
      a.click();
      return;
    }
    throw new Error('Slice a model first before exporting G-code');
  }
}
