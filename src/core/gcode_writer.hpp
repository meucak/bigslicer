#pragma once

#include "config.hpp"
#include "mesh.hpp"
#include "path_planner.hpp"
#include "thread_pool.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace katman {

// Ultra-fast parallel LUT-based G-code serializer (Marlin 2 / Klipper / RRF / Bambu flavors)
std::string serialize_gcode(const std::vector<PlannedLayer>& layers,
                            const SlicerConfig& cfg,
                            const SliceStats& stats,
                            ThreadPool* pool = nullptr);

// Pack all planned layers & moves into compact KTP1 binary stream (24 bytes/move) for WebGL2 / OpenGL 3.3
std::vector<uint8_t> pack_toolpath_ktp(const std::vector<PlannedLayer>& layers,
                                       const Mesh& mesh,
                                       const SlicerConfig& cfg,
                                       const SliceStats& stats,
                                       ThreadPool* pool = nullptr);

}  // namespace katman
