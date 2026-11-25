# BigSlicer v2.0 — Ultra-Optimized C++20 / Clipper2 3D Printing Slicer & 3D Studio

**BigSlicer** is a from-scratch, zero-bloat, multi-threaded 3D printing slicer engineered for extreme slicing speed, physical G-code accuracy, rich pro customization, and universal **Native + Browser (WebGL2 / WebGL1 / Software Z-Buffer Canvas 2D)** operation:

- **Native C++20 Engine + Integrated OpenGL 3.3 Core Viewer** (`787 KB` stripped binary, `-O3 -flto -fno-exceptions -fno-rtti`, `Clipper2 1.5.2`)
- **Offline WebAssembly C++20 Build** (`372 KB` `.wasm` + `12 KB` JS loader — embeds all 153 printers, 17 materials, and full v2.0 engine inside a browser Web Worker, zero server required)
- **Universal 3D Slicer Studio (`WebGL2` / `WebGL1` / Full Software Z-Buffer `Canvas 2D`)**:
  - Interactive **3D Prepare Gizmo**: direct build-plate **Move** (`M`), **Rotate** (`R`, ±90° & 15° Shift-snap), **Scale** (`S`, uniform/per-axis), **Place on Face** (`F`, Moller–Trumbore raycast snapping any triangle face onto `Z=0`), Mirror X/Y, Center, Reset, and live out-of-bounds bed warning cage — synced 1:1 with the C++20 slicing engine.
  - Hardware-instanced **WebGL2** 36-vertex stadium-profile filament beads **and** a complete **Software Z-Buffer 3D Canvas 2D** fallback (works seamlessly even on `--disable-gpu` browsers/laptops!) showing extruded walls, Gyroid infill, vivid emerald-green supports & roof interfaces, travels, Z-seam/retract markers, live 3D brass nozzle, 5 heatmaps, and Ghost Shell.
  - **Searchable 153-Printer Picker** with vendor filters, **Simple / Advanced / Expert** settings tiers, **5-tab dock** (`Quality / Infill / Support / Speed / Machine`), `+M600` layer pause insertion, `1×/4×/16×` playback speed, interactive hide/isolate legend, `TR`/`EN`, Obsidian Studio & Alabaster Drafting themes, **0 `axe-core` WCAG AA violations**.
- **Zero-npm Node.js 22 Docker Backend** (Port `8098`, native C++20 API, zero-copy disk library slicing, SSRF-protected proxy).

---

## Verified Performance Benchmarks

Measured on a 2011 **Intel Core i5-2300** (4 cores, 2.8 GHz Sandy Bridge, local SSD, GCC 14.2 `-O3 -march=x86-64-v2 -flto`):

| Model / Scenario | Triangles | Layers | Moves | G-code | Total Time (4T) | Notes |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| **P-51 Wing_L.stl** (Gyroid 20%) | 76,272 | 54 | 31,917 | 1.00 MB | **29.2 ms** | avg 30.5 ms (slice 4.6 ms) |
| **P-51 Wing_L.stl** + Normal Grid Supports (v2.0) | 76,272 | 54 | ~59,000 | 1.81 MB | **41.3 ms** | **14.4× faster** support stage (`135.8 ms → 9.4 ms`) |
| **P-51 Wing_L.stl** + Organic Tree Supports (v2.0) | 76,272 | 54 | ~53,300 | 1.67 MB | **50.0 ms** | top-down merging trunks + flared brim |
| **P-51 Propeller.stl** (Gyroid 20%) | 39,136 | 184 | 15,748 | 0.61 MB | **36.1 ms** | avg 37.6 ms |
| **P-51 Fuselage_Rear.stl** (Gyroid 20%) | 66,426 | 223 | 55,853 | 1.78 MB | **65.4 ms** | avg 67.8 ms |
| **Parca1_AnaGovde.stl** (584 layers, Gyroid) | 18,578 | 584 | 94,170 | 3.14 MB | **82.9 ms** | 33 identical-layer cache hits |
| **Izomer_Camurluk.stl** (Gyroid 20%) | 13,536 | 193 | 119,354 | 3.83 MB | **98.2 ms** | avg 103.3 ms |
| **Synthetic Sphere 100k** (Rectilinear 4T) | 99,904 | 250 | 21,007 | — | **96.2 ms** | 1T == 4T bit-identical output |
| **Synthetic Sphere 100k** (Gyroid 4T) | 99,904 | 250 | 164,029 | — | **121.0 ms** | vs 379.1 ms 1T (**3.13×** scaling) |
| **Tube 60 mm** (Identical-Layer Cache ON) | 2,048 | 300 | 40,460 | — | **102.0 ms** | **256/300** cached (vs 176.9 ms off) |
| **Overhang & Support Arch** + Tree / Snug Support | 84 | 180 | ~22K | — | **52.5 ms** | includes Fuzzy Skin + `M600` pause |

