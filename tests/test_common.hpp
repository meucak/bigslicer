#pragma once

#include "core/mesh.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace katman_test {

struct TestStats {
    int passed = 0;
    int failed = 0;
};

extern TestStats g_stats;

#define KT_CHECK(cond, msg)                                                                  \
    do {                                                                                     \
        if (cond) {                                                                          \
            ++katman_test::g_stats.passed;                                                   \
        } else {                                                                             \
            ++katman_test::g_stats.failed;                                                   \
            std::fprintf(stderr, "  [FAIL] %s:%d: %s\n", __FILE__, __LINE__, (msg));         \
        }                                                                                    \
    } while (0)

#define KT_CHECK_NEAR(a, b, tol, msg)                                                        \
    do {                                                                                     \
        double _va = static_cast<double>(a);                                                 \
        double _vb = static_cast<double>(b);                                                 \
        if (std::fabs(_va - _vb) <= static_cast<double>(tol)) {                              \
            ++katman_test::g_stats.passed;                                                   \
        } else {                                                                             \
            ++katman_test::g_stats.failed;                                                   \
            std::fprintf(stderr, "  [FAIL] %s:%d: %s (got %.6f, expected %.6f, tol %.6f)\n", \
                         __FILE__, __LINE__, (msg), _va, _vb, static_cast<double>(tol));     \
        }                                                                                    \
    } while (0)

struct RawTri {
    float x0, y0, z0, x1, y1, z1, x2, y2, z2;
};

inline void add_box(std::vector<RawTri>& out,
                    float x0, float y0, float z0,
                    float x1, float y1, float z1,
                    bool invert_winding = false) {
    auto quad = [&](float ax, float ay, float az,
                    float bx, float by, float bz,
                    float cx, float cy, float cz,
                    float dx, float dy, float dz) {
        if (!invert_winding) {
            out.push_back({ax, ay, az, bx, by, bz, cx, cy, cz});
            out.push_back({ax, ay, az, cx, cy, cz, dx, dy, dz});
        } else {
            out.push_back({ax, ay, az, cx, cy, cz, bx, by, bz});
            out.push_back({ax, ay, az, dx, dy, dz, cx, cy, cz});
        }
    };
    // Bottom (-Z)
    quad(x0, y0, z0, x0, y1, z0, x1, y1, z0, x1, y0, z0);
    // Top (+Z)
    quad(x0, y0, z1, x1, y0, z1, x1, y1, z1, x0, y1, z1);
    // Front (-Y)
    quad(x0, y0, z0, x1, y0, z0, x1, y0, z1, x0, y0, z1);
    // Back (+Y)
    quad(x1, y1, z0, x0, y1, z0, x0, y1, z1, x1, y1, z1);
    // Left (-X)
    quad(x0, y1, z0, x0, y0, z0, x0, y0, z1, x0, y1, z1);
    // Right (+X)
    quad(x1, y0, z0, x1, y1, z0, x1, y1, z1, x1, y0, z1);
}

inline std::vector<uint8_t> build_binary_stl(const std::vector<RawTri>& tris, const char* header = "katman test") {
    std::vector<uint8_t> buf(84 + tris.size() * 50, 0);
    std::strncpy(reinterpret_cast<char*>(buf.data()), header, 79);
    uint32_t cnt = static_cast<uint32_t>(tris.size());
    std::memcpy(buf.data() + 80, &cnt, 4);
    for (size_t i = 0; i < tris.size(); ++i) {
        uint8_t* p = buf.data() + 84 + i * 50;
        const RawTri& t = tris[i];
        float v[9] = {t.x0, t.y0, t.z0, t.x1, t.y1, t.z1, t.x2, t.y2, t.z2};
        std::memcpy(p + 12, v, 36);
    }
    return buf;
}

