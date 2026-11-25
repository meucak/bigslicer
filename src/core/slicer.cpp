#include "slicer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace katman {

std::vector<LayerScheduleEntry> build_layer_schedule(const Mesh& mesh, const SlicerConfig& cfg) {
    std::vector<LayerScheduleEntry> sched;
    if (!mesh.ok || mesh.tris.empty()) return sched;

    const float z_max = mesh.bbox_max.z;
    if (z_max <= 1e-4f) return sched;

    const float first_h = std::clamp(cfg.first_layer_height, 0.04f, 0.80f);
    const float nom_h = std::clamp(cfg.layer_height, 0.04f, 0.80f);

    if (!cfg.adaptive_layer_height) {
        float z = 0.0f;
        int idx = 0;
        while (z + 0.02f < z_max) {
            float h = (idx == 0) ? first_h : nom_h;
            float z_next = z + h;
            float z_mid = z + h * 0.5f;
            if (z_mid > z_max) break;
            sched.push_back({z_mid, z_next, h});
            z = z_next;
            ++idx;
        }
        return sched;
    }

    // Dolenc-Makela cusp-height adaptive layer height (PrusaSlicer style)
    // Sample max |nz| of sloped (non-vertical, non-flat) triangles in 0.2mm Z bins
    const float min_h = std::clamp(cfg.adaptive_min_layer, 0.04f, nom_h);
    const float max_h = std::clamp(cfg.adaptive_max_layer, nom_h, 0.80f);
    const float cusp = std::clamp(cfg.adaptive_cusp_error, 0.005f, 0.20f);
    const float bin_step = 0.25f;
    const size_t nbins = static_cast<size_t>(std::ceil(z_max / bin_step)) + 2;
    std::vector<float> bin_max_slope_nz(nbins, 0.0f);

    for (const auto& t : mesh.tris) {
        float anz = std::fabs(t.nz);
        // Ignore pure vertical walls (anz < 0.05) and flat horizontal caps (anz > 0.995)
        if (anz < 0.05f || anz > 0.995f) continue;
        size_t b0 = static_cast<size_t>(std::max(0.0f, t.zmin) / bin_step);
        size_t b1 = static_cast<size_t>(std::max(0.0f, t.zmax) / bin_step);
        b1 = std::min(b1, nbins - 1);
        for (size_t b = b0; b <= b1; ++b) {
            if (anz > bin_max_slope_nz[b]) bin_max_slope_nz[b] = anz;
        }
    }

    // Compute raw adaptive heights from cusp formula: h = clamp(cusp / |nz|, min_h, max_h)
    std::vector<float> raw_h;
    raw_h.reserve(static_cast<size_t>(z_max / min_h) + 4);
    float z = 0.0f;
    raw_h.push_back(first_h);
    z += first_h;
    while (z + 0.02f < z_max) {
        size_t b = std::min(nbins - 1, static_cast<size_t>(z / bin_step));
        float anz = bin_max_slope_nz[b];
        float h = (anz > 0.05f) ? std::clamp(cusp / anz, min_h, max_h) : max_h;
        raw_h.push_back(h);
        z += h;
    }

    // Gaussian/weighted smoothing pass (PrusaSlicer Smooth) keeping Layer 0 fixed
    int rad = std::clamp(cfg.adaptive_smooth_radius, 0, 5);
    if (rad > 0 && raw_h.size() > 3) {
        std::vector<float> smooth_h = raw_h;
        for (int pass = 0; pass < 2; ++pass) {
            for (size_t i = 1; i < raw_h.size(); ++i) {
                double num = 0.0, den = 0.0;
                for (int d = -rad; d <= rad; ++d) {
                    int j = static_cast<int>(i) + d;
                    if (j >= 1 && j < static_cast<int>(raw_h.size())) {
                        double w = std::exp(-0.5 * static_cast<double>(d * d) / (0.6 * rad * rad));
                        num += w * raw_h[static_cast<size_t>(j)];
                        den += w;
                    }
                }
                smooth_h[i] = std::clamp(static_cast<float>(num / den), min_h, max_h);
            }
            raw_h = smooth_h;
        }
    }

    // Re-integrate smoothed heights cleanly up to z_max
    z = 0.0f;
    for (size_t i = 0; i < raw_h.size() && z + 0.02f < z_max; ++i) {
        float h = std::round(raw_h[i] * 100.0f) * 0.01f;  // 0.01mm stepper-friendly quantization
        h = std::clamp(h, min_h, max_h);
        if (i == 0) h = first_h;
        float z_mid = z + h * 0.5f;
        if (z_mid > z_max) break;
        sched.push_back({z_mid, z + h, h});
        z += h;
    }
    return sched;
}

