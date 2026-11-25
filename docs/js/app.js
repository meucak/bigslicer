import { Mat4 } from './math3d.js';
import { Camera, ViewPreset, UpAxis } from './camera.js';
import { MatcapKind, matcapName } from './matcap.js';
import { parseStlBuffer } from './stl_parser.js';
import { DEMO_MODELS, generateDemoStl } from './demo_models.js';
import { extractStlsFromZip } from './zip_stl.js';
import { Renderer } from './renderer.js';
import { RendererWebGL1 } from './renderer_webgl1.js';
import { RendererCanvas2D } from './renderer_canvas2d.js';
import { decodeKtpBuffer, FEATURE_TYPES, COLOR_MODES } from './toolpath_geometry.js';
import { SlicerClient } from './slicer_client.js';
import { detectLang, createTranslator } from './i18n.js';

const $ = (id) => document.getElementById(id);

const FALLBACK_PRESETS = {
  printers: [
    { id: 'generic_220', name: 'Generic 220³ (Marlin 2)', bedX: 220, bedY: 220, bedZ: 250, flavor: 'marlin2', maxFlow: 16 },
    { id: 'ender3_v3', name: 'Creality Ender-3 V3', bedX: 220, bedY: 220, bedZ: 250, flavor: 'klipper', maxFlow: 22 },
    { id: 'prusa_mk4s', name: 'Prusa MK4S (Input Shaper)', bedX: 250, bedY: 210, bedZ: 220, flavor: 'marlin2', maxFlow: 24 },
    { id: 'bambu_p1s', name: 'Bambu Lab P1S / X1C', bedX: 256, bedY: 256, bedZ: 256, flavor: 'bambu', maxFlow: 28 },
    { id: 'voron_24_300', name: 'Voron 2.4 300mm (Klipper)', bedX: 300, bedY: 300, bedZ: 280, flavor: 'klipper', maxFlow: 30 },
    { id: 'creality_k1c', name: 'Creality K1C', bedX: 220, bedY: 220, bedZ: 250, flavor: 'klipper', maxFlow: 26 },
    { id: 'prusa_mini', name: 'Prusa MINI+ (IS)', bedX: 180, bedY: 180, bedZ: 180, flavor: 'marlin2', maxFlow: 15 },
  ],
  materials: [
    { id: 'pla', name: 'PLA', nozzleTemp: 210, bedTemp: 60, maxFlow: 18 },
    { id: 'pla_hs', name: 'PLA+ High-Speed', nozzleTemp: 220, bedTemp: 60, maxFlow: 26 },
    { id: 'petg', name: 'PETG', nozzleTemp: 240, bedTemp: 80, maxFlow: 15 },
    { id: 'abs', name: 'ABS', nozzleTemp: 255, bedTemp: 100, maxFlow: 18 },
    { id: 'asa', name: 'ASA', nozzleTemp: 260, bedTemp: 105, maxFlow: 18 },
    { id: 'tpu95a', name: 'TPU 95A', nozzleTemp: 225, bedTemp: 50, maxFlow: 6 },
  ],
  qualities: [
    { id: 'ultra_008', name: '0.08 mm Ultra Fine', layerHeight: 0.08, firstLayerHeight: 0.16, walls: 4, infillPct: 20 },
    { id: 'quality_012', name: '0.12 mm Quality', layerHeight: 0.12, firstLayerHeight: 0.20, walls: 3, infillPct: 20 },
    { id: 'standard_020', name: '0.20 mm Standard', layerHeight: 0.20, firstLayerHeight: 0.24, walls: 3, infillPct: 20 },
    { id: 'draft_028', name: '0.28 mm Draft Fast', layerHeight: 0.28, firstLayerHeight: 0.28, walls: 2, infillPct: 15 },
    { id: 'strong_020', name: '0.20 mm Structural', layerHeight: 0.20, firstLayerHeight: 0.24, walls: 5, infillPct: 40 },
  ],
};

const state = {
  lang: detectLang(),
  darkTheme: true,
  mode: 'prepare', // 'prepare' | 'preview'
  matcap: MatcapKind.Clay,
  wireframe: false,
  xray: false,
  overhang: false,
  overhangAngle: 50,
  twoSided: false,
  showGrid: true,
  sliceEnabled: false,
  bedX: 220,
  bedY: 220,
  bedZ: 250,

  // Preview state
  previewMinLayer: 0,
  previewMaxLayer: 0,
  previewMoveFrac: 1.0,
  playing: false,
  colorMode: COLOR_MODES.FEATURE,
  highlightFeature: -1,
  showTravels: false,
  travelsAllLayers: false,
  ghostShell: false,
  showNozzle: true,

  // Current model
  modelName: 'flange-housing.stl',
  rawBuffer: null,
  libraryRef: null,
  mesh: null,
  toolpath: null,
  lastStats: null,
  presets: FALLBACK_PRESETS,
  kasaLibrary: [],
};

let t = createTranslator(state.lang);
const cam = new Camera();
cam.setUpAxis(UpAxis.ZUp);
let renderer = null;
const slicer = new SlicerClient();
let rafId = 0;
let lastFrameTime = 0;
let toastTimer = 0;
let lastGcodeFullText = '';

