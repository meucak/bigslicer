#pragma once

#include "config.hpp"

#include "clipper2/clipper.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace katman {

using Pt64 = Clipper2Lib::Point64;
using Path64 = Clipper2Lib::Path64;
using Paths64 = Clipper2Lib::Paths64;
using Rect64 = Clipper2Lib::Rect64;

inline int64_t mm_to_coord(double mm) {
    return static_cast<int64_t>(std::llround(mm * COORD_SCALE));
}

inline double coord_to_mm(int64_t c) {
    return static_cast<double>(c) * INV_COORD_SCALE;
}

inline float coord_to_mm_f(int64_t c) {
    return static_cast<float>(static_cast<double>(c) * INV_COORD_SCALE);
}

// ExPolygon-like island with its direct holes (from Clipper2 PolyTree64)
struct Island {
    Path64 outer;
    Paths64 holes;
    Rect64 bounds;
    double area_mm2 = 0.0;
};

struct ExtrusionPoint {
    int64_t x = 0;
    int64_t y = 0;
    float width_mm = 0.42f;
    float speed_mm_s = 60.0f;
    float flow_mult = 1.0f;
    uint8_t flags = 0;  // bit0=overhang, bit1=bridge, bit2=scarf
};

struct ExtrusionPath {
    FeatureType type = FeatureType::InnerWall;
    bool closed = false;
    float nominal_width_mm = 0.42f;
    float nominal_speed_mm_s = 60.0f;
    std::vector<ExtrusionPoint> pts;
};

// Fast AABB computation & overlap test
Rect64 compute_bounds(const Paths64& paths);
Rect64 compute_bounds(const Path64& path);

inline bool bounds_overlap(const Rect64& a, const Rect64& b, int64_t margin = 0) {
    if (a.IsEmpty() || b.IsEmpty()) return false;
    return !(a.right + margin < b.left || b.right + margin < a.left ||
             a.bottom + margin < b.top || b.bottom + margin < a.top);
}

// Core Clipper2 boolean operations with AABB early-out and cleanup
Paths64 clip_union(const Paths64& a, const Paths64& b,
                   Clipper2Lib::FillRule rule = Clipper2Lib::FillRule::NonZero);
Paths64 clip_union_self(const Paths64& a,
                        Clipper2Lib::FillRule rule = Clipper2Lib::FillRule::NonZero);
Paths64 clip_difference(const Paths64& subject, const Paths64& clip,
                        Clipper2Lib::FillRule rule = Clipper2Lib::FillRule::NonZero);
Paths64 clip_intersection(const Paths64& a, const Paths64& b,
                          Clipper2Lib::FillRule rule = Clipper2Lib::FillRule::NonZero);

// Fast offset (inflate > 0, deflate < 0) in mm
Paths64 offset_paths_mm(const Paths64& paths, double delta_mm,
                        Clipper2Lib::JoinType jt = Clipper2Lib::JoinType::Miter,
                        double miter_limit = 2.5,
                        double arc_tol_mm = 0.015);

// Independent outer contour vs inner hole XY compensation (mm)
Paths64 apply_xy_compensation(const Paths64& paths, double contour_mm, double hole_mm);

// Fast in-place O(N) collinear & micro-edge vertex pruner (runs BEFORE Clipper2!)
void fast_prune_collinear(Paths64& paths, double eps_mm);

// Douglas-Peucker simplification + tiny-area filtering (mm)
Path64 simplify_path_mm(const Path64& path, double eps_mm, bool is_closed = true);
Paths64 simplify_paths_mm(const Paths64& paths, double eps_mm, double min_area_mm2 = 0.005);

// Extract structured islands + direct holes via Clipper2 PolyTree64
std::vector<Island> extract_islands(const Paths64& paths);

// Compute total signed area in mm^2
double total_area_mm2(const Paths64& paths);

// Canonical 64-bit geometry hash for identical-layer deduplication
uint64_t hash_paths(const Paths64& paths);

// Fast ray-casting point-in-paths (NonZero / EvenOdd winding)
bool point_in_path(const Pt64& p, const Path64& poly);
bool point_in_paths(const Pt64& p, const Paths64& paths);

// Check if open segment (a, b) stays strictly inside given polygon set (sampled/exact for combing)
bool segment_crosses_boundary(const Pt64& a, const Pt64& b, const Paths64& boundary);

// Exact Slic3r / PrusaSlicer stadium (pill) extrusion cross-sectional math
inline double pill_area_mm2(double width_mm, double height_mm) {
    if (width_mm <= height_mm) {
        return 0.25 * 3.14159265358979323846 * width_mm * height_mm;
    }
    return height_mm * (width_mm - height_mm) + 0.25 * 3.14159265358979323846 * height_mm * height_mm;
}

inline double pill_spacing_mm(double width_mm, double height_mm) {
    // Spacing = W - h * (1 - pi/4)
    constexpr double OVERLAP_FACTOR = 1.0 - 0.25 * 3.14159265358979323846;  // ~0.2146018366
    double sp = width_mm - height_mm * OVERLAP_FACTOR;
    return std::max(width_mm * 0.5, sp);
}

inline double extrusion_e_mm(double length_mm, double width_mm, double height_mm,
                             double filament_dia_mm, double flow_mult, bool is_bridge = false) {
    double cross_mm2 = is_bridge
        ? (0.25 * 3.14159265358979323846 * width_mm * width_mm)
        : pill_area_mm2(width_mm, height_mm);
    double fil_area = 0.25 * 3.14159265358979323846 * filament_dia_mm * filament_dia_mm;
    return (cross_mm2 * length_mm / fil_area) * flow_mult;
}

}  // namespace katman
