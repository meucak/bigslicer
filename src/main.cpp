#include "core/config.hpp"
#include "core/engine.hpp"
#include "core/profiles.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifdef KATMAN_HAS_VIEWER
#include "viewer/viewer_bridge.hpp"
#endif

namespace {

void print_usage() {
    std::printf(
        "BigSlicer 1.0.0 — Ultra-Optimized 3D Printing Slicer (C++20 / Clipper2 / Multi-Threaded)\n\n"
        "Usage:\n"
        "  bigslicer                       Launch BigSlicer 3D Studio (WebGL2)\n"
        "  bigslicer slice <model.stl|obj> -o <out.gcode> [options]\n"
        "      --ktp <out.ktp>             Also write binary 3D toolpath stream\n"
        "      --stats-json <out.json>     Write detailed JSON stats & profiling\n"
        "      --config <file.json>        Load JSON slicer config\n"
        "      --config-json '<json>'      Inline JSON config overrides\n"
        "      --printer <id>              generic_220|ender3_v3|prusa_mk4s|bambu_p1s|voron_24_300|creality_k1c|prusa_mini\n"
        "      --material <id>             pla|pla_hs|petg|abs|asa|tpu95a\n"
        "      --quality <id>              ultra_008|quality_012|standard_020|draft_028|strong_020\n"
        "      --layer-height <mm>         Layer height (e.g. 0.20)\n"
        "      --adaptive                  Enable Dolenc-Makela adaptive layer height\n"
        "      --walls <N>                 Perimeter count (1..12)\n"
        "      --infill <pct>              Infill density 0..100\n"
        "      --pattern <name>            rectilinear|grid|triangles|gyroid|cubic|honeycomb|concentric|lightning\n"
        "      --support <type>            none|grid|tree\n"
        "      --adhesion <type>           none|skirt|brim|raft\n"
        "      --seam <mode>               aligned|sharpest|rear|nearest|random\n"
        "      --scarf                     Enable scarf joint seam\n"
        "      --vase                      Enable spiral vase mode\n"
        "      --ironing                   Enable top surface ironing\n"
        "      --flavor <fw>               marlin2|klipper|rrf|bambu\n"
        "      --threads <N>               Worker threads (0 = auto)\n"
        "      --no-gcode                  Skip G-code formatting (fast preview/bench)\n"
        "  bigslicer info <model.stl|obj>  Inspect mesh geometry, volume & overhangs\n"
        "  bigslicer bench <model.stl> [--runs N]  Benchmark full multi-threaded slicing pipeline\n"
        "  bigslicer profiles              Print built-in printer/material/quality presets JSON\n"
        "  bigslicer view <model.stl>      Launch native OpenGL 3.3 3D STL Viewer\n"
        "  bigslicer --version | --help\n");
}

bool read_text_file(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool write_binary_file(const std::string& path, const void* data, size_t size) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (size > 0) f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    return f.good();
}

void parse_slice_flags(int argc, char** argv, int start_idx,
                       std::string& in_file,
                       std::string& out_gcode,
                       std::string& out_ktp,
                       std::string& out_stats,
                       katman::SlicerConfig& cfg,
                       int& bench_runs) {
    for (int i = start_idx; i < argc; ++i) {
        const char* a = argv[i];
        auto need = [&](int n) { return i + n < argc; };
        if ((std::strcmp(a, "-o") == 0 || std::strcmp(a, "--output") == 0) && need(1)) {
            out_gcode = argv[++i];
        } else if (std::strcmp(a, "--ktp") == 0 && need(1)) {
            out_ktp = argv[++i];
        } else if (std::strcmp(a, "--stats-json") == 0 && need(1)) {
            out_stats = argv[++i];
        } else if (std::strcmp(a, "--config") == 0 && need(1)) {
            std::string js, err;
            if (read_text_file(argv[++i], js)) katman::parse_config_json(js, cfg, err);
        } else if (std::strcmp(a, "--config-json") == 0 && need(1)) {
            std::string err;
            katman::parse_config_json(argv[++i], cfg, err);
        } else if (std::strcmp(a, "--printer") == 0 && need(1)) {
            katman::apply_printer_preset(cfg, argv[++i]);
        } else if (std::strcmp(a, "--material") == 0 && need(1)) {
            katman::apply_material_preset(cfg, argv[++i]);
        } else if (std::strcmp(a, "--quality") == 0 && need(1)) {
            katman::apply_quality_preset(cfg, argv[++i]);
        } else if (std::strcmp(a, "--layer-height") == 0 && need(1)) {
            cfg.layer_height = static_cast<float>(std::atof(argv[++i]));
        } else if (std::strcmp(a, "--adaptive") == 0) {
            cfg.adaptive_layer_height = true;
        } else if (std::strcmp(a, "--walls") == 0 && need(1)) {
            cfg.wall_count = std::atoi(argv[++i]);
        } else if (std::strcmp(a, "--infill") == 0 && need(1)) {
            cfg.infill_density = static_cast<float>(std::atof(argv[++i]));
        } else if (std::strcmp(a, "--pattern") == 0 && need(1)) {
            std::string err;
            katman::parse_config_json(std::string("{\"infill_pattern\":\"") + argv[++i] + "\"}", cfg, err);
        } else if (std::strcmp(a, "--support") == 0 && need(1)) {
            std::string err;
            katman::parse_config_json(std::string("{\"support_type\":\"") + argv[++i] + "\"}", cfg, err);
        } else if (std::strcmp(a, "--adhesion") == 0 && need(1)) {
            std::string err;
            katman::parse_config_json(std::string("{\"adhesion\":\"") + argv[++i] + "\"}", cfg, err);
        } else if (std::strcmp(a, "--seam") == 0 && need(1)) {
            std::string err;
            katman::parse_config_json(std::string("{\"seam_position\":\"") + argv[++i] + "\"}", cfg, err);
        } else if (std::strcmp(a, "--scarf") == 0) {
            cfg.scarf_seam = true;
        } else if (std::strcmp(a, "--vase") == 0) {
            cfg.spiral_vase = true;
        } else if (std::strcmp(a, "--ironing") == 0) {
            cfg.ironing = true;
        } else if (std::strcmp(a, "--flavor") == 0 && need(1)) {
            std::string err;
            katman::parse_config_json(std::string("{\"flavor\":\"") + argv[++i] + "\"}", cfg, err);
        } else if (std::strcmp(a, "--threads") == 0 && need(1)) {
            cfg.threads = std::atoi(argv[++i]);
        } else if (std::strcmp(a, "--no-cache") == 0) {
            cfg.enable_layer_cache = false;
        } else if (std::strcmp(a, "--no-gcode") == 0) {
            cfg.emit_gcode = false;
        } else if (std::strcmp(a, "--runs") == 0 && need(1)) {
            bench_runs = std::clamp(std::atoi(argv[++i]), 1, 50);
        } else if (a[0] != '-') {
            if (in_file.empty()) in_file = a;
        }
    }
}

int cmd_info(const std::string& path) {
    katman::ThreadPool pool(0);
    katman::Mesh m = katman::load_mesh_file(path, &pool);
    if (!m.ok) {
        std::fprintf(stderr, "Error: %s\n", m.error.c_str());
        return 1;
    }
    std::printf("File       : %s (%s, %.2f MB)\n", m.name.c_str(),
                m.is_binary ? "Binary" : "ASCII", static_cast<double>(m.file_bytes) / (1024.0 * 1024.0));
    std::printf("Triangles  : %u (degenerate skipped: %u)\n",
                static_cast<uint32_t>(m.tris.size()), m.degenerate_skipped);
    std::printf("Dimensions : %.2f x %.2f x %.2f mm\n",
                m.bbox_extent.x, m.bbox_extent.y, m.bbox_extent.z);
    std::printf("Volume     : %.3f cm^3  |  Area: %.2f cm^2\n", m.volume_mm3 * 0.001, m.area_mm2 * 0.01);
    std::printf("Overhangs  : %.1f%% faces > 45 deg\n", m.overhang_face_pct);
    return 0;
}

int cmd_slice(const std::string& in_file,
              const std::string& out_gcode,
              const std::string& out_ktp,
              const std::string& out_stats,
              katman::SlicerConfig cfg) {
    if (in_file.empty()) {
        std::fprintf(stderr, "Error: missing input STL/OBJ file\n");
        return 1;
    }
    cfg.emit_gcode = cfg.emit_gcode && (!out_gcode.empty() || out_ktp.empty());
    cfg.emit_binary_toolpath = !out_ktp.empty();

    katman::SliceResult r = katman::slice_file(in_file, cfg);
    if (!r.ok) {
        std::fprintf(stderr, "Error: %s\n", r.error.c_str());
        return 1;
    }

    if (!out_gcode.empty()) {
        if (!write_binary_file(out_gcode, r.gcode.data(), r.gcode.size())) {
            std::fprintf(stderr, "Error: failed to write G-code to %s\n", out_gcode.c_str());
            return 1;
        }
    }
    if (!out_ktp.empty()) {
        if (!write_binary_file(out_ktp, r.ktp_binary.data(), r.ktp_binary.size())) {
            std::fprintf(stderr, "Error: failed to write KTP to %s\n", out_ktp.c_str());
            return 1;
        }
    }
    std::string stats_js = katman::stats_to_json(r.stats, cfg);
    if (!out_stats.empty()) {
        if (!write_binary_file(out_stats, stats_js.data(), stats_js.size())) {
            std::fprintf(stderr, "Error: failed to write stats JSON to %s\n", out_stats.c_str());
            return 1;
        }
    }

    if (out_stats == "-") {
        std::printf("%s\n", stats_js.c_str());
    } else {
        int hrs = static_cast<int>(r.stats.print_time_sec / 3600.0);
        int mins = static_cast<int>(std::fmod(r.stats.print_time_sec, 3600.0) / 60.0);
        int secs = static_cast<int>(std::fmod(r.stats.print_time_sec, 60.0));
        std::printf(
            "BigSlicer sliced %s in %.2f ms (%u threads, %u cached layers)\n"
            "  Geometry : %u tris | %.1f x %.1f x %.1f mm | %u layers\n"
            "  Toolpath : %u moves (%u extrude, %u travel, %u arcs, %u retracts)\n"
            "  Print    : %dh %dm %ds | %.2f m (%.1f g) | max flow %.1f mm^3/s\n"
            "  Stages   : load=%.1fms slice=%.1fms surf=%.1fms sup=%.1fms fill=%.1fms plan=%.1fms gcode=%.1fms\n",
            in_file.c_str(), r.stats.timings.total_ms, r.stats.threads_used, r.stats.cached_layers,
            r.stats.tri_count, r.stats.model_size_x, r.stats.model_size_y, r.stats.model_size_z, r.stats.layer_count,
            r.stats.total_moves, r.stats.extrusion_moves, r.stats.travel_moves, r.stats.arc_moves, r.stats.retract_count,
            hrs, mins, secs, r.stats.filament_mm * 0.001, r.stats.filament_g, r.stats.max_volumetric_flow_mm3s,
            r.stats.timings.load_transform_ms, r.stats.timings.slice_stitch_ms, r.stats.timings.surfaces_overhang_ms,
            r.stats.timings.supports_ms, r.stats.timings.perimeters_infill_ms,
            r.stats.timings.path_motion_plan_ms, r.stats.timings.gcode_serialize_ms);
    }
    return 0;
}

int cmd_bench(const std::string& in_file, katman::SlicerConfig cfg, int runs) {
    if (in_file.empty()) {
        std::fprintf(stderr, "Usage: bigslicer bench <model.stl> [--runs N]\n");
        return 1;
    }
    double min_ms = 1e9, sum_ms = 0.0, max_ms = 0.0;
    katman::SliceStats best{};
    for (int i = 0; i < runs; ++i) {
        katman::SliceResult r = katman::slice_file(in_file, cfg);
        if (!r.ok) {
            std::fprintf(stderr, "Error: %s\n", r.error.c_str());
            return 1;
        }
        if (r.stats.timings.total_ms < min_ms) {
            min_ms = r.stats.timings.total_ms;
            best = r.stats;
        }
        max_ms = std::max(max_ms, r.stats.timings.total_ms);
        sum_ms += r.stats.timings.total_ms;
    }
    std::printf("Bench (%d runs): %s [%u tris, %u layers, %u moves, %.2f MB G-code]\n",
                runs, in_file.c_str(), best.tri_count, best.layer_count, best.total_moves,
                static_cast<double>(best.gcode_bytes) / (1024.0 * 1024.0));
    std::printf("  Total    : min %.2f ms | avg %.2f ms | max %.2f ms (%u threads, %u cached)\n",
                min_ms, sum_ms / runs, max_ms, best.threads_used, best.cached_layers);
    std::printf("  Stages   : load=%.2f slice=%.2f surf=%.2f sup=%.2f fill=%.2f plan=%.2f gcode=%.2f ms\n",
                best.timings.load_transform_ms, best.timings.slice_stitch_ms, best.timings.surfaces_overhang_ms,
                best.timings.supports_ms, best.timings.perimeters_infill_ms,
                best.timings.path_motion_plan_ms, best.timings.gcode_serialize_ms);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 0;
    }
    const char* cmd = argv[1];
    if (std::strcmp(cmd, "--help") == 0 || std::strcmp(cmd, "-h") == 0) {
        print_usage();
        return 0;
    }
    if (std::strcmp(cmd, "--version") == 0 || std::strcmp(cmd, "-v") == 0) {
        std::printf("bigslicer 1.0.0 (C++20 / Clipper2 1.5.2 / Multi-Threaded)\n");
        return 0;
    }
    if (std::strcmp(cmd, "profiles") == 0) {
        std::printf("%s\n", katman::presets_to_json().c_str());
        return 0;
    }

