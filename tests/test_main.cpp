#include "test_common.hpp"

#include <cstdio>

namespace katman_test {
TestStats g_stats{};
}

int main() {
    std::printf("=== Katman Slicer C++20 Test Suite ===\n");
    katman_test::run_test_mesh_slicer();
    katman_test::run_test_perimeters_infill();
    katman_test::run_test_supports_planner();
    katman_test::run_test_gcode_e2e();

    std::printf("\nResult: %d passed, %d failed\n",
                katman_test::g_stats.passed, katman_test::g_stats.failed);
    return (katman_test::g_stats.failed == 0) ? 0 : 1;
}
