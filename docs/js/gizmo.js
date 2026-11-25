/**
 * BigSlicer 3D Interactive Transform Gizmo & Place-on-Face Engine
 * Works identically across WebGL2, WebGL1, and Canvas 2D backends!
 *
 * Matches C++20 `transform_and_arrange_mesh` (src/core/mesh.cpp) to exact sub-micron parity:
 *   1. Scale + Mirror around mesh local bbox center
 *   2. Rotation R = Rz * Ry * Rx
 *   3. Drop minZ to Z = 0
 *   4. Translate XY center to (bedCenterX + posX, bedCenterY + posY)
 */

import { Mat4 } from './math3d.js';

export const GIZMO_MODES = Object.freeze({
  SELECT: 'select',
  MOVE: 'move',
  ROTATE: 'rotate',
  SCALE: 'scale',
  FACE: 'face',
});

const DEG2RAD = Math.PI / 180;
const RAD2DEG = 180 / Math.PI;
const INV_65535 = 1.0 / 65535.0;

export function createDefaultTransform() {
  return {
    posX: 0,
    posY: 0,
    rotX: 0,
    rotY: 0,
    rotZ: 0,
    scaleX: 1.0,
    scaleY: 1.0,
    scaleZ: 1.0,
    uniformScale: true,
    mirrorX: false,
    mirrorY: false,
  };
}

export function buildRot3x3(rxDeg, ryDeg, rzDeg) {
  const rx = rxDeg * DEG2RAD, ry = ryDeg * DEG2RAD, rz = rzDeg * DEG2RAD;
  const sx = Math.sin(rx), cx = Math.cos(rx);
  const sy = Math.sin(ry), cy = Math.cos(ry);
  const sz = Math.sin(rz), cz = Math.cos(rz);
  // R = Rz * Ry * Rx (row-major 3x3)
  return [
    cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx,
    sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx,
    -sy,     cy * sx,                cy * cx,
  ];
}

