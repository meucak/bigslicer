#include "geometry2d.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace katman {

Rect64 compute_bounds(const Path64& path) {
    if (path.empty()) return Rect64(0, 0, 0, 0);
    int64_t l = std::numeric_limits<int64_t>::max();
    int64_t t = l;
    int64_t r = std::numeric_limits<int64_t>::lowest();
    int64_t b = r;
    for (const auto& p : path) {
        if (p.x < l) l = p.x;
        if (p.x > r) r = p.x;
        if (p.y < t) t = p.y;
        if (p.y > b) b = p.y;
    }
    return Rect64(l, t, r, b);
}

Rect64 compute_bounds(const Paths64& paths) {
    if (paths.empty()) return Rect64(0, 0, 0, 0);
    int64_t l = std::numeric_limits<int64_t>::max();
    int64_t t = l;
    int64_t r = std::numeric_limits<int64_t>::lowest();
    int64_t b = r;
    bool any = false;
    for (const auto& path : paths) {
        for (const auto& p : path) {
            any = true;
            if (p.x < l) l = p.x;
            if (p.x > r) r = p.x;
            if (p.y < t) t = p.y;
            if (p.y > b) b = p.y;
        }
    }
    return any ? Rect64(l, t, r, b) : Rect64(0, 0, 0, 0);
}

Paths64 clip_union_self(const Paths64& a, Clipper2Lib::FillRule rule) {
    if (a.empty()) return {};
    Clipper2Lib::Clipper64 c;
    c.PreserveCollinear(false);
    c.AddSubject(a);
    Paths64 sol;
    c.Execute(Clipper2Lib::ClipType::Union, rule, sol);
    return sol;
}

Paths64 clip_union(const Paths64& a, const Paths64& b, Clipper2Lib::FillRule rule) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    Rect64 ba = compute_bounds(a);
    Rect64 bb = compute_bounds(b);
    if (!bounds_overlap(ba, bb, 10)) {
        Paths64 merged;
        merged.reserve(a.size() + b.size());
        merged.insert(merged.end(), a.begin(), a.end());
        merged.insert(merged.end(), b.begin(), b.end());
        return merged;
    }
    Clipper2Lib::Clipper64 c;
    c.PreserveCollinear(false);
    c.AddSubject(a);
    c.AddClip(b);
    Paths64 sol;
    c.Execute(Clipper2Lib::ClipType::Union, rule, sol);
    return sol;
}

Paths64 clip_difference(const Paths64& subject, const Paths64& clip, Clipper2Lib::FillRule rule) {
    if (subject.empty() || clip.empty()) return subject;
    Rect64 bs = compute_bounds(subject);
    Rect64 bc = compute_bounds(clip);
    if (!bounds_overlap(bs, bc, 10)) return subject;
    Clipper2Lib::Clipper64 c;
    c.PreserveCollinear(false);
    c.AddSubject(subject);
    c.AddClip(clip);
    Paths64 sol;
    c.Execute(Clipper2Lib::ClipType::Difference, rule, sol);
    return sol;
}

Paths64 clip_intersection(const Paths64& a, const Paths64& b, Clipper2Lib::FillRule rule) {
    if (a.empty() || b.empty()) return {};
    Rect64 ba = compute_bounds(a);
    Rect64 bb = compute_bounds(b);
    if (!bounds_overlap(ba, bb, 10)) return {};
    Clipper2Lib::Clipper64 c;
    c.PreserveCollinear(false);
    c.AddSubject(a);
    c.AddClip(b);
    Paths64 sol;
    c.Execute(Clipper2Lib::ClipType::Intersection, rule, sol);
    return sol;
}

