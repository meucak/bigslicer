/**
 * BigSlicer High-Performance Software Z-Buffer 3D Renderer (Canvas 2D)
 * Full-featured fallback for GPU-disabled browsers (e.g., Chromium --disable-gpu):
 *   - Full 4x4 model transform matrix (live Move / Rotate / Scale / Mirror / Place-on-Face)
 *   - Printer build plate (10mm/50mm grid, X/Y origin axes, build volume cage)
 *   - Pixel-accurate Z-buffered 3D extruded filament & support preview (all 13 feature types,
 *     5 heatmaps, hidden/isolated features, travels, Z-seam/retract markers, live brass nozzle,
 *     and translucent Ghost Shell)
 *   - Interactive 3D Transform Gizmo overlay (Move arrows, Rotate rings, Scale handles, OOB warning)
 */

import { Mat4 } from './math3d.js';
import { UpAxis } from './camera.js';
import { MATCAP_SIZE, MatcapKind, generateMatcapRgba8 } from './matcap.js';
import { pickMeshCpu } from './picking.js';
import { FEATURE_TYPES, COLOR_MODES, getNozzlePosition } from './toolpath_geometry.js';

const INV_65535 = 1.0 / 65535.0;
const FAST_TRI_LIMIT = 20000;
const FAST_MOVE_LIMIT = 32000;

function heatRampRgb(t) {
  const u = t < 0 ? 0 : (t > 1 ? 1 : t);
  let r, g, b;
  if (u < 0.25) {
    const k = u * 4.0;
    r = 0.14 + (0.12 - 0.14) * k;
    g = 0.36 + (0.78 - 0.36) * k;
    b = 0.88 + (0.82 - 0.88) * k;
  } else if (u < 0.50) {
    const k = (u - 0.25) * 4.0;
    r = 0.12 + (0.24 - 0.12) * k;
    g = 0.78 + (0.84 - 0.78) * k;
    b = 0.82 + (0.44 - 0.82) * k;
  } else if (u < 0.75) {
    const k = (u - 0.50) * 4.0;
    r = 0.24 + (0.96 - 0.24) * k;
    g = 0.84 + (0.76 - 0.84) * k;
    b = 0.44 + (0.16 - 0.44) * k;
  } else {
    const k = (u - 0.75) * 4.0;
    r = 0.96 + (0.94 - 0.96) * k;
    g = 0.76 + (0.22 - 0.76) * k;
    b = 0.16 + (0.18 - 0.16) * k;
  }
  return [(r * 255) | 0, (g * 255) | 0, (b * 255) | 0];
}

// Precompute 0..255 feature RGB table (zero per-move allocations!)
const FEAT_RGB = FEATURE_TYPES.map((f) => [
  Math.round(f.color[0] * 255),
  Math.round(f.color[1] * 255),
  Math.round(f.color[2] * 255),
]);

export class RendererCanvas2D {
  constructor(canvas) {
    this.canvas = canvas;
    this.backend = 'Canvas 2D';
    this.needsCpuVertices = true;
    this.ctx = null;
    this.width = 1;
    this.height = 1;
    this.dpr = 1;
    this.fp16Fbo = false;
    this.accumCount = 0;
    this.targetSamples = 1;
    this.hasMesh = false;
    this.hasToolpath = false;
    this.toolpath = null;
    this.modelMatrix = Mat4.identity();
    this.glInfo = 'Software Z-Buffer 3D Slicer Rasterizer';
    this.matcapTables = [];
    this.imgData = null;
    this.zbuf = null;
    this.useZBuffer = false;
  }

  init(width, height, dpr) {
    const ctx = this.canvas.getContext('2d', { alpha: false }) || this.canvas.getContext('2d');
    if (!ctx) throw new Error('Canvas 2D context is unavailable.');
    this.ctx = ctx;
    this.useZBuffer = typeof ctx.createImageData === 'function' && typeof ctx.putImageData === 'function';
    this.width = Math.max(1, width | 0);
    this.height = Math.max(1, height | 0);
    this.dpr = dpr || 1;
    for (let i = 0; i < MatcapKind.Count; i++) {
      this.matcapTables.push(generateMatcapRgba8(i));
    }
    this._allocBuffers();
    this.accumCount = 0;
  }

  _allocBuffers() {
    if (!this.useZBuffer || !this.ctx) return;
    if (!this.imgData || this.imgData.width !== this.width || this.imgData.height !== this.height) {
      this.imgData = this.ctx.createImageData(this.width, this.height);
      this.zbuf = new Float32Array(this.width * this.height);
    }
  }

  resize(width, height, dpr) {
    const w = Math.max(1, width | 0);
    const h = Math.max(1, height | 0);
    this.dpr = dpr || 1;
    if (w === this.width && h === this.height) return;
    this.width = w;
    this.height = h;
    this._allocBuffers();
    this.accumCount = 0;
  }

  setTargetSamples() {
    this.targetSamples = 1;
  }

  resetAccumulation() {
    this.accumCount = 0;
  }