inline std::vector<RawTri> make_cylinder(float radius, float height, int segs) {
    std::vector<RawTri> out;
    out.reserve(static_cast<size_t>(segs) * 4);
    constexpr float PI = 3.14159265358979323846f;
    for (int i = 0; i < segs; ++i) {
        float a0 = (2.0f * PI * i) / segs;
        float a1 = (2.0f * PI * ((i + 1) % segs)) / segs;
        float x0 = radius * std::cos(a0), y0 = radius * std::sin(a0);
        float x1 = radius * std::cos(a1), y1 = radius * std::sin(a1);
        // Bottom cap (-Z)
        out.push_back({0, 0, 0, x1, y1, 0, x0, y0, 0});
        // Top cap (+Z)
        out.push_back({0, 0, height, x0, y0, height, x1, y1, height});
        // Side quad
        out.push_back({x0, y0, 0, x1, y1, 0, x1, y1, height});
        out.push_back({x0, y0, 0, x1, y1, height, x0, y0, height});
    }
    return out;
}

inline std::vector<RawTri> make_sphere(float radius, int rings, int sectors) {
    std::vector<RawTri> out;
    out.reserve(static_cast<size_t>(rings * sectors * 2));
    constexpr float PI = 3.14159265358979323846f;
    auto pt = [&](int r, int s) -> std::array<float, 3> {
        float phi = (PI * r) / rings;
        float theta = (2.0f * PI * (s % sectors)) / sectors;
        float sp = std::sin(phi), cp = std::cos(phi);
        return {radius * sp * std::cos(theta), radius * sp * std::sin(theta), radius + radius * cp};
    };
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < sectors; ++s) {
            auto p00 = pt(r, s);
            auto p10 = pt(r + 1, s);
            auto p11 = pt(r + 1, s + 1);
            auto p01 = pt(r, s + 1);
            if (r > 0) {
                out.push_back({p00[0], p00[1], p00[2], p10[0], p10[1], p10[2], p11[0], p11[1], p11[2]});
            }
            if (r + 1 < rings) {
                out.push_back({p00[0], p00[1], p00[2], p11[0], p11[1], p11[2], p01[0], p01[1], p01[2]});
            }
        }
    }
    return out;
}

// Hollow tube (outer radius R_out, inner hole radius R_in)
inline std::vector<RawTri> make_hollow_tube(float r_out, float r_in, float height, int segs) {
    std::vector<RawTri> out;
    constexpr float PI = 3.14159265358979323846f;
    for (int i = 0; i < segs; ++i) {
        float a0 = (2.0f * PI * i) / segs;
        float a1 = (2.0f * PI * ((i + 1) % segs)) / segs;
        float xo0 = r_out * std::cos(a0), yo0 = r_out * std::sin(a0);
        float xo1 = r_out * std::cos(a1), yo1 = r_out * std::sin(a1);
        float xi0 = r_in * std::cos(a0), yi0 = r_in * std::sin(a0);
        float xi1 = r_in * std::cos(a1), yi1 = r_in * std::sin(a1);
        // Outer wall (CCW outward)
        out.push_back({xo0, yo0, 0, xo1, yo1, 0, xo1, yo1, height});
        out.push_back({xo0, yo0, 0, xo1, yo1, height, xo0, yo0, height});
        // Inner wall (CW inward)
        out.push_back({xi0, yi0, 0, xi1, yi1, height, xi1, yi1, 0});
        out.push_back({xi0, yi0, 0, xi0, yi0, height, xi1, yi1, height});
        // Top annulus (+Z)
        out.push_back({xi0, yi0, height, xo0, yo0, height, xo1, yo1, height});
        out.push_back({xi0, yi0, height, xo1, yo1, height, xi1, yi1, height});
        // Bottom annulus (-Z)
        out.push_back({xi0, yi0, 0, xo1, yo1, 0, xo0, yo0, 0});
        out.push_back({xi0, yi0, 0, xi1, yi1, 0, xo1, yo1, 0});
    }
    return out;
}

// Bridge / T-Overhang torture model: two pillars + horizontal spanning bridge + cantilever wings
inline std::vector<RawTri> make_bridge_overhang_arch() {
    std::vector<RawTri> out;
    add_box(out, 0, 0, 0, 8, 12, 14);      // Left pillar
    add_box(out, 22, 0, 0, 30, 12, 14);    // Right pillar
    add_box(out, 0, 0, 14, 30, 12, 18);    // Spanning bridge roof (14mm span!)
    add_box(out, 30, 0, 14, 42, 12, 18);   // 12mm unsupported cantilever overhang!
    return out;
}

void run_test_mesh_slicer();
void run_test_perimeters_infill();
void run_test_supports_planner();
void run_test_gcode_e2e();

}  // namespace katman_test
