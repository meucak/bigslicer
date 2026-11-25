cmake_minimum_required(VERSION 3.20)
project(katman LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KATMAN_ENABLE_VIEWER "Build native X11/OpenGL 3.3 Core viewer integrated with katman" ON)
option(KATMAN_NATIVE_TUNE "Optimize aggressively for x86-64-v2 + AVX/SandyBridge" ON)

# Clipper2 static library (official AngusJohnson Clipper2 1.5.2, Boost-1.0)
add_library(clipper2 STATIC
    third_party/clipper2/src/clipper.engine.cpp
    third_party/clipper2/src/clipper.offset.cpp
    third_party/clipper2/src/clipper.rectclip.cpp
)
target_include_directories(clipper2 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/clipper2/include")
target_compile_options(clipper2 PRIVATE
    -O3
    -fno-exceptions
    -fno-rtti
    -Wno-unused-parameter
)
if(KATMAN_NATIVE_TUNE AND NOT EMSCRIPTEN)
    target_compile_options(clipper2 PRIVATE -march=x86-64-v2 -mtune=sandybridge)
endif()

# Core slicer engine (100% portable C++20: builds for native Linux AND WebAssembly!)
add_library(katman_core STATIC
    src/core/profiles.cpp
    src/core/mesh.cpp
    src/core/geometry2d.cpp
    src/core/slicer.cpp
    src/core/perimeters.cpp
    src/core/surfaces.cpp
    src/core/infill.cpp
    src/core/supports.cpp
    src/core/path_planner.cpp
    src/core/motion_planner.cpp
    src/core/gcode_writer.cpp
    src/core/engine.cpp
)
target_include_directories(katman_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
)
target_link_libraries(katman_core PUBLIC clipper2)
target_compile_options(katman_core PRIVATE
    -O3
    -Wall
    -Wextra
    -Werror
    -fno-exceptions
    -fno-rtti
)
if(KATMAN_NATIVE_TUNE AND NOT EMSCRIPTEN)
    target_compile_options(katman_core PRIVATE -march=x86-64-v2 -mtune=sandybridge)
    target_link_libraries(katman_core PUBLIC pthread m)
endif()

# Enable LTO in Release builds for cross-TU inlining into Clipper2 & hot loops!
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if(ipo_supported AND CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT EMSCRIPTEN)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

if(NOT EMSCRIPTEN)
    # Main CLI (+ optional native OpenGL 3.3 viewer)
    set(KATMAN_CLI_SOURCES src/main.cpp)
    if(KATMAN_ENABLE_VIEWER AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/viewer/viewer_bridge.cpp")
        list(APPEND KATMAN_CLI_SOURCES
            src/viewer/viewer_bridge.cpp
            src/viewer/camera.cpp
            src/viewer/culling.cpp
            src/viewer/gl_loader.cpp
            src/viewer/hud.cpp
            src/viewer/matcap.cpp
            src/viewer/platform_x11.cpp
            src/viewer/renderer.cpp
            src/viewer/stl_loader.cpp
            src/viewer/uri_dir.cpp
        )
    endif()

    add_executable(katman ${KATMAN_CLI_SOURCES})
    target_link_libraries(katman PRIVATE katman_core pthread m)
    target_compile_options(katman PRIVATE -O3 -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
    if(KATMAN_NATIVE_TUNE)
        target_compile_options(katman PRIVATE -march=x86-64-v2 -mtune=sandybridge)
    endif()
    if(KATMAN_ENABLE_VIEWER AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/viewer/viewer_bridge.cpp")
        target_compile_definitions(katman PRIVATE KATMAN_HAS_VIEWER=1)
        target_include_directories(katman PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/viewer")
        target_link_libraries(katman PRIVATE X11 dl)
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_main.cpp")
        # Unit & E2E C++ test suite
        add_executable(katman_tests
            tests/test_main.cpp
            tests/test_mesh_slicer.cpp
            tests/test_perimeters_infill.cpp
            tests/test_supports_planner.cpp
            tests/test_gcode_e2e.cpp
        )
        target_include_directories(katman_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
        target_link_libraries(katman_tests PRIVATE katman_core pthread m)
        target_compile_options(katman_tests PRIVATE -O3 -Wall -Wextra -Werror -fno-exceptions -fno-rtti)

        # Dedicated high-resolution performance benchmark suite
        add_executable(katman_bench tests/bench_slicer.cpp)
        target_link_libraries(katman_bench PRIVATE katman_core pthread m)
        target_compile_options(katman_bench PRIVATE -O3 -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
        if(KATMAN_NATIVE_TUNE)
            target_compile_options(katman_bench PRIVATE -march=x86-64-v2 -mtune=sandybridge)
        endif()

        enable_testing()
        add_test(NAME katman_unit_and_e2e_tests COMMAND katman_tests)
    endif()
endif()
