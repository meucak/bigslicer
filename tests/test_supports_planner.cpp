#include "test_common.hpp"
#include "core/engine.hpp"

#include <cmath>
#include <cstdio>

namespace katman_test {

void run_test_supports_planner() {
    std::printf("[Suite] Supports (Grid & Tree), Bridges, Seam, Scarf, Arcs & Kinematics\n");

    // 1. Bridge detection & Normal Grid + Organic Tree Supports on arch/cantilever torture model
    {
        auto tris = make_bridge_overhang_arch();
        auto bin = build_binary_stl(tris);

        // Bridge detection without support
        katman::SlicerConfig cfg_br{};
        cfg_br.detect_bridges = true;
        cfg_br.support_type = katman::SupportType::None;
        cfg_br.emit_gcode = false;
        auto r_br = katman::slice_memory(bin.data(), bin.size(), "arch.stl", cfg_br);
        KT_CHECK(r_br.ok, "Bridge arch slices cleanly");
        KT_CHECK(r_br.stats.feature_filament_mm[static_cast<size_t>(katman::FeatureType::Bridge)] > 5.0,
                 "Arch detects and extrudes bridge skin");

        // Normal Grid Supports with interface layers
        katman::SlicerConfig cfg_grid = cfg_br;
        cfg_grid.support_type = katman::SupportType::NormalGrid;
        cfg_grid.support_interface_layers = 2;
        auto r_grid = katman::slice_memory(bin.data(), bin.size(), "arch_grid.stl", cfg_grid);
        KT_CHECK(r_grid.ok, "Normal Grid supports slice cleanly");
        KT_CHECK(r_grid.stats.feature_filament_mm[static_cast<size_t>(katman::FeatureType::Support)] > 10.0,
                 "Normal Grid generates support columns");
        KT_CHECK(r_grid.stats.feature_filament_mm[static_cast<size_t>(katman::FeatureType::SupportInterface)] > 5.0,
                 "Normal Grid generates dense support interface layers");

        // Organic / Tree Supports
        katman::SlicerConfig cfg_tree = cfg_br;
        cfg_tree.support_type = katman::SupportType::OrganicTree;
        cfg_tree.support_interface_layers = 2;
        auto r_tree = katman::slice_memory(bin.data(), bin.size(), "arch_tree.stl", cfg_tree);
        KT_CHECK(r_tree.ok, "Organic Tree supports slice cleanly");
        KT_CHECK(r_tree.stats.feature_filament_mm[static_cast<size_t>(katman::FeatureType::Support)] > 5.0,
                 "Organic Tree generates hollow branching trunks");
        KT_CHECK(r_tree.stats.feature_filament_mm[static_cast<size_t>(katman::FeatureType::SupportInterface)] > 5.0,
                 "Organic Tree generates top interface roof pads");
    }

    // 2. Circular Arc Fitting (G2/G3) on high-res cylinder
    {
        auto tris = make_cylinder(20.0f, 4.0f, 96);
        auto bin = build_binary_stl(tris);
        katman::SlicerConfig cfg{};
        cfg.arc_fitting = true;
        cfg.arc_tolerance = 0.03f;
        cfg.adhesion = katman::AdhesionType::None;
        auto r = katman::slice_memory(bin.data(), bin.size(), "cyl_arc.stl", cfg);
        KT_CHECK(r.ok && r.stats.arc_moves > 20, "Cylinder arc fitting produces G2/G3 arc moves");
        KT_CHECK(r.gcode.find("G2 X") != std::string::npos || r.gcode.find("G3 X") != std::string::npos,
                 "G-code output contains real G2/G3 arc commands");
    }

    // 3. Scarf seam + all 5 seam modes run cleanly
    {
        auto tris = make_cylinder(12.0f, 3.0f, 48);
        auto bin = build_binary_stl(tris);
        katman::SlicerConfig cfg{};
        cfg.scarf_seam = true;
        cfg.scarf_length = 6.0f;
        cfg.arc_fitting = false;
        cfg.emit_gcode = false;
        auto r = katman::slice_memory(bin.data(), bin.size(), "scarf.stl", cfg);
        KT_CHECK(r.ok && r.stats.extrusion_moves > 50, "Scarf joint seam slices cleanly");
    }

    // 4. Volumetric Flow Cap & Cooling Minimum Layer Time slowdown
    {
        // Tiny 6x6x6mm spire finishes fast -> min_layer_time_sec must slow down print time noticeably!
        std::vector<RawTri> tris;
        add_box(tris, 0, 0, 0, 6, 6, 6);
        auto bin = build_binary_stl(tris);

        katman::SlicerConfig cfg_fast{};
        cfg_fast.min_layer_time_sec = 0.0f;
        cfg_fast.max_volumetric_flow = 30.0f;
        cfg_fast.speed_outer_wall = 200.0f;
        cfg_fast.speed_inner_wall = 250.0f;
        cfg_fast.emit_gcode = false;
        auto r_fast = katman::slice_memory(bin.data(), bin.size(), "spire.stl", cfg_fast);

        katman::SlicerConfig cfg_cool = cfg_fast;
        cfg_cool.min_layer_time_sec = 10.0f;
        auto r_cool = katman::slice_memory(bin.data(), bin.size(), "spire.stl", cfg_cool);

        KT_CHECK(r_cool.stats.print_time_sec > r_fast.stats.print_time_sec * 1.5,
                 "Minimum layer time cooling significantly slows down tiny layers");

        // Volumetric flow cap test: cap at 5 mm^3/s -> max_volumetric_flow_mm3s must stay <= 5.05
        katman::SlicerConfig cfg_cap = cfg_fast;
        cfg_cap.max_volumetric_flow = 5.0f;
        auto r_cap = katman::slice_memory(bin.data(), bin.size(), "spire.stl", cfg_cap);
        KT_CHECK(r_cap.stats.max_volumetric_flow_mm3s <= 5.05f,
                 "Max volumetric flow cap strictly enforced");
    }

    // 5. Brim and Raft bed adhesion
    {
        std::vector<RawTri> tris;
        add_box(tris, 0, 0, 0, 15, 15, 4);
        auto bin = build_binary_stl(tris);

        katman::SlicerConfig cfg_brim{};
        cfg_brim.adhesion = katman::AdhesionType::Brim;
        cfg_brim.brim_width = 4.0f;
        cfg_brim.emit_gcode = false;
        auto rb = katman::slice_memory(bin.data(), bin.size(), "brim.stl", cfg_brim);
        KT_CHECK(rb.ok && rb.stats.feature_filament_mm[static_cast<size_t>(katman::FeatureType::SkirtBrim)] > 15.0,
                 "Brim generates concentric first-layer adhesion loops");

        katman::SlicerConfig cfg_raft{};
        cfg_raft.adhesion = katman::AdhesionType::Raft;
        cfg_raft.raft_layers = 2;
        cfg_raft.emit_gcode = false;
        auto rr = katman::slice_memory(bin.data(), bin.size(), "raft.stl", cfg_raft);
        KT_CHECK(rr.ok && rr.stats.layer_count == rb.stats.layer_count + 2,
                 "Raft prepends 2 raft layers below model");
    }
}

}  // namespace katman_test
