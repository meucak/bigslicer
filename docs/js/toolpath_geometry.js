/**
 * Zero-Copy KTP1 Binary Toolpath Decoder & Instanced GPU Buffer Builder.
 *
 * KTP1 Binary Layout (written by C++20 / WASM katman engine):
 *   [0..63]   Header (64 bytes):
 *               0:  magic "KTP1"
 *               4:  u32 layerCount
 *               8:  u32 totalMoves
 *               12: f32[3] bboxMin
 *               24: f32[3] bboxMax
 *               36: f32[3] bedSize
 *               48: f32 totalTimeSec
 *               52: f32 totalFilamentMm
 *               56: f32 totalWeightG
 *               60: f32 maxFlowMm3s
 *   [64..]    Layer table (layerCount * 16 bytes):
 *               f32 zTop, f32 height, u32 firstMove, u32 moveCount
 *   [...]     Move table (totalMoves * 24 bytes):
 *               f32 x0, f32 y0, f32 x1, f32 y1, u16 widthUm, u16 speedDmm, u16 flowCmm3, u8 type, u8 flags
 */

export const FEATURE_TYPES = Object.freeze([
  { id: 0,  key: 'travel',      nameEn: 'Travel',            nameTr: 'Boş Hareket',      color: [0.35, 0.72, 0.98], hex: '#59b8fa' },
  { id: 1,  key: 'outerWall',   nameEn: 'Outer Wall',        nameTr: 'Dış Duvar',        color: [0.96, 0.45, 0.14], hex: '#f57324' },
  { id: 2,  key: 'innerWall',   nameEn: 'Inner Wall',        nameTr: 'İç Duvar',         color: [0.96, 0.76, 0.18], hex: '#f5c22e' },
  { id: 3,  key: 'overhang',    nameEn: 'Overhang Wall',     nameTr: 'Sarkma Duvarı',    color: [0.16, 0.52, 0.98], hex: '#2985fa' },
  { id: 4,  key: 'gapFill',     nameEn: 'Arachne / Gap Fill',nameTr: 'Arachne / Boşluk', color: [0.95, 0.95, 0.96], hex: '#f2f2f5' },
  { id: 5,  key: 'topSolid',    nameEn: 'Top Surface',       nameTr: 'Üst Yüzey',        color: [0.92, 0.24, 0.28], hex: '#eb3d47' },
  { id: 6,  key: 'bottomSolid', nameEn: 'Bottom Surface',    nameTr: 'Alt Yüzey',        color: [0.22, 0.80, 0.62], hex: '#38cc9e' },
  { id: 7,  key: 'infill',      nameEn: 'Sparse Infill',     nameTr: 'İç Dolgu',         color: [0.76, 0.28, 0.36], hex: '#c2475c' },
  { id: 8,  key: 'bridge',      nameEn: 'Bridge',            nameTr: 'Köprü',            color: [0.20, 0.85, 0.95], hex: '#33d9f2' },
  { id: 9,  key: 'support',     nameEn: 'Support',           nameTr: 'Destek',           color: [0.26, 0.82, 0.36], hex: '#42d15c' },
  { id: 10, key: 'supportIf',   nameEn: 'Support Interface', nameTr: 'Destek Arayüzü',   color: [0.12, 0.58, 0.32], hex: '#1f9452' },
  { id: 11, key: 'skirtBrim',   nameEn: 'Skirt / Brim',      nameTr: 'Etek / Brim',      color: [0.18, 0.70, 0.66], hex: '#2eb3a8' },
  { id: 12, key: 'ironing',     nameEn: 'Ironing',           nameTr: 'Ütüleme',          color: [0.85, 0.64, 0.98], hex: '#d9a3fa' },
]);

export const COLOR_MODES = Object.freeze({
  FEATURE: 0,
  SPEED: 1,
  FLOW: 2,
  WIDTH: 3,
  HEIGHT: 4,
});

/**
 * Decode a KTP1 ArrayBuffer into GPU-ready flat typed arrays (zero per-move JS objects!).
 * Instance stride = 9 floats (36 bytes):
 *   [0..3]: x0, y0, x1, y1
 *   [4..7]: zTop, height, width, speed
 *   [8]:    packed (featureType | (flags << 8)) as float OR separate flow + meta -> use 10 floats (40 bytes):
 *   [0..3]: x0, y0, x1, y1
 *   [4..7]: zTop, height, width, speed
 *   [8..9]: flow, meta (featureType + flags * 256)
 */