    katman::SlicerConfig cfg{};
    std::string in_file, out_gcode, out_ktp, out_stats;
    int bench_runs = 5;

    if (std::strcmp(cmd, "info") == 0) {
        if (argc < 3) { std::fprintf(stderr, "Usage: bigslicer info <model.stl>\n"); return 1; }
        return cmd_info(argv[2]);
    }
    if (std::strcmp(cmd, "slice") == 0) {
        parse_slice_flags(argc, argv, 2, in_file, out_gcode, out_ktp, out_stats, cfg, bench_runs);
        return cmd_slice(in_file, out_gcode, out_ktp, out_stats, cfg);
    }
    if (std::strcmp(cmd, "bench") == 0) {
        parse_slice_flags(argc, argv, 2, in_file, out_gcode, out_ktp, out_stats, cfg, bench_runs);
        return cmd_bench(in_file, cfg, bench_runs);
    }
    if (std::strcmp(cmd, "view") == 0) {
#ifdef KATMAN_HAS_VIEWER
        std::string file = (argc >= 3) ? argv[2] : "";
        return katman::launch_native_viewer(file);
#else
        std::fprintf(stderr, "Native X11/OpenGL viewer was disabled at build time.\n");
        return 1;
#endif
    }

    // Convenience: if first arg is an existing .stl/.obj file, launch native viewer if built, else info
#ifdef KATMAN_HAS_VIEWER
    if (cmd[0] != '-') {
        return katman::launch_native_viewer(cmd);
    }
#endif
    std::fprintf(stderr, "Unknown command: %s\n", cmd);
    print_usage();
    return 1;
}
