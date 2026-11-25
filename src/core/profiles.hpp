#pragma once

#include "config.hpp"

#include <string>
#include <vector>

namespace katman {

struct PrinterPresetInfo {
    std::string id;
    std::string name;
    std::string vendor;
    std::string kinematics;
    GCodeFlavor flavor;
    CorneringModel cornering;
    float bed_x, bed_y, bed_z;
    float nozzle;
    bool direct_drive;
    bool origin_center;
    float max_flow;
    float retract_length;
    float retract_speed;
    float speed_outer;
    float speed_inner;
    float speed_infill;
    float speed_travel;
    float accel_default;
    float accel_outer;
    float accel_infill;
    float accel_travel;
    float pressure_advance;
    float corner_param;  // JD (mm), SCV (mm/s), or Jerk (mm/s)
};

struct MaterialPresetInfo {
    std::string id;
    std::string name;
    float nozzle_temp;
    float bed_temp;
    float density;
    float max_flow;
    float cost_per_kg;
    int fan_disable_layers;
    int fan_min_pct;
    int fan_max_pct;
    int fan_bridge_pct;
    float min_layer_time;
    float max_speed_cap;  // 0 = uncapped
};

struct QualityPresetInfo {
    std::string id;
    std::string name;
    float layer_height;
    float first_layer_height;
    int walls;
    int top_layers;
    int bottom_layers;
    float infill_pct;
};

const std::vector<PrinterPresetInfo>& list_printer_presets();
const std::vector<MaterialPresetInfo>& list_material_presets();
const std::vector<QualityPresetInfo>& list_quality_presets();
std::string presets_to_json();

}  // namespace katman