  needsMoreSamples() {
    return this.accumCount < this.targetSamples;
  }

  setModelMatrix(m) {
    this.modelMatrix = m ? new Float32Array(m) : Mat4.identity();
    this.accumCount = 0;
  }

  uploadMesh(mesh) {
    this.hasMesh = !!(mesh && mesh.ok && mesh.vertices && mesh.vertices.length > 0);
    this.accumCount = 0;
  }

  uploadToolpath(tp) {
    this.toolpath = tp || null;
    this.hasToolpath = !!(tp && tp.extrudeTotal > 0);
    this.accumCount = 0;
  }

  clearToolpath() {
    this.toolpath = null;
    this.hasToolpath = false;
    this.accumCount = 0;
  }

  _projectPoint(vp, x, y, z) {
    const cw = vp[3] * x + vp[7] * y + vp[11] * z + vp[15];
    if (cw <= 1e-5) return null;
    const invW = 1.0 / cw;
    const cx = vp[0] * x + vp[4] * y + vp[8] * z + vp[12];
    const cy = vp[1] * x + vp[5] * y + vp[9] * z + vp[13];
    const cz = vp[2] * x + vp[6] * y + vp[10] * z + vp[14];
    return {
      sx: (cx * invW * 0.5 + 0.5) * this.width,
      sy: (1.0 - (cy * invW * 0.5 + 0.5)) * this.height,
      z: cz * invW,
    };
  }

  _drawLine3DCtx(ctx, vp, ax, ay, az, bx, by, bz, color, widthPx = 1.5) {
    const pa = this._projectPoint(vp, ax, ay, az);
    const pb = this._projectPoint(vp, bx, by, bz);
    if (!pa || !pb) return;
    if (pa.z > 1.05 && pb.z > 1.05) return;
    ctx.strokeStyle = color;
    ctx.lineWidth = widthPx;
    ctx.beginPath();
    ctx.moveTo(pa.sx, pa.sy);
    ctx.lineTo(pb.sx, pb.sy);
    ctx.stroke();
  }

  _drawLine3DZBuf(vp, ax, ay, az, bx, by, bz, r, g, b, a) {
    const pa = this._projectPoint(vp, ax, ay, az);
    const pb = this._projectPoint(vp, bx, by, bz);
    if (!pa || !pb) return;
    if (pa.z > 1.0 && pb.z > 1.0) return;
    if (pa.z < -1.0 && pb.z < -1.0) return;

    const w = this.width, h = this.height;
    if ((pa.sx < 0 && pb.sx < 0) || (pa.sx >= w && pb.sx >= w)) return;
    if ((pa.sy < 0 && pb.sy < 0) || (pa.sy >= h && pb.sy >= h)) return;

    const data = this.imgData.data;
    const zbuf = this.zbuf;
    const dx = pb.sx - pa.sx;
    const dy = pb.sy - pa.sy;
    const steps = Math.max(1, Math.min(Math.max(w, h) * 2, Math.ceil(Math.max(Math.abs(dx), Math.abs(dy)))));
    const invS = 1.0 / steps;
    const sx = dx * invS, sy = dy * invS, dz = (pb.z - pa.z) * invS;
    const invA = 1.0 - a;

    let x = pa.sx, y = pa.sy, z = pa.z;
    for (let i = 0; i <= steps; i++) {
      const ix = x | 0, iy = y | 0;
      if (ix >= 0 && ix < w && iy >= 0 && iy < h && z >= -1.0 && z <= 1.0) {
        const pi = iy * w + ix;
        if (z <= zbuf[pi] + 1e-4) {
          const o = pi * 4;
          data[o]     = (data[o] * invA + r * a) | 0;
          data[o + 1] = (data[o + 1] * invA + g * a) | 0;
          data[o + 2] = (data[o + 2] * invA + b * a) | 0;
        }
      }
      x += sx; y += sy; z += dz;
    }
  }