export function computeModelTransform(mesh, xform, bedX, bedY, bedZ, originCenter = false) {
  if (!mesh || !mesh.ok) {
    return {
      matrix: Mat4.identity(),
      bboxMin: { x: 0, y: 0, z: 0 },
      bboxMax: { x: 0, y: 0, z: 0 },
      extent: { x: 0, y: 0, z: 0 },
      center: { x: bedX * 0.5, y: bedY * 0.5, z: 10 },
      radius: 25,
      fitsBed: true,
    };
  }

  const cx = (mesh.bboxMin.x + mesh.bboxMax.x) * 0.5;
  const cy = (mesh.bboxMin.y + mesh.bboxMax.y) * 0.5;
  const cz = (mesh.bboxMin.z + mesh.bboxMax.z) * 0.5;

  const sx = Math.max(0.05, xform.scaleX || 1) * (xform.mirrorX ? -1 : 1);
  const sy = Math.max(0.05, xform.scaleY || 1) * (xform.mirrorY ? -1 : 1);
  const sz = Math.max(0.05, xform.scaleZ || 1);

  const R = buildRot3x3(xform.rotX || 0, xform.rotY || 0, xform.rotZ || 0);
  // Linear 3x3 M = R * diag(sx, sy, sz)
  const m00 = R[0] * sx, m01 = R[1] * sy, m02 = R[2] * sz;
  const m10 = R[3] * sx, m11 = R[4] * sy, m12 = R[5] * sz;
  const m20 = R[6] * sx, m21 = R[7] * sy, m22 = R[8] * sz;

  // Evaluate exact rotated bounds over mesh vertices (subsampled cleanly for huge meshes) to drop minZ accurately to Z=0!
  let minX = Infinity, minY = Infinity, minZ = Infinity;
  let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;
  const v = mesh.vertices;
  if (v && mesh.triCount > 0) {
    const bminX = mesh.bboxMin.x, bminY = mesh.bboxMin.y, bminZ = mesh.bboxMin.z;
    const qx = mesh.quantScale.x * INV_65535;
    const qy = mesh.quantScale.y * INV_65535;
    const qz = mesh.quantScale.z * INV_65535;
    const step = mesh.triCount > 30000 ? Math.max(1, Math.floor(mesh.triCount / 15000)) * 3 : 1;
    const nVerts = mesh.triCount * 3;
    for (let i = 0; i < nVerts; i += step) {
      const o = i * 4;
      const lx = bminX + v[o] * qx - cx;
      const ly = bminY + v[o + 1] * qy - cy;
      const lz = bminZ + v[o + 2] * qz - cz;
      const wx = m00 * lx + m01 * ly + m02 * lz;
      const wy = m10 * lx + m11 * ly + m12 * lz;
      const wz = m20 * lx + m21 * ly + m22 * lz;
      if (wx < minX) minX = wx; if (wx > maxX) maxX = wx;
      if (wy < minY) minY = wy; if (wy > maxY) maxY = wy;
      if (wz < minZ) minZ = wz; if (wz > maxZ) maxZ = wz;
    }
  } else {
    const xs = [mesh.bboxMin.x - cx, mesh.bboxMax.x - cx];
    const ys = [mesh.bboxMin.y - cy, mesh.bboxMax.y - cy];
    const zs = [mesh.bboxMin.z - cz, mesh.bboxMax.z - cz];
    for (const x of xs) for (const y of ys) for (const z of zs) {
      const wx = m00 * x + m01 * y + m02 * z;
      const wy = m10 * x + m11 * y + m12 * z;
      const wz = m20 * x + m21 * y + m22 * z;
      if (wx < minX) minX = wx; if (wx > maxX) maxX = wx;
      if (wy < minY) minY = wy; if (wy > maxY) maxY = wy;
      if (wz < minZ) minZ = wz; if (wz > maxZ) maxZ = wz;
    }
  }

  const midX = (minX + maxX) * 0.5;
  const midY = (minY + maxY) * 0.5;
  const bedCx = originCenter ? 0 : bedX * 0.5;
  const bedCy = originCenter ? 0 : bedY * 0.5;
  const tx = bedCx + (xform.posX || 0) - midX;
  const ty = bedCy + (xform.posY || 0) - midY;
  const tz = -minZ;

  // Column-major 4x4: T(tx, ty, tz) * M * T(-cx, -cy, -cz)
  const ox = tx - (m00 * cx + m01 * cy + m02 * cz);
  const oy = ty - (m10 * cx + m11 * cy + m12 * cz);
  const oz = tz - (m20 * cx + m21 * cy + m22 * cz);

  const matrix = new Float32Array([
    m00, m10, m20, 0,
    m01, m11, m21, 0,
    m02, m12, m22, 0,
    ox,  oy,  oz,  1,
  ]);

  const wMin = { x: minX + tx, y: minY + ty, z: 0 };
  const wMax = { x: maxX + tx, y: maxY + ty, z: maxZ - minZ };
  const ext = { x: wMax.x - wMin.x, y: wMax.y - wMin.y, z: wMax.z - wMin.z };
  const bMinX = originCenter ? -bedX * 0.5 : 0;
  const bMaxX = originCenter ?  bedX * 0.5 : bedX;
  const bMinY = originCenter ? -bedY * 0.5 : 0;
  const bMaxY = originCenter ?  bedY * 0.5 : bedY;
  const fitsBed = (
    wMin.x >= bMinX - 0.5 && wMax.x <= bMaxX + 0.5 &&
    wMin.y >= bMinY - 0.5 && wMax.y <= bMaxY + 0.5 &&
    wMax.z <= (bedZ || 250) + 0.5
  );

  return {
    matrix,
    bboxMin: wMin,
    bboxMax: wMax,
    extent: ext,
    center: { x: (wMin.x + wMax.x) * 0.5, y: (wMin.y + wMax.y) * 0.5, z: wMax.z * 0.5 },
    radius: Math.max(18, Math.hypot(ext.x, ext.y, ext.z) * 0.55),
    fitsBed,
  };
}

