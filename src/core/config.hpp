#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace katman {

// 0.1 micrometer fixed-point resolution for Clipper2 64-bit integer geometry
constexpr double COORD_SCALE = 10000.0;
constexpr double INV_COORD_SCALE = 1.0 / 10000.0;

enum class FeatureType : uint8_t {
    Travel = 0,
    OuterWall = 1,
    InnerWall = 2,
    OverhangWall = 3,
    GapFill = 4,
    TopSolid = 5,
    BottomSolid = 6,
    SparseInfill = 7,
    Bridge = 8,
    Support = 9,
    SupportInterface = 10,
    SkirtBrim = 11,
    Ironing = 12,
    Count = 13
};

inline const char* feature_type_name(FeatureType t) {
    switch (t) {
        case FeatureType::Travel: return "Travel";
        case FeatureType::OuterWall: return "Outer wall";
        case FeatureType::InnerWall: return "Inner wall";
        case FeatureType::OverhangWall: return "Overhang wall";
        case FeatureType::GapFill: return "Gap fill";
        case FeatureType::TopSolid: return "Top surface";
        case FeatureType::BottomSolid: return "Bottom surface";
        case FeatureType::SparseInfill: return "Sparse infill";
        case FeatureType::Bridge: return "Bridge";
        case FeatureType::Support: return "Support";
        case FeatureType::SupportInterface: return "Support interface";
        case FeatureType::SkirtBrim: return "Skirt/Brim";
        case FeatureType::Ironing: return "Ironing";
        default: return "Unknown";
    }
}

enum class InfillPattern : uint8_t {
    Rectilinear = 0,
    Grid = 1,
    Triangles = 2,
    Gyroid = 3,
    Cubic = 4,
    Honeycomb = 5,
    Concentric = 6,
    Lightning = 7
};

enum class TopBottomPattern : uint8_t {
    Monotonic = 0,
    Rectilinear = 1,
    Concentric = 2
};

enum class WallOrder : uint8_t {
    InnerFirst = 0,
    OuterFirst = 1
};

enum class SeamPosition : uint8_t {
    Aligned = 0,
    SharpestCorner = 1,
    Rear = 2,
    Nearest = 3,
    Random = 4
};

enum class SupportType : uint8_t {
    None = 0,
    NormalGrid = 1,
    OrganicTree = 2,
    NormalSnug = 3
};

enum class FuzzySkinMode : uint8_t {
    None = 0,
    Outside = 1,
    AllWalls = 2
};

enum class SupportPlacement : uint8_t {
    BuildplateOnly = 0,
    Everywhere = 1
};

enum class AdhesionType : uint8_t {
    None = 0,
    Skirt = 1,
    Brim = 2,
    Raft = 3
};

enum class GCodeFlavor : uint8_t {
    Marlin2 = 0,
    Klipper = 1,
    RepRapFirmware = 2,
    Bambu = 3
};

enum class CorneringModel : uint8_t {
    JunctionDeviation = 0,
    SquareCornerVelocity = 1,
    ClassicJerk = 2
};

struct SlicerConfig {
    // --- Printer / Machine ---
    std::string printer_id = "generic_220";
    std::string material_id = "pla";
    std::string quality_id = "standard_020";
    GCodeFlavor flavor = GCodeFlavor::Marlin2;
    CorneringModel cornering_model = CorneringModel::JunctionDeviation;
    bool relative_extrusion = true;
    bool origin_center = false;
    float bed_size_x = 220.0f;
    float bed_size_y = 220.0f;
    float bed_size_z = 250.0f;
    float nozzle_diameter = 0.40f;
    float filament_diameter = 1.75f;
    float filament_density_g_cm3 = 1.24f;
    float filament_cost_per_kg = 22.0f;

    // --- Transform & Placement ---
    bool auto_center = true;
    bool auto_drop_to_bed = true;
    bool auto_lay_flat = false;
    float pos_x = 0.0f;  // offset from bed center (when auto_center=true) or absolute
    float pos_y = 0.0f;
    float scale_x = 1.0f;
    float scale_y = 1.0f;
    float scale_z = 1.0f;
    float rotate_x_deg = 0.0f;
    float rotate_y_deg = 0.0f;
    float rotate_z_deg = 0.0f;
    bool mirror_x = false;
    bool mirror_y = false;
    int copies = 1;
    float copy_spacing = 8.0f;

