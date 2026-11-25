#include "profiles.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace katman {

const std::vector<PrinterPresetInfo>& list_printer_presets() {
    static const std::vector<PrinterPresetInfo> list = {
        {"generic_220", "Generic 220³ (Marlin 2)", "Generic", "Cartesian", GCodeFlavor::Marlin2, 220, 220, 250, 0.4f, true, 16.0f},
        {"ender3_v3", "Creality Ender-3 V3", "Creality", "CoreXZ", GCodeFlavor::Klipper, 220, 220, 250, 0.4f, true, 22.0f},
        {"prusa_mk4s", "Prusa MK4S (Input Shaper)", "Prusa", "Bed-Slinger IS", GCodeFlavor::Marlin2, 250, 210, 220, 0.4f, true, 24.0f},
        {"bambu_p1s", "Bambu Lab P1S / X1C", "Bambu Lab", "CoreXY", GCodeFlavor::Bambu, 256, 256, 256, 0.4f, true, 28.0f},
        {"voron_24_300", "Voron 2.4 300mm (Klipper)", "Voron", "CoreXY", GCodeFlavor::Klipper, 300, 300, 280, 0.4f, true, 30.0f},
        {"creality_k1c", "Creality K1C", "Creality", "CoreXY", GCodeFlavor::Klipper, 220, 220, 250, 0.4f, true, 26.0f},
        {"prusa_mini", "Prusa MINI+ (Input Shaper)", "Prusa", "Cantilever", GCodeFlavor::Marlin2, 180, 180, 180, 0.4f, false, 15.0f},
    };
    return list;
}

const std::vector<MaterialPresetInfo>& list_material_presets() {
    static const std::vector<MaterialPresetInfo> list = {
        {"pla", "PLA", 210, 60, 1.24f, 18.0f, 20.0f},
        {"pla_hs", "PLA+ High-Speed", 220, 60, 1.24f, 26.0f, 24.0f},
        {"petg", "PETG", 240, 80, 1.27f, 15.0f, 24.0f},
        {"abs", "ABS", 255, 100, 1.04f, 18.0f, 26.0f},
        {"asa", "ASA", 260, 105, 1.07f, 18.0f, 30.0f},
        {"tpu95a", "TPU 95A", 225, 50, 1.21f, 6.0f, 34.0f},
    };
    return list;
}

const std::vector<QualityPresetInfo>& list_quality_presets() {
    static const std::vector<QualityPresetInfo> list = {
        {"ultra_008", "0.08 mm Ultra Fine", 0.08f, 0.16f, 4, 20.0f},
        {"quality_012", "0.12 mm Quality", 0.12f, 0.20f, 3, 20.0f},
        {"standard_020", "0.20 mm Standard", 0.20f, 0.24f, 3, 20.0f},
        {"draft_028", "0.28 mm Draft Fast", 0.28f, 0.28f, 2, 15.0f},
        {"strong_020", "0.20 mm Structural Strong", 0.20f, 0.24f, 5, 40.0f},
    };
    return list;
}

