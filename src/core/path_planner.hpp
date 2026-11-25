#pragma once

#include "config.hpp"
#include "geometry2d.hpp"
#include "perimeters.hpp"
#include "slicer.hpp"
#include "supports.hpp"
#include "surfaces.hpp"
#include "thread_pool.hpp"

#include <cstdint>
#include <vector>

namespace katman {

constexpr uint8_t MOVE_FLAG_RETRACT = 1u << 0;
constexpr uint8_t MOVE_FLAG_UNRETRACT = 1u << 1;
constexpr uint8_t MOVE_FLAG_ZHOP = 1u << 2;
constexpr uint8_t MOVE_FLAG_ARC = 1u << 3;
constexpr uint8_t MOVE_FLAG_ARC_CW = 1u << 4;
constexpr uint8_t MOVE_FLAG_BRIDGE = 1u << 5;
constexpr uint8_t MOVE_FLAG_OVERHANG = 1u << 6;
constexpr uint8_t MOVE_FLAG_WIPE = 1u << 7;

struct PlannedMove {
    float x0 = 0.0f, y0 = 0.0f;
    float x1 = 0.0f, y1 = 0.0f;
    float z0 = 0.0f, z1 = 0.0f;
    float arc_i = 0.0f, arc_j = 0.0f;   // Valid when MOVE_FLAG_ARC is set (offset from x0,y0 to center)
    float width_mm = 0.0f;
    float height_mm = 0.0f;
    float target_speed_mm_s = 60.0f;    // Nominal speed before motion planner clamping
    float actual_speed_mm_s = 60.0f;    // Final speed after volumetric cap & cooling slowdown
    float accel_mm_s2 = 2500.0f;
    float e_delta_mm = 0.0f;            // Filament extruded on this move (mm)
    float flow_mm3_s = 0.0f;
    float length_mm = 0.0f;
    float duration_sec = 0.0f;
    FeatureType type = FeatureType::Travel;
    uint8_t flags = 0;
};

struct PlannedLayer {
    uint32_t index = 0;
    float z_top = 0.0f;
    float height = 0.20f;
    int fan_pct = 100;
    bool has_bridge = false;
    double layer_time_sec = 0.0;
    double layer_filament_mm = 0.0;
    double start_e_abs_mm = 0.0;
    std::vector<PlannedMove> moves;
};

// Plan per-layer path order (TSP NN + 2-opt), Z-seam placement, scarf seam ramp,
// combing, retraction/wipe/z-hop, spiral vase helical Z, and G2/G3 arc fitting.
PlannedLayer plan_single_layer(uint32_t layer_idx,
                               float z_top,
                               float layer_height,
                               const Paths64& contours,
                               const LayerPerimeters& perims,
                               std::vector<ExtrusionPath>&& fill_paths,
                               const std::vector<ExtrusionPath>& support_paths,
                               const std::vector<ExtrusionPath>& adhesion_paths,
                               float start_x,
                               float start_y,
                               const SlicerConfig& cfg,
                               float& out_end_x,
                               float& out_end_y);

}  // namespace katman
