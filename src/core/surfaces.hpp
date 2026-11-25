#pragma once

#include "config.hpp"
#include "geometry2d.hpp"
#include "perimeters.hpp"
#include "slicer.hpp"
#include "thread_pool.hpp"

#include <vector>

namespace katman {

struct LayerSurfaces {
    Paths64 top_skin;        // Solid top shell region (exposed to air within top_layers above)
    Paths64 bottom_skin;     // Solid bottom shell region (supported by model or layer 0)
    Paths64 bridge_skin;     // Unsupported bottom skin spanning open air (needs bridge flow/fan/angle!)
    Paths64 sparse_infill;   // Remaining internal area filled by sparse infill pattern
    Paths64 ironing_area;    // Pure exposed topmost flat surface (layer[i] \ layer[i+1])
    float bridge_angle_deg = 0.0f;
};

// Analyze all layers in parallel to classify Top, Bottom, Bridge, Ironing, and Sparse Infill regions
std::vector<LayerSurfaces> analyze_layer_surfaces(const std::vector<SlicedLayer>& layers,
                                                  const std::vector<LayerPerimeters>& perims,
                                                  const SlicerConfig& cfg,
                                                  ThreadPool* pool = nullptr);

}  // namespace katman