  // Thick depth-tested, shaded 3D filament bead stroke directly into the software Z-buffer!
  _drawBeadZBuf(vp, x0, y0, z0, x1, y1, z1, r, g, b, thicknessPx) {
    const cw0 = vp[3] * x0 + vp[7] * y0 + vp[11] * z0 + vp[15];
    const cw1 = vp[3] * x1 + vp[7] * y1 + vp[11] * z1 + vp[15];
    if (cw0 <= 1e-5 || cw1 <= 1e-5) return;
    const iw0 = 1.0 / cw0, iw1 = 1.0 / cw1;
    const w = this.width, h = this.height;
    const ax = ((vp[0] * x0 + vp[4] * y0 + vp[8] * z0 + vp[12]) * iw0 * 0.5 + 0.5) * w;
    const ay = (1.0 - ((vp[1] * x0 + vp[5] * y0 + vp[9] * z0 + vp[13]) * iw0 * 0.5 + 0.5)) * h;
    const az = (vp[2] * x0 + vp[6] * y0 + vp[10] * z0 + vp[14]) * iw0;

    const bx = ((vp[0] * x1 + vp[4] * y1 + vp[8] * z1 + vp[12]) * iw1 * 0.5 + 0.5) * w;
    const by = (1.0 - ((vp[1] * x1 + vp[5] * y1 + vp[9] * z1 + vp[13]) * iw1 * 0.5 + 0.5)) * h;
    const bz = (vp[2] * x1 + vp[6] * y1 + vp[10] * z1 + vp[14]) * iw1;

    if ((az > 1.0 && bz > 1.0) || (az < -1.0 && bz < -1.0)) return;
    if ((ax < -3 && bx < -3) || (ax >= w + 3 && bx >= w + 3)) return;
    if ((ay < -3 && by < -3) || (ay >= h + 3 && by >= h + 3)) return;

    const dx = bx - ax, dy = by - ay;
    const maxD = Math.max(Math.abs(dx), Math.abs(dy));
    const steps = Math.max(1, Math.min(800, Math.ceil(maxD)));
    const invS = 1.0 / steps;
    const sx = dx * invS, sy = dy * invS, dz = (bz - az) * invS;

    // Directional shading subtly distinguishes X vs Y filament directions for crisp 3D depth!
    const segLen = Math.hypot(x1 - x0, y1 - y0) || 1;
    const shade = 0.78 + 0.22 * Math.abs((x1 - x0) * 0.6 + (y1 - y0) * 0.8) / segLen;
    const cr = Math.min(255, (r * shade) | 0);
    const cg = Math.min(255, (g * shade) | 0);
    const cb = Math.min(255, (b * shade) | 0);
    const hr = Math.min(255, (cr * 1.16 + 10) | 0);
    const hg = Math.min(255, (cg * 1.16 + 10) | 0);
    const hb = Math.min(255, (cb * 1.16 + 10) | 0);
    const dr = (cr * 0.68) | 0;
    const dg = (cg * 0.68) | 0;
    const db = (cb * 0.68) | 0;

    const data = this.imgData.data;
    const zbuf = this.zbuf;
    const thick = thicknessPx >= 2;
    const extra = thicknessPx >= 3;

    let x = ax, y = ay, z = az;
    for (let i = 0; i <= steps; i++) {
      const ix = x | 0, iy = y | 0;
      if (ix >= 1 && ix < w - 1 && iy >= 1 && iy < h - 1 && z >= -1.0 && z <= 1.0) {
        const pi = iy * w + ix;
        if (z < zbuf[pi]) {
          zbuf[pi] = z;
          const o = pi * 4;
          data[o] = cr; data[o + 1] = cg; data[o + 2] = cb;
        }
        if (thick) {
          // Upper rim highlight gives cylindrical 3D bead look!
          const piTop = (iy - 1) * w + ix;
          if (z < zbuf[piTop]) {
            zbuf[piTop] = z;
            const o = piTop * 4;
            data[o] = hr; data[o + 1] = hg; data[o + 2] = hb;
          }
          const piR = pi + 1;
          if (z < zbuf[piR]) {
            zbuf[piR] = z;
            const o = piR * 4;
            data[o] = cr; data[o + 1] = cg; data[o + 2] = cb;
          }
          if (extra) {
            const piBot = (iy + 1) * w + ix;
            if (z < zbuf[piBot]) {
              zbuf[piBot] = z;
              const o = piBot * 4;
              data[o] = dr; data[o + 1] = dg; data[o + 2] = db;
            }
          }
        }
      }
      x += sx; y += sy; z += dz;
    }
  }

  _fillBackgroundZBuf(darkTheme) {
    const w = this.width, h = this.height;
    const data = this.imgData.data;
    this.zbuf.fill(2.0);
    const topR = darkTheme ? 24 : 230, topG = darkTheme ? 28 : 235, topB = darkTheme ? 37 : 242;
    const botR = darkTheme ? 14 : 184, botG = darkTheme ? 17 : 194, botB = darkTheme ? 23 : 206;
    const invH = h > 1 ? 1.0 / (h - 1) : 0;
    for (let y = 0; y < h; y++) {
      const t = y * invH;
      const st = t * t * (3.0 - 2.0 * t);
      const r = (topR + (botR - topR) * st) | 0;
      const g = (topG + (botG - topG) * st) | 0;
      const b = (topB + (botB - topB) * st) | 0;
      let o = y * w * 4;
      for (let x = 0; x < w; x++) {
        data[o] = r; data[o + 1] = g; data[o + 2] = b; data[o + 3] = 255;
        o += 4;
      }
    }
  }