    // --- Layer Height ---
    float layer_height = 0.20f;
    float first_layer_height = 0.24f;
    bool adaptive_layer_height = false;
    float adaptive_min_layer = 0.08f;
    float adaptive_max_layer = 0.28f;
    float adaptive_cusp_error = 0.04f;  // Dolenc-Makela cusp height tolerance (mm)
    int adaptive_smooth_radius = 2;

    // --- Perimeters, Arachne & Accuracy ---
    int wall_count = 3;
    WallOrder wall_order = WallOrder::InnerFirst;
    float outer_wall_width = 0.42f;
    float inner_wall_width = 0.45f;
    float first_layer_width = 0.48f;
    float infill_width = 0.45f;
    float top_solid_width = 0.42f;
    float support_width = 0.40f;
    bool arachne_thin_walls = true;
    float arachne_min_width_ratio = 0.65f;  // min width = nozzle * ratio
    float arachne_max_width_ratio = 1.65f;  // max width = nozzle * ratio
    float elephant_foot_comp = 0.15f;       // inward compensation on layer 0 (mm)
    float xy_contour_comp = 0.0f;           // positive expands outer walls
    float xy_hole_comp = 0.0f;              // positive widens holes
    bool spiral_vase = false;
    FuzzySkinMode fuzzy_skin = FuzzySkinMode::None;
    float fuzzy_skin_thickness = 0.30f;     // mm lateral perturbation
    float fuzzy_skin_point_dist = 0.80f;    // mm sampling pitch along outer wall
    float small_perimeter_speed = 28.0f;    // mm/s for small holes/pins
    float small_perimeter_radius = 6.5f;    // mm equivalent radius threshold (0 = off)
    float simplify_tolerance = 0.012f;      // Douglas-Peucker resolution (mm)

    // --- Top / Bottom / Ironing / Bridging ---
    int top_layers = 4;
    int bottom_layers = 3;
    TopBottomPattern top_pattern = TopBottomPattern::Monotonic;
    TopBottomPattern bottom_pattern = TopBottomPattern::Monotonic;
    float skin_overlap_pct = 15.0f;
    bool detect_bridges = true;
    float bridge_flow = 0.95f;
    float bridge_speed = 25.0f;
    bool ironing = false;
    float ironing_flow = 0.10f;
    float ironing_spacing = 0.10f;
    float ironing_speed = 30.0f;

    // --- Infill ---
    float infill_density = 20.0f;           // 0..100 %
    InfillPattern infill_pattern = InfillPattern::Gyroid;
    float infill_overlap_pct = 20.0f;
    float infill_angle_deg = 45.0f;
    int combine_infill_layers = 1;

    // --- Supports ---
    SupportType support_type = SupportType::None;
    SupportPlacement support_placement = SupportPlacement::BuildplateOnly;
    float support_overhang_angle = 50.0f;   // degrees from horizontal/vertical (angle from vertical: >50 deg needs support)
    float support_density = 18.0f;
    float support_z_gap = 0.20f;
    float support_xy_gap = 0.50f;
    int support_interface_layers = 2;
    float support_interface_density = 75.0f;
    float tree_branch_diameter = 2.4f;
    float tree_max_branch_angle = 45.0f;
    bool support_brim = true;

    // --- Bed Adhesion ---
    AdhesionType adhesion = AdhesionType::Skirt;
    int skirt_loops = 2;
    float skirt_distance = 5.0f;
    float brim_width = 5.0f;
    float brim_gap = 0.10f;
    int raft_layers = 2;
    float raft_expansion = 4.0f;
    float raft_z_gap = 0.18f;

    // --- Seam, Travel & Retraction ---
    SeamPosition seam_position = SeamPosition::SharpestCorner;
    bool scarf_seam = false;
    float scarf_length = 8.0f;
    int scarf_steps = 8;
    bool combing = true;
    float retract_length = 0.8f;
    float retract_speed = 40.0f;
    float unretract_speed = 35.0f;
    float retract_min_travel = 1.5f;
    float z_hop = 0.25f;
    bool wipe_on_retract = true;
    float wipe_distance = 2.0f;
    bool arc_fitting = true;
    float arc_tolerance = 0.025f;