const canvas = $('gl-canvas');

function formatDuration(sec) {
  const s = Math.max(0, Math.round(sec || 0));
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const r = s % 60;
  if (h > 0) return `${h}h ${m}m ${r}s`;
  if (m > 0) return `${m}m ${r}s`;
  return `${r}s`;
}

function showToast(msg, ms = 2600) {
  const el = $('toast');
  el.textContent = msg;
  el.classList.remove('hidden');
  if (toastTimer) clearTimeout(toastTimer);
  toastTimer = setTimeout(() => el.classList.add('hidden'), ms);
}

function applyTranslations() {
  t = createTranslator(state.lang);
  document.documentElement.lang = state.lang;
  document.title = t('appTitle');
  $('sr-heading').textContent = t('srHeading');
  $('btn-lang').textContent = state.lang.toUpperCase();
  document.querySelectorAll('[data-i18n]').forEach((el) => {
    const k = el.getAttribute('data-i18n');
    if (k) el.textContent = t(k);
  });
  $('gcode-search').placeholder = t('searchLayer');
  renderLegend();
}

function initRenderer() {
  const dpr = Math.min(window.devicePixelRatio || 1, 2.0);
  const w = Math.max(1, Math.round(window.innerWidth * dpr));
  const h = Math.max(1, Math.round(window.innerHeight * dpr));
  canvas.width = w;
  canvas.height = h;
  try {
    renderer = new Renderer(canvas);
    renderer.init(w, h, dpr);
    renderer.setTargetSamples(8);
  } catch (e2) {
    console.warn('[bigslicer] WebGL2 init fallback:', e2?.message || e2);
    try {
      renderer = new RendererWebGL1(canvas);
      renderer.init(w, h, dpr);
    } catch {
      renderer = new RendererCanvas2D(canvas);
      renderer.init(w, h, dpr);
    }
  }
  window.__katmanBackend = renderer.backend;
}

function requestRender(resetAccum = true) {
  if (resetAccum && renderer?.resetAccumulation) renderer.resetAccumulation();
  if (!rafId) rafId = requestAnimationFrame(renderLoop);
}

function renderLoop(now) {
  rafId = 0;
  const dt = lastFrameTime ? Math.min(0.05, (now - lastFrameTime) * 0.001) : 0.016;
  lastFrameTime = now;

  let active = cam.step(dt);
  if (active && renderer?.resetAccumulation) renderer.resetAccumulation();

  if (state.playing && state.mode === 'preview' && state.toolpath) {
    state.previewMoveFrac += dt * 0.45;
    if (state.previewMoveFrac >= 1.0) {
      if (state.previewMaxLayer + 1 < state.toolpath.layerCount) {
        state.previewMaxLayer++;
        state.previewMoveFrac = 0.02;
        syncLayerScrubberUi();
      } else {
        state.previewMoveFrac = 1.0;
        state.playing = false;
        $('btn-play').textContent = '▶';
      }
    }
    $('slider-move').value = String(Math.round(state.previewMoveFrac * 1000));
    $('lbl-move-pct').textContent = `${Math.round(state.previewMoveFrac * 100)}%`;
    if (renderer?.resetAccumulation) renderer.resetAccumulation();
    active = true;
  }

  if (renderer) {
    renderer.renderFrame(state.mesh, cam, state);
    if (renderer.needsMoreSamples && renderer.needsMoreSamples()) active = true;
  }

  if (active) {
    rafId = requestAnimationFrame(renderLoop);
  }
}

function computeModelMatrixFromUi() {
  const m = state.mesh;
  if (!m || !m.ok) return Mat4.identity();
  const s = Math.max(0.1, (Number($('cfg-scale').value) || 100) / 100);
  const rx = ((Number($('cfg-rot-x').value) || 0) * Math.PI) / 180;
  const ry = ((Number($('cfg-rot-y').value) || 0) * Math.PI) / 180;
  const rz = ((Number($('cfg-rot-z').value) || 0) * Math.PI) / 180;

  const cx = (m.bboxMin.x + m.bboxMax.x) * 0.5;
  const cy = (m.bboxMin.y + m.bboxMax.y) * 0.5;
  const cz = (m.bboxMin.z + m.bboxMax.z) * 0.5;

  // Build R = Rz * Ry * Rx
  const sx = Math.sin(rx), cax = Math.cos(rx);
  const sy = Math.sin(ry), cay = Math.cos(ry);
  const sz = Math.sin(rz), caz = Math.cos(rz);
  const r00 = caz * cay, r01 = caz * sy * sx - sz * cax, r02 = caz * sy * cax + sz * sx;
  const r10 = sz * cay,  r11 = sz * sy * sx + caz * cax, r12 = sz * sy * cax - caz * sx;
  const r20 = -sy,       r21 = cay * sx,                 r22 = cay * cax;

  // Transform 8 bbox corners to place transformed mesh cleanly at Z=0 & bed center!
  const xs = [m.bboxMin.x - cx, m.bboxMax.x - cx];
  const ys = [m.bboxMin.y - cy, m.bboxMax.y - cy];
  const zs = [m.bboxMin.z - cz, m.bboxMax.z - cz];
  let minZ = Infinity;
  for (const x of xs) for (const y of ys) for (const z of zs) {
    const tz = (r20 * x + r21 * y + r22 * z) * s;
    if (tz < minZ) minZ = tz;
  }

  const tx = state.bedX * 0.5;
  const ty = state.bedY * 0.5;
  const tz = -minZ;

  // Column-major 4x4: M * p = T(tx,ty,tz) * R * S * T(-cx,-cy,-cz) * p
  const m00 = r00 * s, m01 = r01 * s, m02 = r02 * s;
  const m10 = r10 * s, m11 = r11 * s, m12 = r12 * s;
  const m20 = r20 * s, m21 = r21 * s, m22 = r22 * s;

  const ox = tx - (m00 * cx + m01 * cy + m02 * cz);
  const oy = ty - (m10 * cx + m11 * cy + m12 * cz);
  const oz = tz - (m20 * cx + m21 * cy + m22 * cz);

  return new Float32Array([
    m00, m10, m20, 0,
    m01, m11, m21, 0,
    m02, m12, m22, 0,
    ox,  oy,  oz,  1,
  ]);
}