namespace {

struct Seg2D {
    Pt64 a;
    Pt64 b;
};

// Zero-allocation reusable open-addressing hash map: Pt64 -> head index into segment linked list.
// Uses generation counter so reset() is O(1) without memset!
class FastEndpointHash {
public:
    struct Bucket {
        int64_t x = 0;
        int64_t y = 0;
        uint32_t gen = 0;
        int32_t head = -1;
    };

    void prepare(size_t expected_segs) {
        size_t want = 64;
        while (want < expected_segs * 3) want <<= 1;
        if (buckets_.size() < want) {
            buckets_.assign(want, Bucket{});
            mask_ = want - 1;
            gen_ = 1;
        } else {
            ++gen_;
            if (gen_ == 0) {
                for (auto& b : buckets_) b.gen = 0;
                gen_ = 1;
            }
        }
        next_.assign(expected_segs, -1);
    }

    inline uint64_t hash_pt(int64_t x, int64_t y) const {
        uint64_t k = (static_cast<uint64_t>(x) * 0x9e3779b97f4a7c15ull) ^
                     (static_cast<uint64_t>(y) * 0xbf58476d1ce4e5b9ull);
        k ^= (k >> 30);
        k *= 0xbf58476d1ce4e5b9ull;
        return k ^ (k >> 27);
    }

    inline void insert(const Pt64& pt, int32_t seg_idx) {
        size_t idx = hash_pt(pt.x, pt.y) & mask_;
        while (true) {
            Bucket& b = buckets_[idx];
            if (b.gen != gen_) {
                b.gen = gen_;
                b.x = pt.x;
                b.y = pt.y;
                b.head = seg_idx;
                next_[static_cast<size_t>(seg_idx)] = -1;
                return;
            }
            if (b.x == pt.x && b.y == pt.y) {
                next_[static_cast<size_t>(seg_idx)] = b.head;
                b.head = seg_idx;
                return;
            }
            idx = (idx + 1) & mask_;
        }
    }