export function invertMat4(m) {
  const out = new Float32Array(16);
  const a00 = m[0], a01 = m[1], a02 = m[2], a03 = m[3];
  const a10 = m[4], a11 = m[5], a12 = m[6], a13 = m[7];
  const a20 = m[8], a21 = m[9], a22 = m[10], a23 = m[11];
  const a30 = m[12], a31 = m[13], a32 = m[14], a33 = m[15];

  const b00 = a00 * a11 - a01 * a10, b01 = a00 * a12 - a02 * a10;
  const b02 = a00 * a13 - a03 * a10, b03 = a01 * a12 - a02 * a11;
  const b04 = a01 * a13 - a03 * a11, b05 = a02 * a13 - a03 * a12;
  const b06 = a20 * a31 - a21 * a30, b07 = a20 * a32 - a22 * a30;
  const b08 = a20 * a33 - a23 * a30, b09 = a21 * a32 - a22 * a31;
  const b10 = a21 * a33 - a23 * a31, b11 = a22 * a33 - a23 * a32;

  let det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
  if (Math.abs(det) < 1e-12) return null;
  det = 1.0 / det;

  out[0]  = (a11 * b11 - a12 * b10 + a13 * b09) * det;
  out[1]  = (a02 * b10 - a01 * b11 - a03 * b09) * det;
  out[2]  = (a31 * b05 - a32 * b04 + a33 * b03) * det;
  out[3]  = (a22 * b04 - a21 * b05 - a23 * b03) * det;
  out[4]  = (a12 * b08 - a10 * b11 - a13 * b07) * det;
  out[5]  = (a00 * b11 - a02 * b08 + a03 * b07) * det;
  out[6]  = (a32 * b02 - a30 * b05 - a33 * b01) * det;
  out[7]  = (a20 * b05 - a22 * b02 + a23 * b01) * det;
  out[8]  = (a10 * b10 - a11 * b08 + a13 * b06) * det;
  out[9]  = (a01 * b08 - a00 * b10 - a03 * b06) * det;
  out[10] = (a30 * b04 - a31 * b02 + a33 * b00) * det;
  out[11] = (a21 * b02 - a20 * b04 - a23 * b00) * det;
  out[12] = (a11 * b07 - a10 * b09 - a12 * b06) * det;
  out[13] = (a00 * b09 - a01 * b07 + a02 * b06) * det;
  out[14] = (a31 * b01 - a30 * b03 - a32 * b00) * det;
  out[15] = (a20 * b03 - a21 * b01 + a22 * b00) * det;
  return out;
}

export function screenToWorldRay(cssX, cssY, cssW, cssH, vp) {
  const invVP = invertMat4(vp);
  if (!invVP) return null;
  const ndcX = (cssX / Math.max(1, cssW)) * 2.0 - 1.0;
  const ndcY = 1.0 - (cssY / Math.max(1, cssH)) * 2.0;

  const unproj = (z) => {
    const x = invVP[0] * ndcX + invVP[4] * ndcY + invVP[8] * z + invVP[12];
    const y = invVP[1] * ndcX + invVP[5] * ndcY + invVP[9] * z + invVP[13];
    const zv = invVP[2] * ndcX + invVP[6] * ndcY + invVP[10] * z + invVP[14];
    const w = invVP[3] * ndcX + invVP[7] * ndcY + invVP[11] * z + invVP[15];
    const iw = Math.abs(w) > 1e-12 ? 1.0 / w : 1.0;
    return { x: x * iw, y: y * iw, z: zv * iw };
  };

  const p0 = unproj(-1.0);
  const p1 = unproj(1.0);
  const dx = p1.x - p0.x, dy = p1.y - p0.y, dz = p1.z - p0.z;
  const len = Math.hypot(dx, dy, dz) || 1;
  return { origin: p0, dir: { x: dx / len, y: dy / len, z: dz / len } };
}

export function intersectRayPlane(ray, planePoint, planeNormal) {
  if (!ray) return null;
  const denom = ray.dir.x * planeNormal.x + ray.dir.y * planeNormal.y + ray.dir.z * planeNormal.z;
  if (Math.abs(denom) < 1e-6) return null;
  const vx = planePoint.x - ray.origin.x;
  const vy = planePoint.y - ray.origin.y;
  const vz = planePoint.z - ray.origin.z;
  const t = (vx * planeNormal.x + vy * planeNormal.y + vz * planeNormal.z) / denom;
  if (t < 0) return null;
  return {
    t,
    x: ray.origin.x + ray.dir.x * t,
    y: ray.origin.y + ray.dir.y * t,
    z: ray.origin.z + ray.dir.z * t,
  };
}

