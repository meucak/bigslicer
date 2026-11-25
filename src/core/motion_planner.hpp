#pragma once

#include "config.hpp"
#include "path_planner.hpp"
#include "thread_pool.hpp"

#include <vector>

namespace katman {

// Run lookahead trapezoidal velocity planner + Junction-Deviation / Klipper SCV / Classic Jerk,
// volumetric flow throttling, minimum layer time cooling slowdown, fan PWM schedule,
// and exact per-feature print time / filament accumulation across all layers.
void run_motion_and_cooling_planner(std::vector<PlannedLayer>& layers,
                                    const SlicerConfig& cfg,
                                    SliceStats& stats,
                                    ThreadPool* pool = nullptr);

}  // namespace katman