---

## Core Slicing Engine Architecture

BigSlicer synthesizes the best engineering ideas from **PrusaSlicer/Slic3r**, **CuraEngine 5+ (Arachne)**, **OrcaSlicer**, **Bambu Studio**, **SuperSlicer**, and **IdeaMaker** into a clean, cache-friendly C++20 pipeline:

1. **Zero-Copy Mesh Ingestion & Exact 3D Transforms (`src/core/mesh.*`)**
   - Binary STL (with `solid`-header binary trap protection), ASCII STL, and Wavefront OBJ.
   - `mmap` on local filesystems + single bulk `read()` optimization for FUSE/SSHFS mounts, automatic inverted-winding detection & repair, exact XYZ rotation / per-axis scale / XY bed placement (`pos_x`, `pos_y`) / mirror synced 1:1 with the 3D viewport gizmo, multi-copy bed grid arrangement, and auto-lay-flat.
2. **CSR Z-Bucketed Slicing & O(S) Stitching (`src/core/slicer.*`)**
   - Uniform or **Dolenc–Mäkelä cusp-height adaptive layer heights** with Gaussian smoothing.
   - Compressed Sparse Row (CSR) Z-interval index maps triangles directly to crossed layers in O(1).
   - Exact CCW half-open `SLICE_LUT` + generation-stamped flat open-addressing endpoint hash stitcher + 2-stage non-manifold crack recovery.
   - Strict `0.8 µm` collinear diagonal-edge pre-pruner, `Clipper2 1.5.2` `NonZero` normalization, canonical-rotation Douglas–Peucker simplification, and Layer-0 elephant foot compensation.
3. **Perimeters, Arachne, Fuzzy Skin & Slic3r Pill Flow (`src/core/perimeters.*`, `geometry2d.*`)**
   - Exact stadium/pill cross-section math: $A = h(W - h) + \frac{\pi h^2}{4}$, overlap spacing $S = W - h(1 - \frac{\pi}{4})$.
   - Inner-First / Outer-First wall ordering, 4-tier overhang slowdown, **Small Perimeter Slowdown** (`small_perimeter_radius`, `small_perimeter_speed`), **Fuzzy Skin** (`None` / `Outside` / `AllWalls`, deterministic `splitmix64` normal perturbation with layer-0 protection), **Arachne** variable-width thin walls & medial gap fill, **Spiral Vase**, Skirt / Brim / Raft adhesion, and **Identical-Layer Perimeter Cache**.
4. **Cross-Layer Surfaces & 8 High-Speed Infill Patterns (`src/core/surfaces.*`, `infill.*`)**
   - Top/bottom solid skins (Monotonic & Rectilinear), automatic bridge detection with optimal span-angle alignment, top-surface **Ironing**.
   - Fast analytical O(V+L) scanline clipper & Y-bucketed open-polyline clipper supporting **8 patterns**: `Gyroid` (sinusoidal recurrence with $\sqrt{2}$ arc-length density compensation), `Rectilinear`, `Grid`, `Triangles`, `Cubic` (3D Z-shifted), `Honeycomb`, `Concentric`, and top-down `Lightning`.
5. **High-Speed Supports (`src/core/supports.*`)**
   - **Normal Grid** (expanded columns), **Normal Snug** (tight contour-hugging columns), and **Organic Tree** (top-down overhang tip clustering, collision-avoiding merging hollow circular trunks flaring at the build plate).
   - Buildplate-only or Everywhere placement, dense interface roof/floor layers, configurable Z-gap & XY clearance, and **Support Brim**.
   - **14.4× faster** support generation in v2.0 via vectorized scanline infill + cached column merging.
