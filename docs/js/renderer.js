/**
 * Katman WebGL2 Bare-Metal Renderer:
 *   - Evolved directly from stlviewer-web (8-byte UNORM16 mesh, dFdx/dFdy 0-byte normals,
 *     4096-tri chunk frustum+normal-cone culling, 16x Halton SSAA, 0.01ms GPU depth picking)
 *   - PLUS Hardware-Instanced 3D Extruded Filament Tube Renderer (drawArraysInstanced)
 *     rendering 200,000+ moves with real 3D lighting, 5 color modes (Feature/Speed/Flow/Width/Height),
 *     0-ms layer & within-layer playback scrubbing, live 3D brass nozzle marker, and real printer build plate!
 */

import { Mat4, Frustum, subpixelJitterNdc } from './math3d.js';
import { UpAxis } from './camera.js';
import { MATCAP_SIZE, MatcapKind, generateMatcapRgba8 } from './matcap.js';
import { cullChunks } from './culling.js';
import { buildUnitBeadGeometry, FEATURE_TYPES, COLOR_MODES, getNozzlePosition } from './toolpath_geometry.js';

const FULLSCREEN_VS = `#version 300 es
precision highp float;
out vec2 vUV;
void main() {
  const vec2 pos[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
  gl_Position = vec4(pos[gl_VertexID], 0.0, 1.0);
  vUV = pos[gl_VertexID] * 0.5 + 0.5;
}
`;

const BG_FS = `#version 300 es
precision highp float;
in vec2 vUV;
uniform int uDark;
out vec4 FragColor;
void main() {
  float t = clamp(vUV.y, 0.0, 1.0);
  vec2 d = vUV - vec2(0.5);
  float vig = dot(d, d) * 0.35;
  vec3 col;
  if (uDark != 0) {
    vec3 bot = vec3(0.011, 0.014, 0.019);
    vec3 top = vec3(0.025, 0.031, 0.043);
    col = mix(bot, top, smoothstep(0.0, 1.0, t)) * (1.0 - vig * 0.45);
  } else {
    vec3 bot = vec3(0.58, 0.62, 0.68);
    vec3 top = vec3(0.84, 0.87, 0.91);
    col = mix(bot, top, smoothstep(0.0, 1.0, t)) * (1.0 - vig * 0.12);
  }
  FragColor = vec4(col, 1.0);
}
`;

const MESH_VS = `#version 300 es
precision highp float;
precision highp int;
layout(location = 0) in vec4 aPos;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform vec3 uBboxMin;
uniform vec3 uQuantScale;
uniform vec4 uClipPlane;
uniform int uClipOn;

out vec3 vViewPos;
out vec3 vWorldPos;
out vec3 vBary;
out float vClipDist;

void main() {
  vec3 localPos = uBboxMin + aPos.xyz * uQuantScale;
  vec4 wp = uModel * vec4(localPos, 1.0);
  vWorldPos = wp.xyz;
  vec4 vp = uView * wp;
  vViewPos = vp.xyz;
  vClipDist = (uClipOn != 0) ? dot(vec4(wp.xyz, 1.0), uClipPlane) : 1.0;
  int vid = gl_VertexID % 3;
  vBary = vec3(vid == 0 ? 1.0 : 0.0, vid == 1 ? 1.0 : 0.0, vid == 2 ? 1.0 : 0.0);
  gl_Position = uProj * vp;
}
`;

const MESH_FS = `#version 300 es
precision highp float;
precision highp int;
in vec3 vViewPos;
in vec3 vWorldPos;
in vec3 vBary;
in float vClipDist;

uniform sampler2D uMatcap;
uniform int uClipOn;
uniform int uXray;
uniform int uWire;
uniform int uDark;
uniform int uOverhang;
uniform float uOverhangCos;
uniform int uYUp;
uniform float uGhostAlpha;

out vec4 FragColor;

void main() {
  if (uClipOn != 0 && vClipDist < 0.0) discard;

  vec3 dpdx = dFdx(vViewPos);
  vec3 dpdy = dFdy(vViewPos);
  vec3 crossN = cross(dpdx, dpdy);
  float len2 = dot(crossN, crossN);
  vec3 N = (len2 > 1e-20) ? (crossN * inversesqrt(len2)) : vec3(0.0, 0.0, 1.0);

  bool backFace = !gl_FrontFacing;
  if (backFace) N = -N;
  if (N.z < 0.0) {
    N.z = 0.0;
    float xy2 = dot(N.xy, N.xy);
    if (xy2 > 1e-8) N.xy *= inversesqrt(xy2);
  }

  vec3 col;
  float alpha = 1.0;

  if (uGhostAlpha > 0.0) {
    float rim = pow(clamp(1.0 - abs(N.z), 0.0, 1.0), 1.6);
    vec3 base = (uDark != 0) ? vec3(0.42, 0.64, 0.92) : vec3(0.15, 0.30, 0.56);
    col = base * (0.45 + 0.95 * rim);
    alpha = clamp(uGhostAlpha * (0.35 + 1.15 * rim), 0.06, 0.42);
  } else if (uXray != 0) {
    float rim = pow(clamp(1.0 - abs(N.z), 0.0, 1.0), 1.85);
    vec3 base = (uDark != 0) ? vec3(0.16, 0.56, 0.96) : vec3(0.04, 0.24, 0.54);
    col = base * (0.28 + 1.35 * rim);
    alpha = clamp(0.14 + 0.72 * rim, 0.14, 0.88);
  } else {
    vec2 uv = vec2(N.x * 0.492 + 0.5, 0.5 - N.y * 0.492);
    col = texture(uMatcap, uv).rgb;
    if (backFace) {
      vec3 backTint = vec3(0.55, 0.22, 0.14);
      col = mix(col * 0.75, backTint * (0.45 + 0.65 * clamp(N.z, 0.0, 1.0)), 0.68);
    } else if (uOverhang != 0) {
      vec3 wn = cross(dFdx(vWorldPos), dFdy(vWorldPos));
      float wl2 = dot(wn, wn);
      if (wl2 > 1e-20) {
        wn *= inversesqrt(wl2);
        float upDot = (uYUp != 0) ? wn.y : wn.z;
        if (upDot < -uOverhangCos && vWorldPos.z > 0.3) {
          vec3 coral = vec3(0.92, 0.16, 0.10) * (0.35 + 0.65 * clamp(N.z, 0.0, 1.0));
          col = mix(col * 0.30, coral, 0.82);
        }
      }
    }
  }

  if (uWire != 0) {
    vec3 fw = max(fwidth(vBary), vec3(1e-5));
    vec3 f = smoothstep(vec3(0.0), fw * 1.35, vBary);
    float edge = 1.0 - min(min(f.x, f.y), f.z);
    vec3 wc = (uDark != 0) ? vec3(0.24, 0.58, 0.96) : vec3(0.05, 0.18, 0.40);
    col = mix(col, wc, edge * 0.78);
    alpha = max(alpha, edge * 0.85);
  }

  FragColor = vec4(col, alpha);
}
`;