function updateTransformPreview() {
  if (renderer?.setModelMatrix) {
    renderer.setModelMatrix(computeModelMatrixFromUi());
  }
  requestRender(true);
}

function frameBuildPlate(tightToModel = true) {
  if (tightToModel && state.mesh?.ok) {
    const s = Math.max(0.1, (Number($('cfg-scale').value) || 100) / 100);
    const ex = Math.max(20, state.mesh.bboxExtent.x * s);
    const ey = Math.max(20, state.mesh.bboxExtent.y * s);
    const ez = Math.max(15, state.mesh.bboxExtent.z * s);
    const cx = state.bedX * 0.5;
    const cy = state.bedY * 0.5;
    cam.frameBounds(
      { x: cx - ex * 0.65, y: cy - ey * 0.65, z: 0 },
      { x: cx + ex * 0.65, y: cy + ey * 0.65, z: ez },
      canvas.width,
      canvas.height,
      false
    );
    // Nudge slightly left on desktop so model centers optically beside the 316px right dock!
    if (window.innerWidth > 900) {
      cam.pan(-140 * (renderer?.dpr || 1), 12 * (renderer?.dpr || 1), canvas.width, canvas.height);
    }
  } else {
    cam.frameBounds(
      { x: 0, y: 0, z: 0 },
      { x: state.bedX, y: state.bedY, z: Math.min(120, state.bedZ * 0.5) },
      canvas.width,
      canvas.height,
      false
    );
  }
  requestRender(true);
}

function loadMeshBuffer(buffer, name, libraryRef = null) {
  state.rawBuffer = buffer;
  state.modelName = name;
  state.libraryRef = libraryRef;
  state.toolpath = null;
  state.lastStats = null;
  if (renderer?.clearToolpath) renderer.clearToolpath();

  const mesh = parseStlBuffer(buffer, name);
  if (!mesh.ok) {
    showToast(mesh.error || 'Failed to parse STL');
    return;
  }
  state.mesh = mesh;
  if (renderer?.uploadMesh) renderer.uploadMesh(mesh);

  $('info-name').textContent = name;
  $('info-metrics').textContent =
    `${(mesh.triCount / 1000).toFixed(1)}K tris · ${mesh.bboxExtent.x.toFixed(1)}×${mesh.bboxExtent.y.toFixed(1)}×${mesh.bboxExtent.z.toFixed(1)} mm`;

  $('results-card').classList.add('hidden');
  $('export-row').classList.add('hidden');
  $('btn-mode-preview').disabled = true;
  setMode('prepare');
  updateTransformPreview();
  frameBuildPlate(true);
}

