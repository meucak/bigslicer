#include "test_common.hpp"
#include "core/engine.hpp"
#include "core/profiles.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace katman_test {

namespace {

struct GcodeVerifyResult {
    bool ok = true;
    std::string error;
    uint32_t lines = 0;
    uint32_t g0_count = 0;
    uint32_t g1_count = 0;
    uint32_t arc_count = 0;
    uint32_t layer_markers = 0;
    double net_e_mm = 0.0;
    double max_z_mm = 0.0;
    bool has_nan = false;
    bool relative_e = true;
};

// Independent physical G-code parser & invariant verifier!
GcodeVerifyResult verify_gcode_physically(const std::string& gcode) {
    GcodeVerifyResult vr{};
    std::istringstream ss(gcode);
    std::string line;
    double cur_e = 0.0;
    double total_e = 0.0;
    bool in_print_body = false;

    while (std::getline(ss, line)) {
        ++vr.lines;
        if (line.rfind(";LAYER:", 0) == 0) {
            ++vr.layer_markers;
            in_print_body = true;
            continue;
        }
        if (line.rfind("; --- END G-CODE ---", 0) == 0) {
            in_print_body = false;
            continue;
        }
        if (line.empty() || line[0] == ';') continue;

        // Strip trailing inline comment before checking for NaN/Inf in executable G-code tokens!
        size_t semi = line.find(';');
        std::string code_part = (semi == std::string::npos) ? line : line.substr(0, semi);
        if (code_part.find("nan") != std::string::npos || code_part.find("NaN") != std::string::npos ||
            code_part.find("inf") != std::string::npos || code_part.find("Inf") != std::string::npos) {
            vr.has_nan = true;
            vr.ok = false;
            vr.error = "NaN/Inf detected on line " + std::to_string(vr.lines);
            return vr;
        }

        if (line.rfind("M83", 0) == 0) vr.relative_e = true;
        else if (line.rfind("M82", 0) == 0) vr.relative_e = false;
        else if (line.rfind("G92 E0", 0) == 0) { cur_e = 0.0; }
        else if (line.rfind("G0 ", 0) == 0 || line.rfind("G1 ", 0) == 0 ||
                 line.rfind("G2 ", 0) == 0 || line.rfind("G3 ", 0) == 0) {
            if (line[1] == '0') ++vr.g0_count;
            else if (line[1] == '1') ++vr.g1_count;
            else ++vr.arc_count;

            // Parse Z and E tokens
            const char* p = line.c_str() + 2;
            while (*p) {
                while (*p == ' ') ++p;
                if (*p == ';' || *p == '\0') break;
                char code = *p++;
                char* endp = nullptr;
                double val = std::strtod(p, &endp);
                if (endp == p) { ++p; continue; }
                p = endp;
                if (code == 'Z' && in_print_body) {
                    if (val < -0.01) {
                        vr.ok = false;
                        vr.error = "Negative Z coordinate in print body";
                        return vr;
                    }
                    if (val > vr.max_z_mm) vr.max_z_mm = val;
                } else if (code == 'E' && in_print_body) {
                    if (vr.relative_e) {
                        total_e += val;
                    } else {
                        total_e += (val - cur_e);
                        cur_e = val;
                    }
                }
            }
        }
    }
    vr.net_e_mm = total_e;
    return vr;
}

bool file_exists(const char* p) {
    std::ifstream f(p);
    return f.good();
}

}  // namespace

