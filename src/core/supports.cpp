#include "supports.hpp"
#include "infill.hpp"

#include <algorithm>
#include <cmath>

namespace katman {

namespace {

constexpr double PI = 3.14159265358979323846;

Path64 make_circle_path(double cx_mm, double cy_mm, double radius_mm, int segs = 14) {
    Path64 p;
    p.reserve(static_cast<size_t>(segs));
    for (int i = 0; i < segs; ++i) {
        double a = (2.0 * PI * i) / segs;
        p.emplace_back(mm_to_coord(cx_mm + radius_mm * std::cos(a)),
                       mm_to_coord(cy_mm + radius_mm * std::sin(a)));
    }
    return p;
}

struct TreeBranch {
    double x = 0.0;
    double y = 0.0;
    double radius = 1.2;
    int age = 0;
};

inline bool rects_overlap(const Rect64& a, const Rect64& b) {
    return !(a.right < b.left || b.right < a.left || a.bottom < b.top || b.bottom < a.top);
}

}  // namespace

std::vector<LayerSupportData> generate_supports(const std::vector<SlicedLayer>& layers,
                                                const SlicerConfig& cfg,
                                                ThreadPool* pool) {
    const size_t L = layers.size();
    std::vector<LayerSupportData> out(L);
    if (L < 2 || cfg.support_type == SupportType::None || cfg.spiral_vase) return out;

    // 1. Parallel overhang region detection per layer:
    // overhang[i] = layers[i] \ inflate(layers[i-1], h * tan(overhang_angle))
    const double tan_angle = std::tan(std::clamp(cfg.support_overhang_angle, 15.0f, 80.0f) * PI / 180.0);
    std::vector<Paths64> overhangs(L);

    auto detect_ov = [&](uint32_t, size_t i) {
        if (i == 0 || layers[i].contours.empty()) return;
        if (layers[i].geom_hash != 0 && layers[i].geom_hash == layers[i - 1].geom_hash) return;
        double safe_reach = std::max(0.15, static_cast<double>(layers[i].height) * tan_angle);
        // Coarse pre-simplification (0.12mm) cuts Clipper2 support overhang work by ~3x with zero print loss
        Paths64 prev_s = layers[i - 1].contours.empty()
            ? Paths64{}
            : simplify_paths_mm(layers[i - 1].contours, 0.12, 0.08);
        Paths64 supported = prev_s.empty()
            ? Paths64{}
            : offset_paths_mm(prev_s, safe_reach, Clipper2Lib::JoinType::Miter, 2.0, 0.15);
        Paths64 cur_s = simplify_paths_mm(layers[i].contours, 0.12, 0.08);
        Paths64 ov = clip_difference(cur_s.empty() ? layers[i].contours : cur_s, supported);
        if (!ov.empty()) {
            overhangs[i] = simplify_paths_mm(ov, 0.15, 0.25);
        }
    };

    if (pool && L >= 4) {
        pool->parallel_for(L, detect_ov, 1);
    } else {
        for (size_t i = 0; i < L; ++i) detect_ov(0, i);
    }

    // Find highest layer with any overhang — skip all layers above it!
    int max_ov_layer = -1;
    for (int i = static_cast<int>(L) - 1; i >= 1; --i) {
        if (!overhangs[static_cast<size_t>(i)].empty()) {
            max_ov_layer = i;
            break;
        }
    }
    if (max_ov_layer < 1) return out;

    const int z_gap_layers = std::max(1, static_cast<int>(std::ceil(cfg.support_z_gap / std::max(0.05f, cfg.layer_height))));
    const int iface_layers = std::clamp(cfg.support_interface_layers, 0, 5);
    const double xy_gap = std::max(0.15, static_cast<double>(cfg.support_xy_gap));
    const size_t active_L = static_cast<size_t>(std::min<int>(static_cast<int>(L), max_ov_layer + 1));

    // Precompute simplified inflated model collision envelopes in parallel ONLY up to max_ov_layer!
    std::vector<Paths64> collision(active_L);
    std::vector<Rect64> coll_bounds(active_L);
    auto build_coll = [&](uint32_t, size_t i) {
        if (!layers[i].contours.empty()) {
            Paths64 simp = simplify_paths_mm(layers[i].contours, 0.18, 0.10);
            collision[i] = offset_paths_mm(simp.empty() ? layers[i].contours : simp, xy_gap,
                                           Clipper2Lib::JoinType::Miter, 2.0, 0.18);
            coll_bounds[i] = compute_bounds(collision[i]);
        }
    };
    if (pool && active_L >= 4) {
        pool->parallel_for(active_L, build_coll, 1);
    } else {
        for (size_t i = 0; i < active_L; ++i) build_coll(0, i);
    }

    if (cfg.support_type == SupportType::NormalGrid || cfg.support_type == SupportType::NormalSnug) {
        const bool is_snug = (cfg.support_type == SupportType::NormalSnug);
        // 2A. Ultra-Fast Normal Grid / Normal Snug Columnar Support Projection
        std::vector<Paths64> roof_seeds(active_L);
        for (size_t i = static_cast<size_t>(z_gap_layers); i < active_L; ++i) {
            if (!overhangs[i].empty()) {
                size_t top_sup = i - static_cast<size_t>(z_gap_layers);
                Paths64 seed = is_snug
                    ? simplify_paths_mm(overhangs[i], 0.25, 0.35)
                    : offset_paths_mm(overhangs[i], 0.4, Clipper2Lib::JoinType::Miter, 2.0, 0.22);
                if (seed.empty()) continue;
                roof_seeds[top_sup] = roof_seeds[top_sup].empty()
                    ? std::move(seed)
                    : simplify_paths_mm(clip_union(roof_seeds[top_sup], seed), 0.25, 0.30);
            }
        }

        // Fast interface layer propagation (only merge when roof_seeds exist!)
        std::vector<Paths64> iface_mask(active_L);
        for (int i = static_cast<int>(active_L) - 1; i >= 0; --i) {
            size_t ui = static_cast<size_t>(i);
            if (!roof_seeds[ui].empty()) {
                for (int k = 0; k < iface_layers && i - k >= 0; ++k) {
                    size_t tk = static_cast<size_t>(i - k);
                    iface_mask[tk] = iface_mask[tk].empty()
                        ? roof_seeds[ui]
                        : simplify_paths_mm(clip_union(iface_mask[tk], roof_seeds[ui]), 0.20, 0.20);
                }
            }
        }

        // Top-down column projection with O(1) identical-layer skip & AABB early-out!
        Paths64 carried;
        Rect64 carried_bb{};
        uint64_t prev_coll_hash = 0;
        Paths64 prev_valid;
        bool prev_valid_ready = false;

        for (int i = static_cast<int>(active_L) - 1; i >= 0; --i) {
            size_t ui = static_cast<size_t>(i);
            bool seed_added = false;
            if (!roof_seeds[ui].empty()) {
                carried = carried.empty()
                    ? roof_seeds[ui]
                    : simplify_paths_mm(clip_union(carried, roof_seeds[ui]), 0.22, 0.25);
                carried_bb = compute_bounds(carried);
                seed_added = true;
                prev_valid_ready = false;
            }
            if (carried.empty()) continue;

            Paths64 valid;
            if (!seed_added && prev_valid_ready && layers[ui].geom_hash != 0 &&
                layers[ui].geom_hash == prev_coll_hash) {
                // Identical model cross-section and no new overhangs -> O(1) reuse!
                valid = prev_valid;
            } else if (collision[ui].empty() || !rects_overlap(carried_bb, coll_bounds[ui])) {
                valid = carried;
            } else {
                valid = simplify_paths_mm(clip_difference(carried, collision[ui]), 0.22, 0.25);
            }

            prev_coll_hash = layers[ui].geom_hash;
            prev_valid = valid;
            prev_valid_ready = true;

            if (cfg.support_placement == SupportPlacement::BuildplateOnly) {
                if (valid.size() != carried.size() || !seed_added) {
                    carried = valid;
                    carried_bb = compute_bounds(carried);
                }
            }
            if (valid.empty()) continue;

            if (iface_mask[ui].empty()) {
                out[ui].body_region = std::move(valid);
            } else {
                Paths64 iface = simplify_paths_mm(clip_intersection(valid, iface_mask[ui]), 0.16, 0.18);
                Paths64 body = iface.empty() ? std::move(valid) : simplify_paths_mm(clip_difference(valid, iface), 0.18, 0.20);
                out[ui].interface_region = std::move(iface);
                out[ui].body_region = std::move(body);
            }
        }

        // Fast BuildplateOnly floating-island pruning (no expensive per-layer offset_paths_mm!)
        if (cfg.support_placement == SupportPlacement::BuildplateOnly) {
            Paths64 grounded = clip_union(out[0].body_region, out[0].interface_region);
            Rect64 g_bb = compute_bounds(grounded);
            for (size_t i = 1; i < active_L; ++i) {
                if (grounded.empty()) {
                    out[i].body_region.clear();
                    out[i].interface_region.clear();
                    continue;
                }
                if (out[i].body_region.empty() && out[i].interface_region.empty()) {
                    grounded.clear();
                    continue;
                }
                // Only clip against grounded when model geometry actually changed (avoid redundant Clipper2!)
                if (layers[i].geom_hash == 0 || layers[i].geom_hash != layers[i - 1].geom_hash) {
                    if (!out[i].body_region.empty()) {
                        Rect64 b_bb = compute_bounds(out[i].body_region);
                        if (!rects_overlap(b_bb, g_bb)) out[i].body_region.clear();
                        else out[i].body_region = clip_intersection(out[i].body_region, grounded);
                    }
                    if (!out[i].interface_region.empty()) {
                        Rect64 i_bb = compute_bounds(out[i].interface_region);
                        if (!rects_overlap(i_bb, g_bb)) out[i].interface_region.clear();
                        else out[i].interface_region = clip_intersection(out[i].interface_region, grounded);
                    }
                    grounded = simplify_paths_mm(clip_union(out[i].body_region, out[i].interface_region), 0.22, 0.20);
                    g_bb = compute_bounds(grounded);
                }
            }
        }
    } else {
        // 2B. Organic / Tree Supports (Cura / PrusaSlicer / OrcaSlicer Organic style!)
        const double tip_r = std::max(0.65, static_cast<double>(cfg.tree_branch_diameter) * 0.45);
        const double max_r = std::max(tip_r * 2.5, static_cast<double>(cfg.tree_branch_diameter) * 2.3);
        const double max_step_xy = std::tan(std::clamp(cfg.tree_max_branch_angle, 15.0f, 65.0f) * PI / 180.0);
        const double merge_dist = std::max(3.0, static_cast<double>(cfg.tree_branch_diameter) * 1.85);

        std::vector<TreeBranch> active_branches;
        std::vector<Paths64> roof_pads(active_L);

        for (int i = static_cast<int>(active_L) - 1; i >= 0; --i) {
            size_t ui = static_cast<size_t>(i);
            double h = layers[ui].height;
            double step_xy = h * max_step_xy;

            // Spawn new tips for overhangs landing on this layer (below Z gap)
            if (ui + static_cast<size_t>(z_gap_layers) < active_L) {
                const Paths64& ov = overhangs[ui + static_cast<size_t>(z_gap_layers)];
                if (!ov.empty()) {
                    for (int k = 0; k < iface_layers && i - k >= 0; ++k) {
                        size_t tk = static_cast<size_t>(i - k);
                        roof_pads[tk] = roof_pads[tk].empty() ? ov : clip_union(roof_pads[tk], ov);
                    }
                    auto islands = extract_islands(ov);
                    for (const auto& isl : islands) {
                        double lx = coord_to_mm(isl.bounds.left), rx = coord_to_mm(isl.bounds.right);
                        double ty = coord_to_mm(isl.bounds.top), by = coord_to_mm(isl.bounds.bottom);
                        double pitch = std::max(3.5, static_cast<double>(cfg.tree_branch_diameter) * 1.8);
                        bool seeded = false;
                        for (double x = lx + pitch * 0.4; x <= rx; x += pitch) {
                            for (double y = ty + pitch * 0.4; y <= by; y += pitch) {
                                Pt64 pt(mm_to_coord(x), mm_to_coord(y));
                                if (point_in_paths(pt, ov)) {
                                    active_branches.push_back({x, y, tip_r, 0});
                                    seeded = true;
                                }
                            }
                        }
                        if (!seeded) {
                            active_branches.push_back({0.5 * (lx + rx), 0.5 * (ty + by), tip_r, 0});
                        }
                    }
                }
            }

            if (active_branches.empty() && roof_pads[ui].empty()) continue;

            const Paths64& coll = collision[ui];
            for (size_t b = 0; b < active_branches.size(); ++b) {
                TreeBranch& br = active_branches[b];
                ++br.age;
                br.radius = std::min(max_r, br.radius + h * 0.038);
                if (i < 4) br.radius = std::min(max_r * 1.45, br.radius + 0.40);  // wide flared base

                Pt64 bp(mm_to_coord(br.x), mm_to_coord(br.y));
                if (!coll.empty() && point_in_paths(bp, coll)) {
                    Rect64 mb = layers[ui].bounds;
                    double mcx = 0.5 * coord_to_mm(mb.left + mb.right);
                    double mcy = 0.5 * coord_to_mm(mb.top + mb.bottom);
                    double dx = br.x - mcx, dy = br.y - mcy;
                    double len = std::hypot(dx, dy);
                    if (len < 1e-4) { dx = 1.0; dy = 0.0; len = 1.0; }
                    br.x += (dx / len) * step_xy * 1.25;
                    br.y += (dy / len) * step_xy * 1.25;
                }
            }

            for (size_t a = 0; a < active_branches.size(); ++a) {
                for (size_t b = a + 1; b < active_branches.size(); ) {
                    double dx = active_branches[b].x - active_branches[a].x;
                    double dy = active_branches[b].y - active_branches[a].y;
                    double d = std::hypot(dx, dy);
                    if (d < active_branches[a].radius + active_branches[b].radius * 0.6) {
                        double w_a = active_branches[a].radius * active_branches[a].radius;
                        double w_b = active_branches[b].radius * active_branches[b].radius;
                        active_branches[a].x = (active_branches[a].x * w_a + active_branches[b].x * w_b) / (w_a + w_b);
                        active_branches[a].y = (active_branches[a].y * w_a + active_branches[b].y * w_b) / (w_a + w_b);
                        active_branches[a].radius = std::min(max_r, std::sqrt(w_a + w_b * 0.65));
                        active_branches.erase(active_branches.begin() + static_cast<ptrdiff_t>(b));
                    } else {
                        if (d < merge_dist * 3.0) {
                            double pull = std::min(step_xy * 0.45, (d - active_branches[a].radius) * 0.15);
                            double ux = dx / d, uy = dy / d;
                            active_branches[a].x += ux * pull;
                            active_branches[a].y += uy * pull;
                            active_branches[b].x -= ux * pull;
                            active_branches[b].y -= uy * pull;
                        }
                        ++b;
                    }
                }
            }

            Paths64 branch_polys;
            branch_polys.reserve(active_branches.size());
            for (const auto& br : active_branches) {
                branch_polys.push_back(make_circle_path(br.x, br.y, br.radius, 14));
            }
            if (!branch_polys.empty()) {
                branch_polys = clip_union_self(branch_polys);
                if (!coll.empty()) branch_polys = clip_difference(branch_polys, coll);
            }

            Paths64 iface = roof_pads[ui].empty() ? Paths64{} : clip_difference(roof_pads[ui], coll);
            out[ui].interface_region = simplify_paths_mm(iface, 0.06, 0.12);
            out[ui].body_region = simplify_paths_mm(branch_polys, 0.06, 0.12);
        }
    }

    // 3. Parallel toolpath generation for all active support layers
    auto fill_sup = [&](uint32_t, size_t i) {
        const bool is_first = (i == 0);
        const float w = is_first ? cfg.first_layer_width : cfg.support_width;
        const float sp_body = is_first ? cfg.speed_first_layer : cfg.speed_support;
        const float sp_iface = is_first ? cfg.speed_first_layer : cfg.speed_support_interface;

        if (!out[i].body_region.empty()) {
            if (cfg.support_type == SupportType::OrganicTree) {
                Paths64 shell = offset_paths_mm(out[i].body_region, -w * 0.5);
                for (const auto& p : shell) {
                    if (p.size() < 3) continue;
                    ExtrusionPath ep;
                    ep.type = FeatureType::Support;
                    ep.closed = true;
                    ep.nominal_width_mm = w;
                    ep.nominal_speed_mm_s = sp_body;
                    ep.pts.reserve(p.size());
                    for (const auto& pt : p) ep.pts.push_back({pt.x, pt.y, w, sp_body, 1.0f, 0});
                    out[i].paths.push_back(std::move(ep));
                }
                if (i < 3) {
                    generate_scanline_hatch(out[i].body_region, w * 1.4, 45.0, 0.0,
                                            false, true, FeatureType::Support, w, sp_body, 1.0f, 0, out[i].paths);
                }
            } else {
                // Normal Grid & Normal Snug: fast zigzag infill + outer support stabilizer wall on first layer
                double sp = std::max(static_cast<double>(w) * 1.4, static_cast<double>(w) / (cfg.support_density * 0.01));
                generate_scanline_hatch(out[i].body_region, sp, 0.0, 0.0,
                                        false, true, FeatureType::Support, w, sp_body, 1.0f, 0, out[i].paths);
            }

            // Optional Support Brim on Layer 0 for rock-solid bed adhesion of tree/grid feet!
            if (is_first && cfg.support_brim) {
                for (int ring = 1; ring <= 2; ++ring) {
                    Paths64 brim = offset_paths_mm(out[0].body_region, ring * w * 0.95, Clipper2Lib::JoinType::Round, 2.0, 0.15);
                    if (!collision[0].empty()) brim = clip_difference(brim, collision[0]);
                    for (const auto& p : brim) {
                        if (p.size() < 3 || Clipper2Lib::Area(p) <= 0.0) continue;
                        ExtrusionPath ep;
                        ep.type = FeatureType::Support;
                        ep.closed = true;
                        ep.nominal_width_mm = w;
                        ep.nominal_speed_mm_s = sp_body;
                        ep.pts.reserve(p.size());
                        for (const auto& pt : p) ep.pts.push_back({pt.x, pt.y, w, sp_body, 1.0f, 0});
                        out[0].paths.push_back(std::move(ep));
                    }
                }
            }
        }

        if (!out[i].interface_region.empty()) {
            double sp = std::max(static_cast<double>(w) * 1.05,
                                 static_cast<double>(w) / (std::clamp(cfg.support_interface_density, 30.0f, 100.0f) * 0.01));
            double ang = (i & 1u) ? 45.0 : -45.0;
            generate_scanline_hatch(out[i].interface_region, sp, ang, 0.0,
                                    false, true, FeatureType::SupportInterface, w, sp_iface, 1.0f, 0, out[i].paths);
        }
    };

    if (pool && active_L >= 4) {
        pool->parallel_for(active_L, fill_sup, 1);
    } else {
        for (size_t i = 0; i < active_L; ++i) fill_sup(0, i);
    }

    return out;
}

}  // namespace katman
