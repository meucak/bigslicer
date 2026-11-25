#pragma once

#include "config.hpp"
#include "geometry2d.hpp"
#include "surfaces.hpp"
#include "thread_pool.hpp"

#include <vector>

namespace katman {

// Precomputed per-layer lightning infill regions (top-down branching only beneath top surfaces)
std::vector<Paths64> precompute_lightning_regions(const std::vector<SlicedLayer>& layers,
                                                  const std::vector<LayerSurfaces>& surfaces,
                                                  const SlicerConfig& cfg);

// Fast analytical edge-scanline hatch generator (O(V + L) vs O((V+L) log(V+L)) Clipper2!)
// Used for Rectilinear, Grid, Triangles, Cubic, Monotonic Top/Bottom, Bridges, Ironing, and Supports.
void generate_scanline_hatch(const Paths64& region,
                             double spacing_mm,
                             double angle_deg,
                             double phase_offset_mm,
                             bool monotonic,
                             bool connect_serpentine,
                             FeatureType type,
                             float width_mm,
                             float speed_mm_s,
                             float flow_mult,
                             uint8_t flags,
                             std::vector<ExtrusionPath>& out);

// Generate Top Solid, Bottom Solid, Bridge, Ironing, and Sparse Infill toolpaths for one layer
void generate_layer_fill(uint32_t layer_idx,
                         float z_mm,
                         float layer_height,
                         const LayerSurfaces& surf,
                         const Paths64* lightning_mask,
                         const SlicerConfig& cfg,
                         std::vector<ExtrusionPath>& out_paths);

}  // namespace katman