function collectConfigFromUi() {
  const mono = $('cfg-monotonic').checked;
  return {
    printer: $('cfg-printer').value,
    material: $('cfg-material').value,
    quality: $('cfg-quality').value,
    layer_height: Number($('cfg-layer-height').value) || 0.2,
    first_layer_height: Number($('cfg-first-layer').value) || 0.24,
    adaptive_layer_height: $('cfg-adaptive').checked,
    wall_count: Number($('cfg-walls').value) || 3,
    wall_order: $('cfg-wall-order').value,
    arachne_thin_walls: $('cfg-arachne').checked,
    spiral_vase: $('cfg-vase').checked,
    infill_density: Number($('cfg-infill-density').value),
    infill_pattern: $('cfg-infill-pattern').value,
    top_layers: Number($('cfg-top-layers').value),
    bottom_layers: Number($('cfg-bottom-layers').value),
    top_pattern: mono ? 'monotonic' : 'rectilinear',
    bottom_pattern: mono ? 'monotonic' : 'rectilinear',
    detect_bridges: $('cfg-bridges').checked,
    ironing: $('cfg-ironing').checked,
    support_type: $('cfg-support-type').value,
    support_overhang_angle: Number($('cfg-support-angle').value) || 50,
    support_interface_layers: Number($('cfg-support-iface').value) || 2,
    support_placement: $('cfg-support-place').value,
    adhesion: $('cfg-adhesion').value,
    scale: (Number($('cfg-scale').value) || 100) / 100,
    copies: Number($('cfg-copies').value) || 1,
    rotate_x_deg: Number($('cfg-rot-x').value) || 0,
    rotate_y_deg: Number($('cfg-rot-y').value) || 0,
    rotate_z_deg: Number($('cfg-rot-z').value) || 0,
    auto_lay_flat: $('cfg-lay-flat').checked,
    seam_position: $('cfg-seam').value,
    scarf_seam: $('cfg-scarf').checked,
    combing: $('cfg-combing').checked,
    arc_fitting: $('cfg-arcs').checked,
    nozzle_temp: Number($('cfg-nozzle-temp').value) || 210,
    first_layer_nozzle_temp: (Number($('cfg-nozzle-temp').value) || 210) + 5,
    bed_temp: Number($('cfg-bed-temp').value) || 60,
    first_layer_bed_temp: (Number($('cfg-bed-temp').value) || 60) + 5,
    max_volumetric_flow: Number($('cfg-max-flow').value) || 18,
    flavor: $('cfg-flavor').value,
    bed_size_x: state.bedX,
    bed_size_y: state.bedY,
    bed_size_z: state.bedZ,
  };
}

function setMode(mode) {
  if (mode === 'preview' && !state.toolpath) return;
  state.mode = mode;
  const isPrev = mode === 'preview';
  $('btn-mode-prepare').classList.toggle('active', !isPrev);
  $('btn-mode-prepare').setAttribute('aria-selected', String(!isPrev));
  $('btn-mode-preview').classList.toggle('active', isPrev);
  $('btn-mode-preview').setAttribute('aria-selected', String(isPrev));
  $('prepare-bar').classList.toggle('hidden', isPrev);
  $('preview-bar').classList.toggle('hidden', !isPrev);
  $('layer-scrubber').classList.toggle('hidden', !isPrev);
  if (!isPrev) {
    state.playing = false;
    $('btn-play').textContent = '▶';
  }
  renderLegend();
  requestRender(true);
}

function syncLayerScrubberUi() {
  if (!state.toolpath) return;
  const tp = state.toolpath;
  const li = Math.max(0, Math.min(tp.layerCount - 1, state.previewMaxLayer | 0));
  const ly = tp.layers[li];
  $('slider-layer').max = String(Math.max(0, tp.layerCount - 1));
  $('slider-layer').value = String(li);
  $('lbl-layer-num').textContent = `${li + 1}/${tp.layerCount}`;
  $('lbl-layer-z').textContent = `${ly.zTop.toFixed(2)} mm`;
}

function renderLegend() {
  const pill = $('legend-pill');
  if (state.mode !== 'preview' || !state.toolpath) return;
  const cm = state.colorMode;
  pill.innerHTML = '';

  if (cm === COLOR_MODES.FEATURE) {
    const present = new Set();
    if (state.lastStats?.features) {
      for (const f of state.lastStats.features) {
        if (f.id > 0 && (f.filamentMm > 0.01 || f.timeSec > 0.01)) present.add(f.id);
      }
    }
    for (const ft of FEATURE_TYPES) {
      if (ft.id === 0) continue;
      if (present.size > 0 && !present.has(ft.id)) continue;
      const btn = document.createElement('button');
      btn.type = 'button';
      btn.className = `leg-item${state.highlightFeature === ft.id ? ' active' : ''}`;
      btn.innerHTML = `<span class="leg-swatch" style="background:${ft.hex}"></span><span>${state.lang === 'tr' ? ft.nameTr : ft.nameEn}</span>`;
      btn.addEventListener('click', () => {
        state.highlightFeature = state.highlightFeature === ft.id ? -1 : ft.id;
        renderLegend();
        requestRender(true);
      });
      pill.appendChild(btn);
    }
  } else {
    const r = state.toolpath.ranges;
    let lo = r.minSpeed, hi = r.maxSpeed, unit = 'mm/s';
    if (cm === COLOR_MODES.FLOW) { lo = r.minFlow; hi = r.maxFlow; unit = 'mm³/s'; }
    else if (cm === COLOR_MODES.WIDTH) { lo = r.minWidth; hi = r.maxWidth; unit = 'mm'; }
    else if (cm === COLOR_MODES.HEIGHT) { lo = r.minHeight; hi = r.maxHeight; unit = 'mm'; }
    const wrap = document.createElement('div');
    wrap.className = 'heat-legend mono';
    wrap.innerHTML = `<span>${lo.toFixed(2)} ${unit}</span><span class="heat-bar"></span><span>${hi.toFixed(2)} ${unit}</span>`;
    pill.appendChild(wrap);
  }
}