// Hardware-Instanced 3D Filament Bead Tube Shader (PrusaSlicer / Bambu Studio visual grade!)
const TOOLPATH_VS = `#version 300 es
precision highp float;
precision highp int;

// Per-vertex unit stadium prism (divisor 0)
layout(location = 0) in vec3 aLocalPos;   // u in [0,1], v in [-0.5,0.5], w in [-1,0]
layout(location = 1) in vec3 aLocalNorm;

// Per-instance move attributes (divisor 1, stride = 40 bytes = 10 floats)
layout(location = 2) in vec4 aSegXY;      // x0, y0, x1, y1
layout(location = 3) in vec4 aZHWS;       // zTop, height, width, speed
layout(location = 4) in vec2 aFlowMeta;   // flow, featureType + flags*256

uniform mat4 uView;
uniform mat4 uProj;
uniform int uColorMode;
uniform float uRangeMin;
uniform float uRangeMax;
uniform int uHighlightFeature;
uniform int uHiddenMask;
uniform vec3 uFeatureColors[13];

out vec3 vNormal;
out vec3 vColor;
out float vDim;

vec3 heatRamp(float t) {
  t = clamp(t, 0.0, 1.0);
  // Crisp engineering Viridis/Turbo-inspired perceptual ramp (Cobalt -> Cyan -> Emerald -> Amber -> Crimson)
  vec3 c0 = vec3(0.14, 0.36, 0.88);
  vec3 c1 = vec3(0.12, 0.78, 0.82);
  vec3 c2 = vec3(0.24, 0.84, 0.44);
  vec3 c3 = vec3(0.96, 0.76, 0.16);
  vec3 c4 = vec3(0.94, 0.22, 0.18);
  if (t < 0.25) return mix(c0, c1, t * 4.0);
  if (t < 0.50) return mix(c1, c2, (t - 0.25) * 4.0);
  if (t < 0.75) return mix(c2, c3, (t - 0.50) * 4.0);
  return mix(c3, c4, (t - 0.75) * 4.0);
}

void main() {
  vec2 p0 = aSegXY.xy;
  vec2 p1 = aSegXY.zw;
  vec2 d = p1 - p0;
  float len = length(d);
  vec2 tangent = (len > 1e-5) ? (d / len) : vec2(1.0, 0.0);
  vec2 side = vec2(-tangent.y, tangent.x);

  float zTop = aZHWS.x;
  float h = max(0.04, aZHWS.y);
  float w = max(0.08, aZHWS.z);

  vec2 xy = mix(p0, p1, aLocalPos.x) + side * (aLocalPos.y * w);
  float z = zTop + aLocalPos.z * h;
  vec3 worldPos = vec3(xy, z);

  vec3 worldNorm = normalize(vec3(side * aLocalNorm.y, aLocalNorm.z));
  vNormal = mat3(uView) * worldNorm;

  int meta = int(aFlowMeta.y + 0.5);
  int ftype = clamp(meta & 255, 0, 12);

  if ((uHiddenMask & (1 << ftype)) != 0) {
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    vNormal = vec3(0.0, 0.0, 1.0);
    vColor = vec3(0.0);
    vDim = 0.0;
    return;
  }

  if (uColorMode == 0) {
    vColor = uFeatureColors[ftype];
  } else {
    float val = aZHWS.w; // speed
    if (uColorMode == 2) val = aFlowMeta.x;
    else if (uColorMode == 3) val = aZHWS.z;
    else if (uColorMode == 4) val = aZHWS.y;
    float t = (val - uRangeMin) / max(1e-4, uRangeMax - uRangeMin);
    vColor = heatRamp(t);
  }

  vDim = (uHighlightFeature >= 0 && ftype != uHighlightFeature) ? 0.22 : 1.0;

  gl_Position = uProj * (uView * vec4(worldPos, 1.0));
}
`;

const TOOLPATH_FS = `#version 300 es
precision highp float;
in vec3 vNormal;
in vec3 vColor;
in float vDim;
out vec4 FragColor;

void main() {
  vec3 N = normalize(vNormal);
  vec3 L1 = normalize(vec3(0.42, 0.68, 0.60));
  vec3 L2 = normalize(vec3(-0.55, -0.30, 0.78));
  float diff1 = max(dot(N, L1), 0.0);
  float diff2 = max(dot(N, L2), 0.0);
  vec3 H = normalize(L1 + vec3(0.0, 0.0, 1.0));
  float spec = pow(max(dot(N, H), 0.0), 28.0) * 0.24;

  // Convert sRGB palette color to linear for ACES-clean SSAA accumulation
  vec3 lin = pow(max(vColor, vec3(0.0)), vec3(2.2));
  vec3 lit = lin * (0.28 + 0.62 * diff1 + 0.22 * diff2) + vec3(spec);
  lit = mix(vec3(0.08, 0.09, 0.11), lit, vDim);
  FragColor = vec4(lit, 1.0);
}
`;

const PICK_VS = `#version 300 es
precision highp float;
precision highp int;
layout(location = 0) in vec4 aPos;
uniform mat4 uModel;
uniform mat4 uPickVP;
uniform vec3 uBboxMin;
uniform vec3 uQuantScale;
uniform vec4 uClipPlane;
uniform int uClipOn;
out float vClipDist;
void main() {
  vec4 wp = uModel * vec4(uBboxMin + aPos.xyz * uQuantScale, 1.0);
  vClipDist = (uClipOn != 0) ? dot(wp, uClipPlane) : 1.0;
  gl_Position = uPickVP * wp;
}
`;

