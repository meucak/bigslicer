#pragma once

#include "config.hpp"
#include "thread_pool.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace katman {

struct Vec3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// Cache-line friendly triangle struct for slicing (48 bytes -> 1.33 tris/cacheline, 16-byte aligned)
// Plus parallel SoA zmin/zmax arrays for ultra-fast SIMD/branchless Z-range bucketing!
struct alignas(16) SliceTri {
    float x0, y0, z0;
    float x1, y1, z1;
    float x2, y2, z2;
    float nz;           // unit normal Z component (for adaptive cusp & overhang diagnostics)
    float zmin;
    float zmax;
};
static_assert(sizeof(SliceTri) == 48, "SliceTri must be 48 bytes");

struct Mesh {
    bool ok = false;
    std::string error;
    std::string name;
    bool is_binary = true;
    uint64_t file_bytes = 0;
    uint32_t raw_tri_count = 0;
    uint32_t degenerate_skipped = 0;

    Vec3f bbox_min{};
    Vec3f bbox_max{};
    Vec3f bbox_extent{};
    double volume_mm3 = 0.0;
    double area_mm2 = 0.0;
    float overhang_face_pct = 0.0f;
    bool inverted_winding_fixed = false;

    std::vector<SliceTri> tris;
};

// Zero-copy mmap STL (binary + ASCII) or OBJ loader from file path
Mesh load_mesh_file(const std::string& path, ThreadPool* pool = nullptr);

// Memory-buffer STL (binary + ASCII) or OBJ loader (for HTTP API / WASM / tests)
Mesh load_mesh_memory(const uint8_t* data, size_t size, const std::string& name, ThreadPool* pool = nullptr);

// Apply scale, rotate, lay-flat, mirror, drop-to-bed, center-on-bed, and multi-copy grid arrange in-place
void transform_and_arrange_mesh(Mesh& mesh, const SlicerConfig& cfg, ThreadPool* pool = nullptr);

}  // namespace katman