6. **Path, Seam, Pause & Lookahead Kinematics (`src/core/path_planner.*`, `motion_planner.*`)**
   - Smart Z-Seam (`Aligned`, `SharpestCorner`, `Rear`, `Nearest`, `Random`) + **Scarf Joint Seam** flow ramp.
   - Nearest-Neighbor + 2-opt TSP path ordering, Combing, Retraction / Wipe / Z-Hop, **G2/G3 Circular Arc Fitting**.
   - Forward/backward trapezoidal lookahead velocity planner (**Junction Deviation**, **Klipper SCV**, **Classic Jerk**), Max Volumetric Flow capping, Minimum Layer Time cooling slowdown, dynamic fan PWM, **Pause at Layer** (`M600` for Marlin/Bambu/RRF, `PAUSE` for Klipper), and **Custom Start/End G-Code** (`{nozzle_temp}`, `{bed_temp}` placeholders).
7. **Parallel Raw-Pointer G-Code & Compact `KTP1` Binary (`src/core/gcode_writer.*`)**
   - Multi-threaded 32-bit LUT serializer emitting deterministic **Marlin 2**, **Klipper**, **RepRapFirmware**, and **Bambu** G-code (`M82`/`M83`, Pressure/Linear Advance, `M204`/`SET_VELOCITY_LIMIT`, PrusaSlicer/OrcaSlicer metadata).
   - Zero-copy `KTP1` compact binary toolpath format (`24 bytes/move`) streamed directly to WebGL2 / WebGL1 / Software Z-Buffer Canvas 2D.

---

## Included Printer, Material & Quality Presets

- **153 Real 3D Printers** across 15 vendor families — modern CoreXY & classics/legacy models alike:
  - **Creality (27):** `Ender-3`, **`Ender-3 Pro`**, `Ender-3 V2`, `Ender-3 S1 / Pro`, `Ender-3 V3 SE / KE / CoreXZ`, `Ender-5 / Plus`, `CR-10 / V2 / S4 / S5 / Smart Pro`, `K1 / K1C / K1 Max / K2 Plus`, `Sermoon`, etc.
  - **Prusa (11):** `MK2S`, `MK3S+`, `MK4 / MK4S`, `MINI+`, `XL`, `CORE One`, `i3 MK3.5`
  - **Bambu Lab (8):** `A1 mini`, `A1`, `P1P`, `P1S`, `X1C`, `X1E`, `H2D`
  - **Anycubic (16):** `i3 Mega / Mega S`, `Mega Zero`, `Vyper`, `Kobra / 2 / 3 / Max / S1`
  - **Elegoo (11):** `Neptune 2 / 3 / 3 Pro / 3 Plus / 3 Max / 4 / 4 Pro / 4 Plus / 4 Max`, `OrangeStorm Giga`, `Centauri Carbon`
  - **Voron / RatRig / VzBot (13):** `V0.2`, `Trident 250/300/350`, `2.4 250/300/350`, `Switchwire`, `RatRig V-Core 3 / 4`, `VzBot 235 / 330`
  - **Sovol (8)**, **QIDI (6)**, **Artillery (7)**, **FlashForge (6)**, **UltiMaker (5)**, **FLSUN Delta (5, circular `origin_center=true`)**, **Kingroon (4)**, **AnkerMake (2)**
  - **Classics & Other (24):** `Anet A8`, `Anet A8 Plus`, `Tevo Tarantula`, `Wanhao Duplicator i3`, `Monoprice Select Mini V2`, `LulzBot TAZ 6`, `Tronxy X5SA`, `TwoTrees Sapphire`, `Geeetech A10`, `Biqu B1`, `Voxelab Aquila`, `Custom Printer`
- **17 Real Materials:** `PLA`, `PLA High-Speed`, `PLA+`, `PLA Matte`, `PLA Silk`, `PETG`, `PETG High-Speed`, `PETG-CF`, `ABS`, `ASA`, `TPU 95A`, `TPU 85A`, `Nylon (PA6)`, `PA6-CF`, `Polycarbonate (PC)`, `HIPS`, `PVA (Support)`.
- **7 Quality Presets:** `0.08 mm Extra Fine`, `0.12 mm Fine`, `0.16 mm Optimal`, `0.20 mm Standard`, `0.20 mm Structural`, `0.24 mm Fast Draft`, `0.28 mm Extra Draft`.
- **6 Built-in Calibration & CAD Models:** Overhang & Support Arch, Overhang & Bridge Tower, Retraction & Stringing Pillars, Precision Calibration Cube, Threaded Mechanical Flange, Low-Poly Sculpted Vase.