export function intersectRayAabb(ray, bmin, bmax) {
  if (!ray) return null;
  let tmin = -Infinity, tmax = Infinity;
  const axes = ['x', 'y', 'z'];
  for (const a of axes) {
    const d = ray.dir[a];
    const o = ray.origin[a];
    if (Math.abs(d) < 1e-8) {
      if (o < bmin[a] || o > bmax[a]) return null;
    } else {
      let t1 = (bmin[a] - o) / d;
      let t2 = (bmax[a] - o) / d;
      if (t1 > t2) { const tmp = t1; t1 = t2; t2 = tmp; }
      if (t1 > tmin) tmin = t1;
      if (t2 < tmax) tmax = t2;
      if (tmin > tmax) return null;
    }
  }
  return tmax >= 0 ? Math.max(0, tmin) : null;
}

// Moller-Trumbore exact world-space raycast against transformed mesh (used for Place-on-Face & direct pick!)
export function raycastMeshWorld(ray, mesh, modelMatrix) {
  if (!ray || !mesh || !mesh.ok || !mesh.vertices) return null;
  const invM = invertMat4(modelMatrix);
  if (!invM) return null;

  // Transform ray into mesh local space
  const ox = invM[0] * ray.origin.x + invM[4] * ray.origin.y + invM[8] * ray.origin.z + invM[12];
  const oy = invM[1] * ray.origin.x + invM[5] * ray.origin.y + invM[9] * ray.origin.z + invM[13];
  const oz = invM[2] * ray.origin.x + invM[6] * ray.origin.y + invM[10] * ray.origin.z + invM[14];
  let dx = invM[0] * ray.dir.x + invM[4] * ray.dir.y + invM[8] * ray.dir.z;
  let dy = invM[1] * ray.dir.x + invM[5] * ray.dir.y + invM[9] * ray.dir.z;
  let dz = invM[2] * ray.dir.x + invM[6] * ray.dir.y + invM[10] * ray.dir.z;
  const dl = Math.hypot(dx, dy, dz) || 1;
  dx /= dl; dy /= dl; dz /= dl;

  const localRay = { origin: { x: ox, y: oy, z: oz }, dir: { x: dx, y: dy, z: dz } };
  if (intersectRayAabb(localRay, mesh.bboxMin, mesh.bboxMax) === null) return null;

  const v = mesh.vertices;
  const bminX = mesh.bboxMin.x, bminY = mesh.bboxMin.y, bminZ = mesh.bboxMin.z;
  const qx = mesh.quantScale.x * INV_65535;
  const qy = mesh.quantScale.y * INV_65535;
  const qz = mesh.quantScale.z * INV_65535;

  let bestT = Infinity;
  let bestLn = null;

  for (let t = 0; t < mesh.triCount; t++) {
    const b = t * 12;
    const x0 = bminX + v[b] * qx, y0 = bminY + v[b + 1] * qy, z0 = bminZ + v[b + 2] * qz;
    const e1x = bminX + v[b + 4] * qx - x0, e1y = bminY + v[b + 5] * qy - y0, e1z = bminZ + v[b + 6] * qz - z0;
    const e2x = bminX + v[b + 8] * qx - x0, e2y = bminY + v[b + 9] * qy - y0, e2z = bminZ + v[b + 10] * qz - z0;

    const hx = dy * e2z - dz * e2y;
    const hy = dz * e2x - dx * e2z;
    const hz = dx * e2y - dy * e2x;
    const a = e1x * hx + e1y * hy + e1z * hz;
    if (Math.abs(a) < 1e-8) continue;
    const f = 1.0 / a;
    const sx = ox - x0, sy = oy - y0, sz = oz - z0;
    const u = f * (sx * hx + sy * hy + sz * hz);
    if (u < 0 || u > 1) continue;
    const rx = sy * e1z - sz * e1y;
    const ry = sz * e1x - sx * e1z;
    const rz = sx * e1y - sy * e1x;
    const w = f * (dx * rx + dy * ry + dz * rz);
    if (w < 0 || u + w > 1) continue;
    const dist = f * (e2x * rx + e2y * ry + e2z * rz);
    if (dist > 1e-5 && dist < bestT) {
      bestT = dist;
      const nx = e1y * e2z - e1z * e2y;
      const ny = e1z * e2x - e1x * e2z;
      const nz = e1x * e2y - e1y * e2x;
      bestLn = { x: nx, y: ny, z: nz };
    }
  }

  if (!bestLn) return null;

  // Transform hit point & normal back to world space
  const lx = ox + dx * bestT, ly = oy + dy * bestT, lz = oz + dz * bestT;
  const wx = modelMatrix[0] * lx + modelMatrix[4] * ly + modelMatrix[8] * lz + modelMatrix[12];
  const wy = modelMatrix[1] * lx + modelMatrix[5] * ly + modelMatrix[9] * lz + modelMatrix[13];
  const wz = modelMatrix[2] * lx + modelMatrix[6] * ly + modelMatrix[10] * lz + modelMatrix[14];

  // Normal matrix = transpose(inverse(M)) upper 3x3
  let wnx = invM[0] * bestLn.x + invM[1] * bestLn.y + invM[2] * bestLn.z;
  let wny = invM[4] * bestLn.x + invM[5] * bestLn.y + invM[6] * bestLn.z;
  let wnz = invM[8] * bestLn.x + invM[9] * bestLn.y + invM[10] * bestLn.z;
  const nl = Math.hypot(wnx, wny, wnz) || 1;
  wnx /= nl; wny /= nl; wnz /= nl;

  return { point: { x: wx, y: wy, z: wz }, normal: { x: wnx, y: wny, z: wnz } };
}

