#pragma once

#include "config.hpp"
#include "geometry2d.hpp"
#include "slicer.hpp"
#include "thread_pool.hpp"

#include <vector>

namespace katman {

struct LayerSupportData {
    Paths64 body_region;       // Sparse support columns or tree branch shells
    Paths64 interface_region;  // Dense top/bottom support roof/floor interface layers
    std::vector<ExtrusionPath> paths;
};

// Generate Normal Grid or Organic/Tree Supports with interface layers, Z-gap, and XY clearance
std::vector<LayerSupportData> generate_supports(const std::vector<SlicedLayer>& layers,
                                                const SlicerConfig& cfg,
                                                ThreadPool* pool = nullptr);

}  // namespace katman
