#include "perimeters.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace katman {

namespace {

inline double dist_pt_mm(const Pt64& a, const Pt64& b) {
    double dx = static_cast<double>(b.x - a.x) * INV_COORD_SCALE;
    double dy = static_cast<double>(b.y - a.y) * INV_COORD_SCALE;
    return std::sqrt(dx * dx + dy * dy);
}

// Compute minimum distance from point p to any segment of paths (in mm)
double min_dist_to_paths_mm(const Pt64& p, const Paths64& paths) {
    double best_d2 = std::numeric_limits<double>::max();
    const double px = static_cast<double>(p.x);
    const double py = static_cast<double>(p.y);
    for (const auto& poly : paths) {
        const size_t n = poly.size();
        if (n < 2) continue;
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            double ax = static_cast<double>(poly[j].x), ay = static_cast<double>(poly[j].y);
            double bx = static_cast<double>(poly[i].x), by = static_cast<double>(poly[i].y);
            double dx = bx - ax, dy = by - ay;
            double l2 = dx * dx + dy * dy;
            double t = (l2 > 1e-9) ? std::clamp(((px - ax) * dx + (py - ay) * dy) / l2, 0.0, 1.0) : 0.0;
            double cx = ax + t * dx - px;
            double cy = ay + t * dy - py;
            double d2 = cx * cx + cy * cy;
            if (d2 < best_d2) best_d2 = d2;
        }
    }
    return (best_d2 < std::numeric_limits<double>::max()) ? (std::sqrt(best_d2) * INV_COORD_SCALE) : 1e6;
}