/**
 * Compute new Euler angles (rotX, rotY, rotZ) that rotate the clicked world-space face normal
 * smoothly onto (0, 0, -1) so the selected face sits completely flat on Z = 0!
 */
export function computePlaceOnFaceEuler(currentXform, worldNormal) {
  let nx = worldNormal.x, ny = worldNormal.y, nz = worldNormal.z;
  const nl = Math.hypot(nx, ny, nz) || 1;
  nx /= nl; ny /= nl; nz /= nl;

  // Target vector t = (0, 0, -1); find shortest-arc rotation matrix D mapping n -> t
  const dot = -nz; // dot(n, (0, 0, -1))
  let D;
  if (dot > 0.99999) {
    return { rotX: currentXform.rotX, rotY: currentXform.rotY, rotZ: currentXform.rotZ };
  } else if (dot < -0.99999) {
    // 180 deg flip around X axis
    D = [1, 0, 0, 0, -1, 0, 0, 0, -1];
  } else {
    // Axis = n x (0, 0, -1) = (-ny, nx, 0)
    let ax = -ny, ay = nx, az = 0;
    const al = Math.hypot(ax, ay) || 1;
    ax /= al; ay /= al;
    const c = dot;
    const s = Math.sqrt(Math.max(0, 1 - c * c));
    const k = 1 - c;
    D = [
      c + ax * ax * k,     ax * ay * k - az * s, ax * az * k + ay * s,
      ay * ax * k + az * s, c + ay * ay * k,     ay * az * k - ax * s,
      az * ax * k - ay * s, az * ay * k + ax * s, c + az * az * k,
    ];
  }

  // Combine R_new = D * R_old
  const Rold = buildRot3x3(currentXform.rotX || 0, currentXform.rotY || 0, currentXform.rotZ || 0);
  const Rn = new Array(9).fill(0);
  for (let r = 0; r < 3; r++) {
    for (let c = 0; c < 3; c++) {
      Rn[r * 3 + c] = D[r * 3 + 0] * Rold[0 + c] + D[r * 3 + 1] * Rold[3 + c] + D[r * 3 + 2] * Rold[6 + c];
    }
  }

  // Extract intrinsic ZYX Euler (matching R = Rz * Ry * Rx)
  const sy = Math.max(-1, Math.min(1, -Rn[6]));
  const ry = Math.asin(sy);
  let rx, rz;
  if (Math.abs(Math.cos(ry)) > 1e-5) {
    rx = Math.atan2(Rn[7], Rn[8]);
    rz = Math.atan2(Rn[3], Rn[0]);
  } else {
    rx = Math.atan2(-Rn[5], Rn[4]);
    rz = 0;
  }
  const snapZero = (d) => (Math.abs(d) < 0.05 ? 0 : Math.round(d * 10) / 10);
  return {
    rotX: snapZero(rx * RAD2DEG),
    rotY: snapZero(ry * RAD2DEG),
    rotZ: snapZero(rz * RAD2DEG),
  };
}