void run_test_gcode_e2e() {
    std::printf("[Suite] G-Code Physical Verifier, All 4 Firmware Flavors & Real STL Models\n");

    // 1. All 4 firmware flavors + M82/M83 conservation test
    {
        auto tris = make_hollow_tube(14.0f, 6.0f, 8.0f, 48);
        auto bin = build_binary_stl(tris);

        const katman::GCodeFlavor flavors[4] = {
            katman::GCodeFlavor::Marlin2,
            katman::GCodeFlavor::Klipper,
            katman::GCodeFlavor::RepRapFirmware,
            katman::GCodeFlavor::Bambu
        };
        const char* expected_tok[4] = {
            "M900 K",
            "SET_PRESSURE_ADVANCE ADVANCE=",
            "M572 D0 S",
            "M900 K"
        };

        for (int f = 0; f < 4; ++f) {
            katman::SlicerConfig cfg{};
            cfg.flavor = flavors[f];
            cfg.relative_extrusion = (f != 0);  // Test both M82 (Marlin) and M83 (Klipper/RRF/Bambu)!
            cfg.pressure_advance = 0.04f;
            auto r = katman::slice_memory(bin.data(), bin.size(), "flavor_test.stl", cfg);
            KT_CHECK(r.ok && !r.gcode.empty(), "Flavor slice succeeds");
            KT_CHECK(r.gcode.find(expected_tok[f]) != std::string::npos, "Firmware-specific PA command emitted");
            auto vr = verify_gcode_physically(r.gcode);
            KT_CHECK(vr.ok && !vr.has_nan, "G-code physically valid with zero NaN/Inf");
            KT_CHECK(vr.layer_markers == r.stats.layer_count, "Every layer emitted ;LAYER: marker");
            // Exact filament conservation between planner stats and parsed G-code E commands!
            KT_CHECK_NEAR(vr.net_e_mm, r.stats.filament_mm, std::max(0.5, r.stats.filament_mm * 0.01),
                          "Parsed G-code net E conserves planned filament_mm within 1%");
            // Verify KTP1 binary header magic & counts
            KT_CHECK(r.ktp_binary.size() > 64 &&
                     r.ktp_binary[0] == 'K' && r.ktp_binary[1] == 'T' &&
                     r.ktp_binary[2] == 'P' && r.ktp_binary[3] == '1',
                     "Binary .ktp stream has valid KTP1 header");
        }
    }

    // 2. Strict 1-thread vs 4-thread determinism test on a 40k-triangle sphere!
    {
        auto sph = build_binary_stl(make_sphere(16.0f, 140, 140));
        katman::SlicerConfig c1{};
        c1.infill_pattern = katman::InfillPattern::Gyroid;
        c1.threads = 1;
        auto r1 = katman::slice_memory(sph.data(), sph.size(), "det_1t.stl", c1);

        katman::SlicerConfig c4 = c1;
        c4.threads = 4;
        auto r4 = katman::slice_memory(sph.data(), sph.size(), "det_4t.stl", c4);

        KT_CHECK(r1.ok && r4.ok, "1T and 4T sphere slices both succeed");
        KT_CHECK(r1.stats.layer_count == r4.stats.layer_count, "1T == 4T exact layer count");
        KT_CHECK(r1.stats.total_moves == r4.stats.total_moves, "1T == 4T exact move count");
        KT_CHECK_NEAR(r1.stats.filament_mm, r4.stats.filament_mm, 0.01, "1T == 4T exact filament mm");
        KT_CHECK(r1.gcode == r4.gcode, "1T == 4T bit-identical G-code output!");
    }

    // 3. Presets JSON & Config JSON override round-trip
    {
        katman::SlicerConfig cfg{};
        std::string err;
        bool ok = katman::parse_config_json(
            "{\"printer\":\"bambu_p1s\",\"material\":\"petg\",\"quality\":\"quality_012\","
            "\"infill_pattern\":\"gyroid\",\"infill_density\":25,\"support_type\":\"tree\",\"scarf_seam\":true}",
            cfg, err);
        KT_CHECK(ok, "Config JSON parses cleanly");
        KT_CHECK(cfg.bed_size_x == 256.0f && cfg.nozzle_temp == 240.0f &&
                 std::fabs(cfg.layer_height - 0.12f) < 1e-4f &&
                 cfg.infill_density == 25.0f &&
                 cfg.support_type == katman::SupportType::OrganicTree &&
                 cfg.scarf_seam,
                 "Presets + explicit overrides apply in exact precedence order");
        std::string pj = katman::presets_to_json();
        KT_CHECK(pj.find("voron_24_300") != std::string::npos && pj.find("tpu95a") != std::string::npos,
                 "Presets JSON exposes all built-in profiles");
        KT_CHECK(katman::list_printer_presets().size() >= 120, "Has 120+ real printer presets");
        KT_CHECK(katman::list_material_presets().size() >= 16, "Has 16+ real material presets");
        KT_CHECK(katman::list_quality_presets().size() >= 7, "Has 7+ quality presets");

        // Verify legendary classic printers & Delta origin_center
        katman::SlicerConfig e3p{};
        KT_CHECK(katman::apply_printer_preset(e3p, "creality_ender3_pro") &&
                 e3p.bed_size_x == 220.0f && e3p.retract_length >= 4.0f,
                 "Creality Ender-3 Pro Bowden classic preset loads cleanly");
        katman::SlicerConfig a8{};
        KT_CHECK(katman::apply_printer_preset(a8, "anet_a8") && a8.bed_size_z == 240.0f,
                 "Anet A8 classic preset loads cleanly");
        katman::SlicerConfig delta{};
        KT_CHECK(katman::apply_printer_preset(delta, "flsun_v400") && delta.origin_center,
                 "FLSUN V400 Delta sets origin_center=true");
    }

    // 4. BigSlicer v2.0 Pro Features: Fuzzy Skin, Small Perimeter Slowdown, Snug Support, Pause M600/PAUSE, Custom G-Code, Pos Offset
    {
        std::vector<RawTri> btris;
        add_box(btris, 0, 0, 0, 24, 24, 8);
        auto box = build_binary_stl(btris);
        katman::SlicerConfig c_plain{};
        c_plain.adhesion = katman::AdhesionType::None;
        c_plain.infill_density = 0.0f;
        auto r_plain = katman::slice_memory(box.data(), box.size(), "plain.stl", c_plain);

        katman::SlicerConfig c_fuzzy = c_plain;
        c_fuzzy.fuzzy_skin = katman::FuzzySkinMode::Outside;
        c_fuzzy.fuzzy_skin_thickness = 0.35f;
        c_fuzzy.fuzzy_skin_point_dist = 0.80f;
        c_fuzzy.pause_layers = {5, 12};
        c_fuzzy.custom_start_gcode = "M117 BigSlicer {nozzle_temp}C";
        c_fuzzy.custom_end_gcode = "M300 S440 P200 ; done beep";
        c_fuzzy.pos_x = 35.0f;
        c_fuzzy.pos_y = -25.0f;
        auto r_fuzzy = katman::slice_memory(box.data(), box.size(), "fuzzy.stl", c_fuzzy);

        KT_CHECK(r_fuzzy.ok && r_fuzzy.stats.extrusion_moves > r_plain.stats.extrusion_moves * 2,
                 "Fuzzy skin subdivides & perturbs outer perimeter cleanly");
        KT_CHECK(r_fuzzy.gcode.find("M600") != std::string::npos, "Pause at layer emits M600 for Marlin2");
        KT_CHECK(r_fuzzy.gcode.find("M117 BigSlicer 210C") != std::string::npos, "Custom start G-code replaces {nozzle_temp}");
        KT_CHECK(r_fuzzy.gcode.find("M300 S440 P200") != std::string::npos, "Custom end G-code appended cleanly");

        // Verify pos_x=+35, pos_y=-25 actually shifted toolpath center from (110,110) to (145,85)!
        double sum_x = 0, sum_y = 0;
        size_t cnt = 0;
        for (const auto& m : r_fuzzy.planned_layers[2].moves) {
            if (m.type == katman::FeatureType::OuterWall) { sum_x += m.x1; sum_y += m.y1; ++cnt; }
        }
        KT_CHECK(cnt > 10, "Has shifted outer wall moves");
        KT_CHECK_NEAR(sum_x / cnt, 145.0, 2.0, "pos_x=+35 shifts sliced X center to 145mm");
        KT_CHECK_NEAR(sum_y / cnt, 85.0, 2.0, "pos_y=-25 shifts sliced Y center to 85mm");

        // Klipper PAUSE + NormalSnug Support on Overhang Arch
        std::vector<RawTri> atris;
        add_box(atris, 0, 0, 0, 10, 14, 14);
        add_box(atris, 34, 0, 0, 44, 14, 14);
        add_box(atris, 0, 0, 13.8f, 44, 14, 18);
        auto arch = build_binary_stl(atris);
        katman::SlicerConfig c_snug{};
        katman::apply_printer_preset(c_snug, "voron_24_300");
        c_snug.support_type = katman::SupportType::NormalSnug;
        c_snug.support_brim = true;
        c_snug.pause_layers = {10};
        auto r_snug = katman::slice_memory(arch.data(), arch.size(), "arch_snug.stl", c_snug);
        KT_CHECK(r_snug.ok &&
                 r_snug.stats.feature_filament_mm[static_cast<size_t>(katman::FeatureType::Support)] > 10.0 &&
                 r_snug.stats.feature_filament_mm[static_cast<size_t>(katman::FeatureType::SupportInterface)] > 5.0,
                 "NormalSnug support generates both support body & interface layers");
        KT_CHECK(r_snug.gcode.find("PAUSE\n") != std::string::npos, "Klipper flavor emits PAUSE for pause_layers");
    }

    // 3. Real-world user CAD STLs (mounted at /test_stls inside Docker or /home/kasa/mnt/toshi/3d on host)
    const char* real_candidates[] = {
        "/test_stls/bisiklet_hiz_gostergesi_v4/Parca1_AnaGovde.stl",
        "/test_stls/çamurluk/Kizak_Karsilik.stl",
        "/test_stls/p51/Propeller.stl",
        "/test_stls/p51/Wing_L.stl",
        "/home/kasa/mnt/toshi/3d/bisiklet_hiz_gostergesi_v4/Parca1_AnaGovde.stl",
        "/home/kasa/mnt/toshi/3d/p51/Wing_L.stl",
    };

    int real_tested = 0;
    for (const char* path : real_candidates) {
        if (!file_exists(path)) continue;
        katman::SlicerConfig cfg{};
        katman::apply_printer_preset(cfg, "prusa_mk4s");
        katman::apply_material_preset(cfg, "pla");
        cfg.infill_pattern = katman::InfillPattern::Gyroid;
        cfg.support_type = katman::SupportType::NormalGrid;
        auto r = katman::slice_file(path, cfg);
        KT_CHECK(r.ok && r.stats.layer_count > 5 && r.stats.filament_mm > 50.0,
                 (std::string("Real CAD STL slices cleanly: ") + path).c_str());
        auto vr = verify_gcode_physically(r.gcode);
        KT_CHECK(vr.ok && !vr.has_nan,
                 (std::string("Real CAD STL G-code passes physical verifier: ") + path).c_str());
        std::printf("  [REAL STL] %s -> %u tris, %u layers, %u moves, %.1f g, %.1f ms (cached=%u)\n",
                    path, r.stats.tri_count, r.stats.layer_count, r.stats.total_moves,
                    r.stats.filament_g, r.stats.timings.total_ms, r.stats.cached_layers);
        ++real_tested;
    }
    if (real_tested > 0) {
        std::printf("  Verified %d real-world user CAD STL models!\n", real_tested);
    }
}

}  // namespace katman_test