function renderResultsCard(stats, engine) {
  $('results-card').classList.remove('hidden');
  $('export-row').classList.remove('hidden');

  const engTag = engine === 'native-cpp20' ? `C++20 ${stats.threadsUsed}T` : 'WASM C++20';
  $('res-engine-ms').textContent = `${stats.timingsMs.total.toFixed(1)} ms · ${engTag}`;
  $('res-time').textContent = formatDuration(stats.printTimeSec);
  $('res-filament').textContent = `${(stats.filamentMm / 1000).toFixed(1)}m · ${stats.filamentG.toFixed(1)}g`;
  $('res-cost').textContent = `$${stats.estimatedCost.toFixed(2)}`;
  $('res-layers-moves').textContent = `${stats.layerCount} · ${(stats.totalMoves / 1000).toFixed(0)}K`;

  const bars = $('feature-bars');
  bars.innerHTML = '';
  const totalT = Math.max(0.01, stats.printTimeSec);
  const activeFeats = (stats.features || [])
    .filter((f) => f.timeSec > 0.1)
    .sort((a, b) => b.timeSec - a.timeSec)
    .slice(0, 6);

  for (const f of activeFeats) {
    const meta = FEATURE_TYPES[f.id] || FEATURE_TYPES[1];
    const pct = Math.round((f.timeSec / totalT) * 100);
    const row = document.createElement('div');
    row.className = 'fbar-row';
    row.innerHTML =
      `<span class="fbar-dot" style="background:${meta.hex}"></span>` +
      `<span class="fbar-name">${state.lang === 'tr' ? meta.nameTr : meta.nameEn}</span>` +
      `<span class="fbar-val mono">${pct}% · ${formatDuration(f.timeSec)}</span>`;
    bars.appendChild(row);
  }
}

async function runSlice() {
  if (!state.rawBuffer && !state.libraryRef) {
    showToast('Load an STL model first');
    return;
  }
  const btn = $('btn-slice');
  const lbl = $('btn-slice-label');
  btn.disabled = true;
  lbl.textContent = t('slicing');

  try {
    const cfg = collectConfigFromUi();
    const res = await slicer.slice({
      buffer: state.libraryRef && slicer.serverMode ? null : state.rawBuffer,
      name: state.modelName,
      config: cfg,
      libraryRef: state.libraryRef,
    });

    const tp = decodeKtpBuffer(res.ktpBuffer);
    state.toolpath = tp;
    state.lastStats = res.stats;
    state.previewMinLayer = 0;
    state.previewMaxLayer = Math.max(0, tp.layerCount - 1);
    state.previewMoveFrac = 1.0;
    state.highlightFeature = -1;

    if (renderer?.uploadToolpath) renderer.uploadToolpath(tp);
    syncLayerScrubberUi();
    $('slider-move').value = '1000';
    $('lbl-move-pct').textContent = '100%';
    $('btn-mode-preview').disabled = false;

    renderResultsCard(res.stats, res.engine);
    setMode('preview');

    // Update engine badge
    const isNat = res.engine === 'native-cpp20';
    $('engine-dot').classList.toggle('wasm', !isNat);
    $('engine-badge').textContent = isNat
      ? `Native C++20 · ${res.stats.timingsMs.total.toFixed(0)}ms`
      : `WASM C++20 · ${res.stats.timingsMs.total.toFixed(0)}ms`;

    showToast(`${res.stats.layerCount} layers · ${res.stats.timingsMs.total.toFixed(1)} ms (${res.engine})`);
  } catch (err) {
    showToast(`Slice error: ${err?.message || err}`, 4000);
  } finally {
    btn.disabled = false;
    lbl.textContent = t('sliceNow');
  }
}

function populatePresetSelects() {
  const fill = (id, list) => {
    const sel = $(id);
    sel.innerHTML = '';
    for (const item of list) {
      const opt = document.createElement('option');
      opt.value = item.id;
      opt.textContent = item.name;
      sel.appendChild(opt);
    }
  };
  fill('cfg-printer', state.presets.printers);
  fill('cfg-material', state.presets.materials);
  fill('cfg-quality', state.presets.qualities);
  $('cfg-printer').value = 'prusa_mk4s';
  $('cfg-material').value = 'pla';
  $('cfg-quality').value = 'standard_020';
  syncPrinterPresetFields();
}

function syncPrinterPresetFields() {
  const pr = state.presets.printers.find((p) => p.id === $('cfg-printer').value);
  if (pr) {
    state.bedX = pr.bedX || 220;
    state.bedY = pr.bedY || 220;
    state.bedZ = pr.bedZ || 250;
    if (pr.maxFlow) $('cfg-max-flow').value = String(pr.maxFlow);
    if (pr.flavor) $('cfg-flavor').value = pr.flavor;
    updateTransformPreview();
    frameBuildPlate(true);
  }
}

function syncMaterialPresetFields() {
  const m = state.presets.materials.find((x) => x.id === $('cfg-material').value);
  if (m) {
    $('cfg-nozzle-temp').value = String(m.nozzleTemp);
    $('cfg-bed-temp').value = String(m.bedTemp);
    $('cfg-max-flow').value = String(m.maxFlow);
  }
}