// Extract variable-width Arachne medial paths from narrow/thin polygons using multi-step
// inward erosion + collapsed spine extraction (fast, deterministic, zero-gap thin wall fill!)
void extract_arachne_medial_paths(const Paths64& thin_regions,
                                  double min_w_mm,
                                  double max_w_mm,
                                  float base_speed,
                                  std::vector<ExtrusionPath>& out_paths) {
    if (thin_regions.empty()) return;

    // Step 1: Collapse thin regions via slight inward offset and trace paired centerline segments.
    // For any thin polygon whose thickness <= max_w_mm, offsetting inward by min_w/2 leaves either
    // empty (true thin feature) or a razor-thin skeleton polygon whose opposite edges trace the medial axis!
    const double probe_step = std::max(0.06, min_w_mm * 0.42);
    Paths64 eroded = offset_paths_mm(thin_regions, -probe_step, Clipper2Lib::JoinType::Miter, 2.0);
    const Paths64& source_polys = eroded.empty() ? thin_regions : eroded;

    for (const auto& poly : source_polys) {
        if (poly.size() < 3) continue;
        // Compute polygon bounding box & oriented principal axis for ultra-clean thin-wall spine
        Rect64 bb = compute_bounds(poly);
        double w_bb = coord_to_mm(bb.right - bb.left);
        double h_bb = coord_to_mm(bb.bottom - bb.top);
        if (std::max(w_bb, h_bb) < min_w_mm * 0.6) continue;

        // If this eroded polygon is thin (area / half-perimeter <= max_w), extract its single medial spine!
        double perim_mm = 0.0;
        for (size_t i = 0; i < poly.size(); ++i) {
            perim_mm += dist_pt_mm(poly[i], poly[(i + 1) % poly.size()]);
        }
        double area_mm2 = std::fabs(Clipper2Lib::Area(poly)) * INV_COORD_SCALE * INV_COORD_SCALE;
        double est_width = (perim_mm > 1e-4) ? (2.0 * area_mm2 / (perim_mm * 0.5)) : min_w_mm;
        if (!eroded.empty()) est_width += 2.0 * probe_step;

        if (est_width <= max_w_mm * 1.15 && poly.size() >= 3) {
            // Find the two farthest vertices to split boundary into two opposing sides and average!
            size_t idx_a = 0, idx_b = poly.size() / 2;
            double max_d2 = -1.0;
            // O(N) antipodal approximation via 4 extremal directions + refinement
            size_t ext[4] = {0, 0, 0, 0};
            for (size_t i = 1; i < poly.size(); ++i) {
                if (poly[i].x < poly[ext[0]].x) ext[0] = i;
                if (poly[i].x > poly[ext[1]].x) ext[1] = i;
                if (poly[i].y < poly[ext[2]].y) ext[2] = i;
                if (poly[i].y > poly[ext[3]].y) ext[3] = i;
            }
            for (int a = 0; a < 4; ++a) {
                for (int b = a + 1; b < 4; ++b) {
                    double dx = static_cast<double>(poly[ext[a]].x - poly[ext[b]].x);
                    double dy = static_cast<double>(poly[ext[a]].y - poly[ext[b]].y);
                    double d2 = dx * dx + dy * dy;
                    if (d2 > max_d2) { max_d2 = d2; idx_a = ext[a]; idx_b = ext[b]; }
                }
            }
            if (idx_a == idx_b) continue;

            // Walk forward chain (idx_a -> idx_b) and backward chain (idx_a -> idx_b via reverse)
            std::vector<Pt64> side1, side2;
            for (size_t k = idx_a; ; k = (k + 1) % poly.size()) {
                side1.push_back(poly[k]);
                if (k == idx_b) break;
            }
            for (size_t k = idx_a; ; k = (k + poly.size() - 1) % poly.size()) {
                side2.push_back(poly[k]);
                if (k == idx_b) break;
            }

            // Resample both chains by arc-length fraction and emit variable-width medial centerline!
            auto chain_len = [](const std::vector<Pt64>& ch) {
                double s = 0.0;
                for (size_t i = 1; i < ch.size(); ++i) s += dist_pt_mm(ch[i - 1], ch[i]);
                return s;
            };
            auto sample_chain = [](const std::vector<Pt64>& ch, double frac) -> Pt64 {
                if (ch.size() <= 1 || frac <= 0.0) return ch.front();
                if (frac >= 1.0) return ch.back();
                double total = 0.0;
                for (size_t i = 1; i < ch.size(); ++i) total += dist_pt_mm(ch[i - 1], ch[i]);
                double target = frac * total;
                double acc = 0.0;
                for (size_t i = 1; i < ch.size(); ++i) {
                    double seg = dist_pt_mm(ch[i - 1], ch[i]);
                    if (acc + seg >= target && seg > 1e-6) {
                        double t = (target - acc) / seg;
                        return Pt64(
                            static_cast<int64_t>(std::llround(ch[i - 1].x + t * (ch[i].x - ch[i - 1].x))),
                            static_cast<int64_t>(std::llround(ch[i - 1].y + t * (ch[i].y - ch[i - 1].y))));
                    }
                    acc += seg;
                }
                return ch.back();
            };

            double spine_len = 0.5 * (chain_len(side1) + chain_len(side2));
            if (spine_len < min_w_mm * 0.5) continue;
            int samples = std::clamp(static_cast<int>(std::ceil(spine_len / 0.8)) + 1, 2, 128);

            ExtrusionPath ep;
            ep.type = FeatureType::GapFill;
            ep.closed = false;
            ep.nominal_width_mm = static_cast<float>(std::clamp(est_width, min_w_mm, max_w_mm));
            ep.nominal_speed_mm_s = base_speed;
            ep.pts.reserve(static_cast<size_t>(samples));

            for (int s = 0; s < samples; ++s) {
                double frac = static_cast<double>(s) / static_cast<double>(samples - 1);
                Pt64 p1 = sample_chain(side1, frac);
                Pt64 p2 = sample_chain(side2, frac);
                Pt64 mid((p1.x + p2.x) / 2, (p1.y + p2.y) / 2);
                // Local Arachne variable extrusion width = separation + 2*probe_step, clamped!
                double sep = dist_pt_mm(p1, p2) + (eroded.empty() ? 0.0 : 2.0 * probe_step);
                float local_w = static_cast<float>(std::clamp(sep, min_w_mm, max_w_mm));
                if (!ep.pts.empty() && ep.pts.back().x == mid.x && ep.pts.back().y == mid.y) continue;
                ep.pts.push_back({mid.x, mid.y, local_w, base_speed, 1.0f, 0});
            }
            if (ep.pts.size() >= 2) {
                out_paths.push_back(std::move(ep));
            }
        } else {
            // Closed loop gap fill (e.g. concentric narrow ring between inner wall and hole)
            ExtrusionPath ep;
            ep.type = FeatureType::GapFill;
            ep.closed = true;
            float w = static_cast<float>(std::clamp(est_width, min_w_mm, max_w_mm));
            ep.nominal_width_mm = w;
            ep.nominal_speed_mm_s = base_speed;
            ep.pts.reserve(poly.size());
            for (const auto& pt : poly) {
                ep.pts.push_back({pt.x, pt.y, w, base_speed, 1.0f, 0});
            }
            if (ep.pts.size() >= 3) {
                out_paths.push_back(std::move(ep));
            }
        }
    }
}

