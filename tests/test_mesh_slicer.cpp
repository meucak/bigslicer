#include "test_common.hpp"
#include "core/geometry2d.hpp"
#include "core/slicer.hpp"

#include <cmath>
#include <cstdio>

namespace katman_test {

void run_test_mesh_slicer() {
    std::printf("[Suite] Mesh Ingestion, Transform & CSR Z-Slicing\n");

    // 1. Binary STL with 'solid ' header trap (must still parse as binary!)
    {
        std::vector<RawTri> tris;
        add_box(tris, 0, 0, 0, 20, 20, 10);
        auto bin = build_binary_stl(tris, "solid trap_binary_header");
        katman::Mesh m = katman::load_mesh_memory(bin.data(), bin.size(), "cube.stl");
        KT_CHECK(m.ok, "Binary STL with 'solid ' header loads");
        KT_CHECK(m.is_binary, "Binary STL detected despite 'solid ' header");
        KT_CHECK(m.tris.size() == 12, "Cube has 12 triangles");
        KT_CHECK_NEAR(m.volume_mm3, 4000.0, 1.0, "Cube volume == 4000 mm^3");
        KT_CHECK_NEAR(m.area_mm2, 1600.0, 1.0, "Cube surface area == 1600 mm^2");
    }

    // 2. Inverted winding auto-repair
    {
        std::vector<RawTri> tris;
        add_box(tris, 0, 0, 0, 10, 10, 10, true);
        auto bin = build_binary_stl(tris);
        katman::Mesh m = katman::load_mesh_memory(bin.data(), bin.size(), "inv.stl");
        KT_CHECK(m.ok, "Inverted mesh loads");
        KT_CHECK(m.inverted_winding_fixed, "Inverted winding automatically detected & fixed");
        KT_CHECK_NEAR(m.volume_mm3, 1000.0, 1.0, "Repaired cube volume == 1000 mm^3");
    }

    // 3. ASCII STL & OBJ ingestion
    {
        const std::string ascii =
            "solid tri\n"
            "facet normal 0 0 1\nouter loop\n"
            "vertex 0 0 0\nvertex 10 0 0\nvertex 0 10 10\n"
            "endloop\nendfacet\nendsolid tri\n";
        katman::Mesh ma = katman::load_mesh_memory(
            reinterpret_cast<const uint8_t*>(ascii.data()), ascii.size(), "a.stl");
        KT_CHECK(ma.ok && !ma.is_binary && ma.tris.size() == 1, "ASCII STL loads");

        const std::string obj =
            "v 0 0 0\nv 10 0 0\nv 10 10 0\nv 0 10 0\n"
            "f 1 2 3 4\n";
        katman::Mesh mo = katman::load_mesh_memory(
            reinterpret_cast<const uint8_t*>(obj.data()), obj.size(), "quad.obj");
        KT_CHECK(mo.ok && mo.tris.size() == 2, "OBJ quad fan-triangulates to 2 tris");
    }

    // 4. Exact slice area on 20x20x10 cube & hollow tube (CCW outer + CW hole)
    {
        std::vector<RawTri> tris;
        add_box(tris, 0, 0, 0, 20, 20, 10);
        auto bin = build_binary_stl(tris);
        katman::Mesh m = katman::load_mesh_memory(bin.data(), bin.size(), "cube20.stl");
        katman::SlicerConfig cfg{};
        cfg.layer_height = 0.20f;
        cfg.first_layer_height = 0.20f;
        cfg.elephant_foot_comp = 0.0f;
        katman::transform_and_arrange_mesh(m, cfg);

        KT_CHECK_NEAR(m.bbox_min.z, 0.0f, 1e-4f, "Auto-drop places model at Z=0");
        KT_CHECK_NEAR((m.bbox_min.x + m.bbox_max.x) * 0.5f, cfg.bed_size_x * 0.5f, 1e-3f, "Auto-center X");

        auto sched = katman::build_layer_schedule(m, cfg);
        KT_CHECK(sched.size() == 50, "10mm cube @ 0.20mm -> 50 layers");
        auto layers = katman::slice_mesh_layers(m, sched, cfg);
        KT_CHECK(layers.size() == 50, "Sliced 50 layers");
        KT_CHECK_NEAR(katman::total_area_mm2(layers[0].contours), 400.0, 0.05, "Layer 0 area == 400 mm^2");
        KT_CHECK_NEAR(katman::total_area_mm2(layers[25].contours), 400.0, 0.05, "Layer 25 area == 400 mm^2");
        KT_CHECK(layers[25].geom_hash == layers[26].geom_hash, "Identical vertical cube layers share exact geom_hash");
        KT_CHECK(layers[25].open_chains == 0, "Manifold cube has 0 open chains");
    }

    // 5. Hollow tube outer + hole area accuracy
    {
        auto tris = make_hollow_tube(15.0f, 8.0f, 8.0f, 64);
        auto bin = build_binary_stl(tris);
        katman::Mesh m = katman::load_mesh_memory(bin.data(), bin.size(), "tube.stl");
        katman::SlicerConfig cfg{};
        cfg.elephant_foot_comp = 0.0f;
        katman::transform_and_arrange_mesh(m, cfg);
        auto sched = katman::build_layer_schedule(m, cfg);
        auto layers = katman::slice_mesh_layers(m, sched, cfg);
        double expected_area = 3.1415926535 * (15.0 * 15.0 - 8.0 * 8.0);  // ~505.8 mm^2
        double actual_area = katman::total_area_mm2(layers[10].contours);
        KT_CHECK_NEAR(actual_area, expected_area, 3.0, "Hollow tube annulus cross-section area accurate");
        auto islands = katman::extract_islands(layers[10].contours);
        KT_CHECK(islands.size() == 1 && islands[0].holes.size() == 1, "PolyTree extracts 1 island with 1 hole");
    }

    // 6. Adaptive Dolenc-Makela layer heights on sphere (thinner near top/bottom poles, thicker near equator!)
    {
        auto tris = make_sphere(15.0f, 24, 36);
        auto bin = build_binary_stl(tris);
        katman::Mesh m = katman::load_mesh_memory(bin.data(), bin.size(), "sphere.stl");
        katman::SlicerConfig cfg{};
        cfg.adaptive_layer_height = true;
        cfg.adaptive_min_layer = 0.08f;
        cfg.adaptive_max_layer = 0.28f;
        cfg.adaptive_cusp_error = 0.04f;
        katman::transform_and_arrange_mesh(m, cfg);
        auto sched = katman::build_layer_schedule(m, cfg);
        KT_CHECK(sched.size() > 60, "Adaptive sphere builds variable schedule");
        float mid_h = sched[sched.size() / 2].height;
        float top_h = sched[sched.size() - 2].height;
        KT_CHECK(top_h < mid_h, "Adaptive layer height is thinner near curved pole than vertical equator");
    }

    // 7. Non-manifold cracked mesh recovery (drop 1 triangle from side wall -> tolerant stitcher still closes contour!)
    {
        std::vector<RawTri> tris;
        add_box(tris, 0, 0, 0, 20, 20, 10);
        // Crack a tiny missing sliver by nudging one vertex slightly so edges don't share exact int coords
        tris[4].x0 += 0.08f;
        auto bin = build_binary_stl(tris);
        katman::Mesh m = katman::load_mesh_memory(bin.data(), bin.size(), "cracked.stl");
        katman::SlicerConfig cfg{};
        cfg.elephant_foot_comp = 0.0f;
        katman::transform_and_arrange_mesh(m, cfg);
        auto sched = katman::build_layer_schedule(m, cfg);
        auto layers = katman::slice_mesh_layers(m, sched, cfg);
        double area = katman::total_area_mm2(layers[10].contours);
        KT_CHECK(area > 350.0 && area < 420.0, "Cracked non-manifold mesh stitched cleanly");
    }
}

}  // namespace katman_test