function syncQualityPresetFields() {
  const q = state.presets.qualities.find((x) => x.id === $('cfg-quality').value);
  if (q) {
    $('cfg-layer-height').value = q.layerHeight.toFixed(2);
    $('cfg-first-layer').value = q.firstLayerHeight.toFixed(2);
    $('cfg-walls').value = String(q.walls);
    $('cfg-infill-density').value = String(q.infillPct);
    $('lbl-infill-density').textContent = `${q.infillPct}%`;
  }
}

function populateDemoAndLibrary() {
  const dl = $('demo-list');
  dl.innerHTML = '';
  for (const d of DEMO_MODELS) {
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'pop-item';
    btn.innerHTML = `<span>${d.label || d.name}</span><span class="pop-item-sub mono">${d.name}</span>`;
    btn.addEventListener('click', () => {
      $('model-popover').classList.add('hidden');
      const buf = generateDemoStl(d.id);
      loadMeshBuffer(buf, d.name, null);
    });
    dl.appendChild(btn);
  }
}

async function fetchKasaLibrary() {
  try {
    const r = await fetch('./api/library');
    if (!r.ok) return;
    const j = await r.json();
    if (!j.ok || !j.items?.length) return;
    state.kasaLibrary = j.items;
    $('kasa-lib-section').classList.remove('hidden');
    $('kasa-lib-count').textContent = String(j.items.length);
    renderKasaLibraryList('');
  } catch {}
}

function renderKasaLibraryList(query) {
  const list = $('kasa-lib-list');
  list.innerHTML = '';
  const q = query.trim().toLowerCase();
  const filtered = q
    ? state.kasaLibrary.filter((it) => it.name.toLowerCase().includes(q) || it.folder.toLowerCase().includes(q))
    : state.kasaLibrary;
  for (const it of filtered.slice(0, 60)) {
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'pop-item';
    btn.innerHTML = `<span>${it.name}</span><span class="pop-item-sub mono">${(it.bytes / 1024).toFixed(0)} KB</span>`;
    btn.addEventListener('click', async () => {
      $('model-popover').classList.add('hidden');
      showToast(`Loading ${it.name}…`);
      try {
        const resp = await fetch(`./api/library/file?root=${it.root}&path=${encodeURIComponent(it.relPath)}`);
        if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
        const ab = await resp.arrayBuffer();
        loadMeshBuffer(ab, it.name, it);
      } catch (e) {
        showToast(`Load error: ${e.message}`);
      }
    });
    list.appendChild(btn);
  }
}

async function handleFileInput(file) {
  if (!file) return;
  const ab = await file.arrayBuffer();
  if (/\.zip$/i.test(file.name)) {
    const extracted = await extractStlsFromZip(ab);
    if (!extracted.length) { showToast('No STL found in ZIP'); return; }
    loadMeshBuffer(extracted[0].buffer, extracted[0].name, null);
  } else {
    loadMeshBuffer(ab, file.name, null);
  }
}