Paths64 offset_paths_mm(const Paths64& paths, double delta_mm,
                        Clipper2Lib::JoinType jt, double miter_limit, double arc_tol_mm) {
    if (paths.empty() || std::fabs(delta_mm) < 1e-6) return paths;
    Clipper2Lib::ClipperOffset co(miter_limit, arc_tol_mm * COORD_SCALE, false, false);
    co.AddPaths(paths, jt, Clipper2Lib::EndType::Polygon);
    Paths64 sol;
    co.Execute(delta_mm * COORD_SCALE, sol);
    return sol;
}

Paths64 apply_xy_compensation(const Paths64& paths, double contour_mm, double hole_mm) {
    if (paths.empty()) return {};
    if (std::fabs(contour_mm) < 1e-5 && std::fabs(hole_mm) < 1e-5) return paths;
    if (std::fabs(contour_mm - (-hole_mm)) < 1e-5) {
        // Uniform offset: expanding contour by +d shrinks holes by -d automatically!
        return offset_paths_mm(paths, contour_mm);
    }
    // Separate outer contours (area > 0) and holes (area < 0)
    Paths64 outers, holes;
    for (const auto& p : paths) {
        if (Clipper2Lib::Area(p) >= 0.0) outers.push_back(p);
        else holes.push_back(p);
    }
    if (std::fabs(contour_mm) >= 1e-5 && !outers.empty()) {
        outers = offset_paths_mm(outers, contour_mm);
    }
    if (std::fabs(hole_mm) >= 1e-5 && !holes.empty()) {
        // Positive hole_mm widens the hole -> reverse hole to positive, inflate by +hole_mm, subtract from outers!
        Paths64 pos_holes;
        pos_holes.reserve(holes.size());
        for (auto h : holes) {
            std::reverse(h.begin(), h.end());
            pos_holes.push_back(std::move(h));
        }
        pos_holes = offset_paths_mm(pos_holes, hole_mm);
        return clip_difference(outers, pos_holes);
    }
    if (holes.empty()) return outers;
    Paths64 pos_holes;
    pos_holes.reserve(holes.size());
    for (auto h : holes) {
        std::reverse(h.begin(), h.end());
        pos_holes.push_back(std::move(h));
    }
    return clip_difference(outers, pos_holes);
}

void fast_prune_collinear(Paths64& paths, double eps_mm) {
    // Strictly prune collinear mid-edge vertices (e.g. diagonal quad-split points)
    // without greedily dropping true CAD corners near t -> 0 or t -> 1.
    const double eps = std::max(0.0004, eps_mm) * COORD_SCALE;
    const double eps2 = eps * eps;
    for (auto& p : paths) {
        const size_t n = p.size();
        if (n < 4) continue;
        Path64 out;
        out.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            const Pt64& prev = out.empty() ? p[n - 1] : out.back();
            const Pt64& curr = p[i];
            const Pt64& next = p[(i + 1 < n) ? (i + 1) : 0];
            if (curr == prev) continue;
            double dx1 = static_cast<double>(curr.x - prev.x);
            double dy1 = static_cast<double>(curr.y - prev.y);
            double dx = static_cast<double>(next.x - prev.x);
            double dy = static_cast<double>(next.y - prev.y);
            double len2 = dx * dx + dy * dy;
            if (len2 > 1e-6) {
                double cr = dx1 * dy - dy1 * dx;
                if (cr * cr <= eps2 * len2) {
                    double d2x = static_cast<double>(next.x - curr.x);
                    double d2y = static_cast<double>(next.y - curr.y);
                    if (dx1 * d2x + dy1 * d2y >= 0.0) continue;
                }
            }
            out.push_back(curr);
        }
        // Wrap-around check on out[0] against (out.back(), out[1])!
        while (out.size() >= 4) {
            const Pt64& prev = out.back();
            const Pt64& curr = out[0];
            const Pt64& next = out[1];
            double dx1 = static_cast<double>(curr.x - prev.x);
            double dy1 = static_cast<double>(curr.y - prev.y);
            double dx = static_cast<double>(next.x - prev.x);
            double dy = static_cast<double>(next.y - prev.y);
            double len2 = dx * dx + dy * dy;
            if (len2 > 1e-6) {
                double cr = dx1 * dy - dy1 * dx;
                double d2x = static_cast<double>(next.x - curr.x);
                double d2y = static_cast<double>(next.y - curr.y);
                if (cr * cr <= eps2 * len2 && (dx1 * d2x + dy1 * d2y >= 0.0)) {
                    out.erase(out.begin());
                    continue;
                }
            }
            break;
        }
        if (out.size() >= 3) {
            p.swap(out);
        }
    }
}