const PICK_FS = `#version 300 es
precision highp float;
precision highp int;
in float vClipDist;
uniform int uClipOn;
out vec4 FragColor;
void main() {
  if (uClipOn != 0 && vClipDist < 0.0) discard;
  float z = clamp(gl_FragCoord.z, 0.0, 1.0);
  uint zi = uint(z * 16777215.0 + 0.5);
  FragColor = vec4(float((zi >> 16u) & 255u) / 255.0, float((zi >> 8u) & 255u) / 255.0, float(zi & 255u) / 255.0, 1.0);
}
`;

const BED_VS = `#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
uniform mat4 uVP;
out vec3 vWorld;
void main() {
  vWorld = aPos;
  gl_Position = uVP * vec4(aPos, 1.0);
}
`;

const BED_FS = `#version 300 es
precision highp float;
precision highp int;
in vec3 vWorld;
uniform vec2 uBedMin;
uniform vec2 uBedSize;
uniform int uDark;
out vec4 FragColor;

float gridLine(vec2 coord, float spacing) {
  vec2 p = coord / spacing;
  vec2 g = abs(fract(p - 0.5) - 0.5) / max(fwidth(p), vec2(1e-5));
  return 1.0 - min(min(g.x, g.y), 1.0);
}

void main() {
  vec2 p = vWorld.xy - uBedMin;
  if (p.x < 0.0 || p.y < 0.0 || p.x > uBedSize.x || p.y > uBedSize.y) discard;

  float minor = gridLine(p, 10.0);
  float major = gridLine(p, 50.0);
  vec2 edgeDist = min(p, uBedSize - p);
  float fw = max(fwidth(p.x), fwidth(p.y));
  float border = 1.0 - smoothstep(0.0, max(fw * 2.2, 0.4), min(edgeDist.x, edgeDist.y));

  vec3 baseCol  = (uDark != 0) ? vec3(0.032, 0.039, 0.052) : vec3(0.46, 0.50, 0.56);
  vec3 minorCol = (uDark != 0) ? vec3(0.09, 0.11, 0.15)    : vec3(0.35, 0.39, 0.45);
  vec3 majorCol = (uDark != 0) ? vec3(0.16, 0.20, 0.27)    : vec3(0.24, 0.28, 0.34);
  vec3 bordCol  = (uDark != 0) ? vec3(0.24, 0.52, 0.94)    : vec3(0.12, 0.34, 0.78);

  vec3 col = baseCol;
  col = mix(col, minorCol, minor * 0.55);
  col = mix(col, majorCol, major * 0.75);
  col = mix(col, bordCol, border * 0.85);

  // Subtle origin corner X (red) and Y (green) tick along first 25mm
  if (p.y < 1.2 && p.x < 25.0) col = mix(col, vec3(0.90, 0.25, 0.22), 0.75);
  if (p.x < 1.2 && p.y < 25.0) col = mix(col, vec3(0.22, 0.78, 0.35), 0.75);

  float alpha = (uDark != 0) ? 0.84 : 0.72;
  FragColor = vec4(col, alpha);
}
`;

const LINE_VS = `#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aCol;
uniform mat4 uVP;
out vec4 vCol;
void main() {
  gl_Position = uVP * vec4(aPos, 1.0);
  vCol = aCol;
}
`;

const LINE_FS = `#version 300 es
precision highp float;
in vec4 vCol;
out vec4 FragColor;
void main() {
  FragColor = vCol;
}
`;

const ACCUM_FS = `#version 300 es
precision highp float;
in vec2 vUV;
uniform sampler2D uTex;
uniform float uWeight;
out vec4 FragColor;
void main() {
  FragColor = vec4(texture(uTex, vUV).rgb, uWeight);
}
`;

const RESOLVE_FS = `#version 300 es
precision highp float;
in vec2 vUV;
uniform sampler2D uTex;
out vec4 FragColor;
vec3 linearToSrgb(vec3 c) {
  c = clamp(c, 0.0, 1.0);
  vec3 lo = c * 12.92;
  vec3 hi = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
  return mix(hi, lo, lessThanEqual(c, vec3(0.0031308)));
}
void main() {
  vec3 s = linearToSrgb(texture(uTex, vUV).rgb);
  float n = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
  s += (n - 0.5) / 255.0;
  FragColor = vec4(clamp(s, 0.0, 1.0), 1.0);
}
`;

function compileProgram(gl, vsSrc, fsSrc, tag) {
  const comp = (type, src) => {
    const s = gl.createShader(type);
    gl.shaderSource(s, src);
    gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) {
      const log = gl.getShaderInfoLog(s);
      gl.deleteShader(s);
      throw new Error(`${tag} shader error: ${log}`);
    }
    return s;
  };
  const vs = comp(gl.VERTEX_SHADER, vsSrc);
  const fs = comp(gl.FRAGMENT_SHADER, fsSrc);
  const p = gl.createProgram();
  gl.attachShader(p, vs);
  gl.attachShader(p, fs);
  gl.linkProgram(p);
  gl.deleteShader(vs);
  gl.deleteShader(fs);
  if (!gl.getProgramParameter(p, gl.LINK_STATUS)) {
    const log = gl.getProgramInfoLog(p);
    gl.deleteProgram(p);
    throw new Error(`${tag} link error: ${log}`);
  }
  return p;
}

function appendNozzleCone(out, pos) {
  const rTop = 2.6, rTip = 0.25, hTip = 4.2, hBody = 8.0;
  const brass = [0.95, 0.74, 0.22, 0.96];
  const segs = 12;
  const seg = (a, b) => out.push(a[0], a[1], a[2], ...brass, b[0], b[1], b[2], ...brass);
  for (let i = 0; i < segs; i++) {
    const a0 = (Math.PI * 2 * i) / segs;
    const a1 = (Math.PI * 2 * (i + 1)) / segs;
    const p0 = [pos.x + rTip * Math.cos(a0), pos.y + rTip * Math.sin(a0), pos.z + 0.1];
    const p1 = [pos.x + rTop * Math.cos(a0), pos.y + rTop * Math.sin(a0), pos.z + hTip];
    const p2 = [pos.x + rTop * Math.cos(a1), pos.y + rTop * Math.sin(a1), pos.z + hTip];
    const p3 = [pos.x + rTop * Math.cos(a0), pos.y + rTop * Math.sin(a0), pos.z + hTip + hBody];
    const p4 = [pos.x + rTop * Math.cos(a1), pos.y + rTop * Math.sin(a1), pos.z + hTip + hBody];
    seg(p0, p1);
    seg(p1, p2);
    seg(p1, p3);
    seg(p3, p4);
  }
}