    // Pop first unused outgoing segment starting at pt
    inline int32_t find_unused(const Pt64& pt, const std::vector<uint8_t>& used) const {
        size_t idx = hash_pt(pt.x, pt.y) & mask_;
        while (true) {
            const Bucket& b = buckets_[idx];
            if (b.gen != gen_) return -1;
            if (b.x == pt.x && b.y == pt.y) {
                int32_t cur = b.head;
                while (cur >= 0) {
                    if (!used[static_cast<size_t>(cur)]) return cur;
                    cur = next_[static_cast<size_t>(cur)];
                }
                return -1;
            }
            idx = (idx + 1) & mask_;
        }
    }

private:
    std::vector<Bucket> buckets_;
    std::vector<int32_t> next_;
    size_t mask_ = 0;
    uint32_t gen_ = 1;
};

// Branchless lookup table for strict half-open Z plane intersection (v.z > z).
// Produces directed segment (a -> b) so outer mesh contours have CCW (+area) winding
// and inner holes have CW (-area) winding under standard right-hand outward normals!
// mask bits: bit0=(z0>z), bit1=(z1>z), bit2=(z2>z)
// Table stores {eA_v0, eA_v1, eB_v0, eB_v1}
constexpr uint8_t SLICE_LUT[8][4] = {
    {0, 0, 0, 0},  // 000: all <= z
    {0, 1, 0, 2},  // 001: v0 above -> (v0,v1) -> (v0,v2) [CCW]
    {1, 2, 1, 0},  // 010: v1 above -> (v1,v2) -> (v1,v0) [CCW]
    {2, 1, 2, 0},  // 011: v2 below -> (v2,v1) -> (v2,v0) [CCW]
    {2, 0, 2, 1},  // 100: v2 above -> (v2,v0) -> (v2,v1) [CCW]
    {1, 0, 1, 2},  // 101: v1 below -> (v1,v0) -> (v1,v2) [CCW]
    {0, 2, 0, 1},  // 110: v0 below -> (v0,v2) -> (v0,v1) [CCW]
    {0, 0, 0, 0},  // 111: all > z
};

inline Pt64 interp_edge_pt(const float vx[3], const float vy[3], const float vz[3],
                           uint8_t i0, uint8_t i1, float z) {
    // Always order endpoints deterministically (lower z first) so adjacent shared edges
    // compute BIT-IDENTICAL floating-point interpolation and identical int64 coords!
    if (vz[i0] > vz[i1]) std::swap(i0, i1);
    double dz = static_cast<double>(vz[i1]) - static_cast<double>(vz[i0]);
    double t = (std::fabs(dz) > 1e-12) ? ((static_cast<double>(z) - static_cast<double>(vz[i0])) / dz) : 0.5;
    t = std::clamp(t, 0.0, 1.0);
    double ix = static_cast<double>(vx[i0]) + t * (static_cast<double>(vx[i1]) - static_cast<double>(vx[i0]));
    double iy = static_cast<double>(vy[i0]) + t * (static_cast<double>(vy[i1]) - static_cast<double>(vy[i0]));
    return Pt64(static_cast<int64_t>(std::llround(ix * COORD_SCALE)),
                static_cast<int64_t>(std::llround(iy * COORD_SCALE)));
}

void stitch_segments_to_contours(const std::vector<Seg2D>& segs,
                                 FastEndpointHash& hash_a,
                                 Paths64& out_closed,
                                 uint32_t& out_open_chains) {
    out_closed.clear();
    out_open_chains = 0;
    const size_t n = segs.size();
    if (n == 0) return;

    hash_a.prepare(n);
    for (size_t i = 0; i < n; ++i) {
        hash_a.insert(segs[i].a, static_cast<int32_t>(i));
    }

    std::vector<uint8_t> used(n, 0);
    std::vector<Path64> open_chains;

    for (size_t i = 0; i < n; ++i) {
        if (used[i]) continue;
        used[i] = 1;
        Path64 chain;
        chain.reserve(32);
        chain.push_back(segs[i].a);
        Pt64 cur = segs[i].b;
        Pt64 start = segs[i].a;

        while (cur != start) {
            chain.push_back(cur);
            int32_t nxt = hash_a.find_unused(cur, used);
            if (nxt < 0) break;
            used[static_cast<size_t>(nxt)] = 1;
            cur = segs[static_cast<size_t>(nxt)].b;
        }

        if (cur == start) {
            if (chain.size() >= 3) {
                // Rotate closed loop to canonical lexicographical min vertex so identical
                // vertical-wall layers produce bit-identical simplified contours & 99% cache hits!
                size_t min_k = 0;
                for (size_t k = 1; k < chain.size(); ++k) {
                    if (chain[k].x < chain[min_k].x ||
                        (chain[k].x == chain[min_k].x && chain[k].y < chain[min_k].y)) {
                        min_k = k;
                    }
                }
                if (min_k != 0) {
                    std::rotate(chain.begin(), chain.begin() + static_cast<ptrdiff_t>(min_k), chain.end());
                }
                out_closed.push_back(std::move(chain));
            }
        } else {
            if (chain.size() >= 2) open_chains.push_back(std::move(chain));
        }
    }

    // Stage 2: Tolerant greedy chaining for non-manifold / cracked STL open chains
    if (!open_chains.empty()) {
        out_open_chains = static_cast<uint32_t>(open_chains.size());
        const double max_snap_sq = (0.5 * COORD_SCALE) * (0.5 * COORD_SCALE);
        std::vector<uint8_t> merged(open_chains.size(), 0);

        for (size_t i = 0; i < open_chains.size(); ++i) {
            if (merged[i]) continue;
            merged[i] = 1;
            Path64 acc = std::move(open_chains[i]);

            bool progressed = true;
            while (progressed) {
                progressed = false;
                Pt64 tail = acc.back();
                Pt64 head = acc.front();
                double close_dx = static_cast<double>(tail.x - head.x);
                double close_dy = static_cast<double>(tail.y - head.y);
                double close_d2 = close_dx * close_dx + close_dy * close_dy;

                size_t best_j = open_chains.size();
                bool best_rev = false;
                double best_d2 = max_snap_sq;

                for (size_t j = 0; j < open_chains.size(); ++j) {
                    if (merged[j]) continue;
                    const Path64& oc = open_chains[j];
                    double d0x = static_cast<double>(tail.x - oc.front().x);
                    double d0y = static_cast<double>(tail.y - oc.front().y);
                    double d0 = d0x * d0x + d0y * d0y;
                    if (d0 < best_d2) {
                        best_d2 = d0;
                        best_j = j;
                        best_rev = false;
                    }
                    double d1x = static_cast<double>(tail.x - oc.back().x);
                    double d1y = static_cast<double>(tail.y - oc.back().y);
                    double d1 = d1x * d1x + d1y * d1y;
                    if (d1 < best_d2) {
                        best_d2 = d1;
                        best_j = j;
                        best_rev = true;
                    }
                }

                // If closing our own loop is closer than jumping to another open chain, close now!
                if (acc.size() >= 3 && close_d2 <= best_d2 && close_d2 < max_snap_sq) {
                    break;
                }
                if (best_j < open_chains.size()) {
                    merged[best_j] = 1;
                    Path64& oc = open_chains[best_j];
                    if (best_rev) std::reverse(oc.begin(), oc.end());
                    acc.insert(acc.end(), oc.begin(), oc.end());
                    progressed = true;
                }
            }

            if (acc.size() >= 3) {
                out_closed.push_back(std::move(acc));
            }
        }
    }
}

}  // namespace

