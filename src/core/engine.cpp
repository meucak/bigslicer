#include "engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>

namespace katman {

namespace {

using Clock = std::chrono::steady_clock;

double elapsed_ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

}  // namespace

SliceResult slice_mesh(Mesh mesh, const SlicerConfig& cfg, ThreadPool* external_pool) {
    SliceResult res;
    auto t_start = Clock::now();

    if (!mesh.ok || mesh.tris.empty()) {
        res.error = mesh.error.empty() ? "Invalid or empty mesh" : mesh.error;
        return res;
    }

    std::unique_ptr<ThreadPool> owned_pool;
    ThreadPool* pool = external_pool;
    if (!pool) {
        owned_pool = std::make_unique<ThreadPool>(static_cast<uint32_t>(std::max(0, cfg.threads)));
        pool = owned_pool.get();
    }
    res.stats.threads_used = pool->size();

    // Stage 1: Transform, lay-flat, bed placement & multi-copy arrange
    auto t0 = Clock::now();
    transform_and_arrange_mesh(mesh, cfg, pool);
    auto t1 = Clock::now();
    res.stats.timings.load_transform_ms = elapsed_ms(t0, t1);

    res.stats.tri_count = static_cast<uint32_t>(mesh.tris.size());
    res.stats.model_size_x = mesh.bbox_extent.x;
    res.stats.model_size_y = mesh.bbox_extent.y;
    res.stats.model_size_z = mesh.bbox_extent.z;
    res.stats.model_volume_cm3 = static_cast<float>(mesh.volume_mm3 * 0.001);
    res.stats.overhang_face_pct = mesh.overhang_face_pct;

    const float bed_min_x = cfg.origin_center ? -cfg.bed_size_x * 0.5f : 0.0f;
    const float bed_max_x = cfg.origin_center ?  cfg.bed_size_x * 0.5f : cfg.bed_size_x;
    const float bed_min_y = cfg.origin_center ? -cfg.bed_size_y * 0.5f : 0.0f;
    const float bed_max_y = cfg.origin_center ?  cfg.bed_size_y * 0.5f : cfg.bed_size_y;
    res.stats.fits_bed = (mesh.bbox_min.x >= bed_min_x - 0.5f && mesh.bbox_max.x <= bed_max_x + 0.5f &&
                          mesh.bbox_min.y >= bed_min_y - 0.5f && mesh.bbox_max.y <= bed_max_y + 0.5f &&
                          mesh.bbox_max.z <= cfg.bed_size_z + 0.5f);

    // Stage 2: Adaptive/Uniform Z schedule + CSR Z-bucketed parallel slicing & O(S) stitching
    auto t2 = Clock::now();
    auto schedule = build_layer_schedule(mesh, cfg);
    if (schedule.empty()) {
        res.error = "Model height too small to produce any printable layers";
        return res;
    }
    auto layers = slice_mesh_layers(mesh, schedule, cfg, pool);
    auto t3 = Clock::now();
    res.stats.timings.slice_stitch_ms = elapsed_ms(t2, t3);

    uint32_t open_sum = 0;
    for (const auto& l : layers) open_sum += l.open_chains;
    res.stats.non_manifold_open_chains = open_sum;

    const size_t L = layers.size();

    // Stage 3: Perimeters + Identical-Layer Geometry Cache!
    // Identify unique perimeter jobs: layer i can reuse layer i-1's exact perimeter offsets
    // when i > 1, geom_hash[i] == geom_hash[i-1], geom_hash[i-1] == geom_hash[i-2],
    // heights match, and overhang state is identical!
    auto t4 = Clock::now();
    std::vector<int> cache_source(L, -1);
    uint32_t cached_count = 0;
    if (cfg.enable_layer_cache && L >= 4) {
        for (size_t i = 2; i < L; ++i) {
            if (!layers[i].contours.empty() &&
                layers[i].geom_hash != 0 &&
                layers[i].geom_hash == layers[i - 1].geom_hash &&
                layers[i - 1].geom_hash == layers[i - 2].geom_hash &&
                std::fabs(layers[i].height - layers[i - 1].height) < 1e-4f) {
                cache_source[i] = (cache_source[i - 1] >= 0) ? cache_source[i - 1] : static_cast<int>(i - 1);
                ++cached_count;
            }
        }
    }
    res.stats.cached_layers = cached_count;

    // Collect only non-cached layer indices to compute in parallel!
    std::vector<size_t> unique_perim_layers;
    unique_perim_layers.reserve(L - cached_count);
    for (size_t i = 0; i < L; ++i) {
        if (cache_source[i] < 0) unique_perim_layers.push_back(i);
    }

    std::vector<LayerPerimeters> perims(L);
    auto run_perim = [&](uint32_t, size_t idx_pos) {
        size_t i = unique_perim_layers[idx_pos];
        const SlicedLayer* prev = (i > 0) ? &layers[i - 1] : nullptr;
        perims[i] = generate_layer_perimeters(layers[i], prev, cfg);
    };
    if (unique_perim_layers.size() >= 4) {
        pool->parallel_for(unique_perim_layers.size(), run_perim, 1);
    } else {
        for (size_t p = 0; p < unique_perim_layers.size(); ++p) run_perim(0, p);
    }

    // Copy cached perimeters cleanly (O(1) per cached layer vs expensive Clipper2 offsetting!)
    if (cached_count > 0) {
        for (size_t i = 0; i < L; ++i) {
            if (cache_source[i] >= 0) {
                perims[i] = perims[static_cast<size_t>(cache_source[i])];
            }
        }
    }

    // Stage 4: Cross-layer Surface Analysis (Top/Bottom Solid, Bridges, Ironing, Sparse Infill)
    auto t5 = Clock::now();
    auto surfaces = analyze_layer_surfaces(layers, perims, cfg, pool);
    auto t6 = Clock::now();
    res.stats.timings.surfaces_overhang_ms = elapsed_ms(t5, t6);

    // Stage 5: Supports (Normal Grid or Organic Tree)
    auto t7 = Clock::now();
    auto supports = generate_supports(layers, cfg, pool);
    auto t8 = Clock::now();
    res.stats.timings.supports_ms = elapsed_ms(t7, t8);

    // Stage 6: Infill & Solid Skin Toolpath Generation (parallel across all layers!)
    auto lightning_masks = precompute_lightning_regions(layers, surfaces, cfg);
    std::vector<std::vector<ExtrusionPath>> fill_paths(L);
    auto run_fill = [&](uint32_t, size_t i) {
        const Paths64* lmask = lightning_masks.empty() ? nullptr : &lightning_masks[i];
        generate_layer_fill(static_cast<uint32_t>(i), layers[i].z_slice, layers[i].height,
                            surfaces[i], lmask, cfg, fill_paths[i]);
    };
    if (L >= 4) {
        pool->parallel_for(L, run_fill, 1);
    } else {
        for (size_t i = 0; i < L; ++i) run_fill(0, i);
    }
    auto t9 = Clock::now();
    res.stats.timings.perimeters_infill_ms = elapsed_ms(t4, t5) + elapsed_ms(t8, t9);

    // Optional Raft layers beneath model (when cfg.adhesion == AdhesionType::Raft)
    std::vector<PlannedLayer> raft_planned;
    float z_model_offset = 0.0f;
    if (cfg.adhesion == AdhesionType::Raft && !layers.empty() && !layers[0].contours.empty()) {
        Paths64 base_fp = clip_union(layers[0].contours, supports.empty() ? Paths64{} : supports[0].body_region);
        Paths64 raft_poly = offset_paths_mm(base_fp, cfg.raft_expansion, Clipper2Lib::JoinType::Round, 2.0, 0.15);
        int rl = std::clamp(cfg.raft_layers, 1, 5);
        float z_cur = 0.0f;
        float rx = cfg.origin_center ? 0.0f : cfg.bed_size_x * 0.5f;
        float ry = cfg.origin_center ? 0.0f : cfg.bed_size_y * 0.5f;
        for (int r = 0; r < rl; ++r) {
            float rh = (r == 0) ? std::max(0.28f, cfg.first_layer_height) : cfg.layer_height;
            z_cur += rh;
            std::vector<ExtrusionPath> rpaths;
            double sp = (r == 0) ? (cfg.first_layer_width * 2.2) : pill_spacing_mm(cfg.top_solid_width, rh);
            generate_scanline_hatch(raft_poly, sp, (r & 1) ? 90.0 : 0.0, 0.0,
                                    false, true, FeatureType::SkirtBrim,
                                    (r == 0) ? cfg.first_layer_width * 1.25f : cfg.top_solid_width,
                                    cfg.speed_first_layer, 1.0f, 0, rpaths);
            LayerPerimeters empty_p;
            std::vector<ExtrusionPath> empty_s;
            raft_planned.push_back(plan_single_layer(
                static_cast<uint32_t>(r), z_cur, rh, raft_poly, empty_p,
                std::move(rpaths), empty_s, empty_s, rx, ry, cfg, rx, ry));
        }
        z_model_offset = z_cur + cfg.raft_z_gap;
    }

    // Adhesion paths for Layer 0 (Skirt / Brim)
    std::vector<ExtrusionPath> adhesion_l0;
    if (cfg.adhesion == AdhesionType::Skirt || cfg.adhesion == AdhesionType::Brim) {
        Paths64 sup0 = supports.empty() ? Paths64{} : clip_union(supports[0].body_region, supports[0].interface_region);
        adhesion_l0 = generate_adhesion_paths(layers[0].contours, sup0, cfg);
    }

    // Stage 7: Parallel Per-Layer Path Planning (Seam, Scarf, TSP, Combing, Arcs) + Motion & Cooling Planner
    auto t10 = Clock::now();
    std::vector<PlannedLayer> planned(L);
    const float init_x = cfg.origin_center ? 0.0f : cfg.bed_size_x * 0.5f;
    const float init_y = cfg.origin_center ? 0.0f : cfg.bed_size_y * 0.5f;

    auto plan_layer_job = [&](uint32_t, size_t i) {
        float ex = init_x, ey = init_y;
        const std::vector<ExtrusionPath> empty_adh;
        const auto& adh = (i == 0) ? adhesion_l0 : empty_adh;
        planned[i] = plan_single_layer(
            static_cast<uint32_t>(i + raft_planned.size()),
            layers[i].z_top + z_model_offset,
            layers[i].height,
            layers[i].contours,
            perims[i],
            std::move(fill_paths[i]),
            supports[i].paths,
            adh,
            init_x, init_y,
            cfg,
            ex, ey);
    };

    if (L >= 4) {
        pool->parallel_for(L, plan_layer_job, 1);
    } else {
        for (size_t i = 0; i < L; ++i) plan_layer_job(0, i);
    }

    // Prepend raft layers if active
    if (!raft_planned.empty()) {
        std::vector<PlannedLayer> combined;
        combined.reserve(raft_planned.size() + planned.size());
        for (auto& rp : raft_planned) combined.push_back(std::move(rp));
        for (auto& mp : planned) combined.push_back(std::move(mp));
        planned = std::move(combined);
    }

    // Run lookahead kinematics, volumetric flow throttling, and cooling slowdown
    run_motion_and_cooling_planner(planned, cfg, res.stats, pool);
    auto t11 = Clock::now();
    res.stats.timings.path_motion_plan_ms = elapsed_ms(t10, t11);

    // Stage 8: Ultra-Fast Parallel G-Code Formatting & Binary .ktp Packing
    auto t12 = Clock::now();
    if (cfg.emit_gcode) {
        res.gcode = serialize_gcode(planned, cfg, res.stats, pool);
        res.stats.gcode_bytes = res.gcode.size();
    }
    if (cfg.emit_binary_toolpath) {
        res.ktp_binary = pack_toolpath_ktp(planned, mesh, cfg, res.stats, pool);
        res.stats.ktp_bytes = res.ktp_binary.size();
    }
    auto t13 = Clock::now();
    res.stats.timings.gcode_serialize_ms = elapsed_ms(t12, t13);
    res.stats.timings.total_ms = elapsed_ms(t_start, t13);

    res.planned_layers = std::move(planned);
    res.ok = true;
    return res;
}

SliceResult slice_file(const std::string& path, const SlicerConfig& cfg) {
    ThreadPool pool(static_cast<uint32_t>(std::max(0, cfg.threads)));
    auto t0 = Clock::now();
    Mesh m = load_mesh_file(path, &pool);
    auto t1 = Clock::now();
    if (!m.ok) {
        SliceResult r;
        r.error = m.error;
        return r;
    }
    SliceResult r = slice_mesh(std::move(m), cfg, &pool);
    r.stats.timings.load_transform_ms += elapsed_ms(t0, t1);
    r.stats.timings.total_ms += elapsed_ms(t0, t1);
    return r;
}

SliceResult slice_memory(const uint8_t* data, size_t size, const std::string& name, const SlicerConfig& cfg) {
    ThreadPool pool(static_cast<uint32_t>(std::max(0, cfg.threads)));
    auto t0 = Clock::now();
    Mesh m = load_mesh_memory(data, size, name, &pool);
    auto t1 = Clock::now();
    if (!m.ok) {
        SliceResult r;
        r.error = m.error;
        return r;
    }
    SliceResult r = slice_mesh(std::move(m), cfg, &pool);
    r.stats.timings.load_transform_ms += elapsed_ms(t0, t1);
    r.stats.timings.total_ms += elapsed_ms(t0, t1);
    return r;
}

}  // namespace katman