export function decodeKtpBuffer(arrayBuffer) {
  if (!arrayBuffer || arrayBuffer.byteLength < 64) {
    throw new Error('Invalid KTP1 toolpath buffer');
  }
  const dv = new DataView(arrayBuffer);
  if (
    dv.getUint8(0) !== 0x4b || // 'K'
    dv.getUint8(1) !== 0x54 || // 'T'
    dv.getUint8(2) !== 0x50 || // 'P'
    dv.getUint8(3) !== 0x31    // '1'
  ) {
    throw new Error('Bad KTP1 magic header');
  }

  const layerCount = dv.getUint32(4, true);
  const totalMoves = dv.getUint32(8, true);
  const bboxMin = { x: dv.getFloat32(12, true), y: dv.getFloat32(16, true), z: dv.getFloat32(20, true) };
  const bboxMax = { x: dv.getFloat32(24, true), y: dv.getFloat32(28, true), z: dv.getFloat32(32, true) };
  const bedSize = { x: dv.getFloat32(36, true), y: dv.getFloat32(40, true), z: dv.getFloat32(44, true) };
  const totalTimeSec = dv.getFloat32(48, true);
  const totalFilamentMm = dv.getFloat32(52, true);
  const totalWeightG = dv.getFloat32(56, true);
  const maxFlowMm3s = dv.getFloat32(60, true);

  const layerTableOff = 64;
  const moveTableOff = layerTableOff + layerCount * 16;
  const expectedBytes = moveTableOff + totalMoves * 24;
  if (arrayBuffer.byteLength < expectedBytes) {
    throw new Error('Truncated KTP1 buffer');
  }

  const layers = new Array(layerCount);
  let minHeight = Infinity, maxHeight = 0;
  for (let i = 0; i < layerCount; i++) {
    const o = layerTableOff + i * 16;
    const zTop = dv.getFloat32(o, true);
    const height = dv.getFloat32(o + 4, true);
    const firstMove = dv.getUint32(o + 8, true);
    const moveCount = dv.getUint32(o + 12, true);
    if (height < minHeight) minHeight = height;
    if (height > maxHeight) maxHeight = height;
    layers[i] = { zTop, height, firstMove, moveCount, extrudeFirst: 0, extrudeCount: 0, travelFirst: 0, travelCount: 0 };
  }
  if (!Number.isFinite(minHeight)) { minHeight = 0.2; maxHeight = 0.2; }

  // Partition into TWO flat GPU instance arrays (Extrusions & Travels) ordered strictly by layer
  // so rendering any [minLayer..maxLayer] + within-layer move count is a SINGLE O(1) drawArraysInstanced call!
  let extrudeTotal = 0;
  let travelTotal = 0;
  for (let i = 0; i < totalMoves; i++) {
    const ftype = dv.getUint8(moveTableOff + i * 24 + 22);
    if (ftype === 0) travelTotal++;
    else extrudeTotal++;
  }

  const STRIDE = 10; // floats per instance
  const extrudeInstances = new Float32Array(extrudeTotal * STRIDE);
  const travelInstances = new Float32Array(travelTotal * STRIDE);

  let eCursor = 0;
  let tCursor = 0;
  let minSpeed = Infinity, maxSpeed = 0;
  let minWidth = Infinity, maxWidth = 0;
  let maxFlow = Math.max(1.0, maxFlowMm3s);

  for (let li = 0; li < layerCount; li++) {
    const ly = layers[li];
    ly.extrudeFirst = eCursor;
    ly.travelFirst = tCursor;
    const mEnd = ly.firstMove + ly.moveCount;
    for (let mi = ly.firstMove; mi < mEnd; mi++) {
      const o = moveTableOff + mi * 24;
      const x0 = dv.getFloat32(o, true);
      const y0 = dv.getFloat32(o + 4, true);
      const x1 = dv.getFloat32(o + 8, true);
      const y1 = dv.getFloat32(o + 12, true);
      const width = dv.getUint16(o + 16, true) * 0.001;
      const speed = dv.getUint16(o + 18, true) * 0.1;
      const flow = dv.getUint16(o + 20, true) * 0.01;
      const ftype = dv.getUint8(o + 22);
      const flags = dv.getUint8(o + 23);

      if (ftype === 0) {
        const b = tCursor * STRIDE;
        travelInstances[b + 0] = x0;
        travelInstances[b + 1] = y0;
        travelInstances[b + 2] = x1;
        travelInstances[b + 3] = y1;
        travelInstances[b + 4] = ly.zTop;
        travelInstances[b + 5] = ly.height;
        travelInstances[b + 6] = 0.14;
        travelInstances[b + 7] = speed;
        travelInstances[b + 8] = 0.0;
        travelInstances[b + 9] = flags * 256;
        tCursor++;
      } else {
        if (speed > 0 && speed < minSpeed) minSpeed = speed;
        if (speed > maxSpeed) maxSpeed = speed;
        if (width > 0 && width < minWidth) minWidth = width;
        if (width > maxWidth) maxWidth = width;
        if (flow > maxFlow) maxFlow = flow;
        const b = eCursor * STRIDE;
        extrudeInstances[b + 0] = x0;
        extrudeInstances[b + 1] = y0;
        extrudeInstances[b + 2] = x1;
        extrudeInstances[b + 3] = y1;
        extrudeInstances[b + 4] = ly.zTop;
        extrudeInstances[b + 5] = ly.height;
        extrudeInstances[b + 6] = width;
        extrudeInstances[b + 7] = speed;
        extrudeInstances[b + 8] = flow;
        extrudeInstances[b + 9] = ftype + flags * 256;
        eCursor++;
      }
    }
    ly.extrudeCount = eCursor - ly.extrudeFirst;
    ly.travelCount = tCursor - ly.travelFirst;
  }

  if (!Number.isFinite(minSpeed)) { minSpeed = 10; maxSpeed = 100; }
  if (!Number.isFinite(minWidth)) { minWidth = 0.3; maxWidth = 0.6; }

  return {
    layerCount,
    totalMoves,
    extrudeTotal,
    travelTotal,
    bboxMin,
    bboxMax,
    bedSize,
    totalTimeSec,
    totalFilamentMm,
    totalWeightG,
    layers,
    extrudeInstances,
    travelInstances,
    ranges: {
      minSpeed,
      maxSpeed: Math.max(minSpeed + 1, maxSpeed),
      minFlow: 0,
      maxFlow,
      minWidth,
      maxWidth: Math.max(minWidth + 0.05, maxWidth),
      minHeight,
      maxHeight: Math.max(minHeight + 0.02, maxHeight),
    },
  };
}