Path64 simplify_path_mm(const Path64& path, double eps_mm, bool is_closed) {
    if (path.size() < 3) return path;
    double eps = eps_mm * COORD_SCALE;
    return Clipper2Lib::SimplifyPath(path, eps, is_closed);
}

Paths64 simplify_paths_mm(const Paths64& paths, double eps_mm, double min_area_mm2) {
    Paths64 out;
    out.reserve(paths.size());
    const double eps = eps_mm * COORD_SCALE;
    const double min_area_c2 = min_area_mm2 * COORD_SCALE * COORD_SCALE;
    for (const auto& p : paths) {
        if (p.size() < 3) continue;
        // Rotate canonically before Douglas-Peucker so identical vertical walls simplify identically!
        Path64 rot = p;
        size_t min_k = 0;
        for (size_t k = 1; k < rot.size(); ++k) {
            if (rot[k].x < rot[min_k].x || (rot[k].x == rot[min_k].x && rot[k].y < rot[min_k].y)) min_k = k;
        }
        if (min_k != 0) std::rotate(rot.begin(), rot.begin() + static_cast<ptrdiff_t>(min_k), rot.end());
        Path64 sp = Clipper2Lib::SimplifyPath(rot, eps, true);
        if (sp.size() < 3) continue;
        if (std::fabs(Clipper2Lib::Area(sp)) >= min_area_c2) {
            out.push_back(std::move(sp));
        }
    }
    return out;
}

namespace {
void collect_islands_from_node(const Clipper2Lib::PolyPath64& node, std::vector<Island>& out) {
    for (const auto& child : node) {
        if (!child->IsHole()) {
            Island isl;
            isl.outer = child->Polygon();
            isl.bounds = compute_bounds(isl.outer);
            double area_c2 = Clipper2Lib::Area(isl.outer);
            for (const auto& hole : *child) {
                if (hole->IsHole()) {
                    const Path64& hp = hole->Polygon();
                    area_c2 += Clipper2Lib::Area(hp);  // negative
                    isl.holes.push_back(hp);
                    // Nested islands inside holes become their own Island!
                    collect_islands_from_node(*hole, out);
                }
            }
            isl.area_mm2 = area_c2 * INV_COORD_SCALE * INV_COORD_SCALE;
            if (isl.outer.size() >= 3 && isl.area_mm2 > 0.001) {
                out.push_back(std::move(isl));
            }
        } else {
            collect_islands_from_node(*child, out);
        }
    }
}
}  // namespace

std::vector<Island> extract_islands(const Paths64& paths) {
    std::vector<Island> out;
    if (paths.empty()) return out;
    Clipper2Lib::PolyTree64 tree;
    Clipper2Lib::Clipper64 c;
    c.PreserveCollinear(false);
    c.AddSubject(paths);
    c.Execute(Clipper2Lib::ClipType::Union, Clipper2Lib::FillRule::NonZero, tree);
    collect_islands_from_node(tree, out);
    return out;
}

double total_area_mm2(const Paths64& paths) {
    double a = 0.0;
    for (const auto& p : paths) a += Clipper2Lib::Area(p);
    return a * INV_COORD_SCALE * INV_COORD_SCALE;
}