ExtrusionPath path64_to_extrusion(const Path64& poly, FeatureType type,
                                  float width_mm, float speed_mm_s,
                                  const SlicedLayer* prev_layer,
                                  const SlicerConfig& cfg,
                                  bool check_overhang) {
    ExtrusionPath ep;
    ep.type = type;
    ep.closed = true;
    ep.nominal_width_mm = width_mm;
    ep.nominal_speed_mm_s = speed_mm_s;
    ep.pts.reserve(poly.size());

    bool any_overhang = false;
    const bool do_ov = check_overhang && cfg.overhang_slowdown && prev_layer && !prev_layer->contours.empty();
    const double half_w = width_mm * 0.5;
    const Rect64 prev_bb = do_ov ? prev_layer->bounds : Rect64(0, 0, 0, 0);

    for (size_t i = 0; i < poly.size(); ++i) {
        const Pt64& p = poly[i];
        float pt_speed = speed_mm_s;
        uint8_t flags = 0;

        if (do_ov) {
            // Measure midpoint of incoming segment against previous layer support footprint
            const Pt64& prev_p = poly[(i + poly.size() - 1) % poly.size()];
            Pt64 mid((p.x + prev_p.x) / 2, (p.y + prev_p.y) / 2);
            bool inside_prev = (mid.x >= prev_bb.left && mid.x <= prev_bb.right &&
                                mid.y >= prev_bb.top && mid.y <= prev_bb.bottom) &&
                               point_in_paths(mid, prev_layer->contours);
            if (!inside_prev) {
                double d_out = min_dist_to_paths_mm(mid, prev_layer->contours);
                double unsup_frac = std::clamp((d_out + half_w * 0.5) / width_mm, 0.0, 1.0);
                if (unsup_frac >= 0.75) {
                    pt_speed = std::min(pt_speed, cfg.overhang_speed_75);
                    flags |= 1u;
                    any_overhang = true;
                } else if (unsup_frac >= 0.50) {
                    pt_speed = std::min(pt_speed, cfg.overhang_speed_50);
                    flags |= 1u;
                    any_overhang = true;
                } else if (unsup_frac >= 0.25) {
                    pt_speed = std::min(pt_speed, cfg.overhang_speed_25);
                    flags |= 1u;
                    any_overhang = true;
                }
            }
        }
        ep.pts.push_back({p.x, p.y, width_mm, pt_speed, 1.0f, flags});
    }

    if (any_overhang && type == FeatureType::OuterWall) {
        // Check if majority of vertices are overhangs -> tag path for clear visualization
        size_t ov_cnt = 0;
        for (const auto& pt : ep.pts) if (pt.flags & 1u) ++ov_cnt;
        if (ov_cnt * 2 >= ep.pts.size()) {
            ep.type = FeatureType::OverhangWall;
        }
    }
    return ep;
}

}  // namespace