  _rasterizeTriZBuf(p0, p1, p2, r, g, b, alpha, wire, wr, wg, wb, xray) {
    const w = this.width, h = this.height;
    const x0 = p0.sx, y0 = p0.sy, z0 = p0.z;
    const x1 = p1.sx, y1 = p1.sy, z1 = p1.z;
    const x2 = p2.sx, y2 = p2.sy, z2 = p2.z;

    let area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
    if (Math.abs(area) < 1e-4) return;

    let ax = x0, ay = y0, az = z0, bx = x1, by = y1, bz = z1, cx = x2, cy = y2, cz = z2;
    if (area < 0) {
      bx = x2; by = y2; bz = z2;
      cx = x1; cy = y1; cz = z1;
      area = -area;
    }

    const minX = Math.max(0, Math.floor(Math.min(ax, bx, cx)));
    const maxX = Math.min(w - 1, Math.ceil(Math.max(ax, bx, cx)));
    const minY = Math.max(0, Math.floor(Math.min(ay, by, cy)));
    const maxY = Math.min(h - 1, Math.ceil(Math.max(ay, by, cy)));
    if (minX > maxX || minY > maxY) return;

    const invArea = 1.0 / area;
    const a12 = by - cy, b12 = cx - bx;
    const a20 = cy - ay, b20 = ax - cx;
    const a01 = ay - by, b01 = bx - ax;

    const edgeEps = -1e-3;
    const px0 = minX + 0.5, py0 = minY + 0.5;
    let w0Row = (cx - bx) * (py0 - by) - (cy - by) * (px0 - bx);
    let w1Row = (ax - cx) * (py0 - cy) - (ay - cy) * (px0 - cx);
    let w2Row = (bx - ax) * (py0 - ay) - (by - ay) * (px0 - ax);

    const data = this.imgData.data;
    const zbuf = this.zbuf;
    let e0T = 0, e1T = 0, e2T = 0;
    if (wire) {
      e0T = Math.hypot(cx - bx, cy - by) * 1.15;
      e1T = Math.hypot(ax - cx, ay - cy) * 1.15;
      e2T = Math.hypot(bx - ax, by - ay) * 1.15;
    }
    const invA = 1.0 - alpha;

    for (let y = minY; y <= maxY; y++) {
      let w0 = w0Row, w1 = w1Row, w2 = w2Row;
      let pi = y * w + minX;
      for (let x = minX; x <= maxX; x++, pi++) {
        if (w0 >= edgeEps && w1 >= edgeEps && w2 >= edgeEps) {
          const z = (w0 * az + w1 * bz + w2 * cz) * invArea;
          if (z >= -1.0 && z <= 1.0) {
            if (xray) {
              const o = pi * 4;
              data[o]     = (data[o] * invA + r * alpha) | 0;
              data[o + 1] = (data[o + 1] * invA + g * alpha) | 0;
              data[o + 2] = (data[o + 2] * invA + b * alpha) | 0;
            } else if (z < zbuf[pi]) {
              zbuf[pi] = z;
              const o = pi * 4;
              if (wire && (w0 < e0T || w1 < e1T || w2 < e2T)) {
                data[o]     = (r * 0.25 + wr * 0.75) | 0;
                data[o + 1] = (g * 0.25 + wg * 0.75) | 0;
                data[o + 2] = (b * 0.25 + wb * 0.75) | 0;
              } else {
                data[o] = r; data[o + 1] = g; data[o + 2] = b;
              }
            }
          }
        }
        w0 += a12; w1 += a20; w2 += a01;
      }
      w0Row += b12; w1Row += b20; w2Row += b01;
    }
  }