bool apply_printer_preset(SlicerConfig& cfg, const std::string& id) {
    cfg.printer_id = id;
    if (id == "generic_220") {
        cfg.flavor = GCodeFlavor::Marlin2;
        cfg.cornering_model = CorneringModel::JunctionDeviation;
        cfg.bed_size_x = 220; cfg.bed_size_y = 220; cfg.bed_size_z = 250;
        cfg.nozzle_diameter = 0.40f;
        cfg.retract_length = 0.8f; cfg.retract_speed = 40.0f;
        cfg.speed_outer_wall = 60; cfg.speed_inner_wall = 90; cfg.speed_infill = 120; cfg.speed_travel = 180;
        cfg.accel_default = 2000; cfg.accel_outer_wall = 1200; cfg.accel_infill = 2500; cfg.accel_travel = 3000;
        cfg.junction_deviation = 0.02f; cfg.pressure_advance = 0.04f;
        cfg.max_volumetric_flow = std::min(cfg.max_volumetric_flow, 16.0f);
        return true;
    }
    if (id == "ender3_v3") {
        cfg.flavor = GCodeFlavor::Klipper;
        cfg.cornering_model = CorneringModel::SquareCornerVelocity;
        cfg.bed_size_x = 220; cfg.bed_size_y = 220; cfg.bed_size_z = 250;
        cfg.nozzle_diameter = 0.40f;
        cfg.retract_length = 0.8f; cfg.retract_speed = 45.0f;
        cfg.speed_outer_wall = 140; cfg.speed_inner_wall = 220; cfg.speed_infill = 280; cfg.speed_travel = 400;
        cfg.accel_default = 5000; cfg.accel_outer_wall = 3000; cfg.accel_infill = 7000; cfg.accel_travel = 8000;
        cfg.square_corner_velocity = 5.0f; cfg.pressure_advance = 0.035f;
        cfg.max_volumetric_flow = 22.0f;
        return true;
    }
    if (id == "prusa_mk4s") {
        cfg.flavor = GCodeFlavor::Marlin2;
        cfg.cornering_model = CorneringModel::JunctionDeviation;
        cfg.bed_size_x = 250; cfg.bed_size_y = 210; cfg.bed_size_z = 220;
        cfg.nozzle_diameter = 0.40f;
        cfg.retract_length = 1.2f; cfg.retract_speed = 35.0f;
        cfg.speed_outer_wall = 90; cfg.speed_inner_wall = 140; cfg.speed_infill = 200; cfg.speed_travel = 280;
        cfg.accel_default = 3500; cfg.accel_outer_wall = 2000; cfg.accel_infill = 4000; cfg.accel_travel = 4500;
        cfg.junction_deviation = 0.025f; cfg.pressure_advance = 0.035f;
        cfg.max_volumetric_flow = 24.0f;
        return true;
    }
    if (id == "bambu_p1s") {
        cfg.flavor = GCodeFlavor::Bambu;
        cfg.cornering_model = CorneringModel::SquareCornerVelocity;
        cfg.bed_size_x = 256; cfg.bed_size_y = 256; cfg.bed_size_z = 256;
        cfg.nozzle_diameter = 0.40f;
        cfg.retract_length = 0.8f; cfg.retract_speed = 35.0f;
        cfg.speed_outer_wall = 200; cfg.speed_inner_wall = 300; cfg.speed_infill = 380; cfg.speed_travel = 500;
        cfg.accel_default = 8000; cfg.accel_outer_wall = 5000; cfg.accel_infill = 10000; cfg.accel_travel = 12000;
        cfg.square_corner_velocity = 9.0f; cfg.pressure_advance = 0.02f;
        cfg.max_volumetric_flow = 28.0f;
        return true;
    }
    if (id == "voron_24_300") {
        cfg.flavor = GCodeFlavor::Klipper;
        cfg.cornering_model = CorneringModel::SquareCornerVelocity;
        cfg.bed_size_x = 300; cfg.bed_size_y = 300; cfg.bed_size_z = 280;
        cfg.nozzle_diameter = 0.40f;
        cfg.retract_length = 0.4f; cfg.retract_speed = 40.0f;
        cfg.speed_outer_wall = 180; cfg.speed_inner_wall = 260; cfg.speed_infill = 350; cfg.speed_travel = 500;
        cfg.accel_default = 6000; cfg.accel_outer_wall = 4000; cfg.accel_infill = 9000; cfg.accel_travel = 10000;
        cfg.square_corner_velocity = 5.0f; cfg.pressure_advance = 0.03f;
        cfg.max_volumetric_flow = 30.0f;
        return true;
    }
    if (id == "creality_k1c") {
        cfg.flavor = GCodeFlavor::Klipper;
        cfg.cornering_model = CorneringModel::SquareCornerVelocity;
        cfg.bed_size_x = 220; cfg.bed_size_y = 220; cfg.bed_size_z = 250;
        cfg.nozzle_diameter = 0.40f;
        cfg.retract_length = 0.6f; cfg.retract_speed = 40.0f;
        cfg.speed_outer_wall = 180; cfg.speed_inner_wall = 260; cfg.speed_infill = 320; cfg.speed_travel = 450;
        cfg.accel_default = 6500; cfg.accel_outer_wall = 4000; cfg.accel_infill = 9000; cfg.accel_travel = 10000;
        cfg.square_corner_velocity = 6.0f; cfg.pressure_advance = 0.03f;
        cfg.max_volumetric_flow = 26.0f;
        return true;
    }
    if (id == "prusa_mini") {
        cfg.flavor = GCodeFlavor::Marlin2;
        cfg.cornering_model = CorneringModel::JunctionDeviation;
        cfg.bed_size_x = 180; cfg.bed_size_y = 180; cfg.bed_size_z = 180;
        cfg.nozzle_diameter = 0.40f;
        cfg.retract_length = 3.2f; cfg.retract_speed = 55.0f;  // Bowden
        cfg.speed_outer_wall = 75; cfg.speed_inner_wall = 120; cfg.speed_infill = 160; cfg.speed_travel = 220;
        cfg.accel_default = 2500; cfg.accel_outer_wall = 1500; cfg.accel_infill = 3200; cfg.accel_travel = 3500;
        cfg.junction_deviation = 0.02f; cfg.pressure_advance = 0.18f;
        cfg.max_volumetric_flow = 15.0f;
        return true;
    }
    return false;
}