LayerPerimeters generate_layer_perimeters(const SlicedLayer& layer,
                                          const SlicedLayer* prev_layer,
                                          const SlicerConfig& cfg) {
    LayerPerimeters res;
    if (layer.contours.empty()) return res;

    const bool is_first = (layer.index == 0);
    const float w_outer = is_first ? cfg.first_layer_width : cfg.outer_wall_width;
    const float w_inner = is_first ? cfg.first_layer_width : cfg.inner_wall_width;
    const float sp_outer = is_first ? cfg.speed_first_layer : cfg.speed_outer_wall;
    const float sp_inner = is_first ? cfg.speed_first_layer : cfg.speed_inner_wall;
    const float sp_gap = is_first ? cfg.speed_first_layer : cfg.speed_gap_fill;

    const int walls = cfg.spiral_vase ? 1 : std::max(1, cfg.wall_count);
    const double h = layer.height;
    const double outer_inset = w_outer * 0.5;
    const double step_inner = pill_spacing_mm(w_inner, h);
    const double step_outer_to_inner = 0.5 * (pill_spacing_mm(w_outer, h) + pill_spacing_mm(w_inner, h));

    // Loop 0: Outer perimeter centerline = offset(contours, -w_outer / 2)
    Paths64 cur = offset_paths_mm(layer.contours, -outer_inset);
    res.outer_wall_polys = cur;

    const bool check_ov = !is_first && prev_layer && (prev_layer->geom_hash != layer.geom_hash);
    for (const auto& p : cur) {
        if (p.size() < 3) continue;
        res.outer_walls.push_back(
            path64_to_extrusion(p, FeatureType::OuterWall, w_outer, sp_outer, prev_layer, cfg, check_ov));
    }

    auto total_perim_len_mm = [](const Paths64& ps) {
        double s = 0.0;
        for (const auto& poly : ps) {
            const size_t n = poly.size();
            if (n < 2) continue;
            for (size_t i = 0, j = n - 1; i < n; j = i++) {
                s += dist_pt_mm(poly[j], poly[i]);
            }
        }
        return s;
    };

    // Fast O(V) area-deficit check before running expensive Clipper2 thin-wall rescue!
    // If area(cur) + perim_len(cur)*outer_inset already accounts for >=97% of area(contours),
    // there are NO vanished thin fins -> skip 2 Clipper2 ops completely!
    if (cfg.arachne_thin_walls && !cfg.spiral_vase) {
        double c_area = total_area_mm2(layer.contours);
        double est_covered = cur.empty() ? 0.0 : (total_area_mm2(cur) + total_perim_len_mm(cur) * outer_inset);
        if (cur.empty() || (c_area - est_covered > std::max(0.35, c_area * 0.03))) {
            Paths64 covered_by_outer = cur.empty() ? Paths64{} : offset_paths_mm(cur, outer_inset + 0.03);
            Paths64 missed_thin = clip_difference(layer.contours, covered_by_outer);
            if (!missed_thin.empty()) {
                double min_w = cfg.nozzle_diameter * cfg.arachne_min_width_ratio;
                double max_w = cfg.nozzle_diameter * cfg.arachne_max_width_ratio;
                extract_arachne_medial_paths(missed_thin, min_w, max_w, sp_outer, res.gap_fills);
            }
        }
    }

    // Loops 1 .. walls-1: Inner walls
    Paths64 last_valid = cur;
    for (int loop = 1; loop < walls && !cur.empty(); ++loop) {
        double inset = (loop == 1) ? step_outer_to_inner : step_inner;
        Paths64 next = offset_paths_mm(cur, -inset);
        if (next.empty()) {
            // Entire remaining wall collapsed to a thin internal rib -> run Arachne gap fill once!
            if (cfg.arachne_thin_walls && !cfg.spiral_vase) {
                Paths64 gap_zone = offset_paths_mm(cur, -w_inner * 0.45);
                if (!gap_zone.empty()) {
                    double min_w = cfg.nozzle_diameter * cfg.arachne_min_width_ratio;
                    double max_w = cfg.nozzle_diameter * cfg.arachne_max_width_ratio;
                    extract_arachne_medial_paths(gap_zone, min_w, max_w, sp_gap, res.gap_fills);
                }
            }
            break;
        }

        for (const auto& p : next) {
            if (p.size() < 3) continue;
            res.inner_walls.push_back(
                path64_to_extrusion(p, FeatureType::InnerWall, w_inner, sp_inner, prev_layer, cfg, false));
        }

        // Only run inter-wall Arachne gap extraction when polygon count drops or significant area vanished!
        if (cfg.arachne_thin_walls && (next.size() < cur.size())) {
            double cur_a = total_area_mm2(cur);
            double next_a = total_area_mm2(next) + total_perim_len_mm(next) * inset;
            if (cur_a - next_a > std::max(0.45, cur_a * 0.04)) {
                Paths64 inner_covered = offset_paths_mm(next, inset * 0.92);
                Paths64 gap_zone = clip_difference(offset_paths_mm(cur, -w_inner * 0.45), inner_covered);
                if (!gap_zone.empty()) {
                    double min_w = cfg.nozzle_diameter * cfg.arachne_min_width_ratio;
                    double max_w = cfg.nozzle_diameter * cfg.arachne_max_width_ratio;
                    extract_arachne_medial_paths(gap_zone, min_w, max_w, sp_gap, res.gap_fills);
                }
            }
        }

        last_valid = next;
        cur = std::move(next);
    }

    // Inner region remaining for Top/Bottom skins + Sparse Infill
    if (!cfg.spiral_vase && !last_valid.empty()) {
        double last_half = (walls == 1) ? (w_outer * 0.5) : (w_inner * 0.5);
        res.inner_region = offset_paths_mm(last_valid, -last_half);
        // Also catch tiny core gaps inside innermost wall (when infill wouldn't fit)
        if (cfg.arachne_thin_walls && !res.inner_region.empty()) {
            Paths64 test_infill_fit = offset_paths_mm(res.inner_region, -w_inner * 0.35);
            if (test_infill_fit.empty()) {
                double min_w = cfg.nozzle_diameter * cfg.arachne_min_width_ratio;
                double max_w = cfg.nozzle_diameter * cfg.arachne_max_width_ratio;
                extract_arachne_medial_paths(res.inner_region, min_w, max_w, sp_gap, res.gap_fills);
            }
        }
    }

    return res;
}