    // --- Temperatures & Material Flow ---
    float nozzle_temp = 210.0f;
    float first_layer_nozzle_temp = 215.0f;
    float bed_temp = 60.0f;
    float first_layer_bed_temp = 65.0f;
    float flow_ratio = 1.0f;
    float max_volumetric_flow = 18.0f;      // mm^3/s
    float pressure_advance = 0.04f;         // K / advance factor (0 = disabled)

    // --- Speeds (mm/s) ---
    float speed_first_layer = 25.0f;
    float speed_outer_wall = 60.0f;
    float speed_inner_wall = 100.0f;
    float speed_infill = 150.0f;
    float speed_top_solid = 70.0f;
    float speed_bottom_solid = 80.0f;
    float speed_gap_fill = 55.0f;
    float speed_support = 90.0f;
    float speed_support_interface = 55.0f;
    float speed_travel = 220.0f;

    // --- Overhang Slowdown & Cooling ---
    bool overhang_slowdown = true;
    float overhang_speed_25 = 50.0f;        // mm/s for 25-50% unsupported
    float overhang_speed_50 = 30.0f;        // mm/s for 50-75% unsupported
    float overhang_speed_75 = 18.0f;        // mm/s for >75% unsupported
    float min_layer_time_sec = 8.0f;
    float min_print_speed = 12.0f;
    int fan_disable_first_layers = 1;
    int fan_min_pct = 35;
    int fan_max_pct = 100;
    int fan_bridge_pct = 100;

    // --- Kinematics / Acceleration (mm/s^2) ---
    float accel_default = 2500.0f;
    float accel_outer_wall = 1500.0f;
    float accel_infill = 4000.0f;
    float accel_travel = 4500.0f;
    float accel_first_layer = 1000.0f;
    float junction_deviation = 0.02f;       // Marlin 2 JD (mm)
    float square_corner_velocity = 5.0f;    // Klipper SCV (mm/s)
    float classic_jerk = 8.0f;              // Marlin Classic Jerk (mm/s)

    // --- Performance & Output ---
    std::vector<int> pause_layers;          // 0-based layer indices to insert M600 / PAUSE
    std::string custom_start_gcode;
    std::string custom_end_gcode;
    int threads = 0;                        // 0 = auto (hardware_concurrency)
    bool enable_layer_cache = true;
    bool emit_gcode = true;
    bool emit_binary_toolpath = true;
};

struct StageTimingsMs {
    double load_transform_ms = 0.0;
    double slice_stitch_ms = 0.0;
    double surfaces_overhang_ms = 0.0;
    double supports_ms = 0.0;
    double perimeters_infill_ms = 0.0;
    double path_motion_plan_ms = 0.0;
    double gcode_serialize_ms = 0.0;
    double total_ms = 0.0;
};

struct SliceStats {
    uint32_t tri_count = 0;
    uint32_t layer_count = 0;
    uint32_t total_moves = 0;
    uint32_t extrusion_moves = 0;
    uint32_t travel_moves = 0;
    uint32_t arc_moves = 0;
    uint32_t retract_count = 0;
    uint32_t cached_layers = 0;
    uint32_t threads_used = 1;

    float model_size_x = 0.0f;
    float model_size_y = 0.0f;
    float model_size_z = 0.0f;
    float model_volume_cm3 = 0.0f;
    float overhang_face_pct = 0.0f;
    bool fits_bed = true;
    uint32_t non_manifold_open_chains = 0;

    double print_time_sec = 0.0;
    double filament_mm = 0.0;
    double filament_cm3 = 0.0;
    double filament_g = 0.0;
    double estimated_cost = 0.0;
    float max_volumetric_flow_mm3s = 0.0f;
    size_t gcode_bytes = 0;
    size_t ktp_bytes = 0;

    std::array<double, static_cast<size_t>(FeatureType::Count)> feature_time_sec{};
    std::array<double, static_cast<size_t>(FeatureType::Count)> feature_filament_mm{};
    StageTimingsMs timings{};
};

// Apply printer / material / quality preset names into config
bool apply_printer_preset(SlicerConfig& cfg, const std::string& id);
bool apply_material_preset(SlicerConfig& cfg, const std::string& id);
bool apply_quality_preset(SlicerConfig& cfg, const std::string& id);

// Parse flat JSON object of overrides into SlicerConfig (zero external dependencies)
bool parse_config_json(const std::string& json, SlicerConfig& cfg, std::string& err);

// Serialize SliceStats to JSON
std::string stats_to_json(const SliceStats& st, const SlicerConfig& cfg);

}  // namespace katman