  _drawPrinterBedZBuf(vp, state) {
    if (!state.showGrid) return;
    const bx = state.bedX || 220;
    const by = state.bedY || 220;
    const bz = state.bedZ || 250;
    const x0 = state.originCenter ? -bx * 0.5 : 0;
    const y0 = state.originCenter ? -by * 0.5 : 0;
    const x1 = x0 + bx;
    const y1 = y0 + by;
    const gz = -0.04;

    // Subtle build plate quad (drawn into Z-buffer so lines beneath plate occlude cleanly)
    const p00 = this._projectPoint(vp, x0, y0, gz);
    const p10 = this._projectPoint(vp, x1, y0, gz);
    const p11 = this._projectPoint(vp, x1, y1, gz);
    const p01 = this._projectPoint(vp, x0, y1, gz);
    if (p00 && p10 && p11 && p01) {
      const br = state.darkTheme ? 24 : 196;
      const bg = state.darkTheme ? 29 : 204;
      const bb = state.darkTheme ? 38 : 214;
      this._rasterizeTriZBuf(p00, p10, p11, br, bg, bb, 1.0, false, 0, 0, 0, false);
      this._rasterizeTriZBuf(p00, p11, p01, br, bg, bb, 1.0, false, 0, 0, 0, false);
    }

    const minor = state.darkTheme ? [52, 62, 82, 0.55] : [140, 150, 165, 0.50];
    const major = state.darkTheme ? [86, 104, 134, 0.78] : [100, 114, 136, 0.75];
    const border = state.darkTheme ? [66, 146, 245, 0.92] : [32, 96, 210, 0.92];
    const cage = state.darkTheme ? [62, 110, 196, 0.28] : [36, 78, 168, 0.26];

    const step = bx > 320 ? 20 : 10;
    for (let x = Math.ceil(x0 / step) * step; x <= x1 + 0.01; x += step) {
      const c = (Math.round(x) % 50 === 0) ? major : minor;
      this._drawLine3DZBuf(vp, x, y0, 0.0, x, y1, 0.0, c[0], c[1], c[2], c[3]);
    }
    for (let y = Math.ceil(y0 / step) * step; y <= y1 + 0.01; y += step) {
      const c = (Math.round(y) % 50 === 0) ? major : minor;
      this._drawLine3DZBuf(vp, x0, y, 0.0, x1, y, 0.0, c[0], c[1], c[2], c[3]);
    }

    // Bed border + origin X/Y axes
    this._drawLine3DZBuf(vp, x0, y0, 0.05, x1, y0, 0.05, ...border);
    this._drawLine3DZBuf(vp, x1, y0, 0.05, x1, y1, 0.05, ...border);
    this._drawLine3DZBuf(vp, x1, y1, 0.05, x0, y1, 0.05, ...border);
    this._drawLine3DZBuf(vp, x0, y1, 0.05, x0, y0, 0.05, ...border);
    this._drawLine3DZBuf(vp, x0, y0, 0.12, x0 + 30, y0, 0.12, 238, 70, 62, 0.95);
    this._drawLine3DZBuf(vp, x0, y0, 0.12, x0, y0 + 30, 0.12, 60, 210, 96, 0.95);

    // Subtle build volume pillars & top frame
    this._drawLine3DZBuf(vp, x0, y0, 0, x0, y0, bz, ...cage);
    this._drawLine3DZBuf(vp, x1, y0, 0, x1, y0, bz, ...cage);
    this._drawLine3DZBuf(vp, x1, y1, 0, x1, y1, bz, ...cage);
    this._drawLine3DZBuf(vp, x0, y1, 0, x0, y1, bz, ...cage);
    this._drawLine3DZBuf(vp, x0, y0, bz, x1, y0, bz, ...cage);
    this._drawLine3DZBuf(vp, x1, y0, bz, x1, y1, bz, ...cage);
    this._drawLine3DZBuf(vp, x1, y1, bz, x0, y1, bz, ...cage);
    this._drawLine3DZBuf(vp, x0, y1, bz, x0, y0, bz, ...cage);
  }