export class Renderer {
  constructor(canvas) {
    this.canvas = canvas;
    this.backend = 'WebGL2';
    this.gl = null;
    this.width = 1;
    this.height = 1;
    this.dpr = 1;
    this.fp16Fbo = false;
    this.accumCount = 0;
    this.targetSamples = 8;
    this.hasMesh = false;
    this.hasToolpath = false;
    this.toolpath = null;
    this.modelMatrix = Mat4.identity();
    this.matcapTex = [];
    this.lineScratch = new Float32Array(2048);
    this.bedScratch = new Float32Array(42);
    this.triadScratch = new Float32Array(42);
    this.pickPixel = new Uint8Array(4);
    this.lastCstats = { totalChunks: 0, visibleChunks: 0, visibleTris: 0, drawCalls: 0 };
  }

  init(width, height, dpr) {
    let gl = this.canvas.getContext('webgl2', {
      antialias: false, alpha: false, depth: false, stencil: false,
      powerPreference: 'low-power', preserveDrawingBuffer: false,
    });
    if (!gl) gl = this.canvas.getContext('webgl2');
    if (!gl) throw new Error('WebGL2 unavailable');
    this.gl = gl;
    this.width = Math.max(1, width | 0);
    this.height = Math.max(1, height | 0);
    this.dpr = dpr || 1;

    this.extFloat = gl.getExtension('EXT_color_buffer_float');
    this.extHalf = gl.getExtension('EXT_color_buffer_half_float');

    this.bgProg = compileProgram(gl, FULLSCREEN_VS, BG_FS, 'bg');
    this.uBgDark = gl.getUniformLocation(this.bgProg, 'uDark');

    this.meshProg = compileProgram(gl, MESH_VS, MESH_FS, 'mesh');
    this.uMeshModel = gl.getUniformLocation(this.meshProg, 'uModel');
    this.uMeshView = gl.getUniformLocation(this.meshProg, 'uView');
    this.uMeshProj = gl.getUniformLocation(this.meshProg, 'uProj');
    this.uMeshBmin = gl.getUniformLocation(this.meshProg, 'uBboxMin');
    this.uMeshQscale = gl.getUniformLocation(this.meshProg, 'uQuantScale');
    this.uMeshClipPlane = gl.getUniformLocation(this.meshProg, 'uClipPlane');
    this.uMeshClipOn = gl.getUniformLocation(this.meshProg, 'uClipOn');
    this.uMeshMatcap = gl.getUniformLocation(this.meshProg, 'uMatcap');
    this.uMeshXray = gl.getUniformLocation(this.meshProg, 'uXray');
    this.uMeshWire = gl.getUniformLocation(this.meshProg, 'uWire');
    this.uMeshDark = gl.getUniformLocation(this.meshProg, 'uDark');
    this.uMeshOverhang = gl.getUniformLocation(this.meshProg, 'uOverhang');
    this.uMeshOverhangCos = gl.getUniformLocation(this.meshProg, 'uOverhangCos');
    this.uMeshYUp = gl.getUniformLocation(this.meshProg, 'uYUp');
    this.uMeshGhostAlpha = gl.getUniformLocation(this.meshProg, 'uGhostAlpha');

    this.tpProg = compileProgram(gl, TOOLPATH_VS, TOOLPATH_FS, 'toolpath');
    this.uTpView = gl.getUniformLocation(this.tpProg, 'uView');
    this.uTpProj = gl.getUniformLocation(this.tpProg, 'uProj');
    this.uTpColorMode = gl.getUniformLocation(this.tpProg, 'uColorMode');
    this.uTpRangeMin = gl.getUniformLocation(this.tpProg, 'uRangeMin');
    this.uTpRangeMax = gl.getUniformLocation(this.tpProg, 'uRangeMax');
    this.uTpHighlight = gl.getUniformLocation(this.tpProg, 'uHighlightFeature');
    this.uTpHiddenMask = gl.getUniformLocation(this.tpProg, 'uHiddenMask');
    this.uTpFeatColors = gl.getUniformLocation(this.tpProg, 'uFeatureColors');

    // Upload static feature palette once
    const pal = new Float32Array(13 * 3);
    for (let i = 0; i < FEATURE_TYPES.length; i++) {
      pal[i * 3 + 0] = FEATURE_TYPES[i].color[0];
      pal[i * 3 + 1] = FEATURE_TYPES[i].color[1];
      pal[i * 3 + 2] = FEATURE_TYPES[i].color[2];
    }
    gl.useProgram(this.tpProg);
    gl.uniform3fv(this.uTpFeatColors, pal);

    this.pickProg = compileProgram(gl, PICK_VS, PICK_FS, 'pick');
    this.uPickModel = gl.getUniformLocation(this.pickProg, 'uModel');
    this.uPickVP = gl.getUniformLocation(this.pickProg, 'uPickVP');
    this.uPickBmin = gl.getUniformLocation(this.pickProg, 'uBboxMin');
    this.uPickQscale = gl.getUniformLocation(this.pickProg, 'uQuantScale');
    this.uPickClipPlane = gl.getUniformLocation(this.pickProg, 'uClipPlane');
    this.uPickClipOn = gl.getUniformLocation(this.pickProg, 'uClipOn');

    this.bedProg = compileProgram(gl, BED_VS, BED_FS, 'bed');
    this.uBedVP = gl.getUniformLocation(this.bedProg, 'uVP');
    this.uBedMin = gl.getUniformLocation(this.bedProg, 'uBedMin');
    this.uBedSize = gl.getUniformLocation(this.bedProg, 'uBedSize');
    this.uBedDark = gl.getUniformLocation(this.bedProg, 'uDark');

    this.lineProg = compileProgram(gl, LINE_VS, LINE_FS, 'line');
    this.uLineVP = gl.getUniformLocation(this.lineProg, 'uVP');

    this.accumProg = compileProgram(gl, FULLSCREEN_VS, ACCUM_FS, 'accum');
    this.uAccumTex = gl.getUniformLocation(this.accumProg, 'uTex');
    this.uAccumWeight = gl.getUniformLocation(this.accumProg, 'uWeight');

    this.resolveProg = compileProgram(gl, FULLSCREEN_VS, RESOLVE_FS, 'resolve');
    this.uResolveTex = gl.getUniformLocation(this.resolveProg, 'uTex');

    this.emptyVao = gl.createVertexArray();

    // Mesh VAO (8-byte UNORM16)
    this.meshVao = gl.createVertexArray();
    this.meshVbo = gl.createBuffer();
    gl.bindVertexArray(this.meshVao);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.meshVbo);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 4, gl.UNSIGNED_SHORT, true, 8, 0);

    // Instanced 3D Toolpath VAO (Unit 36-vert bead + per-instance 40-byte VBO!)
    const beadGeom = buildUnitBeadGeometry();
    this.beadVertCount = (beadGeom.length / 6) | 0;
    this.beadVbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, this.beadVbo);
    gl.bufferData(gl.ARRAY_BUFFER, beadGeom, gl.STATIC_DRAW);

    this.tpExtrudeVbo = gl.createBuffer();
    this.tpVao = gl.createVertexArray();
    gl.bindVertexArray(this.tpVao);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.beadVbo);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 24, 0);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 24, 12);

    gl.bindBuffer(gl.ARRAY_BUFFER, this.tpExtrudeVbo);
    gl.enableVertexAttribArray(2);
    gl.vertexAttribPointer(2, 4, gl.FLOAT, false, 40, 0);
    gl.vertexAttribDivisor(2, 1);
    gl.enableVertexAttribArray(3);
    gl.vertexAttribPointer(3, 4, gl.FLOAT, false, 40, 16);
    gl.vertexAttribDivisor(3, 1);
    gl.enableVertexAttribArray(4);
    gl.vertexAttribPointer(4, 2, gl.FLOAT, false, 40, 32);
    gl.vertexAttribDivisor(4, 1);

    // Line / Bed / Overlay VAO
    this.lineVao = gl.createVertexArray();
    this.lineVbo = gl.createBuffer();
    gl.bindVertexArray(this.lineVao);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.lineVbo);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 28, 0);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 4, gl.FLOAT, false, 28, 12);
    gl.bindVertexArray(null);

    // Matcap textures
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
    for (let i = 0; i < MatcapKind.Count; i++) {
      const tex = gl.createTexture();
      const pixels = generateMatcapRgba8(i);
      gl.bindTexture(gl.TEXTURE_2D, tex);
      while (gl.getError() !== gl.NO_ERROR) {}
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.SRGB8_ALPHA8, MATCAP_SIZE, MATCAP_SIZE, 0, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
      if (gl.getError() === gl.NO_ERROR) {
        gl.generateMipmap(gl.TEXTURE_2D);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
      } else {
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, MATCAP_SIZE, MATCAP_SIZE, 0, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
      }
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      this.matcapTex.push(tex);
    }
    gl.bindTexture(gl.TEXTURE_2D, null);
    this._createFbos(this.width, this.height);
  }

  _destroyFbos() {
    const gl = this.gl;
    if (!gl) return;
    if (this.sceneFbo) gl.deleteFramebuffer(this.sceneFbo);
    if (this.accumFbo) gl.deleteFramebuffer(this.accumFbo);
    if (this.sceneColor) gl.deleteTexture(this.sceneColor);
    if (this.accumColor) gl.deleteTexture(this.accumColor);
    if (this.sceneDepthRb) gl.deleteRenderbuffer(this.sceneDepthRb);
    this.sceneFbo = this.accumFbo = this.sceneColor = this.accumColor = this.sceneDepthRb = null;
  }

  _createFbos(w, h) {
    const gl = this.gl;
    this._destroyFbos();
    const tryBuild = (intFmt, type, depFmt) => {
      this.sceneColor = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, this.sceneColor);
      gl.texImage2D(gl.TEXTURE_2D, 0, intFmt, w, h, 0, gl.RGBA, type, null);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

      this.sceneDepthRb = gl.createRenderbuffer();
      gl.bindRenderbuffer(gl.RENDERBUFFER, this.sceneDepthRb);
      gl.renderbufferStorage(gl.RENDERBUFFER, depFmt, w, h);

      this.sceneFbo = gl.createFramebuffer();
      gl.bindFramebuffer(gl.FRAMEBUFFER, this.sceneFbo);
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, this.sceneColor, 0);
      gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER, this.sceneDepthRb);
      const ok1 = gl.checkFramebufferStatus(gl.FRAMEBUFFER) === gl.FRAMEBUFFER_COMPLETE;

      this.accumColor = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, this.accumColor);
      gl.texImage2D(gl.TEXTURE_2D, 0, intFmt, w, h, 0, gl.RGBA, type, null);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

      this.accumFbo = gl.createFramebuffer();
      gl.bindFramebuffer(gl.FRAMEBUFFER, this.accumFbo);
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, this.accumColor, 0);
      const ok2 = gl.checkFramebufferStatus(gl.FRAMEBUFFER) === gl.FRAMEBUFFER_COMPLETE;

      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      return ok1 && ok2;
    };
    if ((this.extFloat || this.extHalf) && tryBuild(gl.RGBA16F, gl.HALF_FLOAT, gl.DEPTH_COMPONENT24)) {
      this.fp16Fbo = true; return;
    }
    this._destroyFbos();
    if (tryBuild(gl.RGBA8, gl.UNSIGNED_BYTE, gl.DEPTH_COMPONENT24)) { this.fp16Fbo = false; return; }
    this._destroyFbos();
    if (tryBuild(gl.RGBA8, gl.UNSIGNED_BYTE, gl.DEPTH_COMPONENT16)) { this.fp16Fbo = false; return; }
    throw new Error('Failed to create WebGL2 FBOs');
  }

  resize(w, h, dpr) {
    const nw = Math.max(1, w | 0), nh = Math.max(1, h | 0);
    this.dpr = dpr || 1;
    if (nw === this.width && nh === this.height) return;
    this.width = nw; this.height = nh;
    this._createFbos(nw, nh);
    this.accumCount = 0;
  }

  setTargetSamples(n) {
    this.targetSamples = Math.max(1, n | 0);
    if (this.accumCount > this.targetSamples) this.accumCount = this.targetSamples;
  }

  resetAccumulation() { this.accumCount = 0; }
  needsMoreSamples() { return this.accumCount < this.targetSamples; }

  setModelMatrix(m) {
    this.modelMatrix = m || Mat4.identity();
    this.accumCount = 0;
  }

  uploadMesh(mesh) {
    const gl = this.gl;
    if (!mesh || !mesh.ok || !mesh.vertices?.length) {
      this.hasMesh = false;
      return;
    }
    gl.bindBuffer(gl.ARRAY_BUFFER, this.meshVbo);
    gl.bufferData(gl.ARRAY_BUFFER, mesh.vertices, gl.STATIC_DRAW);
    gl.bindBuffer(gl.ARRAY_BUFFER, null);
    this.hasMesh = true;
    this.accumCount = 0;
  }

  uploadToolpath(tp) {
    const gl = this.gl;
    this.toolpath = tp;
    if (!tp || tp.extrudeTotal === 0) {
      this.hasToolpath = false;
      this.accumCount = 0;
      return;
    }
    gl.bindBuffer(gl.ARRAY_BUFFER, this.tpExtrudeVbo);
    gl.bufferData(gl.ARRAY_BUFFER, tp.extrudeInstances, gl.STATIC_DRAW);
    gl.bindBuffer(gl.ARRAY_BUFFER, null);
    this.hasToolpath = true;
    this.accumCount = 0;
  }

  clearToolpath() {
    this.toolpath = null;
    this.hasToolpath = false;
    this.accumCount = 0;
  }

  renderFrame(mesh, cam, state) {
    const gl = this.gl;
    if (this.accumCount < this.targetSamples) {
      const sampleIdx = this.accumCount;
      const { jx, jy } = subpixelJitterNdc(sampleIdx, this.width, this.height);
      const view = cam.viewMatrix();
      const proj = cam.projMatrix(this.width, this.height, jx, jy);
      const vp = Mat4.multiply(proj, view);

      gl.bindFramebuffer(gl.FRAMEBUFFER, this.sceneFbo);
      gl.viewport(0, 0, this.width, this.height);

      // 1. Background gradient
      gl.disable(gl.DEPTH_TEST);
      gl.depthMask(false);
      gl.disable(gl.CULL_FACE);
      gl.disable(gl.BLEND);
      gl.useProgram(this.bgProg);
      gl.uniform1i(this.uBgDark, state.darkTheme ? 1 : 0);
      gl.bindVertexArray(this.emptyVao);
      gl.drawArrays(gl.TRIANGLES, 0, 3);

      gl.depthMask(true);
      gl.clearDepth(1.0);
      gl.clear(gl.DEPTH_BUFFER_BIT);
      gl.enable(gl.DEPTH_TEST);
      gl.depthFunc(gl.LESS);

      const bx = state.bedX || 220;
      const by = state.bedY || 220;
      const bz = state.bedZ || 250;
      const bMinX = state.originCenter ? -bx * 0.5 : 0;
      const bMinY = state.originCenter ? -by * 0.5 : 0;
      const bMaxX = bMinX + bx;
      const bMaxY = bMinY + by;

      // 2. Printer Build Plate Surface (exact bedX x bedY at Z=0, supports Delta origin_center!)
      if (state.showGrid) {
        const bq = this.bedScratch;
        const gz = -0.04;
        bq.set([
          bMinX, bMinY, gz, 0, 0, 0, 0,
          bMaxX, bMinY, gz, 0, 0, 0, 0,
          bMaxX, bMaxY, gz, 0, 0, 0, 0,
          bMinX, bMinY, gz, 0, 0, 0, 0,
          bMaxX, bMaxY, gz, 0, 0, 0, 0,
          bMinX, bMaxY, gz, 0, 0, 0, 0,
        ]);
        gl.disable(gl.CULL_FACE);
        gl.enable(gl.BLEND);
        gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
        gl.useProgram(this.bedProg);
        gl.uniformMatrix4fv(this.uBedVP, false, vp);
        gl.uniform2f(this.uBedMin, bMinX, bMinY);
        gl.uniform2f(this.uBedSize, bx, by);
        gl.uniform1i(this.uBedDark, state.darkTheme ? 1 : 0);
        gl.bindVertexArray(this.lineVao);
        gl.bindBuffer(gl.ARRAY_BUFFER, this.lineVbo);
        gl.bufferData(gl.ARRAY_BUFFER, bq, gl.STREAM_DRAW);
        gl.drawArrays(gl.TRIANGLES, 0, 6);
        gl.disable(gl.BLEND);

        // Build volume subtle corner pillars
        const cc = state.darkTheme ? [0.24, 0.44, 0.78, 0.28] : [0.14, 0.30, 0.65, 0.28];
        const cage = [];
        const seg = (a, b) => cage.push(a[0], a[1], a[2], ...cc, b[0], b[1], b[2], ...cc);
        seg([bMinX, bMinY, 0], [bMinX, bMinY, bz]); seg([bMaxX, bMinY, 0], [bMaxX, bMinY, bz]);
        seg([bMaxX, bMaxY, 0], [bMaxX, bMaxY, bz]); seg([bMinX, bMaxY, 0], [bMinX, bMaxY, bz]);
        seg([bMinX, bMinY, bz], [bMaxX, bMinY, bz]); seg([bMaxX, bMinY, bz], [bMaxX, bMaxY, bz]);
        seg([bMaxX, bMaxY, bz], [bMinX, bMaxY, bz]); seg([bMinX, bMaxY, bz], [bMinX, bMinY, bz]);
        if (this.lineScratch.length < cage.length) this.lineScratch = new Float32Array(cage.length);
        this.lineScratch.set(cage);
        gl.enable(gl.BLEND);
        gl.depthMask(false);
        gl.useProgram(this.lineProg);
        gl.uniformMatrix4fv(this.uLineVP, false, vp);
        gl.bindBuffer(gl.ARRAY_BUFFER, this.lineVbo);
        gl.bufferData(gl.ARRAY_BUFFER, this.lineScratch.subarray(0, cage.length), gl.STREAM_DRAW);
        gl.drawArrays(gl.LINES, 0, (cage.length / 7) | 0);
        gl.disable(gl.BLEND);
        gl.depthMask(true);
      }

      const inPreview = state.mode === 'preview' && this.hasToolpath && this.toolpath;

      // 3. Hardware-Instanced 3D Filament Toolpaths (in Preview mode!)
      if (inPreview) {
        const tp = this.toolpath;
        const maxLayer = Math.max(0, Math.min(tp.layerCount - 1, state.previewMaxLayer | 0));
        const minLayer = Math.max(0, Math.min(maxLayer, state.previewMinLayer | 0));
        const startInst = tp.layers[minLayer].extrudeFirst;
        const topLy = tp.layers[maxLayer];
        const topVisible = Math.max(0, Math.min(topLy.extrudeCount, Math.ceil(topLy.extrudeCount * (state.previewMoveFrac ?? 1.0))));
        const endInst = topLy.extrudeFirst + topVisible;
        const instCount = Math.max(0, endInst - startInst);

        if (instCount > 0) {
          gl.enable(gl.CULL_FACE);
          gl.cullFace(gl.BACK);
          gl.useProgram(this.tpProg);
          gl.uniformMatrix4fv(this.uTpView, false, view);
          gl.uniformMatrix4fv(this.uTpProj, false, proj);
          const cm = state.colorMode | 0;
          gl.uniform1i(this.uTpColorMode, cm);
          let rmin = tp.ranges.minSpeed, rmax = tp.ranges.maxSpeed;
          if (cm === COLOR_MODES.FLOW) { rmin = tp.ranges.minFlow; rmax = tp.ranges.maxFlow; }
          else if (cm === COLOR_MODES.WIDTH) { rmin = tp.ranges.minWidth; rmax = tp.ranges.maxWidth; }
          else if (cm === COLOR_MODES.HEIGHT) { rmin = tp.ranges.minHeight; rmax = tp.ranges.maxHeight; }
          gl.uniform1f(this.uTpRangeMin, rmin);
          gl.uniform1f(this.uTpRangeMax, rmax);
          gl.uniform1i(this.uTpHighlight, state.highlightFeature ?? -1);
          let hiddenMask = 0;
          if (state.hiddenFeatures) {
            for (const id of state.hiddenFeatures) hiddenMask |= (1 << (id | 0));
          }
          gl.uniform1i(this.uTpHiddenMask, hiddenMask);

          gl.bindVertexArray(this.tpVao);
          // Rebind instance pointers shifted by startInst * 40 bytes (zero buffer re-upload!)
          gl.bindBuffer(gl.ARRAY_BUFFER, this.tpExtrudeVbo);
          const byteOff = startInst * 40;
          gl.vertexAttribPointer(2, 4, gl.FLOAT, false, 40, byteOff + 0);
          gl.vertexAttribPointer(3, 4, gl.FLOAT, false, 40, byteOff + 16);
          gl.vertexAttribPointer(4, 2, gl.FLOAT, false, 40, byteOff + 32);

          gl.drawArraysInstanced(gl.TRIANGLES, 0, this.beadVertCount, instCount);
          gl.disable(gl.CULL_FACE);
        }

        // Optional Travel Moves (on active top layer or visible range)
        if (state.showTravels && tp.travelTotal > 0) {
          const tFirst = state.travelsAllLayers ? tp.layers[minLayer].travelFirst : topLy.travelFirst;
          const tEnd = topLy.travelFirst + topLy.travelCount;
          const tCount = Math.min(25000, Math.max(0, tEnd - tFirst));
          if (tCount > 0) {
            const need = tCount * 14;
            if (this.lineScratch.length < need) this.lineScratch = new Float32Array(need);
            const ls = this.lineScratch;
            const tr = tp.travelInstances;
            const col = [0.28, 0.72, 1.0, 0.62];
            for (let k = 0; k < tCount; k++) {
              const sb = (tFirst + k) * 10;
              const db = k * 14;
              const z = tr[sb + 4] + 0.05;
              ls[db + 0] = tr[sb + 0]; ls[db + 1] = tr[sb + 1]; ls[db + 2] = z;
              ls[db + 3] = col[0]; ls[db + 4] = col[1]; ls[db + 5] = col[2]; ls[db + 6] = col[3];
              ls[db + 7] = tr[sb + 2]; ls[db + 8] = tr[sb + 3]; ls[db + 9] = z;
              ls[db + 10] = col[0]; ls[db + 11] = col[1]; ls[db + 12] = col[2]; ls[db + 13] = col[3];
            }
            gl.enable(gl.BLEND);
            gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
            gl.depthMask(false);
            gl.useProgram(this.lineProg);
            gl.uniformMatrix4fv(this.uLineVP, false, vp);
            gl.bindVertexArray(this.lineVao);
            gl.bindBuffer(gl.ARRAY_BUFFER, this.lineVbo);
            gl.bufferData(gl.ARRAY_BUFFER, ls.subarray(0, need), gl.STREAM_DRAW);
            gl.drawArrays(gl.LINES, 0, tCount * 2);
            gl.disable(gl.BLEND);
            gl.depthMask(true);
          }
        }

        // Live 3D Brass Nozzle Printhead Marker
        if (state.showNozzle) {
          const npos = getNozzlePosition(tp, maxLayer, state.previewMoveFrac ?? 1.0);
          if (npos) {
            const nv = [];
            appendNozzleCone(nv, npos);
            if (this.lineScratch.length < nv.length) this.lineScratch = new Float32Array(nv.length);
            this.lineScratch.set(nv);
            gl.useProgram(this.lineProg);
            gl.uniformMatrix4fv(this.uLineVP, false, vp);
            gl.bindVertexArray(this.lineVao);
            gl.bindBuffer(gl.ARRAY_BUFFER, this.lineVbo);
            gl.bufferData(gl.ARRAY_BUFFER, this.lineScratch.subarray(0, nv.length), gl.STREAM_DRAW);
            gl.drawArrays(gl.LINES, 0, (nv.length / 7) | 0);
          }
        }

        // Optional Z-Seam / Retraction Cross Markers on Active Layer
        if (state.showSeams && tp.travelTotal > 0) {
          const sv = [];
          const tr = tp.travelInstances;
          const tEnd = topLy.travelFirst + topLy.travelCount;
          const r = 0.85;
          const sc = [0.88, 0.65, 1.0, 0.95];
          const addCross = (x, y, z) => {
            sv.push(x - r, y, z, ...sc, x + r, y, z, ...sc);
            sv.push(x, y - r, z, ...sc, x, y + r, z, ...sc);
            sv.push(x, y, z - r * 0.6, ...sc, x, y, z + r * 1.4, ...sc);
          };
          for (let k = topLy.travelFirst; k < tEnd && sv.length < 36000; k++) {
            const sb = k * 10;
            const flags = ((tr[sb + 9] | 0) >> 8) & 255;
            if (flags & 4) addCross(tr[sb], tr[sb + 1], tr[sb + 4] + 0.08);
          }
          if (sv.length > 0) {
            if (this.lineScratch.length < sv.length) this.lineScratch = new Float32Array(sv.length);
            this.lineScratch.set(sv);
            gl.disable(gl.DEPTH_TEST);
            gl.useProgram(this.lineProg);
            gl.uniformMatrix4fv(this.uLineVP, false, vp);
            gl.bindVertexArray(this.lineVao);
            gl.bindBuffer(gl.ARRAY_BUFFER, this.lineVbo);
            gl.bufferData(gl.ARRAY_BUFFER, this.lineScratch.subarray(0, sv.length), gl.STREAM_DRAW);
            gl.drawArrays(gl.LINES, 0, (sv.length / 7) | 0);
            gl.enable(gl.DEPTH_TEST);
          }
        }
      }

      // 4. STL Mesh (solid in Prepare mode, or optional translucent Ghost Shell in Preview mode!)
      const drawMesh = this.hasMesh && mesh && mesh.ok && (!inPreview || state.ghostShell);
      if (drawMesh) {
        const isGhost = inPreview && state.ghostShell;
        const useCull = !isGhost && !state.twoSided && !state.sliceEnabled && !state.xray;
        if (useCull) {
          gl.enable(gl.CULL_FACE);
          gl.cullFace(gl.BACK);
          gl.frontFace(gl.CCW);
        } else {
          gl.disable(gl.CULL_FACE);
        }

        gl.useProgram(this.meshProg);
        gl.uniformMatrix4fv(this.uMeshModel, false, this.modelMatrix);
        gl.uniformMatrix4fv(this.uMeshView, false, view);
        gl.uniformMatrix4fv(this.uMeshProj, false, proj);
        gl.uniform3f(this.uMeshBmin, mesh.bboxMin.x, mesh.bboxMin.y, mesh.bboxMin.z);
        gl.uniform3f(this.uMeshQscale, mesh.quantScale.x, mesh.quantScale.y, mesh.quantScale.z);
        gl.uniform4f(this.uMeshClipPlane, 0, 0, -1, state.sliceZ ?? 9999);
        gl.uniform1i(this.uMeshClipOn, (!inPreview && state.sliceEnabled) ? 1 : 0);
        gl.uniform1i(this.uMeshDark, state.darkTheme ? 1 : 0);
        gl.uniform1i(this.uMeshWire, (!isGhost && state.wireframe) ? 1 : 0);
        gl.uniform1i(this.uMeshOverhang, (!isGhost && state.overhang) ? 1 : 0);
        const ovRad = ((state.overhangAngle || 50) * Math.PI) / 180;
        gl.uniform1f(this.uMeshOverhangCos, Math.sin(ovRad));
        gl.uniform1i(this.uMeshYUp, cam.getUpAxis() === UpAxis.YUp ? 1 : 0);
        gl.uniform1f(this.uMeshGhostAlpha, isGhost ? 0.24 : 0.0);

        const mc = Math.max(0, Math.min(MatcapKind.Count - 1, state.matcap | 0));
        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, this.matcapTex[mc]);
        gl.uniform1i(this.uMeshMatcap, 0);

        gl.bindVertexArray(this.meshVao);
        if (isGhost || state.xray) {
          gl.depthMask(false);
          gl.enable(gl.BLEND);
          gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
          gl.uniform1i(this.uMeshXray, state.xray && !isGhost ? 1 : 0);
          gl.drawArrays(gl.TRIANGLES, 0, mesh.triCount * 3);
          gl.disable(gl.BLEND);
          gl.depthMask(true);
        } else {
          gl.uniform1i(this.uMeshXray, 0);
          gl.drawArrays(gl.TRIANGLES, 0, mesh.triCount * 3);
        }
        gl.disable(gl.CULL_FACE);
      }

      // 4.5. Interactive 3D Transform Gizmo & Out-Of-Bounds Warning Box (Prepare Mode!)
      if (!inPreview && state.gizmoLines && state.gizmoLines.length >= 14) {
        const glines = state.gizmoLines;
        if (this.lineScratch.length < glines.length) this.lineScratch = new Float32Array(glines.length);
        this.lineScratch.set(glines);
        gl.disable(gl.DEPTH_TEST);
        gl.enable(gl.BLEND);
        gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
        gl.useProgram(this.lineProg);
        gl.uniformMatrix4fv(this.uLineVP, false, vp);
        gl.bindVertexArray(this.lineVao);
        gl.bindBuffer(gl.ARRAY_BUFFER, this.lineVbo);
        gl.bufferData(gl.ARRAY_BUFFER, this.lineScratch.subarray(0, glines.length), gl.STREAM_DRAW);
        gl.drawArrays(gl.LINES, 0, (glines.length / 7) | 0);
        gl.disable(gl.BLEND);
        gl.enable(gl.DEPTH_TEST);
      }

      // 5. Progressive SSAA Accumulation
      this.accumCount++;
      gl.bindFramebuffer(gl.FRAMEBUFFER, this.accumFbo);
      gl.disable(gl.DEPTH_TEST);
      gl.depthMask(false);
      if (this.accumCount === 1) gl.disable(gl.BLEND);
      else { gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA); }
      gl.useProgram(this.accumProg);
      gl.activeTexture(gl.TEXTURE0);
      gl.bindTexture(gl.TEXTURE_2D, this.sceneColor);
      gl.uniform1i(this.uAccumTex, 0);
      gl.uniform1f(this.uAccumWeight, 1.0 / this.accumCount);
      gl.bindVertexArray(this.emptyVao);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
      gl.disable(gl.BLEND);
    }

    // 6. Resolve to screen (linear -> sRGB + blue-noise-like dither)
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.viewport(0, 0, this.width, this.height);
    gl.disable(gl.DEPTH_TEST);
    gl.depthMask(false);
    gl.disable(gl.CULL_FACE);
    gl.disable(gl.BLEND);
    gl.useProgram(this.resolveProg);
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, this.accumColor);
    gl.uniform1i(this.uResolveTex, 0);
    gl.bindVertexArray(this.emptyVao);
    gl.drawArrays(gl.TRIANGLES, 0, 3);

    gl.bindVertexArray(null);
    gl.useProgram(null);
    return { accumCount: this.accumCount };
  }
}