function projectToScreen(vp, p, cssW, cssH) {
  const cx = vp[0] * p.x + vp[4] * p.y + vp[8] * p.z + vp[12];
  const cy = vp[1] * p.x + vp[5] * p.y + vp[9] * p.z + vp[13];
  const cw = vp[3] * p.x + vp[7] * p.y + vp[11] * p.z + vp[15];
  if (cw <= 1e-5) return null;
  return {
    x: (cx / cw * 0.5 + 0.5) * cssW,
    y: (1.0 - (cy / cw * 0.5 + 0.5)) * cssH,
  };
}

function distPtSeg2D(px, py, ax, ay, bx, by) {
  const dx = bx - ax, dy = by - ay;
  const l2 = dx * dx + dy * dy;
  const t = l2 > 1e-6 ? Math.max(0, Math.min(1, ((px - ax) * dx + (py - ay) * dy) / l2)) : 0;
  return Math.hypot(px - (ax + t * dx), py - (ay + t * dy));
}

/**
 * Hit-test interactive 3D gizmo handles in screen space!
 * Returns: 'x' | 'y' | 'z' | 'xy' | 'uniform' | null
 */
export function hitTestGizmo(cssX, cssY, cssW, cssH, vp, mode, center, radius) {
  if (mode === GIZMO_MODES.SELECT || mode === GIZMO_MODES.FACE) return null;
  const r = Math.max(18, radius);
  const p0 = projectToScreen(vp, center, cssW, cssH);
  if (!p0) return null;

  if (mode === GIZMO_MODES.MOVE) {
    const px = projectToScreen(vp, { x: center.x + r, y: center.y, z: center.z }, cssW, cssH);
    const py = projectToScreen(vp, { x: center.x, y: center.y + r, z: center.z }, cssW, cssH);
    const pxy = projectToScreen(vp, { x: center.x + r * 0.36, y: center.y + r * 0.36, z: center.z }, cssW, cssH);
    let best = null, bestD = 16;
    if (pxy && Math.hypot(cssX - pxy.x, cssY - pxy.y) < 18) return 'xy';
    if (px) { const d = distPtSeg2D(cssX, cssY, p0.x, p0.y, px.x, px.y); if (d < bestD) { bestD = d; best = 'x'; } }
    if (py) { const d = distPtSeg2D(cssX, cssY, p0.x, p0.y, py.x, py.y); if (d < bestD) { bestD = d; best = 'y'; } }
    return best;
  }

  if (mode === GIZMO_MODES.SCALE) {
    if (Math.hypot(cssX - p0.x, cssY - p0.y) < 18) return 'uniform';
    const px = projectToScreen(vp, { x: center.x + r, y: center.y, z: center.z }, cssW, cssH);
    const py = projectToScreen(vp, { x: center.x, y: center.y + r, z: center.z }, cssW, cssH);
    const pz = projectToScreen(vp, { x: center.x, y: center.y, z: center.z + r }, cssW, cssH);
    let best = null, bestD = 16;
    if (px) { const d = distPtSeg2D(cssX, cssY, p0.x, p0.y, px.x, px.y); if (d < bestD) { bestD = d; best = 'x'; } }
    if (py) { const d = distPtSeg2D(cssX, cssY, p0.x, p0.y, py.x, py.y); if (d < bestD) { bestD = d; best = 'y'; } }
    if (pz) { const d = distPtSeg2D(cssX, cssY, p0.x, p0.y, pz.x, pz.y); if (d < bestD) { bestD = d; best = 'z'; } }
    return best;
  }

  if (mode === GIZMO_MODES.ROTATE) {
    const segs = 40;
    const testRing = (axis) => {
      let minD = Infinity;
      let prev = null;
      for (let i = 0; i <= segs; i++) {
        const a = (i / segs) * Math.PI * 2;
        const ca = Math.cos(a) * r, sa = Math.sin(a) * r;
        const wp = axis === 'x'
          ? { x: center.x, y: center.y + ca, z: center.z + sa }
          : axis === 'y'
            ? { x: center.x + ca, y: center.y, z: center.z + sa }
            : { x: center.x + ca, y: center.y + sa, z: center.z };
        const sp = projectToScreen(vp, wp, cssW, cssH);
        if (prev && sp) {
          const d = distPtSeg2D(cssX, cssY, prev.x, prev.y, sp.x, sp.y);
          if (d < minD) minD = d;
        }
        prev = sp;
      }
      return minD;
    };
    let best = null, bestD = 14;
    for (const ax of ['z', 'x', 'y']) {
      const d = testRing(ax);
      if (d < bestD) { bestD = d; best = ax; }
    }
    return best;
  }
  return null;
}