uint64_t hash_paths(const Paths64& paths) {
    // Order-independent across paths via XOR of per-path splitmix hashes (rotation-invariant per path!)
    auto splitmix64 = [](uint64_t x) -> uint64_t {
        x += 0x9e3779b97f4a7c15ull;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
        return x ^ (x >> 31);
    };

    uint64_t combined = splitmix64(paths.size());
    for (const auto& p : paths) {
        if (p.empty()) continue;
        // Find canonical minimal vertex index for rotation invariance
        size_t min_idx = 0;
        for (size_t i = 1; i < p.size(); ++i) {
            if (p[i].x < p[min_idx].x || (p[i].x == p[min_idx].x && p[i].y < p[min_idx].y)) {
                min_idx = i;
            }
        }
        uint64_t h = splitmix64(p.size());
        for (size_t k = 0; k < p.size(); ++k) {
            const Pt64& pt = p[(min_idx + k) % p.size()];
            // Quantize to 5 um (50 units) with rounding for rock-solid vertical-wall cache matching!
            int64_t qx = (pt.x >= 0) ? ((pt.x + 25) / 50) : ((pt.x - 25) / 50);
            int64_t qy = (pt.y >= 0) ? ((pt.y + 25) / 50) : ((pt.y - 25) / 50);
            h = splitmix64(h ^ splitmix64((static_cast<uint64_t>(qx) << 32) ^ static_cast<uint64_t>(qy)));
        }
        combined ^= h;
    }
    return combined;
}

bool point_in_path(const Pt64& p, const Path64& poly) {
    const size_t n = poly.size();
    if (n < 3) return false;
    bool inside = false;
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const Pt64& a = poly[i];
        const Pt64& b = poly[j];
        if (((a.y > p.y) != (b.y > p.y))) {
            double xint = static_cast<double>(b.x - a.x) * static_cast<double>(p.y - a.y) /
                          static_cast<double>(b.y - a.y) + static_cast<double>(a.x);
            if (static_cast<double>(p.x) < xint) inside = !inside;
        }
    }
    return inside;
}

bool point_in_paths(const Pt64& p, const Paths64& paths) {
    int inside = 0;
    for (const auto& poly : paths) {
        if (point_in_path(p, poly)) inside ^= 1;
    }
    return inside != 0;
}

namespace {
inline int64_t orient2d_sign(const Pt64& a, const Pt64& b, const Pt64& c) {
    __int128 v = static_cast<__int128>(b.x - a.x) * static_cast<__int128>(c.y - a.y) -
                 static_cast<__int128>(b.y - a.y) * static_cast<__int128>(c.x - a.x);
    return (v > 0) ? 1 : (v < 0 ? -1 : 0);
}

inline bool proper_seg_intersect(const Pt64& a, const Pt64& b, const Pt64& c, const Pt64& d) {
    int64_t o1 = orient2d_sign(a, b, c);
    int64_t o2 = orient2d_sign(a, b, d);
    if (o1 == 0 || o2 == 0 || o1 == o2) return false;
    int64_t o3 = orient2d_sign(c, d, a);
    int64_t o4 = orient2d_sign(c, d, b);
    return (o3 != 0 && o4 != 0 && o3 != o4);
}
}  // namespace

bool segment_crosses_boundary(const Pt64& a, const Pt64& b, const Paths64& boundary) {
    int64_t minx = std::min(a.x, b.x), maxx = std::max(a.x, b.x);
    int64_t miny = std::min(a.y, b.y), maxy = std::max(a.y, b.y);
    for (const auto& poly : boundary) {
        const size_t n = poly.size();
        if (n < 3) continue;
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            const Pt64& c = poly[j];
            const Pt64& d = poly[i];
            if (std::max(c.x, d.x) < minx || std::min(c.x, d.x) > maxx ||
                std::max(c.y, d.y) < miny || std::min(c.y, d.y) > maxy) {
                continue;
            }
            if (proper_seg_intersect(a, b, c, d)) return true;
        }
    }
    return false;
}

}  // namespace katman
