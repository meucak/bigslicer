#include "test_common.hpp"
#include "core/engine.hpp"
#include "core/geometry2d.hpp"
#include "core/infill.hpp"
#include "core/perimeters.hpp"

#include <cmath>
#include <cstdio>

namespace katman_test {

void run_test_perimeters_infill() {
    std::printf("[Suite] Flow Math, Perimeters, Arachne Thin Walls & 8 Infill Patterns\n");

    // 1. Exact Slic3r stadium/pill cross-section & spacing math
    {
        double area = katman::pill_area_mm2(0.42, 0.20);
        // A = 0.20*(0.42 - 0.20) + pi*(0.20^2)/4 = 0.044 + 0.0314159 = 0.0754159
        KT_CHECK_NEAR(area, 0.0754159265, 1e-6, "Slic3r pill cross-sectional area exact");
        double sp = katman::pill_spacing_mm(0.42, 0.20);
        // S = 0.42 - 0.20*(1 - pi/4) = 0.42 - 0.04292037 = 0.3770796
        KT_CHECK_NEAR(sp, 0.3770796327, 1e-6, "Slic3r pill overlap spacing exact");
        double e = katman::extrusion_e_mm(100.0, 0.42, 0.20, 1.75, 1.0, false);
        // Volume = 7.54159 mm^3, fil area = pi*(0.875)^2 = 2.40528 mm^2 -> E = 3.13543 mm
        KT_CHECK_NEAR(e, 3.13543, 1e-3, "Exact 100mm extrusion E length matches Slic3r formula");
    }

    // 2. Arachne variable-width thin-wall rescue on a 0.65mm narrow blade (too thin for 2x0.42=0.84mm walls!)
    {
        std::vector<RawTri> tris;
        add_box(tris, 0, 0, 0, 20.0f, 0.65f, 4.0f);  // 0.65mm thin fin!
        auto bin = build_binary_stl(tris);
        katman::SlicerConfig cfg{};
        cfg.outer_wall_width = 0.42f;
        cfg.arachne_thin_walls = true;
        cfg.adhesion = katman::AdhesionType::None;
        auto r = katman::slice_memory(bin.data(), bin.size(), "thin_fin.stl", cfg);
        KT_CHECK(r.ok && r.stats.extrusion_moves > 10, "0.65mm thin fin produces printable extrusions");
        KT_CHECK(r.stats.filament_mm > 5.0, "Thin fin extrudes positive filament");
    }

    // 3. All 8 infill patterns on a 30x30x8mm block produce valid sparse infill!
    {
        std::vector<RawTri> tris;
        add_box(tris, 0, 0, 0, 30, 30, 8);
        auto bin = build_binary_stl(tris);

        const katman::InfillPattern patterns[8] = {
            katman::InfillPattern::Rectilinear,
            katman::InfillPattern::Grid,
            katman::InfillPattern::Triangles,
            katman::InfillPattern::Gyroid,
            katman::InfillPattern::Cubic,
            katman::InfillPattern::Honeycomb,
            katman::InfillPattern::Concentric,
            katman::InfillPattern::Lightning,
        };
        const char* names[8] = {
            "Rectilinear", "Grid", "Triangles", "Gyroid", "Cubic", "Honeycomb", "Concentric", "Lightning"
        };

        for (int p = 0; p < 8; ++p) {
            katman::SlicerConfig cfg{};
            cfg.infill_pattern = patterns[p];
            cfg.infill_density = 20.0f;
            cfg.top_layers = 2;
            cfg.bottom_layers = 2;
            cfg.adhesion = katman::AdhesionType::None;
            cfg.emit_gcode = false;
            auto r = katman::slice_memory(bin.data(), bin.size(), names[p], cfg);
            double inf_mm = r.stats.feature_filament_mm[static_cast<size_t>(katman::FeatureType::SparseInfill)];
            KT_CHECK(r.ok && inf_mm > 20.0, (std::string("Infill pattern generates filament: ") + names[p]).c_str());
        }
    }

    // 4. Spiral Vase mode: 1 outer wall, 0 infill, continuous monotonically rising Z!
    {
        auto tris = make_cylinder(15.0f, 10.0f, 48);
        auto bin = build_binary_stl(tris);
        katman::SlicerConfig cfg{};
        cfg.spiral_vase = true;
        cfg.adhesion = katman::AdhesionType::None;
        cfg.arc_fitting = false;
        auto r = katman::slice_memory(bin.data(), bin.size(), "vase.stl", cfg);
        KT_CHECK(r.ok, "Spiral vase mode succeeds");
        KT_CHECK(r.stats.feature_filament_mm[static_cast<size_t>(katman::FeatureType::SparseInfill)] == 0.0,
                 "Spiral vase has 0 sparse infill");
        bool has_helical_z = false;
        for (size_t l = 1; l < r.planned_layers.size(); ++l) {
            for (const auto& m : r.planned_layers[l].moves) {
                if (m.type == katman::FeatureType::OuterWall && m.z1 > m.z0 + 1e-4f) {
                    has_helical_z = true;
                    break;
                }
            }
        }
        KT_CHECK(has_helical_z, "Spiral vase ramps Z continuously along outer wall");
    }

    // 5. Ironing generates Ironing moves on exposed top surface
    {
        std::vector<RawTri> tris;
        add_box(tris, 0, 0, 0, 20, 20, 3);
        auto bin = build_binary_stl(tris);
        katman::SlicerConfig cfg{};
        cfg.ironing = true;
        cfg.emit_gcode = false;
        auto r = katman::slice_memory(bin.data(), bin.size(), "iron.stl", cfg);
        KT_CHECK(r.ok && r.stats.feature_filament_mm[static_cast<size_t>(katman::FeatureType::Ironing)] > 0.5,
                 "Top surface ironing generates low-flow ironing moves");
    }
}

}  // namespace katman_test
