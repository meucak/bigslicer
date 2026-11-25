import http from 'node:http';
import https from 'node:https';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import zlib from 'node:zlib';
import net from 'node:net';
import dns from 'node:dns/promises';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { fileURLToPath } from 'node:url';

const execFileAsync = promisify(execFile);
const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const MIME_TYPES = Object.freeze({
  '.html': 'text/html; charset=utf-8',
  '.js': 'application/javascript; charset=utf-8',
  '.mjs': 'application/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.ico': 'image/x-icon',
  '.json': 'application/json; charset=utf-8',
  '.webmanifest': 'application/manifest+json; charset=utf-8',
  '.wasm': 'application/wasm',
  '.stl': 'model/stl',
  '.gcode': 'text/plain; charset=utf-8',
  '.txt': 'text/plain; charset=utf-8',
});

const COMPRESSIBLE_EXTS = new Set(['.html', '.js', '.mjs', '.css', '.svg', '.json', '.webmanifest', '.wasm', '.txt']);

export function sanitizeFilename(raw) {
  const base = path.basename(String(raw || 'model.stl'))
    .replace(/[\x00-\x1f<>:"/\\|?*]+/g, '_')
    .replace(/\s+/g, ' ')
    .trim();
  return (base.slice(0, 96) || 'model.stl');
}

export function isPrivateIp(ipRaw) {
  if (!ipRaw || typeof ipRaw !== 'string') return true;
  let ip = ipRaw.trim().toLowerCase();
  if (ip.startsWith('::ffff:')) ip = ip.slice(7);
  const v = net.isIP(ip);
  if (v === 4) {
    const p = ip.split('.').map(Number);
    if (p.length !== 4 || p.some((n) => !Number.isInteger(n) || n < 0 || n > 255)) return true;
    const [a, b] = p;
    if (a === 0 || a === 10 || a === 127 || a >= 224) return true;
    if (a === 100 && b >= 64 && b <= 127) return true;
    if (a === 169 && b === 254) return true;
    if (a === 172 && b >= 16 && b <= 31) return true;
    if (a === 192 && b === 168) return true;
    if (a === 192 && b === 0 && (p[2] === 0 || p[2] === 2)) return true;
    if (a === 198 && (b === 18 || b === 19)) return true;
    return false;
  }
  if (v === 6) {
    if (ip === '::' || ip === '::1') return true;
    const h = parseInt(ip.split(':')[0] || '0', 16);
    if ((h & 0xfe00) === 0xfc00 || (h & 0xffc0) === 0xfe80 || (h & 0xff00) === 0xff00) return true;
    return false;
  }
  return true;
}

function sendJson(res, status, obj) {
  const body = JSON.stringify(obj);
  res.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Cache-Control': 'no-store',
    'X-Content-Type-Options': 'nosniff',
  });
  res.end(body);
}

function readRequestBody(req, maxBytes) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let total = 0;
    let aborted = false;
    req.on('data', (chunk) => {
      if (aborted) return;
      total += chunk.length;
      if (total > maxBytes) {
        aborted = true;
        const err = new Error('Payload too large');
        err.statusCode = 413;
        reject(err);
        req.resume();
        return;
      }
      chunks.push(chunk);
    });
    req.on('end', () => { if (!aborted) resolve(Buffer.concat(chunks, total)); });
    req.on('error', (err) => { if (!aborted) reject(err); });
  });
}

function scanLibraryDirs(libraryDirs, maxDepth = 4, maxItems = 250) {
  const items = [];
  const walk = (rootIdx, rootLabel, baseDir, curDir, depth) => {
    if (depth > maxDepth || items.length >= maxItems) return;
    let entries;
    try { entries = fs.readdirSync(curDir, { withFileTypes: true }); } catch { return; }
    for (const ent of entries) {
      if (items.length >= maxItems) break;
      if (ent.name.startsWith('.') || ent.name === 'node_modules' || ent.name === '__MACOSX') continue;
      const full = path.join(curDir, ent.name);
      if (ent.isDirectory()) {
        walk(rootIdx, rootLabel, baseDir, full, depth + 1);
      } else if (ent.isFile() && /\.(stl|obj)$/i.test(ent.name)) {
        try {
          const st = fs.statSync(full);
          if (st.size > 0 && st.size <= 100 * 1024 * 1024) {
            const relPath = path.relative(baseDir, full).split(path.sep).join('/');
            const folder = path.dirname(relPath) === '.' ? rootLabel : `${rootLabel}/${path.dirname(relPath)}`;
            items.push({ root: rootIdx, relPath, name: ent.name, folder, bytes: st.size, mtimeMs: st.mtimeMs });
          }
        } catch {}
      }
    }
  };
  libraryDirs.forEach((entry, idx) => {
    if (entry?.dir && fs.existsSync(entry.dir)) {
      walk(idx, entry.label || `Library ${idx + 1}`, path.resolve(entry.dir), path.resolve(entry.dir), 0);
    }
  });
  items.sort((a, b) => b.mtimeMs - a.mtimeMs);
  return items;
}

