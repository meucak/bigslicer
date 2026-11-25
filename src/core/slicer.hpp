#pragma once

#include "config.hpp"
#include "geometry2d.hpp"
#include "mesh.hpp"
#include "thread_pool.hpp"

#include <cstdint>
#include <vector>

namespace katman {

struct LayerScheduleEntry {
    float z_slice = 0.0f;    // Z height where mesh is intersected (midpoint of layer!)
    float z_top = 0.0f;      // Z coordinate emitted to G-code (top of extruded layer!)
    float height = 0.20f;    // Thickness of this layer (mm)
};

struct SlicedLayer {
    uint32_t index = 0;
    float z_slice = 0.0f;
    float z_top = 0.0f;
    float height = 0.20f;

    Paths64 contours;        // Cleaned, simplified, NonZero-unioned 2D cross-section
    Rect64 bounds;
    uint64_t geom_hash = 0;
    uint32_t open_chains = 0;
};

// Build uniform or Dolenc-Makela adaptive layer height schedule
std::vector<LayerScheduleEntry> build_layer_schedule(const Mesh& mesh, const SlicerConfig& cfg);

// Ultra-fast CSR Z-bucketed parallel mesh slicing + O(S) open-addressing flat hash stitching
std::vector<SlicedLayer> slice_mesh_layers(const Mesh& mesh,
                                           const std::vector<LayerScheduleEntry>& schedule,
                                           const SlicerConfig& cfg,
                                           ThreadPool* pool = nullptr);

}  // namespace katman
