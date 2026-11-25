#include "test_common.hpp"
#include "core/engine.hpp"
#include "core/profiles.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace katman_test {
TestStats g_stats{};
}

namespace {

bool file_exists(const char* p) {
    std::ifstream f(p);
    return f.good();
}

void run_bench_case(const char* label, const std::vector<uint8_t>& stl_buf, const katman::SlicerConfig& cfg, int runs = 3) {
    double best_ms = 1e9;
    katman::SliceStats best{};
    for (int i = 0; i < runs; ++i) {
        auto r = katman::slice_memory(stl_buf.data(), stl_buf.size(), label, cfg);
        if (!r.ok) {
            std::fprintf(stderr, "BENCH FAIL %s: %s\n", label, r.error.c_str());
            return;
        }
        if (r.stats.timings.total_ms < best_ms) {
            best_ms = r.stats.timings.total_ms;
            best = r.stats;
        }
    }
    std::printf("%-26s | %6u tris | %3u ly | %6u mv | %6.1f ms [ld=%4.1f sl=%5.1f sf=%5.1f sp=%5.1f fl=%5.1f pl=%5.1f gc=%4.1f] | th=%u c=%u\n",
                label, best.tri_count, best.layer_count, best.total_moves, best_ms,
                best.timings.load_transform_ms, best.timings.slice_stitch_ms,
                best.timings.surfaces_overhang_ms, best.timings.supports_ms,
                best.timings.perimeters_infill_ms, best.timings.path_motion_plan_ms,
                best.timings.gcode_serialize_ms, best.threads_used, best.cached_layers);
}

void run_bench_file(const char* label, const char* path, const katman::SlicerConfig& cfg, int runs = 3) {
    if (!file_exists(path)) return;
    double best_ms = 1e9;
    katman::SliceStats best{};
    for (int i = 0; i < runs; ++i) {
        auto r = katman::slice_file(path, cfg);
        if (!r.ok) {
            std::fprintf(stderr, "BENCH FAIL %s: %s\n", label, r.error.c_str());
            return;
        }
        if (r.stats.timings.total_ms < best_ms) {
            best_ms = r.stats.timings.total_ms;
            best = r.stats;
        }
    }
    std::printf("%-26s | %6u tris | %3u ly | %6u mv | %6.1f ms [ld=%4.1f sl=%5.1f sf=%5.1f sp=%5.1f fl=%5.1f pl=%5.1f gc=%4.1f] | th=%u c=%u\n",
                label, best.tri_count, best.layer_count, best.total_moves, best_ms,
                best.timings.load_transform_ms, best.timings.slice_stitch_ms,
                best.timings.surfaces_overhang_ms, best.timings.supports_ms,
                best.timings.perimeters_infill_ms, best.timings.path_motion_plan_ms,
                best.timings.gcode_serialize_ms, best.threads_used, best.cached_layers);
}

}  // namespace

int main() {
    std::printf("=== KATMAN SLICER PERFORMANCE BENCHMARK SUITE ===\n");
    std::printf("%-26s | %11s | %6s | %9s | %9s [breakdown ms]                                         | info\n",
                "Benchmark", "Triangles", "Layers", "Moves", "Total");
    std::printf("---------------------------------------------------------------------------------------------------------------------------------\n");

    // 1. 40mm High-Poly Sphere (100,000 triangles!) — Gyroid + 4 threads vs 1 thread
    auto sphere_100k = katman_test::build_binary_stl(katman_test::make_sphere(25.0f, 224, 224));
    {
        katman::SlicerConfig cfg{};
        cfg.infill_pattern = katman::InfillPattern::Gyroid;
        cfg.threads = 1;
        run_bench_case("Sphere 100k Gyroid (1T)", sphere_100k, cfg, 2);
        cfg.threads = 0;
        run_bench_case("Sphere 100k Gyroid (4T)", sphere_100k, cfg, 3);
        cfg.infill_pattern = katman::InfillPattern::Rectilinear;
        run_bench_case("Sphere 100k Rectil (4T)", sphere_100k, cfg, 3);
    }

    // 2. High-Poly Cylinder (prismatic CAD -> tests identical-layer cache speedup!)
    auto cyl_256 = katman_test::build_binary_stl(katman_test::make_hollow_tube(35.0f, 18.0f, 60.0f, 256));
    {
        katman::SlicerConfig cfg{};
        cfg.infill_pattern = katman::InfillPattern::Grid;
        cfg.enable_layer_cache = false;
        run_bench_case("Tube 60mm NoCache (4T)", cyl_256, cfg, 3);
        cfg.enable_layer_cache = true;
        run_bench_case("Tube 60mm Cached  (4T)", cyl_256, cfg, 3);
    }

    // 3. Real User CAD STLs (P-51 Wing, Fuselage, Propeller, Fender, Speedometer!)
    const struct { const char* name; const char* p1; const char* p2; } real_files[] = {
        {"P51 Wing_L (75k)", "/test_stls/p51/Wing_L.stl", "/home/kasa/mnt/toshi/3d/p51/Wing_L.stl"},
        {"P51 Fuselage_Rear", "/test_stls/p51/Fuselage_Rear.stl", "/home/kasa/mnt/toshi/3d/p51/Fuselage_Rear.stl"},
        {"P51 Propeller", "/test_stls/p51/Propeller.stl", "/home/kasa/mnt/toshi/3d/p51/Propeller.stl"},
        {"Camurluk Izomer", "/test_stls/çamurluk/Izomer_Camurluk.stl", "/home/kasa/mnt/toshi/3d/çamurluk/Izomer_Camurluk.stl"},
        {"Speedo AnaGovde", "/test_stls/bisiklet_hiz_gostergesi_v4/Parca1_AnaGovde.stl", "/home/kasa/mnt/toshi/3d/bisiklet_hiz_gostergesi_v4/Parca1_AnaGovde.stl"},
    };
    for (const auto& rf : real_files) {
        const char* path = file_exists(rf.p1) ? rf.p1 : rf.p2;
        katman::SlicerConfig cfg{};
        katman::apply_printer_preset(cfg, "bambu_p1s");
        cfg.infill_pattern = katman::InfillPattern::Gyroid;
        run_bench_file(rf.name, path, cfg, 3);
    }

    // 4. Support stress benchmark (NormalGrid vs OrganicTree)
    auto arch = katman_test::build_binary_stl(katman_test::make_bridge_overhang_arch());
    {
        katman::SlicerConfig cfg{};
        cfg.support_type = katman::SupportType::NormalGrid;
        run_bench_case("Arch + Grid Support", arch, cfg, 3);
        cfg.support_type = katman::SupportType::OrganicTree;
        run_bench_case("Arch + Tree Support", arch, cfg, 3);
    }

    return 0;
}