async function fetchPublicUrlSafe(rawUrl, maxBytes, maxRedirects = 3) {
  let current = rawUrl;
  for (let hop = 0; hop <= maxRedirects; hop++) {
    let parsed;
    try { parsed = new URL(current); } catch {
      const err = new Error('Invalid URL'); err.statusCode = 400; throw err;
    }
    if (parsed.protocol !== 'http:' && parsed.protocol !== 'https:') {
      const err = new Error('Only http/https URLs supported'); err.statusCode = 400; throw err;
    }
    const host = parsed.hostname;
    if (host === 'localhost' || host.endsWith('.local') || host.endsWith('.lan') || host.endsWith('.internal')) {
      const err = new Error('Private hosts blocked'); err.statusCode = 403; throw err;
    }
    if (net.isIP(host)) {
      if (isPrivateIp(host)) { const err = new Error('Private IP blocked'); err.statusCode = 403; throw err; }
    } else {
      let addrs;
      try { addrs = await dns.lookup(host, { all: true }); } catch {
        const err = new Error('DNS failed'); err.statusCode = 502; throw err;
      }
      if (!addrs.length || addrs.some((a) => isPrivateIp(a.address))) {
        const err = new Error('Private resolved IP blocked'); err.statusCode = 403; throw err;
      }
    }
    const result = await new Promise((resolve, reject) => {
      const mod = parsed.protocol === 'https:' ? https : http;
      const req = mod.request(parsed, { method: 'GET', headers: { 'User-Agent': 'Katman-Slicer/1.0' }, timeout: 15000 }, (resp) => {
        const code = resp.statusCode || 500;
        if (code >= 300 && code < 400 && resp.headers.location) {
          resp.resume();
          return resolve({ redirect: new URL(resp.headers.location, parsed).toString() });
        }
        if (code < 200 || code >= 300) {
          resp.resume();
          const err = new Error(`Upstream HTTP ${code}`); err.statusCode = 502; return reject(err);
        }
        const chunks = [];
        let total = 0;
        resp.on('data', (c) => {
          total += c.length;
          if (total > maxBytes) { req.destroy(); const err = new Error('Remote STL too large'); err.statusCode = 413; return reject(err); }
          chunks.push(c);
        });
        resp.on('end', () => resolve({ buffer: Buffer.concat(chunks, total) }));
        resp.on('error', reject);
      });
      req.on('timeout', () => { req.destroy(); const err = new Error('Timeout'); err.statusCode = 504; reject(err); });
      req.on('error', (e) => { e.statusCode = 502; reject(e); });
      req.end();
    });
    if (result.redirect) { current = result.redirect; continue; }
    return result.buffer;
  }
  const err = new Error('Too many redirects'); err.statusCode = 502; throw err;
}

