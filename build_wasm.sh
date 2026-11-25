#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE_NAME="katman-wasm-builder:trixie"

if ! docker image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
    echo "[build_wasm.sh] Building Emscripten WASM image $IMAGE_NAME..."
    docker build -t "$IMAGE_NAME" -f "$SCRIPT_DIR/Dockerfile.wasm" "$SCRIPT_DIR"
fi

mkdir -p "$SCRIPT_DIR/docs/wasm"

docker run --rm \
    -v "$SCRIPT_DIR:/src" \
    -w /src \
    "$IMAGE_NAME" \
    bash -c '
      em++ -O3 -std=c++20 -fno-exceptions -fno-rtti -flto \
        -I/src/src -I/src/src/core -I/src/third_party/clipper2/include \
        /src/third_party/clipper2/src/clipper.engine.cpp \
        /src/third_party/clipper2/src/clipper.offset.cpp \
        /src/third_party/clipper2/src/clipper.rectclip.cpp \
        /src/src/core/profiles.cpp \
        /src/src/core/mesh.cpp \
        /src/src/core/geometry2d.cpp \
        /src/src/core/slicer.cpp \
        /src/src/core/perimeters.cpp \
        /src/src/core/surfaces.cpp \
        /src/src/core/infill.cpp \
        /src/src/core/supports.cpp \
        /src/src/core/path_planner.cpp \
        /src/src/core/motion_planner.cpp \
        /src/src/core/gcode_writer.cpp \
        /src/src/core/engine.cpp \
        /src/src/wasm_api.cpp \
        -s WASM=1 \
        -s MODULARIZE=1 \
        -s EXPORT_ES6=1 \
        -s EXPORT_NAME=createKatmanWasm \
        -s ENVIRONMENT=web,worker \
        -s ALLOW_MEMORY_GROWTH=1 \
        -s INITIAL_MEMORY=33554432 \
        -s MAXIMUM_MEMORY=536870912 \
        -s EXPORTED_FUNCTIONS="[\"_malloc\",\"_free\",\"_katman_wasm_slice\",\"_katman_wasm_stats_ptr\",\"_katman_wasm_stats_len\",\"_katman_wasm_ktp_ptr\",\"_katman_wasm_ktp_len\",\"_katman_wasm_gcode_ptr\",\"_katman_wasm_gcode_len\",\"_katman_wasm_presets_ptr\",\"_katman_wasm_presets_len\",\"_katman_wasm_clear\"]" \
        -s EXPORTED_RUNTIME_METHODS="[\"HEAPU8\",\"stringToUTF8\",\"lengthBytesUTF8\",\"UTF8ToString\"]" \
        -o /src/docs/wasm/katman_wasm.js
      chown -R '"$(id -u):$(id -g)"' /src/docs/wasm
      ls -lh /src/docs/wasm/
    '