bool apply_material_preset(SlicerConfig& cfg, const std::string& id) {
    cfg.material_id = id;
    if (id == "pla") {
        cfg.nozzle_temp = 210; cfg.first_layer_nozzle_temp = 215;
        cfg.bed_temp = 60; cfg.first_layer_bed_temp = 65;
        cfg.filament_density_g_cm3 = 1.24f; cfg.filament_cost_per_kg = 20.0f;
        cfg.fan_disable_first_layers = 1; cfg.fan_min_pct = 60; cfg.fan_max_pct = 100; cfg.fan_bridge_pct = 100;
        cfg.max_volumetric_flow = std::min(cfg.max_volumetric_flow, 18.0f);
        cfg.min_layer_time_sec = 8.0f;
        return true;
    }
    if (id == "pla_hs") {
        cfg.nozzle_temp = 220; cfg.first_layer_nozzle_temp = 225;
        cfg.bed_temp = 60; cfg.first_layer_bed_temp = 65;
        cfg.filament_density_g_cm3 = 1.24f; cfg.filament_cost_per_kg = 24.0f;
        cfg.fan_disable_first_layers = 1; cfg.fan_min_pct = 75; cfg.fan_max_pct = 100; cfg.fan_bridge_pct = 100;
        cfg.max_volumetric_flow = std::min(cfg.max_volumetric_flow, 26.0f);
        cfg.min_layer_time_sec = 6.0f;
        return true;
    }
    if (id == "petg") {
        cfg.nozzle_temp = 240; cfg.first_layer_nozzle_temp = 245;
        cfg.bed_temp = 80; cfg.first_layer_bed_temp = 85;
        cfg.filament_density_g_cm3 = 1.27f; cfg.filament_cost_per_kg = 24.0f;
        cfg.fan_disable_first_layers = 3; cfg.fan_min_pct = 20; cfg.fan_max_pct = 50; cfg.fan_bridge_pct = 80;
        cfg.max_volumetric_flow = std::min(cfg.max_volumetric_flow, 15.0f);
        cfg.support_z_gap = 0.25f;
        cfg.min_layer_time_sec = 10.0f;
        return true;
    }
    if (id == "abs") {
        cfg.nozzle_temp = 255; cfg.first_layer_nozzle_temp = 260;
        cfg.bed_temp = 100; cfg.first_layer_bed_temp = 105;
        cfg.filament_density_g_cm3 = 1.04f; cfg.filament_cost_per_kg = 26.0f;
        cfg.fan_disable_first_layers = 4; cfg.fan_min_pct = 0; cfg.fan_max_pct = 25; cfg.fan_bridge_pct = 50;
        cfg.max_volumetric_flow = std::min(cfg.max_volumetric_flow, 18.0f);
        cfg.min_layer_time_sec = 12.0f;
        return true;
    }
    if (id == "asa") {
        cfg.nozzle_temp = 260; cfg.first_layer_nozzle_temp = 265;
        cfg.bed_temp = 105; cfg.first_layer_bed_temp = 110;
        cfg.filament_density_g_cm3 = 1.07f; cfg.filament_cost_per_kg = 30.0f;
        cfg.fan_disable_first_layers = 4; cfg.fan_min_pct = 0; cfg.fan_max_pct = 20; cfg.fan_bridge_pct = 45;
        cfg.max_volumetric_flow = std::min(cfg.max_volumetric_flow, 18.0f);
        cfg.min_layer_time_sec = 12.0f;
        return true;
    }
    if (id == "tpu95a") {
        cfg.nozzle_temp = 225; cfg.first_layer_nozzle_temp = 230;
        cfg.bed_temp = 50; cfg.first_layer_bed_temp = 50;
        cfg.filament_density_g_cm3 = 1.21f; cfg.filament_cost_per_kg = 34.0f;
        cfg.fan_disable_first_layers = 2; cfg.fan_min_pct = 30; cfg.fan_max_pct = 60; cfg.fan_bridge_pct = 80;
        cfg.max_volumetric_flow = std::min(cfg.max_volumetric_flow, 6.0f);
        cfg.speed_outer_wall = std::min(cfg.speed_outer_wall, 30.0f);
        cfg.speed_inner_wall = std::min(cfg.speed_inner_wall, 35.0f);
        cfg.speed_infill = std::min(cfg.speed_infill, 40.0f);
        cfg.retract_length = 0.4f;
        cfg.min_layer_time_sec = 10.0f;
        return true;
    }
    return false;
}