export function createKatmanServer(options = {}) {
  const docsDir = path.resolve(options.docsDir || path.join(__dirname, '..', 'docs'));
  const dataDir = path.resolve(options.dataDir || path.join(__dirname, '..', 'server_data'));
  const jobsDir = path.join(dataDir, 'jobs');
  fs.mkdirSync(jobsDir, { recursive: true });

  const katmanBin = options.katmanBin || process.env.BIGSLICER_BIN || process.env.KATMAN_BIN || '/usr/local/bin/bigslicer';
  const hasNative = fs.existsSync(katmanBin);
  const libraryDirs = options.libraryDirs || [];
  const maxStlBytes = options.maxStlBytes || 80 * 1024 * 1024;
  const maxJobsKept = options.maxJobsKept || 24;
  const startTime = Date.now();

  let cachedPresets = null;
  let libCache = { at: 0, items: [] };
  const jobOrder = [];

  const getLibrary = () => {
    if (Date.now() - libCache.at > 5000) {
      libCache = { at: Date.now(), items: scanLibraryDirs(libraryDirs) };
    }
    return libCache.items;
  };

  const pruneJobs = () => {
    while (jobOrder.length > maxJobsKept) {
      const oldId = jobOrder.shift();
      try { fs.rmSync(path.join(jobsDir, `${oldId}.gcode`), { force: true }); } catch {}
    }
  };

  async function runNativeSlice(stlFilePath, configObj) {
    if (!hasNative) {
      const err = new Error('Native katman binary not configured');
      err.statusCode = 503;
      throw err;
    }
    const jobId = crypto.randomBytes(8).toString('hex');
    const outGcode = path.join(jobsDir, `${jobId}.gcode`);
    const tmpBase = path.join(os.tmpdir(), `katman_${jobId}`);
    const outKtp = `${tmpBase}.ktp`;
    const outStats = `${tmpBase}.json`;
    const cfgJson = JSON.stringify(configObj || {});

    try {
      await execFileAsync(
        katmanBin,
        ['slice', stlFilePath, '-o', outGcode, '--ktp', outKtp, '--stats-json', outStats, '--config-json', cfgJson],
        { timeout: 60000, maxBuffer: 4 * 1024 * 1024 }
      );
      const statsBuf = fs.readFileSync(outStats);
      const ktpBuf = fs.readFileSync(outKtp);
      jobOrder.push(jobId);
      pruneJobs();
      return { jobId, statsBuf, ktpBuf };
    } catch (err) {
      try { fs.rmSync(outGcode, { force: true }); } catch {}
      const e = new Error(err?.stderr?.toString()?.trim() || err?.message || 'Native slicing failed');
      e.statusCode = 422;
      throw e;
    } finally {
      try { fs.rmSync(outKtp, { force: true }); } catch {}
      try { fs.rmSync(outStats, { force: true }); } catch {}
    }
  }

  function sendMultiplexedSlice(req, res, jobId, statsBuf, ktpBuf) {
    const hdr = Buffer.allocUnsafe(4);
    hdr.writeUInt32LE(statsBuf.length, 0);
    const payload = Buffer.concat([hdr, statsBuf, ktpBuf]);
    const acceptEnc = String(req.headers['accept-encoding'] || '');
    const headers = {
      'Content-Type': 'application/octet-stream',
      'X-Job-Id': jobId,
      'X-Engine': 'native-cpp20',
      'Cache-Control': 'no-store',
      'X-Content-Type-Options': 'nosniff',
    };
    if (acceptEnc.includes('gzip') && payload.length > 2048) {
      // Fast level-1 gzip compresses KTP1 by 3-4x in <4ms for snappy LAN/WAN previews!
      const gz = zlib.gzipSync(payload, { level: 1 });
      headers['Content-Encoding'] = 'gzip';
      headers['Content-Length'] = gz.length;
      res.writeHead(200, headers);
      return res.end(gz);
    }
    headers['Content-Length'] = payload.length;
    res.writeHead(200, headers);
    return res.end(payload);
  }

  const serveStatic = (req, res, filePath) => {
    const resolved = path.resolve(filePath);
    if (!resolved.startsWith(docsDir + path.sep) && resolved !== docsDir) {
      res.writeHead(403); return res.end('Forbidden');
    }
    let st;
    try { st = fs.statSync(resolved); if (!st.isFile()) throw new Error(); } catch {
      res.writeHead(404); return res.end('Not found');
    }
    const ext = path.extname(resolved).toLowerCase();
    const mime = MIME_TYPES[ext] || 'application/octet-stream';
    const isVolatile = ext === '.html' || ext === '.webmanifest' || path.basename(resolved) === 'sw.js';
    const headers = {
      'Content-Type': mime,
      'X-Content-Type-Options': 'nosniff',
      'Referrer-Policy': 'no-referrer',
      'Cache-Control': isVolatile ? 'no-cache' : 'public, max-age=300',
    };
    const acceptEnc = String(req.headers['accept-encoding'] || '');
    if (COMPRESSIBLE_EXTS.has(ext) && acceptEnc.includes('gzip') && st.size > 256) {
      headers['Content-Encoding'] = 'gzip';
      headers['Vary'] = 'Accept-Encoding';
      res.writeHead(200, headers);
      return fs.createReadStream(resolved).pipe(zlib.createGzip({ level: 6 })).pipe(res);
    }
    headers['Content-Length'] = st.size;
    res.writeHead(200, headers);
    if (req.method === 'HEAD') return res.end();
    return fs.createReadStream(resolved).pipe(res);
  };

  function resolveLibraryFile(rootIdx, rel) {
    const entry = libraryDirs[rootIdx];
    if (!entry?.dir || !rel || !/\.(stl|obj)$/i.test(rel)) return { error: 403, msg: 'Forbidden path' };
    const baseReal = fs.realpathSync(path.resolve(entry.dir));
    const cand = path.resolve(baseReal, rel);
    if (!cand.startsWith(baseReal + path.sep)) return { error: 403, msg: 'Path traversal blocked' };
    if (!fs.existsSync(cand)) return { error: 404, msg: 'File not found' };
    const realFile = fs.realpathSync(cand);
    if (!realFile.startsWith(baseReal + path.sep) || !/\.(stl|obj)$/i.test(realFile)) {
      return { error: 403, msg: 'Symlink escape blocked' };
    }
    return { realFile };
  }

  return http.createServer(async (req, res) => {
    const reqUrl = new URL(req.url || '/', `http://${req.headers.host || 'localhost'}`);
    const pathname = reqUrl.pathname;

    try {
      if (pathname === '/api/health' && req.method === 'GET') {
        return sendJson(res, 200, {
          ok: true,
          service: 'bigslicer',
          version: '1.0.0',
          nativeSlicer: hasNative,
          cpus: os.cpus().length,
          uptimeSec: Math.floor((Date.now() - startTime) / 1000),
          libraryCount: getLibrary().length,
        });
      }

      if (pathname === '/api/presets' && req.method === 'GET') {
        if (!cachedPresets && hasNative) {
          const { stdout } = await execFileAsync(katmanBin, ['profiles'], { timeout: 5000 });
          cachedPresets = stdout.trim();
        }
        if (cachedPresets) {
          res.writeHead(200, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'public, max-age=3600' });
          return res.end(cachedPresets);
        }
        return sendJson(res, 503, { ok: false, error: 'Presets unavailable' });
      }

      // Direct uploaded STL/OBJ -> Native 4-core C++20 Slicer
      if (pathname === '/api/slice' && req.method === 'POST') {
        const body = await readRequestBody(req, maxStlBytes);
        if (!body.length) return sendJson(res, 400, { ok: false, error: 'Empty model payload' });
        let cfgObj = {};
        if (req.headers['x-slicer-config']) {
          try { cfgObj = JSON.parse(decodeURIComponent(String(req.headers['x-slicer-config']))); } catch {
            return sendJson(res, 400, { ok: false, error: 'Invalid X-Slicer-Config JSON' });
          }
        }
        const fname = sanitizeFilename(decodeURIComponent(String(req.headers['x-filename'] || 'model.stl')));
        const ext = /\.obj$/i.test(fname) ? '.obj' : '.stl';
        const tmpIn = path.join(os.tmpdir(), `katman_in_${crypto.randomBytes(8).toString('hex')}${ext}`);
        try {
          fs.writeFileSync(tmpIn, body);
          const { jobId, statsBuf, ktpBuf } = await runNativeSlice(tmpIn, cfgObj);
          return sendMultiplexedSlice(req, res, jobId, statsBuf, ktpBuf);
        } finally {
          try { fs.rmSync(tmpIn, { force: true }); } catch {}
        }
      }

      // Zero-upload direct Kasa STL Archive slicing!
      if (pathname === '/api/slice/library' && req.method === 'POST') {
        const raw = await readRequestBody(req, 64 * 1024);
        let reqBody;
        try { reqBody = JSON.parse(raw.toString('utf8')); } catch {
          return sendJson(res, 400, { ok: false, error: 'Invalid JSON body' });
        }
        const resolved = resolveLibraryFile(Number(reqBody.root), String(reqBody.path || ''));
        if (resolved.error) return sendJson(res, resolved.error, { ok: false, error: resolved.msg });
        const { jobId, statsBuf, ktpBuf } = await runNativeSlice(resolved.realFile, reqBody.config || {});
        return sendMultiplexedSlice(req, res, jobId, statsBuf, ktpBuf);
      }

      // G-code download or bounded preview: GET /api/gcode/:jobId
      const gcMatch = pathname.match(/^\/api\/gcode\/([a-f0-9]{16})$/);
      if (gcMatch && req.method === 'GET') {
        const gcPath = path.join(jobsDir, `${gcMatch[1]}.gcode`);
        if (!fs.existsSync(gcPath)) return sendJson(res, 404, { ok: false, error: 'G-code job expired or not found' });
        const st = fs.statSync(gcPath);
        const previewLimit = Number(reqUrl.searchParams.get('preview') || 0);
        if (previewLimit > 0) {
          const readLen = Math.min(st.size, Math.min(previewLimit, 2 * 1024 * 1024));
          const fd = fs.openSync(gcPath, 'r');
          const buf = Buffer.allocUnsafe(readLen);
          fs.readSync(fd, buf, 0, readLen, 0);
          fs.closeSync(fd);
          res.writeHead(200, {
            'Content-Type': 'text/plain; charset=utf-8',
            'Content-Length': buf.length,
            'X-Total-Bytes': String(st.size),
            'Cache-Control': 'no-store',
          });
          return res.end(buf);
        }
        const dlName = sanitizeFilename(reqUrl.searchParams.get('name') || 'bigslicer_output.gcode').replace(/\.stl$/i, '.gcode');
        res.writeHead(200, {
          'Content-Type': 'text/plain; charset=utf-8',
          'Content-Length': st.size,
          'Content-Disposition': `attachment; filename="${dlName}"`,
          'X-Content-Type-Options': 'nosniff',
        });
        return fs.createReadStream(gcPath).pipe(res);
      }

      if (pathname === '/api/library' && req.method === 'GET') {
        return sendJson(res, 200, { ok: true, items: getLibrary() });
      }

      if (pathname === '/api/library/file' && (req.method === 'GET' || req.method === 'HEAD')) {
        const resolved = resolveLibraryFile(Number(reqUrl.searchParams.get('root')), reqUrl.searchParams.get('path') || '');
        if (resolved.error) return sendJson(res, resolved.error, { ok: false, error: resolved.msg });
        const st = fs.statSync(resolved.realFile);
        const safeName = sanitizeFilename(path.basename(resolved.realFile));
        res.writeHead(200, {
          'Content-Type': 'model/stl',
          'Content-Length': st.size,
          'Content-Disposition': `inline; filename="${safeName}"`,
          'X-Model-Name': encodeURIComponent(safeName),
          'Cache-Control': 'public, max-age=60',
        });
        if (req.method === 'HEAD') return res.end();
        return fs.createReadStream(resolved.realFile).pipe(res);
      }

      if (pathname === '/api/proxy' && req.method === 'GET') {
        const target = reqUrl.searchParams.get('url') || '';
        if (!target) return sendJson(res, 400, { ok: false, error: 'Missing url' });
        const buf = await fetchPublicUrlSafe(target, maxStlBytes);
        res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Length': buf.length, 'Cache-Control': 'no-store' });
        return res.end(buf);
      }

      if (req.method === 'GET' || req.method === 'HEAD') {
        const clean = decodeURIComponent(pathname);
        if (clean === '/' || clean === '') return serveStatic(req, res, path.join(docsDir, 'index.html'));
        const tf = path.join(docsDir, clean.replace(/^\/+/, ''));
        if (fs.existsSync(tf) && fs.statSync(tf).isFile()) return serveStatic(req, res, tf);
        if (!path.extname(clean)) return serveStatic(req, res, path.join(docsDir, 'index.html'));
        res.writeHead(404); return res.end('Not Found');
      }

      res.writeHead(405);
      res.end('Method Not Allowed');
    } catch (err) {
      if (!res.headersSent) {
        sendJson(res, err?.statusCode || 500, { ok: false, error: err?.message || 'Internal Server Error' });
      }
    }
  });
}

if (process.argv[1] && path.resolve(process.argv[1]) === __filename) {
  const port = Number(process.env.PORT || 8098);
  const libDirs = [];
  if (process.env.LIBRARY_DIRS) {
    for (const spec of process.env.LIBRARY_DIRS.split(',')) {
      const [label, dir] = spec.includes('=') ? spec.split('=') : [path.basename(spec), spec];
      if (dir && fs.existsSync(dir.trim())) libDirs.push({ label: label.trim(), dir: dir.trim() });
    }
  }
  const slicerBin = process.env.BIGSLICER_BIN || process.env.KATMAN_BIN || '/usr/local/bin/bigslicer';
  const server = createKatmanServer({
    docsDir: process.env.DOCS_DIR || path.join(__dirname, '..', 'docs'),
    dataDir: process.env.DATA_DIR || path.join(__dirname, '..', 'server_data'),
    katmanBin: slicerBin,
    libraryDirs: libDirs,
  });
  server.listen(port, '0.0.0.0', () => {
    console.log(`[bigslicer] listening on http://0.0.0.0:${port} (native=${fs.existsSync(slicerBin)}, libs=${libDirs.length})`);
  });
}