/**
 * Emit flat colored 3D line segments [x0,y0,z0, r,g,b,a, x1,y1,z1, r,g,b,a, ...]
 * for rendering the active 3D Gizmo + Out-Of-Bounds Warning Box in ANY backend!
 */
export function buildGizmoLineVertices(mode, xformInfo, activeAxis, hoverAxis) {
  const out = [];
  if (!xformInfo) return out;
  const { center, radius, bboxMin, bboxMax, fitsBed } = xformInfo;
  const r = Math.max(18, radius);

  const seg = (a, b, c) => out.push(a[0], a[1], a[2], c[0], c[1], c[2], c[3], b[0], b[1], b[2], c[0], c[1], c[2], c[3]);

  // If model overflows build plate, draw vivid crimson warning bounding box!
  if (!fitsBed) {
    const wc = [0.98, 0.24, 0.22, 0.95];
    const x0 = bboxMin.x, y0 = bboxMin.y, z0 = bboxMin.z + 0.1;
    const x1 = bboxMax.x, y1 = bboxMax.y, z1 = bboxMax.z;
    seg([x0, y0, z0], [x1, y0, z0], wc); seg([x1, y0, z0], [x1, y1, z0], wc);
    seg([x1, y1, z0], [x0, y1, z0], wc); seg([x0, y1, z0], [x0, y0, z0], wc);
    seg([x0, y0, z1], [x1, y0, z1], wc); seg([x1, y0, z1], [x1, y1, z1], wc);
    seg([x1, y1, z1], [x0, y1, z1], wc); seg([x0, y1, z1], [x0, y0, z1], wc);
    seg([x0, y0, z0], [x0, y0, z1], wc); seg([x1, y0, z0], [x1, y0, z1], wc);
    seg([x1, y1, z0], [x1, y1, z1], wc); seg([x0, y1, z0], [x0, y1, z1], wc);
  }

  if (mode === GIZMO_MODES.SELECT || mode === GIZMO_MODES.FACE) return out;

  const colOf = (ax, base) => {
    if (activeAxis === ax) return [1.0, 0.95, 0.40, 1.0];
    if (hoverAxis === ax) return [Math.min(1, base[0] + 0.22), Math.min(1, base[1] + 0.22), Math.min(1, base[2] + 0.22), 1.0];
    return base;
  };

  const cX = colOf('x', [0.96, 0.28, 0.26, 0.96]);
  const cY = colOf('y', [0.22, 0.86, 0.42, 0.96]);
  const cZ = colOf('z', [0.26, 0.58, 0.98, 0.96]);
  const cXY = colOf('xy', [0.98, 0.72, 0.18, 0.92]);
  const cU = colOf('uniform', [0.96, 0.96, 0.98, 0.96]);
  const O = [center.x, center.y, center.z];

  const drawCube = (p, hs, col) => {
    const [x, y, z] = p;
    seg([x - hs, y - hs, z - hs], [x + hs, y - hs, z - hs], col);
    seg([x + hs, y - hs, z - hs], [x + hs, y + hs, z - hs], col);
    seg([x + hs, y + hs, z - hs], [x - hs, y + hs, z - hs], col);
    seg([x - hs, y + hs, z - hs], [x - hs, y - hs, z - hs], col);
    seg([x - hs, y - hs, z + hs], [x + hs, y - hs, z + hs], col);
    seg([x + hs, y - hs, z + hs], [x + hs, y + hs, z + hs], col);
    seg([x + hs, y + hs, z + hs], [x - hs, y + hs, z + hs], col);
    seg([x - hs, y + hs, z + hs], [x - hs, y - hs, z + hs], col);
    seg([x - hs, y - hs, z - hs], [x - hs, y - hs, z + hs], col);
    seg([x + hs, y - hs, z - hs], [x + hs, y - hs, z + hs], col);
    seg([x + hs, y + hs, z - hs], [x + hs, y + hs, z + hs], col);
    seg([x - hs, y + hs, z - hs], [x - hs, y + hs, z + hs], col);
  };

  if (mode === GIZMO_MODES.MOVE) {
    const ah = r * 0.16, aw = r * 0.08;
    // X Arrow
    const tipX = [center.x + r, center.y, center.z];
    seg(O, tipX, cX);
    seg(tipX, [center.x + r - ah, center.y + aw, center.z], cX);
    seg(tipX, [center.x + r - ah, center.y - aw, center.z], cX);
    seg(tipX, [center.x + r - ah, center.y, center.z + aw], cX);
    seg(tipX, [center.x + r - ah, center.y, center.z - aw], cX);
    // Y Arrow
    const tipY = [center.x, center.y + r, center.z];
    seg(O, tipY, cY);
    seg(tipY, [center.x + aw, center.y + r - ah, center.z], cY);
    seg(tipY, [center.x - aw, center.y + r - ah, center.z], cY);
    seg(tipY, [center.x, center.y + r - ah, center.z + aw], cY);
    seg(tipY, [center.x, center.y + r - ah, center.z - aw], cY);
    // XY Plane Drag Square
    const sq = r * 0.36;
    seg([center.x + sq * 0.35, center.y + sq * 0.35, center.z], [center.x + sq, center.y + sq * 0.35, center.z], cXY);
    seg([center.x + sq, center.y + sq * 0.35, center.z], [center.x + sq, center.y + sq, center.z], cXY);
    seg([center.x + sq, center.y + sq, center.z], [center.x + sq * 0.35, center.y + sq, center.z], cXY);
    seg([center.x + sq * 0.35, center.y + sq, center.z], [center.x + sq * 0.35, center.y + sq * 0.35, center.z], cXY);
  } else if (mode === GIZMO_MODES.SCALE) {
    const hs = r * 0.075;
    const tipX = [center.x + r, center.y, center.z];
    const tipY = [center.x, center.y + r, center.z];
    const tipZ = [center.x, center.y, center.z + r];
    seg(O, tipX, cX); drawCube(tipX, hs, cX);
    seg(O, tipY, cY); drawCube(tipY, hs, cY);
    seg(O, tipZ, cZ); drawCube(tipZ, hs, cZ);
    drawCube(O, hs * 1.35, cU);
  } else if (mode === GIZMO_MODES.ROTATE) {
    const segs = 48;
    const drawRing = (axis, col) => {
      for (let i = 0; i < segs; i++) {
        const a0 = (i / segs) * Math.PI * 2;
        const a1 = ((i + 1) / segs) * Math.PI * 2;
        const c0 = Math.cos(a0) * r, s0 = Math.sin(a0) * r;
        const c1 = Math.cos(a1) * r, s1 = Math.sin(a1) * r;
        if (axis === 'x') seg([center.x, center.y + c0, center.z + s0], [center.x, center.y + c1, center.z + s1], col);
        else if (axis === 'y') seg([center.x + c0, center.y, center.z + s0], [center.x + c1, center.y, center.z + s1], col);
        else seg([center.x + c0, center.y + s0, center.z], [center.x + c1, center.y + s1, center.z], col);
      }
    };
    drawRing('x', cX);
    drawRing('y', cY);
    drawRing('z', cZ);
  }

  return out;
}