bool apply_quality_preset(SlicerConfig& cfg, const std::string& id) {
    cfg.quality_id = id;
    if (id == "ultra_008") {
        cfg.layer_height = 0.08f; cfg.first_layer_height = 0.16f;
        cfg.wall_count = 4; cfg.top_layers = 7; cfg.bottom_layers = 5;
        cfg.infill_density = 20.0f; cfg.support_z_gap = 0.16f;
        return true;
    }
    if (id == "quality_012") {
        cfg.layer_height = 0.12f; cfg.first_layer_height = 0.20f;
        cfg.wall_count = 3; cfg.top_layers = 6; cfg.bottom_layers = 4;
        cfg.infill_density = 20.0f; cfg.support_z_gap = 0.18f;
        return true;
    }
    if (id == "standard_020") {
        cfg.layer_height = 0.20f; cfg.first_layer_height = 0.24f;
        cfg.wall_count = 3; cfg.top_layers = 4; cfg.bottom_layers = 3;
        cfg.infill_density = 20.0f; cfg.support_z_gap = 0.20f;
        return true;
    }
    if (id == "draft_028") {
        cfg.layer_height = 0.28f; cfg.first_layer_height = 0.28f;
        cfg.wall_count = 2; cfg.top_layers = 3; cfg.bottom_layers = 3;
        cfg.infill_density = 15.0f; cfg.support_z_gap = 0.28f;
        return true;
    }
    if (id == "strong_020") {
        cfg.layer_height = 0.20f; cfg.first_layer_height = 0.24f;
        cfg.wall_count = 5; cfg.top_layers = 6; cfg.bottom_layers = 5;
        cfg.infill_density = 40.0f; cfg.infill_pattern = InfillPattern::Cubic;
        return true;
    }
    return false;
}

namespace {

inline void skip_ws(const std::string& s, size_t& i) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
}

bool parse_json_string(const std::string& s, size_t& i, std::string& out) {
    skip_ws(s, i);
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    out.clear();
    while (i < s.size()) {
        char c = s[i++];
        if (c == '"') return true;
        if (c == '\\' && i < s.size()) {
            char e = s[i++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                default: out.push_back(e); break;
            }
        } else {
            out.push_back(c);
        }
    }
    return false;
}

bool parse_json_token(const std::string& s, size_t& i, std::string& val, bool& is_str) {
    skip_ws(s, i);
    if (i >= s.size()) return false;
    if (s[i] == '"') {
        is_str = true;
        return parse_json_string(s, i, val);
    }
    is_str = false;
    size_t start = i;
    while (i < s.size() && s[i] != ',' && s[i] != '}' && !std::isspace(static_cast<unsigned char>(s[i]))) {
        ++i;
    }
    val = s.substr(start, i - start);
    return !val.empty();
}