---

## Quick Start

### 1. Terminal Command (`bigslicer`)

```bash
# Open BigSlicer 3D Studio window directly
bigslicer

# List all 153 printers, 17 materials, and 7 qualities
bigslicer profiles

# Slice an STL to G-code via native C++20 engine (e.g. Ender-3 Pro + Tree Supports + Fuzzy Skin)
bigslicer slice model.stl -o model.gcode \
  --printer creality_ender3_pro --material pla --quality standard_020 \
  --pattern gyroid --infill 20 --support tree --fuzzy-skin outside

# Open native OpenGL 3.3 Core 3D viewer
bigslicer view model.stl

# Benchmark any STL
bigslicer bench model.stl --pattern gyroid --runs 5
```

### 2. Docker Web Studio + Native C++20 API (Port `8098`)

```bash
docker compose up -d --build
# Open http://127.0.0.1:8098
```

Check health:
```bash
curl http://127.0.0.1:8098/api/health
```

### 3. Native Build & Offline WebAssembly Build

```bash
# Build native binary + tests + bench inside Docker
./build.sh

# Build 372 KB Emscripten WASM for offline PWA / GitHub Pages
./build_wasm.sh
```

Serve `docs/` on any static host (or open [GitHub Pages](https://meucak.github.io/bigslicer/)) — when `/api/health` is absent, BigSlicer seamlessly runs the exact same C++20 engine with all 153 printers inside a browser Web Worker via WebAssembly.

---

## Verification & Test Suite

- **C++20 Unit, Presets & Real-STL E2E (`106/106 passed`):** `./build/katman_tests` & `./build/katman_bench` (covers 153 printers, Ender-3 Pro / Anet A8 / FLSUN Delta, Fuzzy Skin, Normal Snug, `M600`/`PAUSE`, Custom G-Code, `pos_x/pos_y` bed transforms, 1T == 4T determinism, real CAD STLs).
- **Node.js Server API, `KTP1` Decoder & Security (`8/8 passed`):**
  ```bash
  docker run --rm --entrypoint node -e BIGSLICER_BIN=/usr/local/bin/bigslicer \
    -v "$PWD/tests:/app/tests:ro" bigslicer-bigslicer:latest \
    --test /app/tests/web/test_toolpath_decoder.mjs /app/tests/server/test_server.mjs
  ```
- **Headless Chromium Full-Stack E2E (`100% passed`):**
  - **WebGL2:** 153-printer searchable modal (`Creality Ender-3 Pro`), 3D Move/Rotate gizmo, Organic Tree Supports, Normal Snug + Fuzzy Skin + `M600` pause inspection, **0 `axe-core` WCAG AA violations** across Printer Modal, Prepare Dark, Prepare Light, and Preview modes.
  - **Software Z-Buffer Canvas 2D (`?renderer=canvas2d` — `--disable-gpu` simulation):** pixel-verified 3D walls + vivid green supports rendering.
  - **Offline WASM Worker (372 KB):** 153 printers loaded and sliced 100% client-side.

---

## Project Structure

```text
bigslicer/
├── src/
│   ├── core/          # C++20 multi-threaded slicer engine (13 modules, 153 printers)
│   ├── viewer/        # Integrated bare-metal OpenGL 3.3 Core STL viewer
│   ├── wasm_api.cpp   # Emscripten C++20 -> WebAssembly bridge
│   └── main.cpp       # Unified CLI (slice / view / bench / info / profiles)
├── third_party/
│   └── clipper2/      # Official AngusJohnson/Clipper2 1.5.2 (Boost-1.0)
├── docs/              # Zero-dependency WebGL2/WebGL1/Canvas2D 3D Studio + 372 KB WASM PWA
│   └── js/gizmo.js    # Interactive 3D Move/Rotate/Scale/Place-on-Face engine
├── server/            # Zero-npm Node.js 22 native slicer API & library server
├── tests/             # C++20 (106 tests), benchmarks, Node API (8 tests) & Chromium E2E
├── CMakeLists.txt     # Native + headless CMake configuration
├── Dockerfile         # Multi-stage Debian Trixie C++20 + Node 22 runtime image
└── docker-compose.yml # Port 8098 production service
```

## License

MIT License (vendored `Clipper2 1.5.2` under Boost Software License 1.0).
