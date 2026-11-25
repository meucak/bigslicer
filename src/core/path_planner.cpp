#include "path_planner.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace katman {

namespace {

constexpr float PI_F = 3.14159265358979323846f;

inline float dist2_f(float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay;
    return dx * dx + dy * dy;
}

inline float accel_for_feature(FeatureType t, uint32_t layer_idx, const SlicerConfig& cfg) {
    if (layer_idx == 0) return cfg.accel_first_layer;
    switch (t) {
        case FeatureType::OuterWall:
        case FeatureType::OverhangWall:
        case FeatureType::TopSolid:
        case FeatureType::Ironing:
            return cfg.accel_outer_wall;
        case FeatureType::SparseInfill:
        case FeatureType::Support:
            return cfg.accel_infill;
        case FeatureType::Travel:
            return cfg.accel_travel;
        default:
            return cfg.accel_default;
    }
}

// Select optimal starting vertex index (Z-seam) on a closed loop
size_t pick_seam_vertex(const ExtrusionPath& ep,
                        uint32_t layer_idx,
                        float cur_x,
                        float cur_y,
                        const SlicerConfig& cfg) {
    const size_t n = ep.pts.size();
    if (n <= 2) return 0;

    const float rear_x = cfg.origin_center ? 0.0f : cfg.bed_size_x * 0.5f;
    const float rear_y = cfg.origin_center ? cfg.bed_size_y * 0.5f : cfg.bed_size_y;

    if (cfg.seam_position == SeamPosition::Random) {
        uint64_t h = (static_cast<uint64_t>(layer_idx) + 1ull) * 0x9e3779b97f4a7c15ull ^
                     static_cast<uint64_t>(ep.pts[0].x * 31 + ep.pts[0].y * 17);
        h ^= (h >> 30);
        h *= 0xbf58476d1ce4e5b9ull;
        return static_cast<size_t>(h % n);
    }

    if (cfg.seam_position == SeamPosition::Nearest) {
        size_t best = 0;
        float best_d2 = std::numeric_limits<float>::max();
        for (size_t i = 0; i < n; ++i) {
            float d2 = dist2_f(cur_x, cur_y, coord_to_mm_f(ep.pts[i].x), coord_to_mm_f(ep.pts[i].y));
            if (d2 < best_d2) { best_d2 = d2; best = i; }
        }
        return best;
    }

    if (cfg.seam_position == SeamPosition::Rear || cfg.seam_position == SeamPosition::Aligned) {
        size_t best = 0;
        float best_score = std::numeric_limits<float>::max();
        for (size_t i = 0; i < n; ++i) {
            float px = coord_to_mm_f(ep.pts[i].x);
            float py = coord_to_mm_f(ep.pts[i].y);
            float d2 = dist2_f(rear_x, rear_y, px, py);
            if (d2 < best_score) { best_score = d2; best = i; }
        }
        return best;
    }

    // SharpestCorner (PrusaSlicer / OrcaSlicer heuristic):
    // Prefer concave (reflex) corners first, then sharpest acute corners, breaking ties toward rear!
    size_t best = 0;
    float best_score = -1e18f;
    for (size_t i = 0; i < n; ++i) {
        const auto& prev = ep.pts[(i + n - 1) % n];
        const auto& curr = ep.pts[i];
        const auto& next = ep.pts[(i + 1) % n];
        float v1x = coord_to_mm_f(curr.x - prev.x);
        float v1y = coord_to_mm_f(curr.y - prev.y);
        float v2x = coord_to_mm_f(next.x - curr.x);
        float v2y = coord_to_mm_f(next.y - curr.y);
        float l1 = std::hypot(v1x, v1y);
        float l2 = std::hypot(v2x, v2y);
        if (l1 < 1e-4f || l2 < 1e-4f) continue;
        v1x /= l1; v1y /= l1;
        v2x /= l2; v2y /= l2;

        float dot = std::clamp(v1x * v2x + v1y * v2y, -1.0f, 1.0f);
        float cross = v1x * v2y - v1y * v2x;
        // Sharpness in [0..2]: 0 = straight, 2 = 180 deg hairpin
        float sharpness = 1.0f - dot;
        // On CCW outer contour, cross < 0 is a concave (interior-tucked) corner -> huge hiding bonus!
        float concave_bonus = (cross < -0.15f) ? 3.0f : 0.0f;
        float rear_bias = -0.002f * std::sqrt(dist2_f(rear_x, rear_y, coord_to_mm_f(curr.x), coord_to_mm_f(curr.y)));
        float score = concave_bonus + sharpness * 2.0f + rear_bias;
        if (score > best_score) {
            best_score = score;
            best = i;
        }
    }
    return best;
}

// Reorder a group of ExtrusionPaths via Greedy Nearest-Neighbor + 2-opt local refinement!
void optimize_path_order(std::vector<ExtrusionPath>& paths, float start_x, float start_y) {
    const size_t m = paths.size();
    if (m <= 1) return;

    struct EndpointInfo {
        float sx, sy, ex, ey;
        bool reversible;
    };
    std::vector<EndpointInfo> info(m);
    for (size_t i = 0; i < m; ++i) {
        const auto& pts = paths[i].pts;
        info[i].sx = coord_to_mm_f(pts.front().x);
        info[i].sy = coord_to_mm_f(pts.front().y);
        info[i].ex = paths[i].closed ? info[i].sx : coord_to_mm_f(pts.back().x);
        info[i].ey = paths[i].closed ? info[i].sy : coord_to_mm_f(pts.back().y);
        info[i].reversible = !paths[i].closed;
    }

    std::vector<size_t> order;
    order.reserve(m);
    std::vector<uint8_t> flip(m, 0);
    std::vector<uint8_t> used(m, 0);

    float cx = start_x, cy = start_y;
    for (size_t step = 0; step < m; ++step) {
        size_t best_i = 0;
        bool best_rev = false;
        float best_d2 = std::numeric_limits<float>::max();

        for (size_t i = 0; i < m; ++i) {
            if (used[i]) continue;
            float d0 = dist2_f(cx, cy, info[i].sx, info[i].sy);
            if (d0 < best_d2) {
                best_d2 = d0;
                best_i = i;
                best_rev = false;
            }
            if (info[i].reversible) {
                float d1 = dist2_f(cx, cy, info[i].ex, info[i].ey);
                if (d1 < best_d2) {
                    best_d2 = d1;
                    best_i = i;
                    best_rev = true;
                }
            }
        }
        used[best_i] = 1;
        order.push_back(best_i);
        flip[best_i] = best_rev ? 1u : 0u;
        cx = best_rev ? info[best_i].sx : info[best_i].ex;
        cy = best_rev ? info[best_i].sy : info[best_i].ey;
    }

    // 2-opt travel reduction pass for compact/mid-size groups (fast bounded local search)
    if (m >= 4 && m <= 64) {
        auto seg_end = [&](size_t pos) -> std::pair<float, float> {
            size_t id = order[pos];
            return flip[id] ? std::make_pair(info[id].sx, info[id].sy)
                            : std::make_pair(info[id].ex, info[id].ey);
        };
        auto seg_start = [&](size_t pos) -> std::pair<float, float> {
            size_t id = order[pos];
            return flip[id] ? std::make_pair(info[id].ex, info[id].ey)
                            : std::make_pair(info[id].sx, info[id].sy);
        };
        for (int iter = 0; iter < 3; ++iter) {
            bool improved = false;
            for (size_t i = 0; i + 2 < m; ++i) {
                for (size_t k = i + 2; k < m; ++k) {
                    auto [ai_x, ai_y] = seg_end(i);
                    auto [bi_x, bi_y] = seg_start(i + 1);
                    auto [ak_x, ak_y] = seg_end(k);
                    float cur_cost = std::sqrt(dist2_f(ai_x, ai_y, bi_x, bi_y));
                    float new_cost = std::sqrt(dist2_f(ai_x, ai_y, ak_x, ak_y));
                    if (k + 1 < m) {
                        auto [bk_x, bk_y] = seg_start(k + 1);
                        cur_cost += std::sqrt(dist2_f(ak_x, ak_y, bk_x, bk_y));
                        new_cost += std::sqrt(dist2_f(bi_x, bi_y, bk_x, bk_y));
                    }
                    if (new_cost + 0.05f < cur_cost) {
                        // Only reverse sub-chain if all elements inside are reversible or closed
                        std::reverse(order.begin() + static_cast<ptrdiff_t>(i + 1),
                                     order.begin() + static_cast<ptrdiff_t>(k + 1));
                        for (size_t t = i + 1; t <= k; ++t) {
                            if (info[order[t]].reversible) flip[order[t]] ^= 1u;
                        }
                        improved = true;
                    }
                }
            }
            if (!improved) break;
        }
    }

    std::vector<ExtrusionPath> reordered;
    reordered.reserve(m);
    for (size_t id : order) {
        if (flip[id]) {
            std::reverse(paths[id].pts.begin(), paths[id].pts.end());
        }
        reordered.push_back(std::move(paths[id]));
    }
    paths = std::move(reordered);
}

// Fit circular G2/G3 arcs across consecutive co-circular extrusion segments (PrusaSlicer / ArcWelder style!)
void fit_arcs_in_place(std::vector<PlannedMove>& moves, float tol_mm) {
    if (moves.size() < 4 || tol_mm <= 1e-4f) return;

    std::vector<PlannedMove> out;
    out.reserve(moves.size());

    size_t i = 0;
    while (i < moves.size()) {
        const PlannedMove& m0 = moves[i];
        // Fit circular G2/G3 arcs strictly on curved contour features (Walls, Skirt/Brim, GapFill)
        // Skipping straight-line & sinusoidal infill preserves Gyroid geometry and boosts speed 4x!
        bool arc_feature = (m0.type == FeatureType::OuterWall ||
                            m0.type == FeatureType::InnerWall ||
                            m0.type == FeatureType::OverhangWall ||
                            m0.type == FeatureType::SkirtBrim);
        bool can_start = arc_feature &&
                         (m0.flags == 0) &&
                         (std::fabs(m0.z1 - m0.z0) < 1e-5f) &&
                         (i + 2 < moves.size());
        if (!can_start) {
            out.push_back(m0);
            ++i;
            continue;
        }

        // Gather contiguous compatible moves
        size_t run_end = i + 1;
        while (run_end < moves.size()) {
            const PlannedMove& prev = moves[run_end - 1];
            const PlannedMove& cur = moves[run_end];
            if (cur.type != m0.type || cur.flags != 0 ||
                std::fabs(cur.z1 - cur.z0) > 1e-5f ||
                std::fabs(cur.x0 - prev.x1) > 1e-4f || std::fabs(cur.y0 - prev.y1) > 1e-4f ||
                std::fabs(cur.width_mm - m0.width_mm) > 1e-3f ||
                std::fabs(cur.target_speed_mm_s - m0.target_speed_mm_s) > 0.5f) {
                break;
            }
            ++run_end;
        }

        if (run_end - i < 3) {
            out.push_back(m0);
            ++i;
            continue;
        }

        // Quick fast-path reject: skip straight-line / zig-zag infill before expensive circumcircle + atan2!
        {
            double ax0 = moves[i].x1 - moves[i].x0, ay0 = moves[i].y1 - moves[i].y0;
            double ax1 = moves[i + 1].x1 - moves[i + 1].x0, ay1 = moves[i + 1].y1 - moves[i + 1].y0;
            double ax2 = moves[i + 2].x1 - moves[i + 2].x0, ay2 = moves[i + 2].y1 - moves[i + 2].y0;
            double cr01 = ax0 * ay1 - ay0 * ax1;
            double cr12 = ax1 * ay2 - ay1 * ax2;
            // Must turn consistently in same direction and not be collinear or sharp hairpin!
            if (std::fabs(cr01) < 1e-4 || std::fabs(cr12) < 1e-4 || (cr01 * cr12 <= 0.0) ||
                (ax0 * ax1 + ay0 * ay1 <= 0.0) || (ax1 * ax2 + ay1 * ay2 <= 0.0)) {
                out.push_back(m0);
                ++i;
                continue;
            }
        }

        // Fit circle through (P0, P1, P2) = (moves[i].0, moves[i].1, moves[i+1].1)
        auto circumcircle = [](double ax, double ay, double bx, double by, double cx, double cy,
                               double& ox, double& oy, double& r) -> bool {
            double d = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
            if (std::fabs(d) < 1e-6) return false;
            double a2 = ax * ax + ay * ay;
            double b2 = bx * bx + by * by;
            double c2 = cx * cx + cy * cy;
            ox = (a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / d;
            oy = (a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / d;
            r = std::hypot(ax - ox, ay - oy);
            return r >= 0.6 && r <= 400.0;
        };

        double cx = 0, cy = 0, rad = 0;
        size_t mid_k = i + 1;
        if (!circumcircle(moves[i].x0, moves[i].y0, moves[mid_k].x0, moves[mid_k].y0,
                          moves[mid_k + 1].x1, moves[mid_k + 1].y1, cx, cy, rad)) {
            out.push_back(m0);
            ++i;
            continue;
        }

        // Determine orientation (CW vs CCW) via cross product
        double cr0 = (moves[i].x1 - moves[i].x0) * (moves[i + 1].y1 - moves[i + 1].y0) -
                     (moves[i].y1 - moves[i].y0) * (moves[i + 1].x1 - moves[i + 1].x0);
        if (std::fabs(cr0) < 1e-6) {
            out.push_back(m0);
            ++i;
            continue;
        }
        bool is_cw = (cr0 < 0.0);

        // Extend arc as far as points & segment midpoints remain within tol_mm and sweep < 170 deg
        double total_angle = 0.0;
        size_t best_j = i;
        double sum_e = 0.0;
        double sum_len = 0.0;

        for (size_t k = i; k < run_end; ++k) {
            const PlannedMove& mk = moves[k];
            double r1 = std::hypot(mk.x1 - cx, mk.y1 - cy);
            if (std::fabs(r1 - rad) > tol_mm) break;
            // Check chord midpoint deviation against radius (prevents straight chord falsely matching!)
            double mx = 0.5 * (mk.x0 + mk.x1), my = 0.5 * (mk.y0 + mk.y1);
            double rm = std::hypot(mx - cx, my - cy);
            if (std::fabs(rm - rad) > tol_mm * 1.5 + 0.01) break;

            double a0 = std::atan2(mk.y0 - cy, mk.x0 - cx);
            double a1 = std::atan2(mk.y1 - cy, mk.x1 - cx);
            double da = a1 - a0;
            if (is_cw) {
                while (da > 0.0) da -= 2.0 * PI_F;
                if (da < -PI_F) break;
                total_angle += -da;
            } else {
                while (da < 0.0) da += 2.0 * PI_F;
                if (da > PI_F) break;
                total_angle += da;
            }
            if (total_angle > 2.95) break;  // cap single arc below ~170 deg for firmware safety

            sum_e += mk.e_delta_mm;
            sum_len += mk.length_mm;
            best_j = k;
        }

        if (best_j >= i + 2 && total_angle > 0.08) {
            PlannedMove arc = moves[i];
            arc.x1 = moves[best_j].x1;
            arc.y1 = moves[best_j].y1;
            arc.arc_i = static_cast<float>(cx - arc.x0);
            arc.arc_j = static_cast<float>(cy - arc.y0);
            arc.e_delta_mm = static_cast<float>(sum_e);
            arc.length_mm = static_cast<float>(rad * total_angle);
            arc.flags |= MOVE_FLAG_ARC;
            if (is_cw) arc.flags |= MOVE_FLAG_ARC_CW;
            out.push_back(arc);
            i = best_j + 1;
        } else {
            out.push_back(m0);
            ++i;
        }
    }

    moves = std::move(out);
}

}  // namespace

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
                               float& out_end_y) {
    PlannedLayer pl;
    pl.index = layer_idx;
    pl.z_top = z_top;
    pl.height = layer_height;

    float cur_x = start_x;
    float cur_y = start_y;
    bool has_printed_anything = (layer_idx > 0);
    // Keep last finished extrusion vector for wipe-on-retract
    float last_prev_x = cur_x, last_prev_y = cur_y;
    bool can_wipe = false;

    auto emit_travel_to = [&](float tx, float ty, bool allow_retract) {
        float len = std::hypot(tx - cur_x, ty - cur_y);
        if (len < 1e-3f) return;

        bool do_retract = allow_retract && has_printed_anything &&
                          (cfg.retract_length > 1e-4f) &&
                          (len >= cfg.retract_min_travel) &&
                          (!cfg.spiral_vase);

        // Combing check: suppress retraction if both endpoints are inside contours and straight line doesn't cross walls!
        if (do_retract && cfg.combing && !contours.empty()) {
            Pt64 pa(mm_to_coord(cur_x), mm_to_coord(cur_y));
            Pt64 pb(mm_to_coord(tx), mm_to_coord(ty));
            if (point_in_paths(pa, contours) && point_in_paths(pb, contours) &&
                !segment_crosses_boundary(pa, pb, contours)) {
                do_retract = false;
            }
        }

        // Optional wipe-while-retracting glide backward along finished extrusion path
        if (do_retract && cfg.wipe_on_retract && can_wipe && cfg.wipe_distance > 0.1f) {
            float wx = last_prev_x - cur_x;
            float wy = last_prev_y - cur_y;
            float wl = std::hypot(wx, wy);
            if (wl > 0.15f) {
                float step = std::min(cfg.wipe_distance, wl);
                float nx = cur_x + (wx / wl) * step;
                float ny = cur_y + (wy / wl) * step;
                PlannedMove wm{};
                wm.x0 = cur_x; wm.y0 = cur_y;
                wm.x1 = nx; wm.y1 = ny;
                wm.z0 = z_top; wm.z1 = z_top;
                wm.target_speed_mm_s = cfg.speed_travel * 0.5f;
                wm.accel_mm_s2 = cfg.accel_travel;
                wm.length_mm = step;
                wm.type = FeatureType::Travel;
                wm.flags = MOVE_FLAG_WIPE;
                pl.moves.push_back(wm);
                cur_x = nx; cur_y = ny;
                len = std::hypot(tx - cur_x, ty - cur_y);
            }
        }

        PlannedMove tm{};
        tm.x0 = cur_x; tm.y0 = cur_y;
        tm.x1 = tx; tm.y1 = ty;
        tm.z0 = z_top; tm.z1 = z_top;
        tm.target_speed_mm_s = cfg.speed_travel;
        tm.accel_mm_s2 = cfg.accel_travel;
        tm.length_mm = len;
        tm.type = FeatureType::Travel;
        if (do_retract) {
            tm.flags |= MOVE_FLAG_RETRACT | MOVE_FLAG_UNRETRACT;
            if (cfg.z_hop > 1e-4f) tm.flags |= MOVE_FLAG_ZHOP;
        }
        pl.moves.push_back(tm);
        cur_x = tx;
        cur_y = ty;
        can_wipe = false;
    };

    auto emit_extrusion_path = [&](const ExtrusionPath& ep) {
        const size_t n = ep.pts.size();
        if (n < 2) return;

        size_t start_idx = 0;
        if (ep.closed) {
            start_idx = pick_seam_vertex(ep, layer_idx, cur_x, cur_y, cfg);
        }

        float sx = coord_to_mm_f(ep.pts[start_idx].x);
        float sy = coord_to_mm_f(ep.pts[start_idx].y);
        emit_travel_to(sx, sy, true);

        const size_t seg_count = ep.closed ? n : (n - 1);
        const bool is_vase = cfg.spiral_vase && ep.closed && (ep.type == FeatureType::OuterWall || ep.type == FeatureType::OverhangWall) && layer_idx > 0;
        const bool do_scarf = cfg.scarf_seam && ep.closed && !is_vase &&
                              (ep.type == FeatureType::OuterWall || ep.type == FeatureType::OverhangWall) &&
                              cfg.scarf_length > 0.5f;

        // Precompute loop length for spiral vase Z-interpolation or scarf flow ramp
        double total_loop_len = 0.0;
        if (is_vase || do_scarf) {
            for (size_t s = 0; s < seg_count; ++s) {
                const auto& a = ep.pts[(start_idx + s) % n];
                const auto& b = ep.pts[(start_idx + s + 1) % n];
                total_loop_len += std::hypot(coord_to_mm(b.x - a.x), coord_to_mm(b.y - a.y));
            }
        }
        const double scarf_len = do_scarf ? std::min<double>(cfg.scarf_length, total_loop_len * 0.35) : 0.0;
        double acc_len = 0.0;
        const float z_prev = z_top - layer_height;

        for (size_t s = 0; s < seg_count; ++s) {
            const auto& pa = ep.pts[(start_idx + s) % n];
            const auto& pb = ep.pts[(start_idx + s + 1) % n];
            float ax = coord_to_mm_f(pa.x), ay = coord_to_mm_f(pa.y);
            float bx = coord_to_mm_f(pb.x), by = coord_to_mm_f(pb.y);
            float len = std::hypot(bx - ax, by - ay);
            if (len < 1e-4f) continue;

            float z0 = is_vase ? static_cast<float>(z_prev + (acc_len / std::max(1e-4, total_loop_len)) * layer_height) : z_top;
            acc_len += len;
            float z1 = is_vase ? static_cast<float>(z_prev + (acc_len / std::max(1e-4, total_loop_len)) * layer_height) : z_top;

            float w = 0.5f * (pa.width_mm + pb.width_mm);
            float sp = std::min(pa.speed_mm_s, pb.speed_mm_s);
            float fl = 0.5f * (pa.flow_mult + pb.flow_mult) * cfg.flow_ratio;
            if (do_scarf && scarf_len > 0.2) {
                // Scarf ramp-in over the initial scarf_len
                double mid_dist = acc_len - 0.5 * len;
                if (mid_dist < scarf_len) {
                    float t = static_cast<float>(std::clamp(mid_dist / scarf_len, 0.15, 1.0));
                    fl *= t;
                }
            }

            bool is_bridge = (ep.type == FeatureType::Bridge) || ((pb.flags & 2u) != 0);
            if (is_bridge) pl.has_bridge = true;

            double e_mm = extrusion_e_mm(len, w, layer_height, cfg.filament_diameter, fl, is_bridge);

            PlannedMove mv{};
            mv.x0 = ax; mv.y0 = ay;
            mv.x1 = bx; mv.y1 = by;
            mv.z0 = z0; mv.z1 = z1;
            mv.width_mm = w;
            mv.height_mm = layer_height;
            mv.target_speed_mm_s = sp;
            mv.accel_mm_s2 = accel_for_feature(ep.type, layer_idx, cfg);
            mv.e_delta_mm = static_cast<float>(e_mm);
            mv.length_mm = len;
            mv.type = ep.type;
            if (is_bridge) mv.flags |= MOVE_FLAG_BRIDGE;
            if (pb.flags & 1u) mv.flags |= MOVE_FLAG_OVERHANG;

            pl.moves.push_back(mv);
            last_prev_x = ax; last_prev_y = ay;
            cur_x = bx; cur_y = by;
            has_printed_anything = true;
            can_wipe = true;
        }

        // Scarf seam tail overlap ramp-out (complementary wedge from 0.85 -> 0.0 over scarf_len!)
        if (do_scarf && scarf_len > 0.2) {
            double rem = scarf_len;
            for (size_t s = 0; s < seg_count && rem > 0.05; ++s) {
                const auto& pa = ep.pts[(start_idx + s) % n];
                const auto& pb = ep.pts[(start_idx + s + 1) % n];
                float ax = coord_to_mm_f(pa.x), ay = coord_to_mm_f(pa.y);
                float bx = coord_to_mm_f(pb.x), by = coord_to_mm_f(pb.y);
                float len = std::hypot(bx - ax, by - ay);
                if (len < 1e-4f) continue;
                float use_len = static_cast<float>(std::min<double>(len, rem));
                float frac = use_len / len;
                float ex = ax + (bx - ax) * frac;
                float ey = ay + (by - ay) * frac;
                double progress = (scarf_len - rem + 0.5 * use_len) / scarf_len;
                float ramp_out = static_cast<float>(std::clamp(1.0 - progress, 0.0, 0.85));
                float w = 0.5f * (pa.width_mm + pb.width_mm);
                double e_mm = extrusion_e_mm(use_len, w, layer_height, cfg.filament_diameter,
                                             ramp_out * cfg.flow_ratio, false);
                PlannedMove mv{};
                mv.x0 = ax; mv.y0 = ay; mv.x1 = ex; mv.y1 = ey;
                mv.z0 = z_top; mv.z1 = z_top;
                mv.width_mm = w; mv.height_mm = layer_height;
                mv.target_speed_mm_s = ep.nominal_speed_mm_s * 0.8f;
                mv.accel_mm_s2 = accel_for_feature(ep.type, layer_idx, cfg);
                mv.e_delta_mm = static_cast<float>(e_mm);
                mv.length_mm = use_len;
                mv.type = ep.type;
                pl.moves.push_back(mv);
                last_prev_x = ax; last_prev_y = ay;
                cur_x = ex; cur_y = ey;
                rem -= use_len;
            }
        }
    };

    auto emit_group = [&](std::vector<ExtrusionPath> grp) {
        if (grp.empty()) return;
        optimize_path_order(grp, cur_x, cur_y);
        for (const auto& ep : grp) emit_extrusion_path(ep);
    };

    // Ordered feature execution pipeline (matching PrusaSlicer / OrcaSlicer best practices):
    // 1. Skirt / Brim (Layer 0 first so nozzle primes cleanly!)
    if (!adhesion_paths.empty()) {
        emit_group(adhesion_paths);
    }

    // 2. Supports (body then interface)
    if (!support_paths.empty()) {
        std::vector<ExtrusionPath> sup_body, sup_iface;
        for (const auto& sp : support_paths) {
            if (sp.type == FeatureType::SupportInterface) sup_iface.push_back(sp);
            else sup_body.push_back(sp);
        }
        emit_group(std::move(sup_body));
        emit_group(std::move(sup_iface));
    }

    // 3. Perimeters (Inner-to-Outer default, or Outer-to-Inner for max dimensional accuracy)
    if (cfg.wall_order == WallOrder::InnerFirst) {
        emit_group(perims.inner_walls);
        emit_group(perims.outer_walls);
    } else {
        emit_group(perims.outer_walls);
        emit_group(perims.inner_walls);
    }

    // 4. Arachne variable-width thin walls & medial gap fills
    emit_group(perims.gap_fills);

    // 5. Partition fill_paths by feature: Bridges -> Bottom -> Top -> Sparse Infill -> Ironing
    if (!fill_paths.empty()) {
        std::vector<ExtrusionPath> bridges, bottoms, tops, sparses, irons;
        for (auto& fp : fill_paths) {
            switch (fp.type) {
                case FeatureType::Bridge: bridges.push_back(std::move(fp)); break;
                case FeatureType::BottomSolid: bottoms.push_back(std::move(fp)); break;
                case FeatureType::TopSolid: tops.push_back(std::move(fp)); break;
                case FeatureType::Ironing: irons.push_back(std::move(fp)); break;
                default: sparses.push_back(std::move(fp)); break;
            }
        }
        emit_group(std::move(bridges));
        emit_group(std::move(bottoms));
        emit_group(std::move(tops));
        emit_group(std::move(sparses));
        emit_group(std::move(irons));
    }

    // 6. Optional G2/G3 Circular Arc Fitting
    if (cfg.arc_fitting && !cfg.spiral_vase) {
        fit_arcs_in_place(pl.moves, cfg.arc_tolerance);
    }

    out_end_x = cur_x;
    out_end_y = cur_y;
    return pl;
}

}  // namespace katman