std::string to_lower_copy(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

bool parse_config_json(const std::string& json, SlicerConfig& cfg, std::string& err) {
    size_t i = 0;
    skip_ws(json, i);
    if (i >= json.size()) return true;  // empty = defaults
    if (json[i] != '{') {
        err = "Config JSON must start with '{'";
        return false;
    }
    ++i;

    // Collect key-values first so preset keys apply before explicit overrides!
    struct KV { std::string k; std::string v; bool is_str; };
    std::vector<KV> kvs;

    while (i < json.size()) {
        skip_ws(json, i);
        if (i < json.size() && json[i] == '}') { ++i; break; }
        std::string key;
        if (!parse_json_string(json, i, key)) {
            err = "Invalid JSON key";
            return false;
        }
        skip_ws(json, i);
        if (i >= json.size() || json[i] != ':') {
            err = "Expected ':' after JSON key '" + key + "'";
            return false;
        }
        ++i;
        std::string val;
        bool is_str = false;
        if (!parse_json_token(json, i, val, is_str)) {
            err = "Invalid JSON value for '" + key + "'";
            return false;
        }
        kvs.push_back({key, val, is_str});
        skip_ws(json, i);
        if (i < json.size() && json[i] == ',') { ++i; continue; }
        if (i < json.size() && json[i] == '}') { ++i; break; }
    }

    // Pass 1: presets
    for (const auto& kv : kvs) {
        if (kv.k == "printer" || kv.k == "printer_id") apply_printer_preset(cfg, kv.v);
    }
    for (const auto& kv : kvs) {
        if (kv.k == "material" || kv.k == "material_id") apply_material_preset(cfg, kv.v);
    }
    for (const auto& kv : kvs) {
        if (kv.k == "quality" || kv.k == "quality_id") apply_quality_preset(cfg, kv.v);
    }

    auto as_f = [](const std::string& v) { return static_cast<float>(std::strtod(v.c_str(), nullptr)); };
    auto as_i = [](const std::string& v) { return static_cast<int>(std::strtol(v.c_str(), nullptr, 10)); };
    auto as_b = [](const std::string& v) { return v == "true" || v == "1"; };

    // Pass 2: explicit field overrides
    for (const auto& kv : kvs) {
        const std::string& k = kv.k;
        const std::string& v = kv.v;
        std::string vl = to_lower_copy(v);

        if (k == "flavor") {
            if (vl == "klipper") cfg.flavor = GCodeFlavor::Klipper;
            else if (vl == "rrf" || vl == "reprap" || vl == "reprapfirmware") cfg.flavor = GCodeFlavor::RepRapFirmware;
            else if (vl == "bambu") cfg.flavor = GCodeFlavor::Bambu;
            else cfg.flavor = GCodeFlavor::Marlin2;
        } else if (k == "cornering_model") {
            if (vl == "scv" || vl == "klipper") cfg.cornering_model = CorneringModel::SquareCornerVelocity;
            else if (vl == "jerk") cfg.cornering_model = CorneringModel::ClassicJerk;
            else cfg.cornering_model = CorneringModel::JunctionDeviation;
        } else if (k == "relative_extrusion") cfg.relative_extrusion = as_b(v);
        else if (k == "origin_center") cfg.origin_center = as_b(v);
        else if (k == "bed_size_x") cfg.bed_size_x = std::clamp(as_f(v), 50.0f, 1000.0f);
        else if (k == "bed_size_y") cfg.bed_size_y = std::clamp(as_f(v), 50.0f, 1000.0f);
        else if (k == "bed_size_z") cfg.bed_size_z = std::clamp(as_f(v), 50.0f, 1000.0f);
        else if (k == "nozzle_diameter") cfg.nozzle_diameter = std::clamp(as_f(v), 0.15f, 1.5f);
        else if (k == "filament_diameter") cfg.filament_diameter = std::clamp(as_f(v), 1.0f, 3.5f);
        else if (k == "filament_density_g_cm3") cfg.filament_density_g_cm3 = std::clamp(as_f(v), 0.5f, 5.0f);
        else if (k == "filament_cost_per_kg") cfg.filament_cost_per_kg = std::max(0.0f, as_f(v));
        else if (k == "auto_center") cfg.auto_center = as_b(v);
        else if (k == "auto_drop_to_bed") cfg.auto_drop_to_bed = as_b(v);
        else if (k == "auto_lay_flat") cfg.auto_lay_flat = as_b(v);
        else if (k == "pos_x") cfg.pos_x = as_f(v);
        else if (k == "pos_y") cfg.pos_y = as_f(v);
        else if (k == "scale") { float s = std::clamp(as_f(v), 0.01f, 100.0f); cfg.scale_x = cfg.scale_y = cfg.scale_z = s; }
        else if (k == "scale_x") cfg.scale_x = std::clamp(as_f(v), 0.01f, 100.0f);
        else if (k == "scale_y") cfg.scale_y = std::clamp(as_f(v), 0.01f, 100.0f);
        else if (k == "scale_z") cfg.scale_z = std::clamp(as_f(v), 0.01f, 100.0f);
        else if (k == "rotate_x_deg") cfg.rotate_x_deg = as_f(v);
        else if (k == "rotate_y_deg") cfg.rotate_y_deg = as_f(v);
        else if (k == "rotate_z_deg") cfg.rotate_z_deg = as_f(v);
        else if (k == "mirror_x") cfg.mirror_x = as_b(v);
        else if (k == "mirror_y") cfg.mirror_y = as_b(v);
        else if (k == "copies") cfg.copies = std::clamp(as_i(v), 1, 16);
        else if (k == "copy_spacing") cfg.copy_spacing = std::clamp(as_f(v), 1.0f, 50.0f);
        else if (k == "layer_height") cfg.layer_height = std::clamp(as_f(v), 0.04f, 0.80f);
        else if (k == "first_layer_height") cfg.first_layer_height = std::clamp(as_f(v), 0.06f, 0.80f);
        else if (k == "adaptive_layer_height") cfg.adaptive_layer_height = as_b(v);
        else if (k == "adaptive_min_layer") cfg.adaptive_min_layer = std::clamp(as_f(v), 0.04f, 0.50f);
        else if (k == "adaptive_max_layer") cfg.adaptive_max_layer = std::clamp(as_f(v), 0.08f, 0.80f);
        else if (k == "adaptive_cusp_error") cfg.adaptive_cusp_error = std::clamp(as_f(v), 0.005f, 0.20f);
        else if (k == "wall_count") cfg.wall_count = std::clamp(as_i(v), 1, 12);
        else if (k == "wall_order") cfg.wall_order = (vl == "outer" || vl == "outer_first") ? WallOrder::OuterFirst : WallOrder::InnerFirst;
        else if (k == "outer_wall_width") cfg.outer_wall_width = std::clamp(as_f(v), 0.15f, 2.0f);
        else if (k == "inner_wall_width") cfg.inner_wall_width = std::clamp(as_f(v), 0.15f, 2.0f);
        else if (k == "first_layer_width") cfg.first_layer_width = std::clamp(as_f(v), 0.15f, 2.0f);
        else if (k == "infill_width") cfg.infill_width = std::clamp(as_f(v), 0.15f, 2.0f);
        else if (k == "arachne_thin_walls") cfg.arachne_thin_walls = as_b(v);
        else if (k == "elephant_foot_comp") cfg.elephant_foot_comp = std::clamp(as_f(v), 0.0f, 1.0f);
        else if (k == "xy_contour_comp") cfg.xy_contour_comp = std::clamp(as_f(v), -1.0f, 1.0f);
        else if (k == "xy_hole_comp") cfg.xy_hole_comp = std::clamp(as_f(v), -1.0f, 1.0f);
        else if (k == "spiral_vase") cfg.spiral_vase = as_b(v);
        else if (k == "top_layers") cfg.top_layers = std::clamp(as_i(v), 0, 20);
        else if (k == "bottom_layers") cfg.bottom_layers = std::clamp(as_i(v), 0, 20);
        else if (k == "top_pattern") {
            if (vl == "concentric") cfg.top_pattern = TopBottomPattern::Concentric;
            else if (vl == "rectilinear") cfg.top_pattern = TopBottomPattern::Rectilinear;
            else cfg.top_pattern = TopBottomPattern::Monotonic;
        } else if (k == "bottom_pattern") {
            if (vl == "concentric") cfg.bottom_pattern = TopBottomPattern::Concentric;
            else if (vl == "rectilinear") cfg.bottom_pattern = TopBottomPattern::Rectilinear;
            else cfg.bottom_pattern = TopBottomPattern::Monotonic;
        } else if (k == "detect_bridges") cfg.detect_bridges = as_b(v);
        else if (k == "ironing") cfg.ironing = as_b(v);
        else if (k == "infill_density") cfg.infill_density = std::clamp(as_f(v), 0.0f, 100.0f);
        else if (k == "infill_pattern") {
            if (vl == "rectilinear") cfg.infill_pattern = InfillPattern::Rectilinear;
            else if (vl == "grid") cfg.infill_pattern = InfillPattern::Grid;
            else if (vl == "triangles") cfg.infill_pattern = InfillPattern::Triangles;
            else if (vl == "cubic") cfg.infill_pattern = InfillPattern::Cubic;
            else if (vl == "honeycomb") cfg.infill_pattern = InfillPattern::Honeycomb;
            else if (vl == "concentric") cfg.infill_pattern = InfillPattern::Concentric;
            else if (vl == "lightning") cfg.infill_pattern = InfillPattern::Lightning;
            else cfg.infill_pattern = InfillPattern::Gyroid;
        } else if (k == "infill_overlap_pct") cfg.infill_overlap_pct = std::clamp(as_f(v), 0.0f, 60.0f);
        else if (k == "support_type") {
            if (vl == "grid" || vl == "normal") cfg.support_type = SupportType::NormalGrid;
            else if (vl == "tree" || vl == "organic") cfg.support_type = SupportType::OrganicTree;
            else cfg.support_type = SupportType::None;
        } else if (k == "support_placement") {
            cfg.support_placement = (vl == "everywhere") ? SupportPlacement::Everywhere : SupportPlacement::BuildplateOnly;
        } else if (k == "support_overhang_angle") cfg.support_overhang_angle = std::clamp(as_f(v), 15.0f, 80.0f);
        else if (k == "support_density") cfg.support_density = std::clamp(as_f(v), 5.0f, 60.0f);
        else if (k == "support_z_gap") cfg.support_z_gap = std::clamp(as_f(v), 0.0f, 1.0f);
        else if (k == "support_xy_gap") cfg.support_xy_gap = std::clamp(as_f(v), 0.1f, 3.0f);
        else if (k == "support_interface_layers") cfg.support_interface_layers = std::clamp(as_i(v), 0, 5);
        else if (k == "adhesion") {
            if (vl == "none") cfg.adhesion = AdhesionType::None;
            else if (vl == "brim") cfg.adhesion = AdhesionType::Brim;
            else if (vl == "raft") cfg.adhesion = AdhesionType::Raft;
            else cfg.adhesion = AdhesionType::Skirt;
        } else if (k == "brim_width") cfg.brim_width = std::clamp(as_f(v), 1.0f, 30.0f);
        else if (k == "skirt_loops") cfg.skirt_loops = std::clamp(as_i(v), 0, 10);
        else if (k == "seam_position") {
            if (vl == "aligned") cfg.seam_position = SeamPosition::Aligned;
            else if (vl == "rear") cfg.seam_position = SeamPosition::Rear;
            else if (vl == "nearest") cfg.seam_position = SeamPosition::Nearest;
            else if (vl == "random") cfg.seam_position = SeamPosition::Random;
            else cfg.seam_position = SeamPosition::SharpestCorner;
        } else if (k == "scarf_seam") cfg.scarf_seam = as_b(v);
        else if (k == "combing") cfg.combing = as_b(v);
        else if (k == "retract_length") cfg.retract_length = std::clamp(as_f(v), 0.0f, 12.0f);
        else if (k == "retract_speed") cfg.retract_speed = std::clamp(as_f(v), 5.0f, 120.0f);
        else if (k == "z_hop") cfg.z_hop = std::clamp(as_f(v), 0.0f, 5.0f);
        else if (k == "wipe_on_retract") cfg.wipe_on_retract = as_b(v);
        else if (k == "arc_fitting") cfg.arc_fitting = as_b(v);
        else if (k == "nozzle_temp") cfg.nozzle_temp = std::clamp(as_f(v), 0.0f, 350.0f);
        else if (k == "first_layer_nozzle_temp") cfg.first_layer_nozzle_temp = std::clamp(as_f(v), 0.0f, 350.0f);
        else if (k == "bed_temp") cfg.bed_temp = std::clamp(as_f(v), 0.0f, 140.0f);
        else if (k == "first_layer_bed_temp") cfg.first_layer_bed_temp = std::clamp(as_f(v), 0.0f, 140.0f);
        else if (k == "flow_ratio") cfg.flow_ratio = std::clamp(as_f(v), 0.5f, 1.5f);
        else if (k == "max_volumetric_flow") cfg.max_volumetric_flow = std::clamp(as_f(v), 1.0f, 80.0f);
        else if (k == "pressure_advance") cfg.pressure_advance = std::clamp(as_f(v), 0.0f, 1.5f);
        else if (k == "speed_outer_wall") cfg.speed_outer_wall = std::clamp(as_f(v), 5.0f, 600.0f);
        else if (k == "speed_inner_wall") cfg.speed_inner_wall = std::clamp(as_f(v), 5.0f, 600.0f);
        else if (k == "speed_infill") cfg.speed_infill = std::clamp(as_f(v), 5.0f, 600.0f);
        else if (k == "speed_top_solid") cfg.speed_top_solid = std::clamp(as_f(v), 5.0f, 600.0f);
        else if (k == "speed_travel") cfg.speed_travel = std::clamp(as_f(v), 20.0f, 1000.0f);
        else if (k == "speed_first_layer") cfg.speed_first_layer = std::clamp(as_f(v), 5.0f, 150.0f);
        else if (k == "overhang_slowdown") cfg.overhang_slowdown = as_b(v);
        else if (k == "min_layer_time_sec") cfg.min_layer_time_sec = std::clamp(as_f(v), 0.0f, 60.0f);
        else if (k == "fan_max_pct") cfg.fan_max_pct = std::clamp(as_i(v), 0, 100);
        else if (k == "accel_default") cfg.accel_default = std::clamp(as_f(v), 100.0f, 30000.0f);
        else if (k == "accel_outer_wall") cfg.accel_outer_wall = std::clamp(as_f(v), 100.0f, 20000.0f);
        else if (k == "threads") cfg.threads = std::clamp(as_i(v), 0, 64);
        else if (k == "enable_layer_cache") cfg.enable_layer_cache = as_b(v);
        else if (k == "emit_gcode") cfg.emit_gcode = as_b(v);
        else if (k == "emit_binary_toolpath") cfg.emit_binary_toolpath = as_b(v);
    }
    return true;
}

std::string presets_to_json() {
    std::string out = "{\"printers\":[";
    const auto& pr = list_printer_presets();
    for (size_t i = 0; i < pr.size(); ++i) {
        char buf[384];
        std::snprintf(buf, sizeof(buf),
            "%s{\"id\":\"%s\",\"name\":\"%s\",\"vendor\":\"%s\",\"kinematics\":\"%s\","
            "\"bedX\":%.0f,\"bedY\":%.0f,\"bedZ\":%.0f,\"nozzle\":%.2f,\"directDrive\":%s,\"maxFlow\":%.1f}",
            i ? "," : "", pr[i].id.c_str(), pr[i].name.c_str(), pr[i].vendor.c_str(),
            pr[i].kinematics.c_str(), pr[i].bed_x, pr[i].bed_y, pr[i].bed_z,
            pr[i].nozzle, pr[i].direct_drive ? "true" : "false", pr[i].max_flow);
        out += buf;
    }
    out += "],\"materials\":[";
    const auto& mat = list_material_presets();
    for (size_t i = 0; i < mat.size(); ++i) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "%s{\"id\":\"%s\",\"name\":\"%s\",\"nozzleTemp\":%.0f,\"bedTemp\":%.0f,"
            "\"density\":%.2f,\"maxFlow\":%.1f,\"costPerKg\":%.1f}",
            i ? "," : "", mat[i].id.c_str(), mat[i].name.c_str(),
            mat[i].nozzle_temp, mat[i].bed_temp, mat[i].density, mat[i].max_flow, mat[i].cost_per_kg);
        out += buf;
    }
    out += "],\"qualities\":[";
    const auto& q = list_quality_presets();
    for (size_t i = 0; i < q.size(); ++i) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "%s{\"id\":\"%s\",\"name\":\"%s\",\"layerHeight\":%.2f,\"firstLayerHeight\":%.2f,"
            "\"walls\":%d,\"infillPct\":%.0f}",
            i ? "," : "", q[i].id.c_str(), q[i].name.c_str(),
            q[i].layer_height, q[i].first_layer_height, q[i].walls, q[i].infill_pct);
        out += buf;
    }
    out += "]}";
    return out;
}