  _drawToolpathZBuf(vp, cam, state) {
    const tp = this.toolpath;
    if (!tp || !tp.layerCount || tp.extrudeTotal === 0) return;

    const maxLayer = Math.max(0, Math.min(tp.layerCount - 1, state.previewMaxLayer | 0));
    const minLayer = Math.max(0, Math.min(maxLayer, state.previewMinLayer | 0));
    const frac = Math.max(0, Math.min(1, state.previewMoveFrac ?? 1.0));
    const cm = state.colorMode | 0;
    const hl = state.highlightFeature ?? -1;
    const hidden = state.hiddenFeatures;
    const ranges = tp.ranges;
    let rmin = ranges.minSpeed, rmax = ranges.maxSpeed;
    if (cm === COLOR_MODES.FLOW) { rmin = ranges.minFlow; rmax = ranges.maxFlow; }
    else if (cm === COLOR_MODES.WIDTH) { rmin = ranges.minWidth; rmax = ranges.maxWidth; }
    else if (cm === COLOR_MODES.HEIGHT) { rmin = ranges.minHeight; rmax = ranges.maxHeight; }
    const invRange = 1.0 / Math.max(1e-4, rmax - rmin);

    const startInst = tp.layers[minLayer].extrudeFirst;
    const topLy = tp.layers[maxLayer];
    const topVisible = Math.max(0, Math.min(topLy.extrudeCount, Math.ceil(topLy.extrudeCount * frac)));
    const endInst = topLy.extrudeFirst + topVisible;
    const totalVisible = Math.max(0, endInst - startInst);
    if (totalVisible === 0) return;

    // 2-pass adaptive quality: snappy fast pass while dragging, 100% full detail pass once still!
    const fastPass = totalVisible > FAST_MOVE_LIMIT && this.accumCount === 0;
    this.targetSamples = totalVisible > FAST_MOVE_LIMIT ? 2 : 1;
    const stride = fastPass ? Math.max(2, Math.ceil(totalVisible / FAST_MOVE_LIMIT)) : 1;

    // Scale bead pixel thickness cleanly from camera distance
    const dist = cam.distance();
    const thickPx = dist < 110 ? 3 : (dist < 260 ? 2 : 1);
    const inst = tp.extrudeInstances;

    for (let idx = startInst; idx < endInst; idx += stride) {
      const b = idx * 10;
      const feat = (inst[b + 9] | 0) & 255;
      if (hidden && hidden.has(feat)) continue;

      let r, g, bl;
      if (cm === COLOR_MODES.FEATURE) {
        const fc = FEAT_RGB[feat] || FEAT_RGB[1];
        r = fc[0]; g = fc[1]; bl = fc[2];
      } else {
        let val = inst[b + 7];
        if (cm === COLOR_MODES.FLOW) val = inst[b + 8];
        else if (cm === COLOR_MODES.WIDTH) val = inst[b + 6];
        else if (cm === COLOR_MODES.HEIGHT) val = inst[b + 5];
        const hc = heatRampRgb((val - rmin) * invRange);
        r = hc[0]; g = hc[1]; bl = hc[2];
      }

      if (hl >= 0 && feat !== hl) {
        r = (r * 0.22 + 24) | 0;
        g = (g * 0.22 + 26) | 0;
        bl = (bl * 0.22 + 32) | 0;
      }

      const z = inst[b + 4];
      // Draw supports slightly bolder so they stand out crisply!
      const px = (feat === 9 || feat === 10) ? Math.max(2, thickPx) : thickPx;
      this._drawBeadZBuf(vp, inst[b], inst[b + 1], z, inst[b + 2], inst[b + 3], z, r, g, bl, px);
    }

    // Always draw active top layer at full stride=1 so top slice is razor sharp even while scrubbing!
    if (stride > 1) {
      for (let idx = topLy.extrudeFirst; idx < endInst; idx++) {
        const b = idx * 10;
        const feat = (inst[b + 9] | 0) & 255;
        if (hidden && hidden.has(feat)) continue;
        const fc = FEAT_RGB[feat] || FEAT_RGB[1];
        let r = fc[0], g = fc[1], bl = fc[2];
        if (cm !== COLOR_MODES.FEATURE) {
          let val = inst[b + 7];
          if (cm === COLOR_MODES.FLOW) val = inst[b + 8];
          else if (cm === COLOR_MODES.WIDTH) val = inst[b + 6];
          else if (cm === COLOR_MODES.HEIGHT) val = inst[b + 5];
          const hc = heatRampRgb((val - rmin) * invRange);
          r = hc[0]; g = hc[1]; bl = hc[2];
        }
        const z = inst[b + 4];
        this._drawBeadZBuf(vp, inst[b], inst[b + 1], z, inst[b + 2], inst[b + 3], z, r, g, bl, Math.max(2, thickPx));
      }
    }

    // Optional Travel Moves
    if (state.showTravels && tp.travelTotal > 0) {
      const tFirst = state.travelsAllLayers ? tp.layers[minLayer].travelFirst : topLy.travelFirst;
      const tEnd = topLy.travelFirst + topLy.travelCount;
      const tCount = Math.min(12000, Math.max(0, tEnd - tFirst));
      const tr = tp.travelInstances;
      for (let k = 0; k < tCount; k++) {
        const sb = (tFirst + k) * 10;
        const z = tr[sb + 4] + 0.05;
        this._drawLine3DZBuf(vp, tr[sb], tr[sb + 1], z, tr[sb + 2], tr[sb + 3], z, 72, 184, 255, 0.65);
      }
    }
  }

