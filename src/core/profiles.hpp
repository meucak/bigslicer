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
    float bed_x, bed_y, bed_z;
    float nozzle;
    bool direct_drive;
    float max_flow;
};

struct MaterialPresetInfo {
    std::string id;
    std::string name;
    float nozzle_temp;
    float bed_temp;
    float density;
    float max_flow;
    float cost_per_kg;
};

struct QualityPresetInfo {
    std::string id;
    std::string name;
    float layer_height;
    float first_layer_height;
    int walls;
    float infill_pct;
};

const std::vector<PrinterPresetInfo>& list_printer_presets();
const std::vector<MaterialPresetInfo>& list_material_presets();
const std::vector<QualityPresetInfo>& list_quality_presets();
std::string presets_to_json();

}  // namespace katman
