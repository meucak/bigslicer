#include "motion_planner.hpp"

#include <algorithm>
#include <cmath>

namespace katman {

namespace {

// Compute maximum allowed cornering speed between two consecutive moves using the configured firmware model
float compute_junction_speed(const PlannedMove& prev, const PlannedMove& curr, const SlicerConfig& cfg) {
    if (prev.length_mm < 1e-4f || curr.length_mm < 1e-4f) return 0.0f;
    if ((curr.flags & (MOVE_FLAG_RETRACT | MOVE_FLAG_ZHOP)) != 0) return 0.0f;

    float v1x = (prev.x1 - prev.x0) / prev.length_mm;
    float v1y = (prev.y1 - prev.y0) / prev.length_mm;
    float v2x = (curr.x1 - curr.x0) / curr.length_mm;
    float v2y = (curr.y1 - curr.y0) / curr.length_mm;
    float cos_theta = std::clamp(-(v1x * v2x + v1y * v2y), -1.0f, 1.0f);
    // Note: cos_theta = -1 means straight ahead (no bend!), +1 means 180 deg hairpin reversal!
    if (cos_theta <= -0.9999f) {
        return std::min(prev.actual_speed_mm_s, curr.actual_speed_mm_s);
    }
    if (cos_theta >= 0.9999f) {
        return 0.0f;
    }

    float a_min = std::min(prev.accel_mm_s2, curr.accel_mm_s2);
    float vmax = std::min(prev.actual_speed_mm_s, curr.actual_speed_mm_s);

    if (cfg.cornering_model == CorneringModel::JunctionDeviation) {
        // Marlin 2 / PrusaSlicer 2.9.4 Junction Deviation:
        // sin(theta/2) = sqrt(0.5 * (1 - cos_theta))
        float sin_half = std::sqrt(std::max(0.0f, 0.5f * (1.0f - cos_theta)));
        float denom = std::max(1e-5f, 1.0f - sin_half);
        float jd = std::max(0.001f, cfg.junction_deviation);
        float vj = std::sqrt(a_min * jd * sin_half / denom);
        return std::min(vmax, vj);
    }

    if (cfg.cornering_model == CorneringModel::SquareCornerVelocity) {
        // Klipper Square Corner Velocity:
        // Centripetal radius equivalent for 90 deg corner at SCV: R_scv = SCV^2 / (a * (sqrt(2)-1))
        float scv = std::max(0.5f, cfg.square_corner_velocity);
        float sin_half = std::sqrt(std::max(0.0f, 0.5f * (1.0f - cos_theta)));
        float cos_half = std::sqrt(std::max(0.0f, 0.5f * (1.0f + cos_theta)));
        if (cos_half < 1e-4f) return vmax;
        float tan_half = sin_half / cos_half;
        // Klipper formula: v_j^2 = a * scv_delta * sin(theta/2) / (1 - sin(theta/2))
        float scv_jd = (scv * scv * (1.41421356f - 1.0f)) / std::max(100.0f, a_min);
        float vj = std::sqrt(a_min * scv_jd * sin_half / std::max(1e-5f, 1.0f - sin_half));
        (void)tan_half;
        return std::min(vmax, vj);
    }

    // Classic Marlin Jerk: instantaneous vector delta <= classic_jerk
    float dv_unit = std::hypot(v2x - v1x, v2y - v1y);
    if (dv_unit < 1e-4f) return vmax;
    return std::min(vmax, cfg.classic_jerk / dv_unit);
}

// Integrate exact trapezoidal / triangular profile duration for a single block (d, v_in, v_out, v_nom, a)
double trapezoid_time_sec(double d, double v_in, double v_out, double v_nom, double a) {
    if (d <= 1e-6) return 0.0;
    v_nom = std::max(1.0, v_nom);
    a = std::max(50.0, a);
    v_in = std::min(v_in, v_nom);
    v_out = std::min(v_out, v_nom);

    double d_acc = (v_nom * v_nom - v_in * v_in) / (2.0 * a);
    double d_dec = (v_nom * v_nom - v_out * v_out) / (2.0 * a);

    if (d_acc + d_dec <= d) {
        // Full trapezoid: accelerate -> cruise -> decelerate
        double d_cruise = d - d_acc - d_dec;
        return (v_nom - v_in) / a + d_cruise / v_nom + (v_nom - v_out) / a;
    }

    // Triangular profile: peak velocity v_peak < v_nom
    double v_peak_sq = std::max(std::max(v_in * v_in, v_out * v_out),
                                a * d + 0.5 * (v_in * v_in + v_out * v_out));
    double v_peak = std::sqrt(v_peak_sq);
    return (v_peak - v_in) / a + (v_peak - v_out) / a;
}

void solve_layer_kinematics(PlannedLayer& layer, const SlicerConfig& cfg, float speed_scale) {
    auto& moves = layer.moves;
    const size_t n = moves.size();
    if (n == 0) {
        layer.layer_time_sec = 0.0;
        layer.layer_filament_mm = 0.0;
        return;
    }

    // 1. Apply volumetric flow cap + optional cooling slowdown scale
    const double fil_area = 0.25 * 3.14159265358979323846 * cfg.filament_diameter * cfg.filament_diameter;
    for (size_t i = 0; i < n; ++i) {
        PlannedMove& m = moves[i];
        float sp = m.target_speed_mm_s;
        if (m.type != FeatureType::Travel && m.e_delta_mm > 0.0f && m.length_mm > 1e-4f) {
            if (speed_scale < 0.999f) {
                sp = std::max(cfg.min_print_speed, sp * speed_scale);
            }
            // Volumetric flow Q = (e_delta * fil_area / length) * speed <= max_volumetric_flow
            double vol_per_mm = (static_cast<double>(m.e_delta_mm) * fil_area) / static_cast<double>(m.length_mm);
            if (vol_per_mm > 1e-6) {
                float max_sp_flow = static_cast<float>(cfg.max_volumetric_flow / vol_per_mm);
                sp = std::min(sp, max_sp_flow);
            }
            m.actual_speed_mm_s = std::max(1.0f, sp);
            m.flow_mm3_s = static_cast<float>(vol_per_mm * m.actual_speed_mm_s);
        } else {
            m.actual_speed_mm_s = std::max(1.0f, sp);
            m.flow_mm3_s = 0.0f;
        }
    }

    // 2. Compute junction velocities at boundaries v_j[0..n]
    std::vector<float> vj(n + 1, 0.0f);
    for (size_t i = 1; i < n; ++i) {
        vj[i] = compute_junction_speed(moves[i - 1], moves[i], cfg);
    }

    // 3. Forward lookahead pass: vj[i+1] <= sqrt(vj[i]^2 + 2*a*d)
    for (size_t i = 0; i < n; ++i) {
        float vmax_next = std::sqrt(vj[i] * vj[i] + 2.0f * moves[i].accel_mm_s2 * moves[i].length_mm);
        if (vj[i + 1] > vmax_next) vj[i + 1] = vmax_next;
    }

    // 4. Backward lookahead pass: vj[i] <= sqrt(vj[i+1]^2 + 2*a*d)
    for (int i = static_cast<int>(n) - 1; i >= 0; --i) {
        size_t ui = static_cast<size_t>(i);
        float vmax_prev = std::sqrt(vj[ui + 1] * vj[ui + 1] + 2.0f * moves[ui].accel_mm_s2 * moves[ui].length_mm);
        if (vj[ui] > vmax_prev) vj[ui] = vmax_prev;
    }

    // 5. Integrate exact durations + retraction/z-hop overhead
    double total_t = 0.0;
    double total_e = 0.0;
    const double retract_t = (cfg.retract_length > 1e-4f)
        ? (cfg.retract_length / std::max(5.0f, cfg.retract_speed) +
           cfg.retract_length / std::max(5.0f, cfg.unretract_speed))
        : 0.0;
    const double zhop_t = (cfg.z_hop > 1e-4f) ? (2.0 * cfg.z_hop / 12.0) : 0.0;

    for (size_t i = 0; i < n; ++i) {
        PlannedMove& m = moves[i];
        double dt = trapezoid_time_sec(m.length_mm, vj[i], vj[i + 1], m.actual_speed_mm_s, m.accel_mm_s2);
        if (m.flags & MOVE_FLAG_RETRACT) dt += retract_t;
        if (m.flags & MOVE_FLAG_ZHOP) dt += zhop_t;
        m.duration_sec = static_cast<float>(dt);
        total_t += dt;
        total_e += m.e_delta_mm;
    }
    // Add layer-change Z-step time
    total_t += static_cast<double>(layer.height) / 10.0;

    layer.layer_time_sec = total_t;
    layer.layer_filament_mm = total_e;
}

}  // namespace

void run_motion_and_cooling_planner(std::vector<PlannedLayer>& layers,
                                    const SlicerConfig& cfg,
                                    SliceStats& stats,
                                    ThreadPool* pool) {
    const size_t L = layers.size();
    if (L == 0) return;

    // Parallel per-layer lookahead kinematic pass + minimum layer time cooling pass!
    auto plan_one = [&](uint32_t, size_t i) {
        PlannedLayer& ly = layers[i];
        solve_layer_kinematics(ly, cfg, 1.0f);

        // Cooling slowdown if layer finishes faster than min_layer_time_sec (skip layer 0)
        if (i > 0 && cfg.min_layer_time_sec > 0.5f &&
            ly.layer_time_sec > 0.05 && ly.layer_time_sec < cfg.min_layer_time_sec) {
            float scale = static_cast<float>(ly.layer_time_sec / cfg.min_layer_time_sec);
            scale = std::clamp(scale, 0.12f, 1.0f);
            solve_layer_kinematics(ly, cfg, scale);
        }

        // Dynamic cooling fan PWM scheduling
        if (static_cast<int>(i) < cfg.fan_disable_first_layers) {
            ly.fan_pct = 0;
        } else if (ly.has_bridge) {
            ly.fan_pct = cfg.fan_bridge_pct;
        } else if (cfg.min_layer_time_sec > 0.5f && ly.layer_time_sec < cfg.min_layer_time_sec * 2.5) {
            double t = 1.0 - std::clamp((ly.layer_time_sec - cfg.min_layer_time_sec) /
                                        std::max(1.0, static_cast<double>(cfg.min_layer_time_sec) * 1.5), 0.0, 1.0);
            ly.fan_pct = static_cast<int>(std::lround(cfg.fan_min_pct + t * (cfg.fan_max_pct - cfg.fan_min_pct)));
        } else {
            ly.fan_pct = cfg.fan_min_pct;
        }
        ly.fan_pct = std::clamp(ly.fan_pct, 0, 100);
    };

    if (pool && L >= 4) {
        pool->parallel_for(L, plan_one, 1);
    } else {
        for (size_t i = 0; i < L; ++i) plan_one(0, i);
    }

    // Sequential cumulative stats + absolute E prefix sum (tiny O(L) + O(M) accumulation)
    double total_time = 0.0;
    double total_e = 0.0;
    uint32_t total_moves = 0, ext_moves = 0, trv_moves = 0, arc_moves = 0, ret_cnt = 0;
    float max_flow = 0.0f;
    stats.feature_time_sec.fill(0.0);
    stats.feature_filament_mm.fill(0.0);

    for (auto& ly : layers) {
        ly.start_e_abs_mm = total_e;
        total_time += ly.layer_time_sec;
        total_e += ly.layer_filament_mm;
        for (const auto& m : ly.moves) {
            ++total_moves;
            size_t fi = static_cast<size_t>(m.type);
            if (fi < stats.feature_time_sec.size()) {
                stats.feature_time_sec[fi] += m.duration_sec;
                stats.feature_filament_mm[fi] += m.e_delta_mm;
            }
            if (m.type == FeatureType::Travel) ++trv_moves;
            else ++ext_moves;
            if (m.flags & MOVE_FLAG_ARC) ++arc_moves;
            if (m.flags & MOVE_FLAG_RETRACT) ++ret_cnt;
            if (m.flow_mm3_s > max_flow) max_flow = m.flow_mm3_s;
        }
    }

    const double fil_area = 0.25 * 3.14159265358979323846 * cfg.filament_diameter * cfg.filament_diameter;
    stats.layer_count = static_cast<uint32_t>(L);
    stats.total_moves = total_moves;
    stats.extrusion_moves = ext_moves;
    stats.travel_moves = trv_moves;
    stats.arc_moves = arc_moves;
    stats.retract_count = ret_cnt;
    stats.print_time_sec = total_time;
    stats.filament_mm = total_e;
    stats.filament_cm3 = (total_e * fil_area) * 0.001;
    stats.filament_g = stats.filament_cm3 * cfg.filament_density_g_cm3;
    stats.estimated_cost = (stats.filament_g * 0.001) * cfg.filament_cost_per_kg;
    stats.max_volumetric_flow_mm3s = max_flow;
}

}  // namespace katman
