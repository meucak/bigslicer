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
import {
  GIZMO_MODES,
  createDefaultTransform,
  computeModelTransform,
  screenToWorldRay,
  intersectRayPlane,
  intersectRayAabb,
  raycastMeshWorld,
  computePlaceOnFaceEuler,
  hitTestGizmo,
  buildGizmoLineVertices,
} from './gizmo.js';

const $ = (id) => document.getElementById(id);

const FALLBACK_PRESETS = {
  printers: [
    { id: 'prusa_mk4s', name: 'Prusa MK4S (High-Flow IS)', vendor: 'Prusa', kinematics: 'Cartesian IS', flavor: 'marlin2', bedX: 250, bedY: 210, bedZ: 220, nozzle: 0.4, directDrive: true, originCenter: false, maxFlow: 26, retractLen: 0.8, retractSpd: 35, speedOuter: 110, speedInner: 165, speedInfill: 220, speedTravel: 320, accelDefault: 4000, accelOuter: 2400, pa: 0.035 },
    { id: 'creality_ender3_pro', name: 'Creality Ender-3 Pro', vendor: 'Creality', kinematics: 'Cartesian', flavor: 'marlin2', bedX: 220, bedY: 220, bedZ: 250, nozzle: 0.4, directDrive: false, originCenter: false, maxFlow: 12, retractLen: 5.0, retractSpd: 45, speedOuter: 45, speedInner: 60, speedInfill: 70, speedTravel: 150, accelDefault: 500, accelOuter: 500, pa: 0.0 },
    { id: 'bambu_p1s', name: 'Bambu Lab P1S', vendor: 'Bambu Lab', kinematics: 'CoreXY', flavor: 'bambu', bedX: 256, bedY: 256, bedZ: 256, nozzle: 0.4, directDrive: true, originCenter: false, maxFlow: 28, retractLen: 0.8, retractSpd: 35, speedOuter: 200, speedInner: 300, speedInfill: 380, speedTravel: 500, accelDefault: 8000, accelOuter: 5000, pa: 0.02 },
    { id: 'voron_24_300', name: 'Voron 2.4 300mm (Klipper)', vendor: 'Voron', kinematics: 'CoreXY', flavor: 'klipper', bedX: 300, bedY: 300, bedZ: 280, nozzle: 0.4, directDrive: true, originCenter: false, maxFlow: 30, retractLen: 0.4, retractSpd: 40, speedOuter: 180, speedInner: 260, speedInfill: 350, speedTravel: 500, accelDefault: 6500, accelOuter: 4000, pa: 0.03 },
  ],
  materials: [
    { id: 'pla', name: 'PLA (Standard)', nozzleTemp: 210, bedTemp: 60, maxFlow: 18, fanMax: 100, minLayerTime: 8 },
    { id: 'petg', name: 'PETG', nozzleTemp: 240, bedTemp: 80, maxFlow: 15, fanMax: 50, minLayerTime: 10 },
    { id: 'abs', name: 'ABS', nozzleTemp: 255, bedTemp: 100, maxFlow: 18, fanMax: 25, minLayerTime: 12 },
    { id: 'tpu95a', name: 'TPU 95A (Flexible)', nozzleTemp: 225, bedTemp: 50, maxFlow: 6, fanMax: 60, minLayerTime: 10 },
  ],
  qualities: [
    { id: 'ultra_008', name: '0.08 mm Ultra Detail', layerHeight: 0.08, firstLayerHeight: 0.16, walls: 4, topLayers: 7, bottomLayers: 5, infillPct: 20 },
    { id: 'quality_012', name: '0.12 mm Fine Quality', layerHeight: 0.12, firstLayerHeight: 0.20, walls: 3, topLayers: 6, bottomLayers: 4, infillPct: 20 },
    { id: 'standard_020', name: '0.20 mm Standard', layerHeight: 0.20, firstLayerHeight: 0.24, walls: 3, topLayers: 4, bottomLayers: 3, infillPct: 20 },
    { id: 'draft_028', name: '0.28 mm Extra Draft Fast', layerHeight: 0.28, firstLayerHeight: 0.28, walls: 2, topLayers: 3, bottomLayers: 3, infillPct: 15 },
    { id: 'strong_020', name: '0.20 mm Structural Strong', layerHeight: 0.20, firstLayerHeight: 0.24, walls: 5, topLayers: 6, bottomLayers: 5, infillPct: 40 },
  ],
};

