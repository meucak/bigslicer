#include "viewer_bridge.hpp"
#include "platform_x11.hpp"

namespace katman {

int launch_native_viewer(const std::string& stl_path) {
    return faset::run_viewer(stl_path);
}

}  // namespace katman