std::vector<ExtrusionPath> generate_adhesion_paths(const Paths64& layer0_contours,
                                                   const Paths64& layer0_supports,
                                                   const SlicerConfig& cfg) {
    std::vector<ExtrusionPath> out;
    if (cfg.adhesion == AdhesionType::None) return out;

    Paths64 footprint = clip_union(layer0_contours, layer0_supports);
    if (footprint.empty()) return out;

    // Keep only outer boundaries (ignore internal holes for skirt; brim can hug outer)
    Paths64 outers;
    for (const auto& p : footprint) {
        if (Clipper2Lib::Area(p) > 0.0) outers.push_back(p);
    }
    if (outers.empty()) outers = footprint;

    const float w = cfg.first_layer_width;
    const float sp = cfg.speed_first_layer;
    const double spacing = pill_spacing_mm(w, cfg.first_layer_height);

    if (cfg.adhesion == AdhesionType::Skirt) {
        if (cfg.skirt_loops <= 0) return out;
        // Inflate convex-like outer footprint by skirt_distance, then add concentric loops outward
        for (int loop = 0; loop < cfg.skirt_loops; ++loop) {
            double dist = cfg.skirt_distance + w * 0.5 + loop * spacing;
            Paths64 rings = offset_paths_mm(outers, dist, Clipper2Lib::JoinType::Round, 2.0, 0.15);
            for (const auto& r : rings) {
                if (r.size() < 3 || Clipper2Lib::Area(r) <= 0.0) continue;
                ExtrusionPath ep;
                ep.type = FeatureType::SkirtBrim;
                ep.closed = true;
                ep.nominal_width_mm = w;
                ep.nominal_speed_mm_s = sp;
                ep.pts.reserve(r.size());
                for (const auto& pt : r) ep.pts.push_back({pt.x, pt.y, w, sp, 1.0f, 0});
                out.push_back(std::move(ep));
            }
        }
    } else if (cfg.adhesion == AdhesionType::Brim) {
        int loops = std::max(1, static_cast<int>(std::ceil(cfg.brim_width / spacing)));
        // Print brim from outermost inward or innermost outward (outermost first primes nozzle cleanly!)
        for (int loop = loops - 1; loop >= 0; --loop) {
            double dist = cfg.brim_gap + w * 0.5 + loop * spacing;
            Paths64 rings = offset_paths_mm(outers, dist, Clipper2Lib::JoinType::Round, 2.0, 0.08);
            for (const auto& r : rings) {
                if (r.size() < 3 || Clipper2Lib::Area(r) <= 0.0) continue;
                ExtrusionPath ep;
                ep.type = FeatureType::SkirtBrim;
                ep.closed = true;
                ep.nominal_width_mm = w;
                ep.nominal_speed_mm_s = sp;
                ep.pts.reserve(r.size());
                for (const auto& pt : r) ep.pts.push_back({pt.x, pt.y, w, sp, 1.0f, 0});
                out.push_back(std::move(ep));
            }
        }
    }
    return out;
}

}  // namespace katman
