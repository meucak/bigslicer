#include "surfaces.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace katman {

namespace {

// Find optimal bridge angle (0..165 deg in 15 deg steps) that minimizes unsupported bridge span
// across bridge_skin anchored to prev_contours (PrusaSlicer/Cura bridge direction optimizer).
float compute_best_bridge_angle(const Paths64& bridge_skin, const Paths64& prev_contours) {
    if (bridge_skin.empty()) return 0.0f;
    Rect64 bb = compute_bounds(bridge_skin);
    double best_score = std::numeric_limits<double>::max();
    float best_deg = 0.0f;

    // Sample candidate directions and evaluate projected span width of bridge polygon
    constexpr double DEG2RAD = 3.14159265358979323846 / 180.0;
    for (int deg = 0; deg < 180; deg += 15) {
        double rad = deg * DEG2RAD;
        double cs = std::cos(rad), sn = std::sin(rad);
        double min_u = std::numeric_limits<double>::max();
        double max_u = std::numeric_limits<double>::lowest();
        int anchored = 0;
        for (const auto& poly : bridge_skin) {
            for (const auto& p : poly) {
                double u = static_cast<double>(p.x) * cs + static_cast<double>(p.y) * sn;
                if (u < min_u) min_u = u;
                if (u > max_u) max_u = u;
            }
        }
        // Check if endpoints along this direction touch prev_contours
        if (!prev_contours.empty()) {
            double cx = 0.5 * (bb.left + bb.right);
            double cy = 0.5 * (bb.top + bb.bottom);
            double half_span = 0.5 * (max_u - min_u);
            Pt64 p1(static_cast<int64_t>(cx + cs * (half_span + mm_to_coord(0.5))),
                    static_cast<int64_t>(cy + sn * (half_span + mm_to_coord(0.5))));
            Pt64 p2(static_cast<int64_t>(cx - cs * (half_span + mm_to_coord(0.5))),
                    static_cast<int64_t>(cy - sn * (half_span + mm_to_coord(0.5))));
            if (point_in_paths(p1, prev_contours)) ++anchored;
            if (point_in_paths(p2, prev_contours)) ++anchored;
        }
        // Prefer shorter span AND both ends anchored!
        double span_mm = (max_u - min_u) * INV_COORD_SCALE;
        double score = span_mm - anchored * 5.0;
        if (score < best_score) {
            best_score = score;
            best_deg = static_cast<float>(deg);
        }
    }
    return best_deg;
}

}  // namespace