const state = {
  lang: detectLang(),
  darkTheme: true,
  mode: 'prepare', // 'prepare' | 'preview'
  level: 'advanced', // 'simple' | 'advanced' | 'expert'
  gizmoMode: GIZMO_MODES.MOVE,
  activeGizmoAxis: null,
  hoverGizmoAxis: null,
  gizmoLines: [],

  matcap: MatcapKind.Clay,
  wireframe: false,
  xray: false,
  overhang: false,
  overhangAngle: 50,
  twoSided: false,
  showGrid: true,
  sliceEnabled: false,

  printerId: 'prusa_mk4s',
  bedX: 250,
  bedY: 210,
  bedZ: 220,
  originCenter: false,

  xform: createDefaultTransform(),
  xformInfo: null,

  // Preview state
  previewMinLayer: 0,
  previewMaxLayer: 0,
  previewMoveFrac: 1.0,
  playing: false,
  playSpeed: 1,
  colorMode: COLOR_MODES.FEATURE,
  highlightFeature: -1,
  hiddenFeatures: new Set(),
  showTravels: false,
  travelsAllLayers: false,
  showSeams: false,
  ghostShell: false,
  showNozzle: true,

  // Model & slicer data
  modelName: 'flange-housing.stl',
  rawBuffer: null,
  libraryRef: null,
  mesh: null,
  toolpath: null,
  lastStats: null,
  presets: FALLBACK_PRESETS,
  kasaLibrary: [],
  selectedVendor: 'ALL',
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

function showToast(msg, ms = 2800) {
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
  $('printer-search').placeholder = t('searchPrinter');
  updateGizmoInspectorHeader();
  renderLegend();
}

function initRenderer() {
  const dpr = Math.min(window.devicePixelRatio || 1, 2.0);
  const w = Math.max(1, Math.round(window.innerWidth * dpr));
  const h = Math.max(1, Math.round(window.innerHeight * dpr));
  canvas.width = w;
  canvas.height = h;
  const forceCanvas = new URLSearchParams(window.location.search).get('renderer') === 'canvas2d';
  if (forceCanvas) {
    renderer = new RendererCanvas2D(canvas);
    renderer.init(w, h, dpr);
  } else {
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
  }
  window.__katmanBackend = renderer.backend;
  window.__bigslicerBackend = renderer.backend;
  window.__bigslicerState = state;
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
    state.previewMoveFrac += dt * 0.45 * state.playSpeed;
    while (state.previewMoveFrac >= 1.0 && state.playing) {
      if (state.previewMaxLayer + 1 < state.toolpath.layerCount) {
        state.previewMaxLayer++;
        state.previewMoveFrac -= 1.0;
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

function refreshModelTransform(syncInputs = true) {
  const info = computeModelTransform(
    state.mesh,
    state.xform,
    state.bedX,
    state.bedY,
    state.bedZ,
    state.originCenter
  );
  state.xformInfo = info;
  if (renderer?.setModelMatrix) renderer.setModelMatrix(info.matrix);

  state.gizmoLines = buildGizmoLineVertices(
    state.gizmoMode,
    info,
    state.activeGizmoAxis,
    state.hoverGizmoAxis
  );

  $('xform-oob-warn').classList.toggle('hidden', info.fitsBed);

  if (syncInputs) {
    $('cfg-pos-x').value = String(Math.round(state.xform.posX * 10) / 10);
    $('cfg-pos-y').value = String(Math.round(state.xform.posY * 10) / 10);
    $('cfg-rot-x').value = String(Math.round(state.xform.rotX * 10) / 10);
    $('cfg-rot-y').value = String(Math.round(state.xform.rotY * 10) / 10);
    $('cfg-rot-z').value = String(Math.round(state.xform.rotZ * 10) / 10);
    $('cfg-scale-x').value = String(Math.round(state.xform.scaleX * 100));
    $('cfg-scale-y').value = String(Math.round(state.xform.scaleY * 100));
    $('cfg-scale-z').value = String(Math.round(state.xform.scaleZ * 100));
  }

  if (state.mesh?.ok) {
    $('info-metrics').textContent =
      `${(state.mesh.triCount / 1000).toFixed(1)}K tris · ${info.extent.x.toFixed(1)}×${info.extent.y.toFixed(1)}×${info.extent.z.toFixed(1)} mm`;
  }
  requestRender(true);
}

function updateGizmoInspectorHeader() {
  const titleEl = $('xform-card-title');
  if (state.gizmoMode === GIZMO_MODES.ROTATE) titleEl.textContent = t('xformRot');
  else if (state.gizmoMode === GIZMO_MODES.SCALE) titleEl.textContent = t('xformScale');
  else titleEl.textContent = t('xformPos');
}

function setGizmoMode(mode) {
  state.gizmoMode = mode;
  state.activeGizmoAxis = null;
  state.hoverGizmoAxis = null;
  document.querySelectorAll('.gizmo-btn[data-gizmo]').forEach((b) => {
    b.classList.toggle('active', b.dataset.gizmo === mode);
  });
  $('xform-grp-move').classList.toggle('hidden', mode !== GIZMO_MODES.MOVE && mode !== GIZMO_MODES.SELECT);
  $('xform-grp-rotate').classList.toggle('hidden', mode !== GIZMO_MODES.ROTATE);
  $('xform-grp-scale').classList.toggle('hidden', mode !== GIZMO_MODES.SCALE);
  $('xform-card').classList.toggle('hidden', state.mode !== 'prepare' || mode === GIZMO_MODES.SELECT || mode === GIZMO_MODES.FACE);
  canvas.classList.toggle('cursor-move', mode === GIZMO_MODES.MOVE && state.mode === 'prepare');
  canvas.classList.toggle('cursor-crosshair', mode === GIZMO_MODES.FACE && state.mode === 'prepare');
  updateGizmoInspectorHeader();
  if (mode === GIZMO_MODES.FACE) showToast(t('faceHint'), 2600);
  refreshModelTransform(false);
}

function frameBuildPlate(tightToModel = true) {
  const cx = state.originCenter ? 0 : state.bedX * 0.5;
  const cy = state.originCenter ? 0 : state.bedY * 0.5;
  if (tightToModel && state.mesh?.ok && state.xformInfo) {
    const { bboxMin, bboxMax } = state.xformInfo;
    const padX = Math.max(12, (bboxMax.x - bboxMin.x) * 0.25);
    const padY = Math.max(12, (bboxMax.y - bboxMin.y) * 0.25);
    cam.frameBounds(
      { x: bboxMin.x - padX, y: bboxMin.y - padY, z: 0 },
      { x: bboxMax.x + padX, y: bboxMax.y + padY, z: Math.max(15, bboxMax.z) },
      canvas.width,
      canvas.height,
      false
    );
    if (window.innerWidth > 960) {
      cam.pan(-95 * (renderer?.dpr || 1), 10 * (renderer?.dpr || 1), canvas.width, canvas.height);
    }
  } else {
    const x0 = state.originCenter ? -state.bedX * 0.5 : 0;
    const y0 = state.originCenter ? -state.bedY * 0.5 : 0;
    cam.frameBounds(
      { x: x0, y: y0, z: 0 },
      { x: x0 + state.bedX, y: y0 + state.bedY, z: Math.min(120, state.bedZ * 0.5) },
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
  state.xform = createDefaultTransform();
  if (renderer?.clearToolpath) renderer.clearToolpath();

  const mesh = parseStlBuffer(buffer, name);
  if (!mesh.ok) {
    showToast(mesh.error || 'Failed to parse STL');
    return;
  }
  state.mesh = mesh;
  if (renderer?.uploadMesh) renderer.uploadMesh(mesh);

  $('info-name').textContent = name;
  $('results-card').classList.add('hidden');
  $('export-row').classList.add('hidden');
  $('btn-mode-preview').disabled = true;
  setMode('prepare');
  refreshModelTransform(true);
  frameBuildPlate(true);
}

function collectConfigFromUi() {
  const noz = Number($('cfg-nozzle').value) || 0.4;
  return {
    printer: state.printerId,
    material: $('cfg-material').value,
    quality: $('cfg-quality').value,
    nozzle_diameter: noz,
    bed_size_x: state.bedX,
    bed_size_y: state.bedY,
    bed_size_z: state.bedZ,
    origin_center: state.originCenter,

    // Exact 3D Gizmo Transform parity with C++20 engine!
    pos_x: state.xform.posX,
    pos_y: state.xform.posY,
    scale_x: state.xform.scaleX,
    scale_y: state.xform.scaleY,
    scale_z: state.xform.scaleZ,
    rotate_x_deg: state.xform.rotX,
    rotate_y_deg: state.xform.rotY,
    rotate_z_deg: state.xform.rotZ,
    mirror_x: state.xform.mirrorX,
    mirror_y: state.xform.mirrorY,
    copies: Number($('cfg-copies').value) || 1,

    // Quality, Perimeters & Texture
    layer_height: Number($('cfg-layer-height').value) || 0.2,
    first_layer_height: Number($('cfg-first-layer').value) || 0.24,
    adaptive_layer_height: $('cfg-adaptive').checked,
    wall_count: Number($('cfg-walls').value) || 3,
    wall_order: $('cfg-wall-order').value,
    outer_wall_width: Number($('cfg-outer-width').value) || 0.42,
    inner_wall_width: Number($('cfg-inner-width').value) || 0.45,
    xy_contour_comp: Number($('cfg-xy-contour').value) || 0,
    xy_hole_comp: Number($('cfg-xy-hole').value) || 0,
    elephant_foot_comp: Number($('cfg-elephant').value) || 0.15,
    arachne_thin_walls: $('cfg-arachne').checked,
    spiral_vase: $('cfg-vase').checked,
    fuzzy_skin: $('cfg-fuzzy').value,
    fuzzy_skin_thickness: Number($('cfg-fuzzy-thick').value) || 0.30,
    fuzzy_skin_point_dist: Number($('cfg-fuzzy-dist').value) || 0.80,

    // Infill & Surfaces
    infill_density: Number($('cfg-infill-density').value),
    infill_pattern: $('cfg-infill-pattern').value,
    top_layers: Number($('cfg-top-layers').value),
    bottom_layers: Number($('cfg-bottom-layers').value),
    top_pattern: $('cfg-top-pattern').value,
    bottom_pattern: $('cfg-top-pattern').value,
    detect_bridges: $('cfg-bridges').checked,
    bridge_flow: Number($('cfg-bridge-flow').value) || 0.95,
    bridge_speed: Number($('cfg-bridge-speed').value) || 25,
    ironing: $('cfg-ironing').checked,
    ironing_flow: Number($('cfg-iron-flow').value) || 0.10,
    ironing_spacing: Number($('cfg-iron-space').value) || 0.10,
    infill_angle_deg: Number($('cfg-infill-angle').value) || 45,
    infill_overlap_pct: Number($('cfg-infill-overlap').value) || 20,

    // Supports & Adhesion
    support_type: $('cfg-support-type').value,
    support_placement: $('cfg-support-place').value,
    support_overhang_angle: Number($('cfg-support-angle').value) || 50,
    support_interface_layers: Number($('cfg-support-iface').value) || 2,
    support_density: Number($('cfg-support-density').value) || 18,
    support_z_gap: Number($('cfg-support-zgap').value) || 0.20,
    support_xy_gap: Number($('cfg-support-xygap').value) || 0.50,
    tree_branch_diameter: Number($('cfg-tree-dia').value) || 2.4,
    tree_max_branch_angle: Number($('cfg-tree-angle').value) || 45,
    support_brim: $('cfg-support-brim').checked,
    adhesion: $('cfg-adhesion').value,
    brim_width: Number($('cfg-brim-width').value) || 5,
    raft_layers: Number($('cfg-raft-layers').value) || 2,

    // Temps, Flow, Speeds & Accel
    nozzle_temp: Number($('cfg-nozzle-temp').value) || 210,
    first_layer_nozzle_temp: (Number($('cfg-nozzle-temp').value) || 210) + 5,
    bed_temp: Number($('cfg-bed-temp').value) || 60,
    first_layer_bed_temp: Math.max(0, (Number($('cfg-bed-temp').value) || 60) + 5),
    flow_ratio: (Number($('cfg-flow-ratio').value) || 100) / 100,
    max_volumetric_flow: Number($('cfg-max-flow').value) || 18,
    pressure_advance: Number($('cfg-pa').value) || 0,
    speed_outer_wall: Number($('cfg-spd-outer').value) || 110,
    speed_inner_wall: Number($('cfg-spd-inner').value) || 165,
    speed_infill: Number($('cfg-spd-infill').value) || 220,
    speed_travel: Number($('cfg-spd-travel').value) || 320,
    speed_first_layer: Number($('cfg-spd-first').value) || 25,
    small_perimeter_speed: Number($('cfg-spd-small').value) || 28,
    overhang_slowdown: $('cfg-overhang-slow').checked,
    accel_default: Number($('cfg-accel-def').value) || 4000,
    accel_outer_wall: Number($('cfg-accel-outer').value) || 2400,
    fan_max_pct: Number($('cfg-fan-max').value) || 100,
    min_layer_time_sec: Number($('cfg-min-layer-time').value) || 8,

    // Seam, Retraction, Firmware, Pause & Custom G-Code
    seam_position: $('cfg-seam').value,
    scarf_seam: $('cfg-scarf').checked,
    combing: $('cfg-combing').checked,
    arc_fitting: $('cfg-arcs').checked,
    retract_length: Number($('cfg-retract-len').value) || 0.8,
    retract_speed: Number($('cfg-retract-spd').value) || 35,
    z_hop: Number($('cfg-zhop').value) || 0.25,
    wipe_on_retract: $('cfg-wipe').checked,
    flavor: $('cfg-flavor').value,
    pause_layers: $('cfg-pause-layers').value.trim(),
    custom_start_gcode: $('cfg-custom-start').value,
    custom_end_gcode: $('cfg-custom-end').value,
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
  $('gizmo-rail').classList.toggle('hidden', isPrev);
  $('xform-card').classList.toggle('hidden', isPrev || state.gizmoMode === GIZMO_MODES.SELECT || state.gizmoMode === GIZMO_MODES.FACE);
  canvas.classList.toggle('cursor-move', !isPrev && state.gizmoMode === GIZMO_MODES.MOVE);
  canvas.classList.toggle('cursor-crosshair', !isPrev && state.gizmoMode === GIZMO_MODES.FACE);
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
      const isHidden = state.hiddenFeatures.has(ft.id);
      const btn = document.createElement('button');
      btn.type = 'button';
      btn.title = t('legendHint');
      btn.className = `leg-item${state.highlightFeature === ft.id ? ' active' : ''}${isHidden ? ' muted' : ''}`;
      btn.innerHTML = `<span class="leg-swatch" style="background:${ft.hex}"></span><span>${state.lang === 'tr' ? ft.nameTr : ft.nameEn}</span>`;
      btn.addEventListener('click', (e) => {
        if (e.shiftKey) {
          if (state.hiddenFeatures.has(ft.id)) state.hiddenFeatures.delete(ft.id);
          else state.hiddenFeatures.add(ft.id);
        } else {
          state.highlightFeature = state.highlightFeature === ft.id ? -1 : ft.id;
        }
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
    .filter((f) => f.timeSec > 0.05 || f.filamentMm > 0.05)
    .sort((a, b) => b.timeSec - a.timeSec)
    .slice(0, 7);

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
    state.hiddenFeatures.clear();

    if (renderer?.uploadToolpath) renderer.uploadToolpath(tp);
    syncLayerScrubberUi();
    $('slider-move').value = '1000';
    $('lbl-move-pct').textContent = '100%';
    $('btn-mode-preview').disabled = false;

    renderResultsCard(res.stats, res.engine);
    setMode('preview');

    const isNat = res.engine === 'native-cpp20';
    $('engine-dot').classList.toggle('wasm', !isNat);
    $('engine-badge').textContent = isNat
      ? `Native C++20 · ${res.stats.timingsMs.total.toFixed(0)}ms`
      : `WASM C++20 · ${res.stats.timingsMs.total.toFixed(0)}ms`;

    const supFeat = (res.stats.features || []).find((f) => f.id === 9 || f.id === 10);
    const hasSup = (res.stats.features || []).some((f) => (f.id === 9 || f.id === 10) && f.filamentMm > 0.1);
    if (cfg.support_type !== 'none' && !hasSup) {
      showToast(
        state.lang === 'tr'
          ? `${res.stats.layerCount} katman (${res.stats.timingsMs.total.toFixed(1)} ms) · Bu modelde >${cfg.support_overhang_angle}° sarkma yok`
          : `${res.stats.layerCount} layers (${res.stats.timingsMs.total.toFixed(1)} ms) · No overhang >${cfg.support_overhang_angle}°`,
        3600
      );
    } else {
      const supTag = hasSup ? (state.lang === 'tr' ? ' · Destek aktif' : ' · Supports active') : '';
      showToast(`${res.stats.layerCount} layers · ${res.stats.timingsMs.total.toFixed(1)} ms${supTag}`);
    }
    void supFeat;
  } catch (err) {
    showToast(`Slice error: ${err?.message || err}`, 4000);
  } finally {
    btn.disabled = false;
    lbl.textContent = t('sliceNow');
  }
}

// --- 150+ Printer Picker Modal & Presets ---
function applySelectedPrinter(printerId, frameCam = true) {
  const pr = state.presets.printers.find((p) => p.id === printerId) || state.presets.printers[0];
  if (!pr) return;
  state.printerId = pr.id;
  state.bedX = pr.bedX || 220;
  state.bedY = pr.bedY || 220;
  state.bedZ = pr.bedZ || 250;
  state.originCenter = !!pr.originCenter;

  $('lbl-active-printer').textContent = pr.name;
  $('lbl-active-printer-sub').textContent =
    `${state.bedX}×${state.bedY}×${state.bedZ} mm · ${(pr.flavor || 'marlin2').toUpperCase()} · ${pr.directDrive === false ? 'Bowden' : 'Direct'} · ${state.presets.printers.length}`;

  $('cfg-bed-x').value = String(state.bedX);
  $('cfg-bed-y').value = String(state.bedY);
  $('cfg-bed-z').value = String(state.bedZ);
  $('cfg-origin-center').checked = state.originCenter;
  if (pr.flavor) $('cfg-flavor').value = pr.flavor;
  if (pr.maxFlow) $('cfg-max-flow').value = String(pr.maxFlow);
  if (pr.retractLen != null) $('cfg-retract-len').value = String(pr.retractLen);
  if (pr.retractSpd != null) $('cfg-retract-spd').value = String(pr.retractSpd);
  if (pr.speedOuter != null) $('cfg-spd-outer').value = String(pr.speedOuter);
  if (pr.speedInner != null) $('cfg-spd-inner').value = String(pr.speedInner);
  if (pr.speedInfill != null) $('cfg-spd-infill').value = String(pr.speedInfill);
  if (pr.speedTravel != null) $('cfg-spd-travel').value = String(pr.speedTravel);
  if (pr.accelDefault != null) $('cfg-accel-def').value = String(pr.accelDefault);
  if (pr.accelOuter != null) $('cfg-accel-outer').value = String(pr.accelOuter);
  if (pr.pa != null) $('cfg-pa').value = String(pr.pa);

  refreshModelTransform(false);
  if (frameCam) frameBuildPlate(true);
}

function renderPrinterModal() {
  const printers = state.presets.printers || [];
  const vendors = ['ALL', ...Array.from(new Set(printers.map((p) => p.vendor || 'Other')))];
  const vc = $('vendor-chips');
  vc.innerHTML = '';
  for (const v of vendors) {
    const b = document.createElement('button');
    b.type = 'button';
    b.className = `vendor-chip${state.selectedVendor === v ? ' active' : ''}`;
    b.textContent = v === 'ALL' ? (state.lang === 'tr' ? `Tümü (${printers.length})` : `All (${printers.length})`) : v;
    b.addEventListener('click', () => {
      state.selectedVendor = v;
      renderPrinterModal();
    });
    vc.appendChild(b);
  }

  const q = $('printer-search').value.trim().toLowerCase();
  const grid = $('printer-grid');
  grid.innerHTML = '';
  const list = printers.filter((p) => {
    if (state.selectedVendor !== 'ALL' && (p.vendor || 'Other') !== state.selectedVendor) return false;
    if (!q) return true;
    return (
      p.name.toLowerCase().includes(q) ||
      (p.vendor || '').toLowerCase().includes(q) ||
      (p.kinematics || '').toLowerCase().includes(q) ||
      (p.flavor || '').toLowerCase().includes(q) ||
      p.id.toLowerCase().includes(q)
    );
  });

  for (const p of list) {
    const card = document.createElement('button');
    card.type = 'button';
    card.className = `printer-card${p.id === state.printerId ? ' selected' : ''}`;
    card.innerHTML =
      `<span class="printer-card-name">${p.name}</span>` +
      `<span class="printer-card-meta mono">${p.bedX}×${p.bedY}×${p.bedZ} mm · ${p.kinematics || 'FDM'} · ${(p.flavor || 'marlin2').toUpperCase()}</span>` +
      `<span class="printer-card-meta mono">${p.directDrive === false ? 'Bowden' : 'Direct Drive'} · ${p.maxFlow || 16} mm³/s · ${p.speedOuter || 60} mm/s</span>`;
    card.addEventListener('click', () => {
      applySelectedPrinter(p.id, true);
      $('printer-modal').classList.add('hidden');
      showToast(`${p.name} (${p.bedX}×${p.bedY}×${p.bedZ})`);
    });
    grid.appendChild(card);
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
  fill('cfg-material', state.presets.materials);
  fill('cfg-quality', state.presets.qualities);
  $('cfg-material').value = 'pla';
  $('cfg-quality').value = 'standard_020';
  applySelectedPrinter(state.printerId, false);
  syncMaterialPresetFields();
  syncQualityPresetFields();
}

function syncMaterialPresetFields() {
  const m = state.presets.materials.find((x) => x.id === $('cfg-material').value);
  if (m) {
    $('cfg-nozzle-temp').value = String(m.nozzleTemp);
    $('cfg-bed-temp').value = String(m.bedTemp);
    if (m.maxFlow) $('cfg-max-flow').value = String(m.maxFlow);
    if (m.fanMax != null) $('cfg-fan-max').value = String(m.fanMax);
    if (m.minLayerTime != null) $('cfg-min-layer-time').value = String(m.minLayerTime);
  }
}

function syncQualityPresetFields() {
  const q = state.presets.qualities.find((x) => x.id === $('cfg-quality').value);
  if (q) {
    $('cfg-layer-height').value = q.layerHeight.toFixed(2);
    $('cfg-first-layer').value = q.firstLayerHeight.toFixed(2);
    $('cfg-walls').value = String(q.walls);
    if (q.topLayers != null) $('cfg-top-layers').value = String(q.topLayers);
    if (q.bottomLayers != null) $('cfg-bottom-layers').value = String(q.bottomLayers);
    $('cfg-infill-density').value = String(q.infillPct);
    $('lbl-infill-density').textContent = `${q.infillPct}%`;
  }
}

function populateDemoModels() {
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
      if (d.id === 'overhang-arch' || d.id === 'temp-tower') {
        if ($('cfg-support-type').value === 'none') {
          $('cfg-support-type').value = 'tree';
        }
      }
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

// --- 3D Viewport Pointer Interaction (Gizmo Drag + Direct Model Drag + Place-on-Face + Camera Orbit/Pan) ---
function currentViewProj() {
  const view = cam.viewMatrix();
  const proj = cam.projMatrix(canvas.width, canvas.height);
  return Mat4.multiply(proj, view);
}

function bindEvents() {
  window.addEventListener('resize', () => {
    const dpr = Math.min(window.devicePixelRatio || 1, 2.0);
    const w = Math.max(1, Math.round(window.innerWidth * dpr));
    const h = Math.max(1, Math.round(window.innerHeight * dpr));
    canvas.width = w; canvas.height = h;
    renderer?.resize(w, h, dpr);
    requestRender(true);
  });

  let dragMode = null; // 'orbit' | 'pan' | 'gizmo'
  let gizmoDrag = null;
  let lastX = 0, lastY = 0;

  canvas.addEventListener('pointerdown', (e) => {
    $('model-popover').classList.add('hidden');
    lastX = e.clientX; lastY = e.clientY;
    const cssW = window.innerWidth, cssH = window.innerHeight;
    const vp = currentViewProj();

    // Right or Middle click ALWAYS pans/orbits camera cleanly
    if (e.button === 2 || e.button === 1 || (e.button === 0 && e.altKey)) {
      dragMode = e.button === 2 ? 'pan' : 'orbit';
      canvas.setPointerCapture(e.pointerId);
      return;
    }

    if (e.button === 0 && state.mode === 'prepare' && state.mesh?.ok && state.xformInfo) {
      const ray = screenToWorldRay(e.clientX, e.clientY, cssW, cssH, vp);

      // 1. Place on Face mode: raycast exact triangle & snap face normal to Z=0!
      if (state.gizmoMode === GIZMO_MODES.FACE) {
        const hit = raycastMeshWorld(ray, state.mesh, state.xformInfo.matrix);
        if (hit) {
          const euler = computePlaceOnFaceEuler(state.xform, hit.normal);
          state.xform.rotX = euler.rotX;
          state.xform.rotY = euler.rotY;
          state.xform.rotZ = euler.rotZ;
          refreshModelTransform(true);
          showToast(t('faceSnapped'));
          return;
        }
        // Clicked background in Face mode -> orbit camera
        dragMode = 'orbit';
        canvas.setPointerCapture(e.pointerId);
        return;
      }

      // 2. Hit-test active 3D Gizmo handles (Move / Rotate / Scale)
      const hitAxis = hitTestGizmo(
        e.clientX, e.clientY, cssW, cssH, vp,
        state.gizmoMode, state.xformInfo.center, state.xformInfo.radius
      );

      // 3. In Move mode, clicking directly ON the model ALSO grabs it in XY!
      let directBodyGrab = false;
      if (!hitAxis && state.gizmoMode === GIZMO_MODES.MOVE) {
        const tBox = intersectRayAabb(ray, state.xformInfo.bboxMin, state.xformInfo.bboxMax);
        if (tBox !== null) directBodyGrab = true;
      }

      if (hitAxis || directBodyGrab) {
        const axis = hitAxis || 'xy';
        state.activeGizmoAxis = axis;
        dragMode = 'gizmo';
        const planeZ = state.xformInfo.center.z;
        const startHit = intersectRayPlane(ray, { x: 0, y: 0, z: planeZ }, { x: 0, y: 0, z: 1 });
        gizmoDrag = {
          mode: state.gizmoMode,
          axis,
          startX: e.clientX,
          startY: e.clientY,
          planeZ,
          startHit,
          orig: { ...state.xform },
        };
        canvas.setPointerCapture(e.pointerId);
        refreshModelTransform(false);
        return;
      }
    }

    // Default: Orbit (left drag on empty space) or Pan (Shift+left)
    dragMode = e.shiftKey ? 'pan' : 'orbit';
    canvas.setPointerCapture(e.pointerId);
  });

  canvas.addEventListener('pointermove', (e) => {
    const cssW = window.innerWidth, cssH = window.innerHeight;
    if (!dragMode) {
      if (state.mode === 'prepare' && state.xformInfo && state.gizmoMode !== GIZMO_MODES.SELECT && state.gizmoMode !== GIZMO_MODES.FACE) {
        const hv = hitTestGizmo(e.clientX, e.clientY, cssW, cssH, currentViewProj(), state.gizmoMode, state.xformInfo.center, state.xformInfo.radius);
        if (hv !== state.hoverGizmoAxis) {
          state.hoverGizmoAxis = hv;
          refreshModelTransform(false);
        }
      }
      return;
    }

    const dx = e.clientX - lastX, dy = e.clientY - lastY;
    lastX = e.clientX; lastY = e.clientY;
    const dpr = renderer?.dpr || 1;

    if (dragMode === 'gizmo' && gizmoDrag) {
      const { mode, axis, orig } = gizmoDrag;
      if (mode === GIZMO_MODES.MOVE) {
        const ray = screenToWorldRay(e.clientX, e.clientY, cssW, cssH, currentViewProj());
        const hit = intersectRayPlane(ray, { x: 0, y: 0, z: gizmoDrag.planeZ }, { x: 0, y: 0, z: 1 });
        if (hit && gizmoDrag.startHit) {
          let nx = orig.posX + (axis !== 'y' ? (hit.x - gizmoDrag.startHit.x) : 0);
          let ny = orig.posY + (axis !== 'x' ? (hit.y - gizmoDrag.startHit.y) : 0);
          if (e.shiftKey) { nx = Math.round(nx / 5) * 5; ny = Math.round(ny / 5) * 5; }
          state.xform.posX = Math.round(nx * 10) / 10;
          state.xform.posY = Math.round(ny * 10) / 10;
          refreshModelTransform(true);
        }
      } else if (mode === GIZMO_MODES.ROTATE) {
        const deltaDeg = (e.clientX - gizmoDrag.startX) * 0.65 - (e.clientY - gizmoDrag.startY) * 0.35;
        const snap = (v) => (e.shiftKey ? Math.round(v / 15) * 15 : Math.round(v * 2) / 2);
        const wrap = (v) => ((((v + 180) % 360) + 360) % 360) - 180;
        if (axis === 'x') state.xform.rotX = wrap(snap(orig.rotX + deltaDeg));
        else if (axis === 'y') state.xform.rotY = wrap(snap(orig.rotY + deltaDeg));
        else state.xform.rotZ = wrap(snap(orig.rotZ + deltaDeg));
        refreshModelTransform(true);
      } else if (mode === GIZMO_MODES.SCALE) {
        const factor = Math.max(0.1, Math.min(5.0, 1.0 + ((e.clientX - gizmoDrag.startX) - (e.clientY - gizmoDrag.startY)) * 0.0045));
        if (axis === 'uniform' || state.xform.uniformScale) {
          state.xform.scaleX = Math.max(0.1, Math.min(5.0, orig.scaleX * factor));
          state.xform.scaleY = Math.max(0.1, Math.min(5.0, orig.scaleY * factor));
          state.xform.scaleZ = Math.max(0.1, Math.min(5.0, orig.scaleZ * factor));
        } else if (axis === 'x') {
          state.xform.scaleX = Math.max(0.1, Math.min(5.0, orig.scaleX * factor));
        } else if (axis === 'y') {
          state.xform.scaleY = Math.max(0.1, Math.min(5.0, orig.scaleY * factor));
        } else if (axis === 'z') {
          state.xform.scaleZ = Math.max(0.1, Math.min(5.0, orig.scaleZ * factor));
        }
        refreshModelTransform(true);
      }
      return;
    }

    if (dragMode === 'orbit') cam.orbit(dx * dpr, dy * dpr, canvas.width, canvas.height);
    else cam.pan(dx * dpr, dy * dpr, canvas.width, canvas.height);
    requestRender(true);
  });

  canvas.addEventListener('pointerup', (e) => {
    dragMode = null;
    gizmoDrag = null;
    if (state.activeGizmoAxis) {
      state.activeGizmoAxis = null;
      refreshModelTransform(false);
    }
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
  window.addEventListener('dragover', (e) => e.preventDefault());
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

  // Left Gizmo Rail
  document.querySelectorAll('.gizmo-btn[data-gizmo]').forEach((btn) => {
    btn.addEventListener('click', () => setGizmoMode(btn.dataset.gizmo));
  });
  $('btn-mirror-x').addEventListener('click', () => {
    state.xform.mirrorX = !state.xform.mirrorX;
    refreshModelTransform(true);
  });
  $('btn-center-part').addEventListener('click', () => {
    state.xform.posX = 0; state.xform.posY = 0;
    refreshModelTransform(true);
  });
  $('btn-reset-xform').addEventListener('click', () => {
    state.xform = createDefaultTransform();
    $('cfg-copies').value = '1';
    refreshModelTransform(true);
    frameBuildPlate(true);
  });
  $('btn-xform-toggle').addEventListener('click', () => {
    const hidden = $('xform-body').classList.toggle('hidden');
    $('btn-xform-toggle').textContent = hidden ? '+' : '−';
  });
  $('btn-rot-x90').addEventListener('click', () => { state.xform.rotX = ((state.xform.rotX + 90 + 180) % 360) - 180; refreshModelTransform(true); });
  $('btn-rot-y90').addEventListener('click', () => { state.xform.rotY = ((state.xform.rotY + 90 + 180) % 360) - 180; refreshModelTransform(true); });
  $('btn-rot-z90').addEventListener('click', () => { state.xform.rotZ = ((state.xform.rotZ + 90 + 180) % 360) - 180; refreshModelTransform(true); });

  // Numeric Transform Inputs synced bidirectionally with 3D Gizmo!
  $('cfg-pos-x').addEventListener('input', (e) => { state.xform.posX = Number(e.target.value) || 0; refreshModelTransform(false); });
  $('cfg-pos-y').addEventListener('input', (e) => { state.xform.posY = Number(e.target.value) || 0; refreshModelTransform(false); });
  $('cfg-rot-x').addEventListener('input', (e) => { state.xform.rotX = Number(e.target.value) || 0; refreshModelTransform(false); });
  $('cfg-rot-y').addEventListener('input', (e) => { state.xform.rotY = Number(e.target.value) || 0; refreshModelTransform(false); });
  $('cfg-rot-z').addEventListener('input', (e) => { state.xform.rotZ = Number(e.target.value) || 0; refreshModelTransform(false); });
  $('cfg-uniform-scale').addEventListener('change', (e) => { state.xform.uniformScale = e.target.checked; });
  const bindScale = (id, key) => {
    $(id).addEventListener('input', (e) => {
      const s = Math.max(0.1, Math.min(5.0, (Number(e.target.value) || 100) / 100));
      if (state.xform.uniformScale) {
        state.xform.scaleX = state.xform.scaleY = state.xform.scaleZ = s;
        $('cfg-scale-x').value = $('cfg-scale-y').value = $('cfg-scale-z').value = String(Math.round(s * 100));
      } else {
        state.xform[key] = s;
      }
      refreshModelTransform(false);
    });
  };
  bindScale('cfg-scale-x', 'scaleX');
  bindScale('cfg-scale-y', 'scaleY');
  bindScale('cfg-scale-z', 'scaleZ');

  // Complexity Level Bar (Simple / Advanced / Expert)
  document.querySelectorAll('.lvl-btn').forEach((btn) => {
    btn.addEventListener('click', () => {
      state.level = btn.dataset.level;
      document.body.classList.remove('level-simple', 'level-advanced', 'level-expert');
      document.body.classList.add(`level-${state.level}`);
      document.querySelectorAll('.lvl-btn').forEach((b) => b.classList.toggle('active', b === btn));
    });
  });

  // 5 Dock Tabs
  document.querySelectorAll('.dock-tab').forEach((btn) => {
    btn.addEventListener('click', () => {
      const tab = btn.dataset.tab;
      document.querySelectorAll('.dock-tab').forEach((b) => {
        const on = b.dataset.tab === tab;
        b.classList.toggle('active', on);
        b.setAttribute('aria-selected', String(on));
      });
      ['quality', 'infill', 'support', 'speed', 'machine'].forEach((p) => {
        $(`panel-${p}`).classList.toggle('hidden', p !== tab);
      });
    });
  });

  // Searchable 150+ Printer Picker Modal
  $('btn-open-printer-modal').addEventListener('click', () => {
    $('printer-modal').classList.remove('hidden');
    renderPrinterModal();
    $('printer-search').focus();
  });
  $('btn-printer-close').addEventListener('click', () => $('printer-modal').classList.add('hidden'));
  $('printer-search').addEventListener('input', renderPrinterModal);

  // Material, Quality, Nozzle & Manual Bed listeners
  $('cfg-material').addEventListener('change', syncMaterialPresetFields);
  $('cfg-quality').addEventListener('change', syncQualityPresetFields);
  $('cfg-nozzle').addEventListener('change', (e) => {
    const n = Number(e.target.value) || 0.4;
    $('cfg-outer-width').value = (Math.round(n * 1.05 * 100) / 100).toFixed(2);
    $('cfg-inner-width').value = (Math.round(n * 1.125 * 100) / 100).toFixed(2);
  });
  $('cfg-infill-density').addEventListener('input', (e) => {
    $('lbl-infill-density').textContent = `${e.target.value}%`;
  });
  $('cfg-support-angle').addEventListener('input', (e) => {
    state.overhangAngle = Number(e.target.value) || 50;
    requestRender(true);
  });
  const syncManualBed = () => {
    state.bedX = Math.max(80, Number($('cfg-bed-x').value) || 220);
    state.bedY = Math.max(80, Number($('cfg-bed-y').value) || 220);
    state.bedZ = Math.max(80, Number($('cfg-bed-z').value) || 250);
    state.originCenter = $('cfg-origin-center').checked;
    refreshModelTransform(false);
  };
  ['cfg-bed-x', 'cfg-bed-y', 'cfg-bed-z', 'cfg-origin-center'].forEach((id) => $(id).addEventListener('change', syncManualBed));

  // Slice & Export CTA
  $('btn-slice').addEventListener('click', runSlice);
  $('btn-export-gcode').addEventListener('click', () => {
    slicer.downloadGcode(state.modelName).catch((e) => showToast(e.message));
  });
  $('btn-inspect-gcode').addEventListener('click', async () => {
    try {
      const { text, truncated, totalBytes } = await slicer.fetchGcodeText(1536 * 1024);
      lastGcodeFullText = text;
      $('gcode-viewer').textContent = text;
      $('gcode-meta').textContent =
        `${(totalBytes / 1024).toFixed(1)} KB${truncated ? ' (showing first 1.5 MB)' : ''} · ${text.split('\n').length} lines`;
      $('gcode-modal').classList.remove('hidden');
    } catch (e) { showToast(e.message); }
  });
  $('btn-gcode-close').addEventListener('click', () => $('gcode-modal').classList.add('hidden'));
  $('btn-gcode-copy').addEventListener('click', async () => {
    try { await navigator.clipboard.writeText($('gcode-viewer').textContent); showToast(t('copied')); } catch {}
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

  // Preview Controls & Layer Scrubber
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
    syncLayerScrubberUi(); requestRender(true);
  });
  $('btn-layer-down').addEventListener('click', () => {
    if (!state.toolpath) return;
    state.previewMaxLayer = Math.max(0, state.previewMaxLayer - 1);
    syncLayerScrubberUi(); requestRender(true);
  });
  $('btn-add-pause').addEventListener('click', () => {
    const cur = state.previewMaxLayer | 0;
    const raw = $('cfg-pause-layers').value.trim();
    const set = new Set(raw ? raw.split(/[^0-9]+/).filter(Boolean).map(Number) : []);
    if (set.has(cur)) set.delete(cur); else set.add(cur);
    $('cfg-pause-layers').value = Array.from(set).sort((a, b) => a - b).join(', ');
    showToast(`${t('pauseAdded')}: #${cur + 1}`);
  });
  $('slider-move').addEventListener('input', (e) => {
    state.previewMoveFrac = (Number(e.target.value) || 0) / 1000;
    $('lbl-move-pct').textContent = `${Math.round(state.previewMoveFrac * 100)}%`;
    requestRender(true);
  });
  $('btn-play').addEventListener('click', () => {
    if (!state.toolpath) return;
    state.playing = !state.playing;
    if (state.playing && state.previewMoveFrac >= 0.99 && state.previewMaxLayer >= state.toolpath.layerCount - 1) {
      state.previewMaxLayer = 0; state.previewMoveFrac = 0.02; syncLayerScrubberUi();
    }
    $('btn-play').textContent = state.playing ? '❚❚' : '▶';
    requestRender(true);
  });
  $('btn-play-spd').addEventListener('click', () => {
    const speeds = [1, 4, 16];
    state.playSpeed = speeds[(speeds.indexOf(state.playSpeed) + 1) % speeds.length];
    $('btn-play-spd').textContent = `${state.playSpeed}×`;
  });
  $('sel-color-mode').addEventListener('change', (e) => {
    state.colorMode = Number(e.target.value) | 0;
    renderLegend(); requestRender(true);
  });
  $('btn-travels').addEventListener('click', () => {
    state.showTravels = !state.showTravels;
    $('btn-travels').setAttribute('aria-pressed', String(state.showTravels));
    requestRender(true);
  });
  $('btn-seams').addEventListener('click', () => {
    state.showSeams = !state.showSeams;
    $('btn-seams').setAttribute('aria-pressed', String(state.showSeams));
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

  // Theme, Language & Mobile Dock
  $('btn-theme').addEventListener('click', () => {
    state.darkTheme = !state.darkTheme;
    document.body.classList.toggle('theme-dark', state.darkTheme);
    document.body.classList.toggle('theme-light', !state.darkTheme);
    requestRender(true);
  });
  $('btn-lang').addEventListener('click', () => {
    state.lang = state.lang === 'tr' ? 'en' : 'tr';
    try { localStorage.setItem('bigslicer-lang', state.lang); } catch {}
    applyTranslations();
  });
  $('btn-dock-toggle').addEventListener('click', () => {
    const c = $('slicer-dock').classList.toggle('collapsed');
    $('btn-dock-toggle').setAttribute('aria-expanded', String(!c));
  });

  // Keyboard Shortcuts (PrusaSlicer / OrcaSlicer standard!)
  window.addEventListener('keydown', (e) => {
    if (e.target && /^(INPUT|SELECT|TEXTAREA)$/.test(e.target.tagName)) return;
    if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') { e.preventDefault(); runSlice(); return; }
    if (e.key === 'Escape') {
      $('gcode-modal').classList.add('hidden');
      $('printer-modal').classList.add('hidden');
      $('model-popover').classList.add('hidden');
      return;
    }
    const k = e.key.toLowerCase();
    if (k === 'o') $('file-input').click();
    else if (k === 't') $('btn-theme').click();
    else if (k === '0') $('btn-view-iso').click();
    else if (k === '5') $('btn-view-top').click();
    else if (k === '1') $('btn-view-front').click();
    else if (state.mode === 'prepare' && k === 'v') setGizmoMode(GIZMO_MODES.SELECT);
    else if (state.mode === 'prepare' && k === 'm') setGizmoMode(GIZMO_MODES.MOVE);
    else if (state.mode === 'prepare' && k === 'r') setGizmoMode(GIZMO_MODES.ROTATE);
    else if (state.mode === 'prepare' && k === 's') setGizmoMode(GIZMO_MODES.SCALE);
    else if (state.mode === 'prepare' && k === 'f') setGizmoMode(GIZMO_MODES.FACE);
    else if (e.key === ' ' && state.mode === 'preview') { e.preventDefault(); $('btn-play').click(); }
    else if (e.key === 'ArrowUp' && state.mode === 'preview') { e.preventDefault(); $('btn-layer-up').click(); }
    else if (e.key === 'ArrowDown' && state.mode === 'preview') { e.preventDefault(); $('btn-layer-down').click(); }
  });
}

async function boot() {
  applyTranslations();
  initRenderer();
  populateDemoModels();
  populatePresetSelects();
  bindEvents();
  setGizmoMode(GIZMO_MODES.MOVE);

  // Load initial built-in demo STL immediately
  const demoBuf = generateDemoStl('flange-housing');
  loadMeshBuffer(demoBuf, 'flange-housing.stl', null);

  // Detect Native C++20 Docker Server vs Offline WebAssembly C++20 Worker
  const { serverMode, health } = await slicer.detectBackend();
  $('engine-dot').classList.toggle('wasm', !serverMode);
  $('engine-badge').textContent = serverMode
    ? `Native C++20 · ${health?.cpus || 4}T`
    : 'WASM C++20 (Offline)';

  const presets = await slicer.loadPresets();
  if (presets?.printers?.length) {
    state.presets = presets;
    populatePresetSelects();
  }
  if (serverMode) fetchKasaLibrary();
}

boot();