/**
 * Build a 12-triangle unit 3D stadium/hexagonal filament bead mesh for hardware instancing!
 * Local coords: u in [0, 1] along segment, v in [-0.5, 0.5] across width, w in [-1, 0] height below zTop.
 * Returns Float32Array of (pos.xyz, normal.xyz) * 36 vertices (6 floats/vert).
 */
export function buildUnitBeadGeometry() {
  // 6-point stadium-like cross section (v, w) + outward normals (nv, nw)
  const profile = [
    [-0.36,  0.00],  // 0: top-left
    [ 0.36,  0.00],  // 1: top-right
    [ 0.50, -0.50],  // 2: mid-right
    [ 0.36, -1.00],  // 3: bot-right
    [-0.36, -1.00],  // 4: bot-left
    [-0.50, -0.50],  // 5: mid-left
  ];

  const verts = [];
  const pushV = (u, v, w, nu, nv, nw) => verts.push(u, v, w, nu, nv, nw);

  for (let i = 0; i < 6; i++) {
    const a = profile[i];
    const b = profile[(i + 1) % 6];
    // Outward normal in (v, w) plane
    const dv = b[0] - a[0];
    const dw = b[1] - a[1];
    // Cross(tangent +u, edge (dv, dw)) -> (-dw, dv) or (dw, -dv); pick outward from center (0, -0.5)
    let nv = -dw;
    let nw = dv;
    const mv = 0.5 * (a[0] + b[0]);
    const mw = 0.5 * (a[1] + b[1]) + 0.5;
    if (nv * mv + nw * mw < 0) { nv = -nv; nw = -nw; }
    const len = Math.hypot(nv, nw) || 1;
    nv /= len; nw /= len;

    // Quad along u = [0..1] -> 2 triangles
    pushV(0, a[0], a[1], 0, nv, nw);
    pushV(1, a[0], a[1], 0, nv, nw);
    pushV(1, b[0], b[1], 0, nv, nw);

    pushV(0, a[0], a[1], 0, nv, nw);
    pushV(1, b[0], b[1], 0, nv, nw);
    pushV(0, b[0], b[1], 0, nv, nw);
  }

  return new Float32Array(verts);
}

/**
 * Evaluate exact head nozzle (x, y, z) for layerIndex and moveFraction [0..1]
 */
export function getNozzlePosition(toolpath, maxLayerIdx, moveProgress01) {
  if (!toolpath || toolpath.layerCount === 0) return null;
  const li = Math.max(0, Math.min(toolpath.layerCount - 1, maxLayerIdx | 0));
  const ly = toolpath.layers[li];
  if (!ly || ly.extrudeCount === 0) {
    return { x: toolpath.bedSize.x * 0.5, y: toolpath.bedSize.y * 0.5, z: ly ? ly.zTop : 0 };
  }
  const idx = ly.extrudeFirst + Math.max(0, Math.min(ly.extrudeCount - 1, Math.floor(moveProgress01 * ly.extrudeCount)));
  const b = idx * 10;
  return {
    x: toolpath.extrudeInstances[b + 2],
    y: toolpath.extrudeInstances[b + 3],
    z: ly.zTop,
  };
}