std::vector<LayerSurfaces> analyze_layer_surfaces(const std::vector<SlicedLayer>& layers,
                                                  const std::vector<LayerPerimeters>& perims,
                                                  const SlicerConfig& cfg,
                                                  ThreadPool* pool) {
    const size_t L = layers.size();
    std::vector<LayerSurfaces> out(L);
    if (L == 0 || cfg.spiral_vase) return out;

    const double overlap_infill_mm = cfg.infill_width * (cfg.infill_overlap_pct * 0.01);
    const double overlap_skin_mm = cfg.top_solid_width * (cfg.skin_overlap_pct * 0.01);

    // Identify pure identical middle layers (no top/bottom skin change!) so sparse infill overlap
    // offset is computed ONCE and reused across vertical stacks (20x speedup on prismatic CAD!)
    std::vector<int> pure_sparse_cache(L, -1);
    if (cfg.enable_layer_cache && cfg.infill_density < 99.5f && L >= 4) {
        const size_t t_win = static_cast<size_t>(std::max(0, cfg.top_layers));
        const size_t b_win = static_cast<size_t>(std::max(0, cfg.bottom_layers));
        for (size_t i = 0; i < L; ++i) {
            if (i < b_win || i + t_win >= L) continue;
            uint64_t h = layers[i].geom_hash;
            if (h == 0) continue;
            bool flat_window = true;
            for (size_t k = 1; k <= t_win && flat_window; ++k) {
                if (layers[i + k].geom_hash != h) flat_window = false;
            }
            for (size_t k = 1; k <= b_win && flat_window; ++k) {
                if (layers[i - k].geom_hash != h) flat_window = false;
            }
            if (flat_window && i > 0 && layers[i - 1].geom_hash == h &&
                std::fabs(layers[i].height - layers[i - 1].height) < 1e-4f) {
                if (pure_sparse_cache[i - 1] >= 0) pure_sparse_cache[i] = pure_sparse_cache[i - 1];
                else if (i > b_win && i + t_win < L) pure_sparse_cache[i] = static_cast<int>(i - 1);
            }
        }
    }

    auto process_layer = [&](uint32_t, size_t i) {
        if (pure_sparse_cache[i] >= 0) return;  // will copy directly from cache source!
        const Paths64& inner = perims[i].inner_region;
        if (inner.empty()) return;

        // Fast path: 100% infill -> everything is solid!
        if (cfg.infill_density >= 99.5f) {
            Paths64 expanded = (overlap_skin_mm > 1e-4) ? offset_paths_mm(inner, overlap_skin_mm) : inner;
            if (i + 1 >= L || (cfg.top_layers > 0 && static_cast<int>(L - 1 - i) < cfg.top_layers)) {
                out[i].top_skin = std::move(expanded);
            } else {
                out[i].bottom_skin = std::move(expanded);
            }
            if (cfg.ironing) {
                Paths64 exposed = (i + 1 < L) ? clip_difference(inner, layers[i + 1].contours) : inner;
                out[i].ironing_area = std::move(exposed);
            }
            return;
        }

        // 1. Top skin detection: region exposed to air within [i+1 .. i+top_layers]
        Paths64 top_mask;
        if (cfg.top_layers > 0) {
            if (i + static_cast<size_t>(cfg.top_layers) >= L) {
                top_mask = inner;  // Topmost N layers are always full top skin
            } else {
                bool all_same = true;
                for (int k = 1; k <= cfg.top_layers; ++k) {
                    if (layers[i + static_cast<size_t>(k)].geom_hash != layers[i].geom_hash) {
                        all_same = false;
                        break;
                    }
                }
                if (!all_same) {
                    Paths64 covered = inner;
                    for (int k = 1; k <= cfg.top_layers && !covered.empty(); ++k) {
                        covered = clip_intersection(covered, layers[i + static_cast<size_t>(k)].contours);
                    }
                    top_mask = clip_difference(inner, covered);
                }
            }
        }

        // 2. Ironing area: strictly top exposed surface (inner \ layers[i+1])
        if (cfg.ironing && !top_mask.empty()) {
            if (i + 1 >= L) {
                out[i].ironing_area = inner;
            } else if (layers[i + 1].geom_hash != layers[i].geom_hash) {
                out[i].ironing_area = clip_difference(inner, layers[i + 1].contours);
            }
        }

        // 3. Bottom skin & Bridge detection: region exposed below within [i-bottom_layers .. i-1]
        Paths64 bottom_mask;
        Paths64 bridge_mask;
        if (cfg.bottom_layers > 0) {
            if (i == 0) {
                bottom_mask = inner;
            } else if (i < static_cast<size_t>(cfg.bottom_layers)) {
                bottom_mask = inner;
                if (cfg.detect_bridges && layers[i - 1].geom_hash != layers[i].geom_hash) {
                    bridge_mask = clip_difference(inner, layers[i - 1].contours);
                    if (!bridge_mask.empty()) {
                        bottom_mask = clip_difference(bottom_mask, bridge_mask);
                    }
                }
            } else {
                bool all_same = true;
                for (int k = 1; k <= cfg.bottom_layers; ++k) {
                    if (layers[i - static_cast<size_t>(k)].geom_hash != layers[i].geom_hash) {
                        all_same = false;
                        break;
                    }
                }
                if (!all_same) {
                    Paths64 supported = inner;
                    for (int k = 1; k <= cfg.bottom_layers && !supported.empty(); ++k) {
                        supported = clip_intersection(supported, layers[i - static_cast<size_t>(k)].contours);
                    }
                    bottom_mask = clip_difference(inner, supported);
                    if (cfg.detect_bridges && !bottom_mask.empty()) {
                        bridge_mask = clip_difference(bottom_mask, layers[i - 1].contours);
                        if (!bridge_mask.empty()) {
                            bottom_mask = clip_difference(bottom_mask, bridge_mask);
                        }
                    }
                }
            }
        }

        // Fast-path: pure interior sparse infill layer (no top/bottom/bridge skins at all!)
        // Reuses precomputed outer_wall_polys as safe clamp -> saves 2-4 Clipper2 operations per layer!
        if (top_mask.empty() && bottom_mask.empty() && bridge_mask.empty()) {
            if (overlap_infill_mm > 1e-4) {
                Paths64 exp = offset_paths_mm(inner, overlap_infill_mm);
                out[i].sparse_infill = perims[i].outer_wall_polys.empty()
                    ? std::move(exp)
                    : clip_intersection(exp, perims[i].outer_wall_polys);
            } else {
                out[i].sparse_infill = inner;
            }
            return;
        }

        // Ensure mutually exclusive masks: priority Bridge > Top > Bottom > Sparse Infill
        if (!bridge_mask.empty()) {
            top_mask = clip_difference(top_mask, bridge_mask);
            bottom_mask = clip_difference(bottom_mask, bridge_mask);
        }
        if (!top_mask.empty() && !bottom_mask.empty()) {
            bottom_mask = clip_difference(bottom_mask, top_mask);
        }

        // Anchor skins slightly into surrounding sparse infill for clean overlap
        Paths64 solid_union = clip_union(clip_union(top_mask, bottom_mask), bridge_mask);
        Paths64 sparse = clip_difference(inner, solid_union);

        // Reuse precomputed outer_wall_polys (already inset by -outer_wall_width/2!) instead of re-offsetting!
        const Paths64& safe_bounds = perims[i].outer_wall_polys;
        auto expand_clamped = [&](const Paths64& p, double delta) -> Paths64 {
            if (p.empty() || delta <= 1e-4) return p;
            Paths64 exp = offset_paths_mm(p, delta);
            return safe_bounds.empty() ? exp : clip_intersection(exp, safe_bounds);
        };

        out[i].top_skin = expand_clamped(top_mask, overlap_skin_mm);
        out[i].bottom_skin = expand_clamped(bottom_mask, overlap_skin_mm);
        out[i].bridge_skin = expand_clamped(bridge_mask, overlap_skin_mm * 1.5);
        out[i].sparse_infill = expand_clamped(sparse, overlap_infill_mm);

        if (!out[i].bridge_skin.empty() && i > 0) {
            out[i].bridge_angle_deg = compute_best_bridge_angle(out[i].bridge_skin, layers[i - 1].contours);
        }
    };

    if (pool && L >= 4) {
        pool->parallel_for(L, process_layer, 1);
    } else {
        for (size_t i = 0; i < L; ++i) process_layer(0, i);
    }

    // Copy cached pure-sparse middle layers in O(1)
    for (size_t i = 0; i < L; ++i) {
        if (pure_sparse_cache[i] >= 0) {
            out[i] = out[static_cast<size_t>(pure_sparse_cache[i])];
        }
    }
    return out;
}

}  // namespace katman
