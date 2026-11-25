#pragma once

#include "config.hpp"
#include "gcode_writer.hpp"
#include "infill.hpp"
#include "mesh.hpp"
#include "motion_planner.hpp"
#include "path_planner.hpp"
#include "perimeters.hpp"
#include "slicer.hpp"
#include "supports.hpp"
#include "surfaces.hpp"
#include "thread_pool.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace katman {

struct SliceResult {
    bool ok = false;
    std::string error;
    SliceStats stats{};
    std::string gcode;
    std::vector<uint8_t> ktp_binary;
    std::vector<PlannedLayer> planned_layers;  // Available for native OpenGL preview / inspection
};

// Full end-to-end ultra-optimized multi-threaded slicing pipeline
SliceResult slice_mesh(Mesh mesh, const SlicerConfig& cfg, ThreadPool* external_pool = nullptr);

// Convenience entrypoints
SliceResult slice_file(const std::string& path, const SlicerConfig& cfg);
SliceResult slice_memory(const uint8_t* data, size_t size, const std::string& name, const SlicerConfig& cfg);

}  // namespace katman
