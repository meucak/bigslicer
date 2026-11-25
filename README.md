# BigSlicer — Ultra-Optimized C++20 / Clipper2 3D Printing Slicer & WebGL2 Studio

**BigSlicer** is a from-scratch, zero-bloat, multi-threaded 3D printing slicer engineered for extreme slicing speed, physical G-code accuracy, and dual **Native + Browser** operation:

- **Native C++20 Engine + Integrated OpenGL 3.3 Core Viewer** (`739 KB` stripped binary, `-O3 -flto -fno-exceptions -fno-rtti`, `Clipper2 1.5.2`)
- **Offline WebAssembly C++20 Build** (`308 KB` `.wasm` + `12 KB` JS loader — runs 100% client-side in Web Workers, zero server required)
- **WebGL2 Hardware-Instanced 3D Slicer Studio** (36-vertex stadium-profile extruded filament beads, 5 heatmaps, live 3D brass nozzle, layer/move scrubber, Ghost Shell, G-code Inspector, `TR`/`EN`, Obsidian Studio & Alabaster Drafting themes, **0 `axe-core` WCAG AA violations**)
- **Zero-npm Node.js 22 Docker Backend** (Port `8098`, native C++20 API, zero-copy disk library slicing, SSRF-protected proxy)

---

## Verified Performance Benchmarks

Measured on a 2011 **Intel Core i5-2300** (4 cores, 2.8 GHz Sandy Bridge, local SSD, GCC 14.2 `-O3 -march=x86-64-v2 -flto`):

| Model / Scenario | Triangles | Layers | Moves | G-code | Total Time (4T) | Notes |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| **P-51 Wing_L.stl** (Gyroid 20%) | 76,272 | 54 | 31,917 | 1.00 MB | **29.2 ms** | avg 30.5 ms (slice 4.6 ms) |
| **P-51 Wing_L.stl** + Organic Tree Supports | 76,272 | 54 | 53,272 | 1.67 MB | **63.2 ms** | full top-down merging trunks |
| **P-51 Wing_L.stl** + Normal Grid Supports | 76,272 | 54 | 59,083 | 1.81 MB | **187.4 ms** | dense interface + Z-gap |
| **P-51 Propeller.stl** (Gyroid 20%) | 39,136 | 184 | 15,748 | 0.61 MB | **36.1 ms** | avg 37.6 ms |
| **P-51 Fuselage_Rear.stl** (Gyroid 20%) | 66,426 | 223 | 55,853 | 1.78 MB | **65.4 ms** | avg 67.8 ms |
| **Parca1_AnaGovde.stl** (584 layers, Gyroid) | 18,578 | 584 | 94,170 | 3.14 MB | **82.9 ms** | 33 identical-layer cache hits |
| **Izomer_Camurluk.stl** (Gyroid 20%) | 13,536 | 193 | 119,354 | 3.83 MB | **98.2 ms** | avg 103.3 ms |
| **Synthetic Sphere 100k** (Rectilinear 4T) | 99,904 | 250 | 21,007 | — | **96.2 ms** | 1T == 4T bit-identical output |
| **Synthetic Sphere 100k** (Gyroid 4T) | 99,904 | 250 | 164,029 | — | **121.0 ms** | vs 379.1 ms 1T (**3.13×** scaling) |
| **Tube 60 mm** (Identical-Layer Cache ON) | 2,048 | 300 | 40,460 | — | **102.0 ms** | **256/300** cached (vs 176.9 ms off) |
| **Overhang Arch** + Grid / Tree Support | 48 | 90 | 14.5K / 16.6K | — | **5.7 ms / 9.0 ms** | 85/90 cached layers |

---

## Core Slicing Engine Architecture

BigSlicer synthesizes the strongest architectural ideas from **PrusaSlicer/Slic3r**, **CuraEngine 5+ (Arachne)**, and **OrcaSlicer** into a clean, cache-friendly C++20 pipeline:

1. **Zero-Copy Mesh Ingestion (`src/core/mesh.*`)**
   - Binary STL (with `solid`-header binary trap protection), ASCII STL, and Wavefront OBJ.
   - `mmap` on local filesystems + single bulk `read()` optimization for FUSE/SSHFS mounts, automatic inverted-winding detection & repair, multi-copy bed grid arrangement, auto-lay-flat, scale/rotate/mirror.
2. **CSR Z-Bucketed Slicing & O(S) Stitching (`src/core/slicer.*`)**
   - Uniform or **Dolenc–Mäkelä cusp-height adaptive layer heights** with Gaussian smoothing.
   - Compressed Sparse Row (CSR) Z-interval index maps triangles directly to crossed layers in O(1) without scanning non-intersecting triangles.
   - Exact CCW half-open `SLICE_LUT` + generation-stamped flat open-addressing endpoint hash stitcher + 2-stage non-manifold crack recovery.
   - Strict `0.8 µm` collinear diagonal-edge pre-pruner (preserves CAD corners & unlocks identical-layer geometry hashing across vertical walls), followed by `Clipper2 1.5.2` `NonZero` normalization, canonical-rotation Douglas–Peucker simplification, and Layer-0 elephant foot compensation.
3. **Perimeters, Arachne & Slic3r Pill Flow (`src/core/perimeters.*`, `geometry2d.*`)**
   - Exact stadium/pill cross-section math: $A = h(W - h) + \frac{\pi h^2}{4}$, overlap spacing $S = W - h(1 - \frac{\pi}{4})$.
   - Inner-First / Outer-First wall ordering, 4-tier overhang slowdown, **Arachne variable-width thin walls & medial gap fill** (with O(V) area-deficit early-out), **Spiral Vase** continuous helical Z, Skirt / Brim / Raft adhesion.
   - **Identical-Layer Perimeter Cache** (`O(1)` reuse across matching consecutive Z slices).
4. **Cross-Layer Surfaces & 8 High-Speed Infill Patterns (`src/core/surfaces.*`, `infill.*`)**
   - Top/bottom solid skins (Monotonic & Rectilinear), automatic bridge detection with optimal span-angle alignment, top-surface **Ironing**.
   - Fast analytical O(V+L) scanline clipper & Y-bucketed open-polyline clipper supporting **8 patterns**: `Gyroid` (sinusoidal recurrence with $\sqrt{2}$ arc-length density compensation), `Rectilinear`, `Grid`, `Triangles`, `Cubic` (3D Z-shifted), `Honeycomb`, `Concentric`, and top-down `Lightning`.
5. **Supports (`src/core/supports.*`)**
   - **Normal Grid** supports (Buildplate-only or Everywhere, dense interface roof/floor layers, configurable Z-gap & XY clearance).
   - **Organic Tree** supports (top-down overhang tip clustering, collision-avoiding merging hollow circular trunks flaring at the build plate + dense roof pads).
6. **Path, Seam & Lookahead Kinematics (`src/core/path_planner.*`, `motion_planner.*`)**
   - Smart Z-Seam (`Aligned`, `SharpestCorner`, `Rear`, `Nearest`, `Random`) + **Scarf Joint Seam** flow ramp.
   - Nearest-Neighbor + 2-opt TSP path ordering, Combing, Retraction / Wipe / Z-Hop, **G2/G3 Circular Arc Fitting** on contours.
   - Forward/backward trapezoidal lookahead velocity planner supporting **Junction Deviation**, **Klipper Square Corner Velocity**, and **Classic Jerk**, plus Max Volumetric Flow capping, Minimum Layer Time cooling slowdown, and dynamic fan PWM scheduling.