function bindEvents() {
  // Resize
  window.addEventListener('resize', () => {
    const dpr = Math.min(window.devicePixelRatio || 1, 2.0);
    const w = Math.max(1, Math.round(window.innerWidth * dpr));
    const h = Math.max(1, Math.round(window.innerHeight * dpr));
    canvas.width = w; canvas.height = h;
    renderer?.resize(w, h, dpr);
    requestRender(true);
  });

  // Orbit / Pan / Zoom on 3D Canvas
  let dragging = false, dragBtn = 0, lastX = 0, lastY = 0;
  canvas.addEventListener('pointerdown', (e) => {
    dragging = true; dragBtn = e.button; lastX = e.clientX; lastY = e.clientY;
    canvas.setPointerCapture(e.pointerId);
    $('model-popover').classList.add('hidden');
  });
  canvas.addEventListener('pointermove', (e) => {
    if (!dragging) return;
    const dx = e.clientX - lastX, dy = e.clientY - lastY;
    lastX = e.clientX; lastY = e.clientY;
    const dpr = renderer?.dpr || 1;
    if (dragBtn === 0 && !e.shiftKey) cam.orbit(dx * dpr, dy * dpr, canvas.width, canvas.height);
    else cam.pan(dx * dpr, dy * dpr, canvas.width, canvas.height);
    requestRender(true);
  });
  canvas.addEventListener('pointerup', (e) => {
    dragging = false;
    try { canvas.releasePointerCapture(e.pointerId); } catch {}
  });
  canvas.addEventListener('contextmenu', (e) => e.preventDefault());
  canvas.addEventListener('wheel', (e) => {
    e.preventDefault();
    const dpr = renderer?.dpr || 1;
    const steps = -Math.sign(e.deltaY) * Math.min(3, Math.abs(e.deltaY) / 100);
    cam.zoom(steps, e.clientX * dpr, e.clientY * dpr, canvas.width, canvas.height);
    requestRender(true);
  }, { passive: false });

  // Drag & Drop .stl / .zip
  window.addEventListener('dragover', (e) => { e.preventDefault(); });
  window.addEventListener('drop', (e) => {
    e.preventDefault();
    const f = e.dataTransfer?.files?.[0];
    if (f) handleFileInput(f);
  });

  // Model Hub Popover
  $('btn-model-menu').addEventListener('click', () => {
    const p = $('model-popover');
    const open = p.classList.toggle('hidden') === false;
    $('btn-model-menu').setAttribute('aria-expanded', String(open));
  });
  $('btn-open-file').addEventListener('click', () => {
    $('model-popover').classList.add('hidden');
    $('file-input').click();
  });
  $('file-input').addEventListener('change', (e) => {
    const f = e.target.files?.[0];
    if (f) handleFileInput(f);
    e.target.value = '';
  });
  $('lib-search').addEventListener('input', (e) => renderKasaLibraryList(e.target.value));

  // Mode Tabs
  $('btn-mode-prepare').addEventListener('click', () => setMode('prepare'));
  $('btn-mode-preview').addEventListener('click', () => setMode('preview'));

  // Dock Tabs
  document.querySelectorAll('.dock-tab').forEach((btn) => {
    btn.addEventListener('click', () => {
      const tab = btn.dataset.tab;
      document.querySelectorAll('.dock-tab').forEach((b) => {
        const on = b.dataset.tab === tab;
        b.classList.toggle('active', on);
        b.setAttribute('aria-selected', String(on));
      });
      ['quality', 'infill', 'advanced'].forEach((p) => {
        $(`panel-${p}`).classList.toggle('hidden', p !== tab);
      });
    });
  });

  // Preset & live transform listeners
  $('cfg-printer').addEventListener('change', syncPrinterPresetFields);
  $('cfg-material').addEventListener('change', syncMaterialPresetFields);
  $('cfg-quality').addEventListener('change', syncQualityPresetFields);
  $('cfg-infill-density').addEventListener('input', (e) => {
    $('lbl-infill-density').textContent = `${e.target.value}%`;
  });
  $('cfg-support-angle').addEventListener('input', (e) => {
    state.overhangAngle = Number(e.target.value) || 50;
    requestRender(true);
  });
  ['cfg-scale', 'cfg-rot-x', 'cfg-rot-y', 'cfg-rot-z'].forEach((id) => {
    $(id).addEventListener('input', updateTransformPreview);
  });

  // Slice & Export CTA
  $('btn-slice').addEventListener('click', runSlice);
  $('btn-export-gcode').addEventListener('click', () => {
    slicer.downloadGcode(state.modelName).catch((e) => showToast(e.message));
  });
  $('btn-inspect-gcode').addEventListener('click', async () => {
    try {
      const { text, truncated, totalBytes } = await slicer.fetchGcodeText(512 * 1024);
      lastGcodeFullText = text;
      $('gcode-viewer').textContent = text;
      $('gcode-meta').textContent =
        `${(totalBytes / 1024).toFixed(1)} KB${truncated ? ' (showing first 512 KB)' : ''} · ${text.split('\n').length} lines`;
      $('gcode-modal').classList.remove('hidden');
    } catch (e) { showToast(e.message); }
  });
  $('btn-gcode-close').addEventListener('click', () => $('gcode-modal').classList.add('hidden'));
  $('btn-gcode-copy').addEventListener('click', async () => {
    try {
      await navigator.clipboard.writeText($('gcode-viewer').textContent);
      showToast(t('copied'));
    } catch {}
  });
  $('gcode-search').addEventListener('input', (e) => {
    const q = e.target.value.trim().toLowerCase();
    if (!q) { $('gcode-viewer').textContent = lastGcodeFullText; return; }
    const lines = lastGcodeFullText.split('\n');
    const matched = [];
    for (let i = 0; i < lines.length && matched.length < 400; i++) {
      if (lines[i].toLowerCase().includes(q)) matched.push(`${String(i + 1).padStart(6, ' ')} | ${lines[i]}`);
    }
    $('gcode-viewer').textContent = matched.length ? matched.join('\n') : '(No matches)';
  });

  // Prepare Toolbar
  $('btn-matcap').addEventListener('click', () => {
    state.matcap = (state.matcap + 1) % MatcapKind.Count;
    $('btn-matcap').textContent = matcapName(state.matcap);
    requestRender(true);
  });
  $('btn-wire').addEventListener('click', () => {
    state.wireframe = !state.wireframe;
    $('btn-wire').setAttribute('aria-pressed', String(state.wireframe));
    requestRender(true);
  });
  $('btn-overhang').addEventListener('click', () => {
    state.overhang = !state.overhang;
    $('btn-overhang').setAttribute('aria-pressed', String(state.overhang));
    requestRender(true);
  });
  $('btn-view-iso').addEventListener('click', () => { cam.setPreset(ViewPreset.Iso, true); requestRender(true); });
  $('btn-view-top').addEventListener('click', () => { cam.setPreset(ViewPreset.Top, true); requestRender(true); });
  $('btn-view-front').addEventListener('click', () => { cam.setPreset(ViewPreset.Front, true); requestRender(true); });
  $('btn-reset-cam').addEventListener('click', () => frameBuildPlate(true));

  // Preview Layer & Playback Controls
  $('slider-layer').addEventListener('input', (e) => {
    state.previewMaxLayer = Number(e.target.value) | 0;
    state.previewMoveFrac = 1.0;
    $('slider-move').value = '1000';
    $('lbl-move-pct').textContent = '100%';
    syncLayerScrubberUi();
    requestRender(true);
  });
  $('btn-layer-up').addEventListener('click', () => {
    if (!state.toolpath) return;
    state.previewMaxLayer = Math.min(state.toolpath.layerCount - 1, state.previewMaxLayer + 1);
    syncLayerScrubberUi();
    requestRender(true);
  });
  $('btn-layer-down').addEventListener('click', () => {
    if (!state.toolpath) return;
    state.previewMaxLayer = Math.max(0, state.previewMaxLayer - 1);
    syncLayerScrubberUi();
    requestRender(true);
  });
  $('slider-move').addEventListener('input', (e) => {
    state.previewMoveFrac = Number(e.target.value) / 1000;
    $('lbl-move-pct').textContent = `${Math.round(state.previewMoveFrac * 100)}%`;
    requestRender(true);
  });
  $('btn-play').addEventListener('click', () => {
    if (!state.toolpath) return;
    state.playing = !state.playing;
    if (state.playing && state.previewMoveFrac >= 0.99) state.previewMoveFrac = 0.01;
    $('btn-play').textContent = state.playing ? '⏸' : '▶';
    if (state.playing) requestRender(true);
  });
  $('sel-color-mode').addEventListener('change', (e) => {
    state.colorMode = Number(e.target.value) | 0;
    state.highlightFeature = -1;
    renderLegend();
    requestRender(true);
  });
  $('btn-travels').addEventListener('click', () => {
    state.showTravels = !state.showTravels;
    $('btn-travels').setAttribute('aria-pressed', String(state.showTravels));
    requestRender(true);
  });
  $('btn-ghost').addEventListener('click', () => {
    state.ghostShell = !state.ghostShell;
    $('btn-ghost').setAttribute('aria-pressed', String(state.ghostShell));
    requestRender(true);
  });
  $('btn-nozzle').addEventListener('click', () => {
    state.showNozzle = !state.showNozzle;
    $('btn-nozzle').setAttribute('aria-pressed', String(state.showNozzle));
    $('btn-nozzle').classList.toggle('active-chip', state.showNozzle);
    requestRender(true);
  });

  // Theme & Lang & Mobile Dock
  $('btn-theme').addEventListener('click', () => {
    state.darkTheme = !state.darkTheme;
    document.body.classList.toggle('theme-dark', state.darkTheme);
    document.body.classList.toggle('theme-light', !state.darkTheme);
    requestRender(true);
  });
  $('btn-lang').addEventListener('click', () => {
    state.lang = state.lang === 'tr' ? 'en' : 'tr';
    try { localStorage.setItem('katman-lang', state.lang); } catch {}
    applyTranslations();
  });
  $('btn-dock-toggle').addEventListener('click', () => {
    const col = $('slicer-dock').classList.toggle('collapsed');
    $('btn-dock-toggle').setAttribute('aria-expanded', String(!col));
  });

  // Keyboard shortcuts
  window.addEventListener('keydown', (e) => {
    if (e.target && ['INPUT', 'SELECT', 'TEXTAREA'].includes(e.target.tagName)) {
      if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') { e.preventDefault(); runSlice(); }
      return;
    }
    if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') { e.preventDefault(); runSlice(); return; }
    if (e.key === 'o' || e.key === 'O') $('file-input').click();
    else if (e.key === 't' || e.key === 'T') $('btn-theme').click();
    else if (e.key === 'r' || e.key === 'R') frameBuildPlate(true);
    else if (e.key === 'ArrowUp' && state.mode === 'preview') { e.preventDefault(); $('btn-layer-up').click(); }
    else if (e.key === 'ArrowDown' && state.mode === 'preview') { e.preventDefault(); $('btn-layer-down').click(); }
    else if (e.key === ' ' && state.mode === 'preview') { e.preventDefault(); $('btn-play').click(); }
    else if (e.key === 'Escape') {
      $('model-popover').classList.add('hidden');
      $('gcode-modal').classList.add('hidden');
    }
  });
}

async function boot() {
  initRenderer();
  applyTranslations();
  populatePresetSelects();
  populateDemoAndLibrary();
  bindEvents();

  // Load crisp built-in CAD demo immediately so viewport is live in <50ms!
  const initStl = generateDemoStl(DEMO_MODELS[0].id);
  loadMeshBuffer(initStl, DEMO_MODELS[0].name, null);

  // Detect Kasa Native C++20 Backend vs Offline WASM
  const { serverMode, health } = await slicer.detectBackend();
  $('engine-dot').classList.toggle('wasm', !serverMode);
  $('engine-badge').textContent = serverMode
    ? `Native C++20 · ${health?.cpus || 4}T`
    : 'WASM C++20 (Offline)';

  if (serverMode) {
    fetchKasaLibrary();
    try {
      const p = await slicer.loadPresets();
      if (p?.printers?.length) {
        state.presets = p;
        populatePresetSelects();
      }
    } catch {}
  }

  if ('serviceWorker' in navigator && !serverMode) {
    navigator.serviceWorker.register('./sw.js').catch(() => {});
  }
}

boot();