  renderFrame(mesh, cam, state) {
    const ctx = this.ctx;
    const w = this.width, h = this.height;
    let cstats = { totalChunks: 0, frustumCulled: 0, coneCulled: 0, visibleChunks: 0, visibleTris: 0, drawCalls: 1 };

    const view = cam.viewMatrix();
    const proj = cam.projMatrix(w, h);
    const vp = Mat4.multiply(proj, view);
    const modelValid = this.hasMesh && mesh && mesh.ok && mesh.vertices;
    const inPreview = state.mode === 'preview' && this.hasToolpath && this.toolpath;
    const isYUp = cam.getUpAxis() === UpAxis.YUp;

    this._allocBuffers();
    this._fillBackgroundZBuf(state.darkTheme);
    this._drawPrinterBedZBuf(vp, state);

    // 1. In Preview mode: draw Z-buffered 3D toolpaths & supports!
    if (inPreview) {
      this._drawToolpathZBuf(vp, cam, state);
    }

    // 2. Mesh (solid in Prepare mode, or optional translucent Ghost Shell in Preview mode!)
    const drawMesh = modelValid && (!inPreview || state.ghostShell);
    if (drawMesh) {
      const isGhost = inPreview && state.ghostShell;
      const useXray = isGhost || state.xray;
      const useCull = !useXray && !state.twoSided;

      const fastMesh = mesh.triCount > FAST_TRI_LIMIT && this.accumCount === 0;
      if (!inPreview) this.targetSamples = mesh.triCount > FAST_TRI_LIMIT ? 2 : 1;
      const stride = fastMesh ? Math.max(2, Math.ceil(mesh.triCount / FAST_TRI_LIMIT)) : 1;

      const verts = mesh.vertices;
      const bminX = mesh.bboxMin.x, bminY = mesh.bboxMin.y, bminZ = mesh.bboxMin.z;
      const sx = mesh.quantScale.x * INV_65535;
      const sy = mesh.quantScale.y * INV_65535;
      const sz = mesh.quantScale.z * INV_65535;
      const M = this.modelMatrix;
      const m00 = M[0], m10 = M[1], m20 = M[2];
      const m01 = M[4], m11 = M[5], m21 = M[6];
      const m02 = M[8], m12 = M[9], m22 = M[10];
      const m03 = M[12], m13 = M[13], m23 = M[14];

      const mc = Math.max(0, Math.min(MatcapKind.Count - 1, state.matcap | 0));
      const lut = this.matcapTables[mc];
      const wr = state.darkTheme ? 68 : 18;
      const wg = state.darkTheme ? 156 : 54;
      const wb = state.darkTheme ? 248 : 118;
      const ovCos = -Math.sin(((state.overhangAngle || 50) * Math.PI) / 180);

      const nVerts = mesh.triCount * 3;
      for (let vi = 0; vi < nVerts; vi += 3 * stride) {
        const i0 = vi * 4;
        const lx0 = bminX + verts[i0] * sx,     ly0 = bminY + verts[i0 + 1] * sy,  lz0 = bminZ + verts[i0 + 2] * sz;
        const lx1 = bminX + verts[i0 + 4] * sx, ly1 = bminY + verts[i0 + 5] * sy,  lz1 = bminZ + verts[i0 + 6] * sz;
        const lx2 = bminX + verts[i0 + 8] * sx, ly2 = bminY + verts[i0 + 9] * sy,  lz2 = bminZ + verts[i0 + 10] * sz;

        const x0 = m00 * lx0 + m01 * ly0 + m02 * lz0 + m03;
        const y0 = m10 * lx0 + m11 * ly0 + m12 * lz0 + m13;
        const z0 = m20 * lx0 + m21 * ly0 + m22 * lz0 + m23;
        const x1 = m00 * lx1 + m01 * ly1 + m02 * lz1 + m03;
        const y1 = m10 * lx1 + m11 * ly1 + m12 * lz1 + m13;
        const z1 = m20 * lx1 + m21 * ly1 + m22 * lz1 + m23;
        const x2 = m00 * lx2 + m01 * ly2 + m02 * lz2 + m03;
        const y2 = m10 * lx2 + m11 * ly2 + m12 * lz2 + m13;
        const z2 = m20 * lx2 + m21 * ly2 + m22 * lz2 + m23;

        const e1x = x1 - x0, e1y = y1 - y0, e1z = z1 - z0;
        const e2x = x2 - x0, e2y = y2 - y0, e2z = z2 - z0;
        const wnx = e1y * e2z - e1z * e2y;
        const wny = e1z * e2x - e1x * e2z;
        const wnz = e1x * e2y - e1y * e2x;

        let vnx = view[0] * wnx + view[4] * wny + view[8] * wnz;
        let vny = view[1] * wnx + view[5] * wny + view[9] * wnz;
        let vnz = view[2] * wnx + view[6] * wny + view[10] * wnz;
        const backFace = vnz < 0;
        if (useCull && backFace) continue;

        const p0 = this._projectPoint(vp, x0, y0, z0);
        const p1 = this._projectPoint(vp, x1, y1, z1);
        const p2 = this._projectPoint(vp, x2, y2, z2);
        if (!p0 || !p1 || !p2) continue;
        if (Math.max(p0.sx, p1.sx, p2.sx) < 0 || Math.min(p0.sx, p1.sx, p2.sx) > w) continue;
        if (Math.max(p0.sy, p1.sy, p2.sy) < 0 || Math.min(p0.sy, p1.sy, p2.sy) > h) continue;
        if (Math.min(p0.z, p1.z, p2.z) > 1.0 || Math.max(p0.z, p1.z, p2.z) < -1.0) continue;

        const nLen = Math.hypot(vnx, vny, vnz);
        if (nLen > 1e-12) { const inv = 1.0 / nLen; vnx *= inv; vny *= inv; vnz *= inv; }
        else { vnx = 0; vny = 0; vnz = 1; }
        if (backFace) { vnx = -vnx; vny = -vny; vnz = -vnz; }

        let r, g, b, a = 1.0;
        if (useXray) {
          const rim = Math.pow(Math.max(0, Math.min(1, 1.0 - Math.abs(vnz))), 1.7);
          r = state.darkTheme ? Math.min(255, (72 + 120 * rim) | 0) : Math.min(255, (32 + 64 * rim) | 0);
          g = state.darkTheme ? Math.min(255, (140 + 90 * rim) | 0) : Math.min(255, (78 + 80 * rim) | 0);
          b = state.darkTheme ? Math.min(255, (235 + 20 * rim) | 0) : Math.min(255, (156 + 60 * rim) | 0);
          a = isGhost ? Math.min(0.32, 0.08 + 0.24 * rim) : Math.min(0.82, 0.14 + 0.65 * rim);
        } else {
          const tx = Math.max(0, Math.min(MATCAP_SIZE - 1, Math.round((vnx * 0.492 + 0.5) * (MATCAP_SIZE - 1))));
          const ty = Math.max(0, Math.min(MATCAP_SIZE - 1, Math.round((0.5 - vny * 0.492) * (MATCAP_SIZE - 1))));
          const idx = (ty * MATCAP_SIZE + tx) * 4;
          r = lut[idx]; g = lut[idx + 1]; b = lut[idx + 2];
          const wLen = Math.hypot(wnx, wny, wnz);
          const upDot = wLen > 1e-12 ? (isYUp ? wny / wLen : wnz / wLen) : 0;
          if (!backFace && state.overhang && upDot < ovCos && Math.min(z0, z1, z2) > 0.3) {
            const shade = 0.42 + 0.58 * Math.max(0, Math.min(1, vnz));
            r = Math.min(255, Math.round(r * 0.25 + 242 * shade * 0.80));
            g = Math.min(255, Math.round(g * 0.25 + 58 * shade * 0.72));
            b = Math.min(255, Math.round(b * 0.25 + 44 * shade * 0.72));
          }
        }
        this._rasterizeTriZBuf(p0, p1, p2, r, g, b, a, !isGhost && !!state.wireframe, wr, wg, wb, useXray);
      }
    }

    ctx.putImageData(this.imgData, 0, 0);

    // 3. Vector Overlays drawn sharply on top: Live 3D Brass Nozzle, Seam/Retract Markers & Interactive 3D Gizmo!
    if (inPreview && state.showNozzle) {
      const maxLayer = Math.max(0, Math.min(this.toolpath.layerCount - 1, state.previewMaxLayer | 0));
      const npos = getNozzlePosition(this.toolpath, maxLayer, state.previewMoveFrac ?? 1.0);
      if (npos) {
        const rTop = 2.6, rTip = 0.25, hTip = 4.2, hBody = 8.0, segs = 10;
        const col = '#f2bd38';
        for (let i = 0; i < segs; i++) {
          const a0 = (Math.PI * 2 * i) / segs, a1 = (Math.PI * 2 * (i + 1)) / segs;
          const c0 = Math.cos(a0), s0 = Math.sin(a0), c1 = Math.cos(a1), s1 = Math.sin(a1);
          this._drawLine3DCtx(ctx, vp, npos.x + rTip * c0, npos.y + rTip * s0, npos.z + 0.1,
                              npos.x + rTop * c0, npos.y + rTop * s0, npos.z + hTip, col, 1.4 * this.dpr);
          this._drawLine3DCtx(ctx, vp, npos.x + rTop * c0, npos.y + rTop * s0, npos.z + hTip,
                              npos.x + rTop * c1, npos.y + rTop * s1, npos.z + hTip, col, 1.4 * this.dpr);
          this._drawLine3DCtx(ctx, vp, npos.x + rTop * c0, npos.y + rTop * s0, npos.z + hTip,
                              npos.x + rTop * c0, npos.y + rTop * s0, npos.z + hTip + hBody, col, 1.4 * this.dpr);
        }
      }
    }

    // Optional Z-Seam & Retraction Dots on active layer
    if (inPreview && state.showSeams && this.toolpath) {
      const maxLayer = Math.max(0, Math.min(this.toolpath.layerCount - 1, state.previewMaxLayer | 0));
      const ly = this.toolpath.layers[maxLayer];
      const tr = this.toolpath.travelInstances;
      const tEnd = ly.travelFirst + ly.travelCount;
      for (let k = ly.travelFirst; k < tEnd; k++) {
        const sb = k * 10;
        const flags = ((tr[sb + 9] | 0) >> 8) & 255;
        if (flags & 4) { // retract
          const sp = this._projectPoint(vp, tr[sb], tr[sb + 1], tr[sb + 4]);
          if (sp) {
            ctx.fillStyle = '#e0aaff';
            ctx.beginPath();
            ctx.arc(sp.sx, sp.sy, 2.8 * this.dpr, 0, Math.PI * 2);
            ctx.fill();
          }
        }
      }
    }

    // Interactive 3D Transform Gizmo (in Prepare mode) + Out-of-bounds red warning box
    if (!inPreview && state.gizmoLines && state.gizmoLines.length >= 14) {
      const gl = state.gizmoLines;
      ctx.lineCap = 'round';
      for (let i = 0; i < gl.length; i += 14) {
        const r = Math.round(gl[i + 3] * 255);
        const g = Math.round(gl[i + 4] * 255);
        const b = Math.round(gl[i + 5] * 255);
        const a = gl[i + 6];
        this._drawLine3DCtx(ctx, vp, gl[i], gl[i + 1], gl[i + 2], gl[i + 7], gl[i + 8], gl[i + 9],
                            `rgba(${r},${g},${b},${a.toFixed(2)})`, 2.4 * this.dpr);
      }
    }

    this.accumCount = Math.min(this.targetSamples, this.accumCount + 1);
    return { cstats, accumCount: this.accumCount };
  }

  pickWorldPoint(cssX, cssY, cam, mesh, state) {
    return pickMeshCpu(cssX, cssY, this.width, this.height, this.dpr, cam, mesh, state);
  }
}