std::vector<SlicedLayer> slice_mesh_layers(const Mesh& mesh,
                                           const std::vector<LayerScheduleEntry>& schedule,
                                           const SlicerConfig& cfg,
                                           ThreadPool* pool) {
    const size_t L = schedule.size();
    std::vector<SlicedLayer> layers(L);
    if (L == 0 || !mesh.ok || mesh.tris.empty()) return layers;

    // Extract Z array for fast lookup
    std::vector<float> z_planes(L);
    for (size_t i = 0; i < L; ++i) {
        layers[i].index = static_cast<uint32_t>(i);
        layers[i].z_slice = schedule[i].z_slice;
        layers[i].z_top = schedule[i].z_top;
        layers[i].height = schedule[i].height;
        z_planes[i] = schedule[i].z_slice;
    }

    // Build CSR Z-interval bucket index: maps each layer -> exact list of triangles crossing z_planes[l]
    // Uniform spacing check enables O(1) division instead of binary search!
    const bool is_uniform = !cfg.adaptive_layer_height && L > 1;
    const float z0 = z_planes[0];
    const float z1 = (L > 1) ? z_planes[1] : z0 + cfg.layer_height;
    const float inv_lh = (L > 1 && (z1 - z0) > 1e-6f) ? (1.0f / (z1 - z0)) : (1.0f / cfg.layer_height);

    auto find_layer_range = [&](float zmin, float zmax, uint32_t& lo, uint32_t& hi) -> bool {
        if (zmax <= z_planes[0] || zmin > z_planes[L - 1]) return false;
        if (is_uniform) {
            // Note: z_planes[0] might have first_layer_height != layer_height; z_planes[1..L-1] are uniform!
            if (zmax <= z_planes[1]) {
                lo = 0;
                hi = (zmin <= z_planes[0] && zmax > z_planes[0]) ? 1u : 0u;
                return hi > lo;
            }
            float base = z_planes[1];
            float step = (L > 2) ? (z_planes[2] - z_planes[1]) : cfg.layer_height;
            float inv_s = 1.0f / step;
            int i0 = (zmin <= z_planes[0]) ? 0 : static_cast<int>(std::ceil((zmin - base) * inv_s)) + 1;
            int i1 = static_cast<int>(std::floor((zmax - base) * inv_s)) + 1;
            // Refine bounds exactly against z_planes for zero rounding drift
            i0 = std::clamp(i0, 0, static_cast<int>(L));
            i1 = std::clamp(i1, 0, static_cast<int>(L) - 1);
            while (i0 > 0 && z_planes[i0 - 1] >= zmin) --i0;
            while (i0 < static_cast<int>(L) && z_planes[i0] < zmin) ++i0;
            while (i1 + 1 < static_cast<int>(L) && z_planes[i1 + 1] < zmax) ++i1;
            while (i1 >= 0 && z_planes[i1] >= zmax) --i1;
            if (i0 > i1) return false;
            lo = static_cast<uint32_t>(i0);
            hi = static_cast<uint32_t>(i1 + 1);
            return true;
        }
        (void)inv_lh;
        auto it0 = std::lower_bound(z_planes.begin(), z_planes.end(), zmin);
        auto it1 = std::lower_bound(z_planes.begin(), z_planes.end(), zmax);
        lo = static_cast<uint32_t>(it0 - z_planes.begin());
        hi = static_cast<uint32_t>(it1 - z_planes.begin());
        return hi > lo;
    };

    const size_t ntris = mesh.tris.size();
    std::vector<uint32_t> counts(L, 0);
    std::vector<uint32_t> tri_lo(ntris, 0);
    std::vector<uint32_t> tri_hi(ntris, 0);

    uint64_t total_refs = 0;
    for (size_t t = 0; t < ntris; ++t) {
        const SliceTri& tr = mesh.tris[t];
        if (tr.zmax <= tr.zmin + 1e-7f) continue;  // horizontal triangle never crosses plane
        uint32_t lo = 0, hi = 0;
        if (find_layer_range(tr.zmin, tr.zmax, lo, hi)) {
            tri_lo[t] = lo;
            tri_hi[t] = hi;
            total_refs += (hi - lo);
            for (uint32_t l = lo; l < hi; ++l) ++counts[l];
        }
    }

    std::vector<uint32_t> offsets(L + 1, 0);
    for (size_t l = 0; l < L; ++l) offsets[l + 1] = offsets[l] + counts[l];

    std::vector<uint32_t> csr_tris(static_cast<size_t>(total_refs));
    std::vector<uint32_t> cursor = offsets;
    for (size_t t = 0; t < ntris; ++t) {
        uint32_t lo = tri_lo[t], hi = tri_hi[t];
        for (uint32_t l = lo; l < hi; ++l) {
            csr_tris[cursor[l]++] = static_cast<uint32_t>(t);
        }
    }

    // Reusable per-worker scratch buffers (0 heap churn across layers!)
    const uint32_t nw = pool ? pool->size() : 1u;
    std::vector<FastEndpointHash> hashes(nw);
    std::vector<std::vector<Seg2D>> seg_bufs(nw);
    for (uint32_t w = 0; w < nw; ++w) seg_bufs[w].reserve(1024);

    auto slice_one_layer = [&](uint32_t tid, size_t l) {
        const float z = z_planes[l];
        const uint32_t b = offsets[l];
        const uint32_t e = offsets[l + 1];
        if (b == e) return;

        auto& segs = seg_bufs[tid];
        segs.clear();
        segs.reserve(e - b);

        for (uint32_t k = b; k < e; ++k) {
            const SliceTri& tr = mesh.tris[csr_tris[k]];
            uint32_t mask = (tr.z0 > z ? 1u : 0u) |
                            (tr.z1 > z ? 2u : 0u) |
                            (tr.z2 > z ? 4u : 0u);
            if (mask == 0u || mask == 7u) continue;

            const float vx[3] = {tr.x0, tr.x1, tr.x2};
            const float vy[3] = {tr.y0, tr.y1, tr.y2};
            const float vz[3] = {tr.z0, tr.z1, tr.z2};
            const auto& lut = SLICE_LUT[mask];
            Pt64 pa = interp_edge_pt(vx, vy, vz, lut[0], lut[1], z);
            Pt64 pb = interp_edge_pt(vx, vy, vz, lut[2], lut[3], z);
            if (pa != pb) {
                segs.push_back({pa, pb});
            }
        }

        if (segs.empty()) return;

        Paths64 raw_closed;
        uint32_t open_cnt = 0;
        stitch_segments_to_contours(segs, hashes[tid], raw_closed, open_cnt);
        layers[l].open_chains = open_cnt;
        if (raw_closed.empty()) return;

        // Pre-prune strictly collinear diagonal-split vertices BEFORE Clipper2 Vatti sweep-line
        // (tight 0.8 um threshold strips quad diagonals without ever touching true corners!)
        fast_prune_collinear(raw_closed, 0.0008);

        // Clean & normalize topology via Clipper2 Union(NonZero)
        Paths64 clean = clip_union_self(raw_closed, Clipper2Lib::FillRule::NonZero);
        if (clean.empty()) {
            // Fallback for degenerate self-inverting CAD shells
            clean = clip_union_self(raw_closed, Clipper2Lib::FillRule::EvenOdd);
        }

        // Simplify redundant colinear/micro vertices (PrusaSlicer resolution ~0.012 mm)
        clean = simplify_paths_mm(clean, cfg.simplify_tolerance, 0.005);

        // Apply XY contour / hole compensation and layer-0 elephant foot compensation
        double contour_comp = cfg.xy_contour_comp;
        double hole_comp = cfg.xy_hole_comp;
        if (l == 0 && cfg.elephant_foot_comp > 1e-5f) {
            contour_comp -= cfg.elephant_foot_comp;
            hole_comp += cfg.elephant_foot_comp;
        }
        if (std::fabs(contour_comp) > 1e-5 || std::fabs(hole_comp) > 1e-5) {
            clean = apply_xy_compensation(clean, contour_comp, hole_comp);
        }

        layers[l].contours = std::move(clean);
        layers[l].bounds = compute_bounds(layers[l].contours);
        layers[l].geom_hash = hash_paths(layers[l].contours);
    };

    if (pool && L >= 4) {
        pool->parallel_for(L, slice_one_layer, 1);
    } else {
        for (size_t l = 0; l < L; ++l) slice_one_layer(0, l);
    }

    return layers;
}

}  // namespace katman