7. **Parallel Raw-Pointer G-Code & Compact `KTP1` Binary (`src/core/gcode_writer.*`)**
   - Multi-threaded 32-bit LUT serializer emitting deterministic **Marlin 2**, **Klipper**, **RepRapFirmware**, and **Bambu** G-code (`M82` absolute & `M83` relative extrusion, Pressure/Linear Advance, `M204`/`SET_VELOCITY_LIMIT`, PrusaSlicer/OrcaSlicer metadata).
   - Zero-copy `KTP1` compact binary toolpath format (`24 bytes/move`) streamed directly to WebGL2 hardware instancing.

---

## Included Printer, Material & Quality Presets

- **7 Printers:** `prusa_mk4s` (Prusa MK4S Input Shaper), `bambu_p1s` (Bambu Lab P1S/X1C), `voron_24_300` (Voron 2.4 300mm Klipper), `creality_k1c` (Creality K1C), `ender3_v3` (Creality Ender-3 V3), `prusa_mini` (Prusa MINI+ IS), `generic_220` (Generic 220³ Marlin 2).
- **6 Materials:** `pla`, `pla_hs`, `petg`, `abs`, `asa`, `tpu95a`.
- **5 Qualities:** `ultra_008` (0.08 mm), `quality_012` (0.12 mm), `standard_020` (0.20 mm), `draft_028` (0.28 mm), `strong_020` (0.20 mm Structural).

---

## Quick Start

### 1. Terminal Command (`bigslicer`)

```bash
# Open BigSlicer 3D Studio window directly
bigslicer

# Slice an STL to G-code via native C++20 engine
bigslicer slice model.stl -o model.gcode \
  --printer prusa_mk4s --material pla --quality standard_020 \
  --pattern gyroid --infill 20 --support tree

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

# Build 308 KB Emscripten WASM for offline PWA / GitHub Pages
./build_wasm.sh
```

Serve `docs/` on any static host (or open [GitHub Pages](https://meucak.github.io/bigslicer/)) — when `/api/health` is absent, BigSlicer seamlessly runs the exact same C++20 engine inside a browser Web Worker via WebAssembly.

---

## Verification & Test Suite

- **C++20 Unit & Real-STL E2E (`91/91 passed`):** `./build/katman_tests` & `./build/katman_bench`
- **Node.js Server API, `KTP1` Decoder & Security (`8/8 passed`):**
  ```bash
  docker run --rm --entrypoint node -e BIGSLICER_BIN=/usr/local/bin/bigslicer \
    -v "$PWD/tests:/app/tests:ro" bigslicer-bigslicer:latest \
    --test /app/tests/web/test_toolpath_decoder.mjs /app/tests/server/test_server.mjs
  ```
- **Headless Chromium True WebGL2 + `axe-core` WCAG AA + Offline WASM E2E (`100% passed`):**
  True `WebGL2` instanced rendering, **0 `axe-core` WCAG AA violations** across Prepare Dark, Prepare Light, and Preview modes, Native C++20 Gyroid & Organic Tree Supports, G-code Inspector search, and Offline WASM Worker fallback.

---

## Project Structure

```text
bigslicer/
├── src/
│   ├── core/          # C++20 multi-threaded slicer engine (13 modules)
│   ├── viewer/        # Integrated bare-metal OpenGL 3.3 Core STL viewer
│   ├── wasm_api.cpp   # Emscripten C++20 -> WebAssembly bridge
│   └── main.cpp       # Unified CLI (slice / view / bench / info / profiles)
├── third_party/
│   └── clipper2/      # Official AngusJohnson/Clipper2 1.5.2 (Boost-1.0)
├── docs/              # Zero-dependency WebGL2 3D Slicer Studio + 308 KB WASM PWA
├── server/            # Zero-npm Node.js 22 native slicer API & library server
├── tests/             # C++20 unit/E2E tests, benchmarks, Node API & Playwright E2E
├── CMakeLists.txt     # Native + headless CMake configuration
├── Dockerfile         # Multi-stage Debian Trixie C++20 + Node 22 runtime image
└── docker-compose.yml # Port 8098 production service
```

## License

MIT License (vendored `Clipper2 1.5.2` under Boost Software License 1.0).