std::string stats_to_json(const SliceStats& st, const SlicerConfig& cfg) {
    char buf[1536];
    std::snprintf(buf, sizeof(buf),
        "{\"triCount\":%u,\"layerCount\":%u,\"totalMoves\":%u,\"extrusionMoves\":%u,"
        "\"travelMoves\":%u,\"arcMoves\":%u,\"retractCount\":%u,\"cachedLayers\":%u,"
        "\"threadsUsed\":%u,\"modelSize\":[%.2f,%.2f,%.2f],\"bedSize\":[%.0f,%.0f,%.0f],"
        "\"modelVolumeCm3\":%.3f,\"overhangFacePct\":%.2f,\"fitsBed\":%s,\"openChains\":%u,"
        "\"printTimeSec\":%.2f,\"filamentMm\":%.2f,\"filamentCm3\":%.3f,\"filamentG\":%.2f,"
        "\"estimatedCost\":%.3f,\"maxVolumetricFlow\":%.2f,\"gcodeBytes\":%zu,\"ktpBytes\":%zu,"
        "\"timingsMs\":{\"loadTransform\":%.3f,\"sliceStitch\":%.3f,\"surfaces\":%.3f,"
        "\"supports\":%.3f,\"perimetersInfill\":%.3f,\"pathMotion\":%.3f,\"gcodeWrite\":%.3f,\"total\":%.3f},",
        st.tri_count, st.layer_count, st.total_moves, st.extrusion_moves,
        st.travel_moves, st.arc_moves, st.retract_count, st.cached_layers,
        st.threads_used, st.model_size_x, st.model_size_y, st.model_size_z,
        cfg.bed_size_x, cfg.bed_size_y, cfg.bed_size_z,
        st.model_volume_cm3, st.overhang_face_pct, st.fits_bed ? "true" : "false",
        st.non_manifold_open_chains,
        st.print_time_sec, st.filament_mm, st.filament_cm3, st.filament_g,
        st.estimated_cost, st.max_volumetric_flow_mm3s, st.gcode_bytes, st.ktp_bytes,
        st.timings.load_transform_ms, st.timings.slice_stitch_ms, st.timings.surfaces_overhang_ms,
        st.timings.supports_ms, st.timings.perimeters_infill_ms, st.timings.path_motion_plan_ms,
        st.timings.gcode_serialize_ms, st.timings.total_ms);

    std::string out(buf);
    out += "\"features\":[";
    for (size_t f = 0; f < static_cast<size_t>(FeatureType::Count); ++f) {
        char fb[160];
        std::snprintf(fb, sizeof(fb), "%s{\"id\":%zu,\"name\":\"%s\",\"timeSec\":%.2f,\"filamentMm\":%.2f}",
            f ? "," : "", f, feature_type_name(static_cast<FeatureType>(f)),
            st.feature_time_sec[f], st.feature_filament_mm[f]);
        out += fb;
    }
    out += "]}";
    return out;
}

}  // namespace katman
