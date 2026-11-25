#pragma once

#include "config.hpp"
#include "geometry2d.hpp"
#include "slicer.hpp"

#include <vector>

namespace katman {

struct LayerPerimeters {
    std::vector<ExtrusionPath> outer_walls;
    std::vector<ExtrusionPath> inner_walls;
    std::vector<ExtrusionPath> gap_fills;   // Arachne variable-width thin walls & medial gap fills
    Paths64 inner_region;                   // Region remaining inside innermost perimeter (for skins + infill)
    Paths64 outer_wall_polys;               // Centerline polygons of outer walls (for overhang/combing)
};

// Generate outer/inner perimeters, Arachne variable-width thin walls & gap fill,
// overhang wall speed modulation, and compute inner_region for skins/infill.
LayerPerimeters generate_layer_perimeters(const SlicedLayer& layer,
                                          const SlicedLayer* prev_layer,
                                          const SlicerConfig& cfg);

// Generate Skirt or Brim paths around Layer 0 (+ support footprints if present)
std::vector<ExtrusionPath> generate_adhesion_paths(const Paths64& layer0_contours,
                                                   const Paths64& layer0_supports,
                                                   const SlicerConfig& cfg);

}  // namespace katman
