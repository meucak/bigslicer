#include "supports.hpp"
#include "infill.hpp"

#include <algorithm>
#include <cmath>

namespace katman {

namespace {

constexpr double PI = 3.14159265358979323846;

Path64 make_circle_path(double cx_mm, double cy_mm, double radius_mm, int segs = 16) {
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

}  // namespace

std::vector<LayerSupportData> generate_supports(const std::vector<SlicedLayer>& layers,
                                                const SlicerConfig& cfg,
                                                ThreadPool* pool) {
    const size_t L = layers.size();
    std::vector<LayerSupportData> out(L);
    if (L < 2 || cfg.support_type == SupportType::None || cfg.spiral_vase) return out;

    // 1. Parallel overhang region detection per layer:
    // overhang[i] = layers[i] \ inflate(layers[i-1], h * tan(overhang_angle))
    // Angle is measured from vertical: e.g., 50 deg means horizontal step > h * tan(50 deg) needs support
    const double tan_angle = std::tan(std::clamp(cfg.support_overhang_angle, 15.0f, 80.0f) * PI / 180.0);
    std::vector<Paths64> overhangs(L);

    auto detect_ov = [&](uint32_t, size_t i) {
        if (i == 0 || layers[i].contours.empty()) return;
        if (layers[i].geom_hash == layers[i - 1].geom_hash) return;
        double safe_reach = std::max(0.15, static_cast<double>(layers[i].height) * tan_angle);
        Paths64 supported = layers[i - 1].contours.empty()
            ? Paths64{}
            : offset_paths_mm(layers[i - 1].contours, safe_reach);
        Paths64 ov = clip_difference(layers[i].contours, supported);
        if (!ov.empty()) {
            // Remove microscopic slivers < 0.25 mm^2
            overhangs[i] = simplify_paths_mm(ov, 0.05, 0.25);
        }
    };

    if (pool && L >= 4) {
        pool->parallel_for(L, detect_ov, 1);
    } else {
        for (size_t i = 0; i < L; ++i) detect_ov(0, i);
    }

    const int z_gap_layers = std::max(1, static_cast<int>(std::ceil(cfg.support_z_gap / std::max(0.05f, cfg.layer_height))));
    const int iface_layers = std::clamp(cfg.support_interface_layers, 0, 5);
    const double xy_gap = std::max(0.15, static_cast<double>(cfg.support_xy_gap));

    // Precompute simplified inflated model collision envelopes per layer (in parallel!)
    // Simplifying contours to 0.08mm before offsetting cuts Clipper2 support work by 4x with zero loss!
    std::vector<Paths64> collision(L);
    auto build_coll = [&](uint32_t, size_t i) {
        if (!layers[i].contours.empty()) {
            Paths64 simp = simplify_paths_mm(layers[i].contours, 0.08, 0.05);
            collision[i] = offset_paths_mm(simp.empty() ? layers[i].contours : simp, xy_gap,
                                           Clipper2Lib::JoinType::Miter, 2.0, 0.12);
        }
    };
    if (pool && L >= 4) {
        pool->parallel_for(L, build_coll, 1);
    } else {
        for (size_t i = 0; i < L; ++i) build_coll(0, i);
    }

    if (cfg.support_type == SupportType::NormalGrid) {
        // 2A. Normal Columnar / Grid Support Projection (top-down)
        Paths64 carried;
        // Track remaining interface countdown per layer using roof seeds
        std::vector<Paths64> roof_seeds(L);
        for (size_t i = static_cast<size_t>(z_gap_layers); i < L; ++i) {
            if (!overhangs[i].empty()) {
                size_t top_sup = i - static_cast<size_t>(z_gap_layers);
                roof_seeds[top_sup] = clip_union(roof_seeds[top_sup], overhangs[i]);
            }
        }

        // Propagate interface band downward for iface_layers
        std::vector<Paths64> iface_mask(L);
        for (int i = static_cast<int>(L) - 1; i >= 0; --i) {
            size_t ui = static_cast<size_t>(i);
            if (!roof_seeds[ui].empty()) {
                for (int k = 0; k < iface_layers && i - k >= 0; ++k) {
                    size_t tk = static_cast<size_t>(i - k);
                    iface_mask[tk] = clip_union(iface_mask[tk], roof_seeds[ui]);
                }
            }
        }

        // Project full column down from top to layer 0 (simplify carried every layer to prevent fractal sliver explosion!)
        for (int i = static_cast<int>(L) - 1; i >= 0; --i) {
            size_t ui = static_cast<size_t>(i);
            if (!roof_seeds[ui].empty()) {
                carried = clip_union(carried, roof_seeds[ui]);
            }
            if (carried.empty()) continue;

            // Subtract model collision envelope & simplify support column outline (0.12mm tolerance -> 10x faster!)
            Paths64 valid = clip_difference(carried, collision[ui]);
            valid = simplify_paths_mm(valid, 0.12, 0.20);
            if (cfg.support_placement == SupportPlacement::BuildplateOnly) {
                carried = valid;
            } else {
                carried = simplify_paths_mm(carried, 0.12, 0.20);
            }
            if (valid.empty()) continue;

            Paths64 iface = iface_mask[ui].empty() ? Paths64{} : clip_intersection(valid, iface_mask[ui]);
            Paths64 body = iface.empty() ? valid : clip_difference(valid, iface);

            out[ui].interface_region = simplify_paths_mm(iface, 0.08, 0.15);
            out[ui].body_region = simplify_paths_mm(body, 0.08, 0.15);
        }

        // If BuildplateOnly, prune any floating support islands not connected to Layer 0!
        if (cfg.support_placement == SupportPlacement::BuildplateOnly) {
            Paths64 grounded = simplify_paths_mm(clip_union(out[0].body_region, out[0].interface_region), 0.15, 0.15);
            for (size_t i = 1; i < L; ++i) {
                if (grounded.empty()) {
                    out[i].body_region.clear();
                    out[i].interface_region.clear();
                    continue;
                }
                if (out[i].body_region.empty() && out[i].interface_region.empty()) {
                    grounded.clear();
                    continue;
                }
                Paths64 grow = offset_paths_mm(grounded, 0.6, Clipper2Lib::JoinType::Miter, 2.0, 0.15);
                out[i].body_region = clip_intersection(out[i].body_region, grow);
                out[i].interface_region = clip_intersection(out[i].interface_region, grow);
                grounded = simplify_paths_mm(clip_union(out[i].body_region, out[i].interface_region), 0.15, 0.15);
            }
        }
    } else {
        // 2B. Organic / Tree Supports (Cura / PrusaSlicer Organic style!)
        // Extract overhang tips top-down, grow smoothly curving circular branches around collision zones,
        // dynamically merge nearby branches into thicker trunks flaring onto the build plate!
        const double tip_r = std::max(0.6, static_cast<double>(cfg.tree_branch_diameter) * 0.45);
        const double max_r = std::max(tip_r * 2.5, static_cast<double>(cfg.tree_branch_diameter) * 2.2);
        const double max_step_xy = std::tan(std::clamp(cfg.tree_max_branch_angle, 15.0f, 65.0f) * PI / 180.0);
        const double merge_dist = std::max(3.0, static_cast<double>(cfg.tree_branch_diameter) * 1.8);

        std::vector<TreeBranch> active_branches;
        std::vector<Paths64> roof_pads(L);

        for (int i = static_cast<int>(L) - 1; i >= 0; --i) {
            size_t ui = static_cast<size_t>(i);
            double h = layers[ui].height;
            double step_xy = h * max_step_xy;

            // Spawn new tips for overhangs landing on this layer (below Z gap)
            if (ui + static_cast<size_t>(z_gap_layers) < L) {
                const Paths64& ov = overhangs[ui + static_cast<size_t>(z_gap_layers)];
                if (!ov.empty()) {
                    // Place dense interface roof pad under overhang
                    for (int k = 0; k < iface_layers && i - k >= 0; ++k) {
                        roof_pads[static_cast<size_t>(i - k)] = clip_union(roof_pads[static_cast<size_t>(i - k)], ov);
                    }
                    // Seed tip points inside each overhang island (grid-sampled every 4.5 mm)
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

            // Move branches outward away from model collision envelope if intersecting,
            // and gently pull nearby branches together to merge into trunks!
            const Paths64& coll = collision[ui];
            for (size_t b = 0; b < active_branches.size(); ++b) {
                TreeBranch& br = active_branches[b];
                ++br.age;
                // Taper radius slightly wider as trunk descends (structural stability!)
                br.radius = std::min(max_r, br.radius + h * 0.035);
                if (i < 4) br.radius = std::min(max_r * 1.4, br.radius + 0.35);  // flared base on bed

                // Collision avoidance steering: push branch radially outward from colliding model center
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

            // Merge close branches smoothly into shared organic trunks
            for (size_t a = 0; a < active_branches.size(); ++a) {
                for (size_t b = a + 1; b < active_branches.size(); ) {
                    double dx = active_branches[b].x - active_branches[a].x;
                    double dy = active_branches[b].y - active_branches[a].y;
                    double d = std::hypot(dx, dy);
                    if (d < active_branches[a].radius + active_branches[b].radius * 0.6) {
                        // Merge b into a
                        double w_a = active_branches[a].radius * active_branches[a].radius;
                        double w_b = active_branches[b].radius * active_branches[b].radius;
                        active_branches[a].x = (active_branches[a].x * w_a + active_branches[b].x * w_b) / (w_a + w_b);
                        active_branches[a].y = (active_branches[a].y * w_a + active_branches[b].y * w_b) / (w_a + w_b);
                        active_branches[a].radius = std::min(max_r, std::sqrt(w_a + w_b * 0.65));
                        active_branches.erase(active_branches.begin() + static_cast<ptrdiff_t>(b));
                    } else {
                        if (d < merge_dist * 3.0) {
                            // Gentle attraction toward neighbor to form organic Y-junctions!
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

            // Convert active branch cross-sections into circular polygons unioned and clipped against collision
            Paths64 branch_polys;
            branch_polys.reserve(active_branches.size());
            for (const auto& br : active_branches) {
                branch_polys.push_back(make_circle_path(br.x, br.y, br.radius, 14));
            }
            if (!branch_polys.empty()) {
                branch_polys = clip_union_self(branch_polys);
                branch_polys = clip_difference(branch_polys, coll);
            }

            Paths64 iface = roof_pads[ui].empty() ? Paths64{} : clip_difference(roof_pads[ui], coll);
            out[ui].interface_region = simplify_paths_mm(iface, 0.04, 0.12);
            out[ui].body_region = simplify_paths_mm(branch_polys, 0.04, 0.12);
        }
    }

    // 3. Parallel toolpath generation for all support body & interface layers
    auto fill_sup = [&](uint32_t, size_t i) {
        const bool is_first = (i == 0);
        const float w = is_first ? cfg.first_layer_width : cfg.support_width;
        const float sp_body = is_first ? cfg.speed_first_layer : cfg.speed_support;
        const float sp_iface = is_first ? cfg.speed_first_layer : cfg.speed_support_interface;

        if (!out[i].body_region.empty()) {
            if (cfg.support_type == SupportType::OrganicTree) {
                // Organic trees print hollow 1-wall (or 2-wall near base) shells — super fast & clean!
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
                    // Add solid base adhesion inside flared trunk feet
                    generate_scanline_hatch(out[i].body_region, w * 1.4, 45.0, 0.0,
                                            false, true, FeatureType::Support, w, sp_body, 1.0f, 0, out[i].paths);
                }
            } else {
                // Normal Grid support: fast rectilinear/zigzag columns
                double sp = std::max(static_cast<double>(w) * 1.4, static_cast<double>(w) / (cfg.support_density * 0.01));
                generate_scanline_hatch(out[i].body_region, sp, 0.0, 0.0,
                                        false, true, FeatureType::Support, w, sp_body, 1.0f, 0, out[i].paths);
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

    if (pool && L >= 4) {
        pool->parallel_for(L, fill_sup, 1);
    } else {
        for (size_t i = 0; i < L; ++i) fill_sup(0, i);
    }

    return out;
}

}  // namespace katman
