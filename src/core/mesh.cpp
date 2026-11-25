#include "mesh.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

#ifndef __EMSCRIPTEN__
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>
#endif

namespace katman {

namespace {

inline bool is_finite3(float x, float y, float z) {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

inline void load_f3(const uint8_t* p, float& x, float& y, float& z) {
    std::memcpy(&x, p, 4);
    std::memcpy(&y, p + 4, 4);
    std::memcpy(&z, p + 8, 4);
}

inline bool is_binary_stl_buffer(const uint8_t* data, size_t size, uint32_t& out_tris) {
    if (size < 84) return false;
    uint32_t tc = 0;
    std::memcpy(&tc, data + 80, 4);
    if (tc == 0 || tc > 25000000u) return false;
    uint64_t expected = 84ull + static_cast<uint64_t>(tc) * 50ull;
    if (size >= expected && (size - expected) <= 64) {
        out_tris = tc;
        return true;
    }
    return false;
}

inline bool is_obj_hint(const uint8_t* data, size_t size, const std::string& name) {
    if (name.size() >= 4) {
        std::string ext = name.substr(name.size() - 4);
        for (char& c : ext) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (ext == ".obj") return true;
    }
    size_t scan = std::min<size_t>(size, 1024);
    bool has_v = false, has_f = false;
    for (size_t i = 0; i + 2 < scan; ++i) {
        if ((i == 0 || data[i - 1] == '\n') && data[i] == 'v' && data[i + 1] == ' ') has_v = true;
        if ((i == 0 || data[i - 1] == '\n') && data[i] == 'f' && data[i + 1] == ' ') has_f = true;
    }
    return has_v && has_f;
}

inline SliceTri make_slice_tri(float x0, float y0, float z0,
                               float x1, float y1, float z1,
                               float x2, float y2, float z2,
                               double& vol6, double& area2) {
    SliceTri t{};
    t.x0 = x0; t.y0 = y0; t.z0 = z0;
    t.x1 = x1; t.y1 = y1; t.z1 = z1;
    t.x2 = x2; t.y2 = y2; t.z2 = z2;
    t.zmin = std::min(z0, std::min(z1, z2));
    t.zmax = std::max(z0, std::max(z1, z2));

    double e1x = static_cast<double>(x1) - x0;
    double e1y = static_cast<double>(y1) - y0;
    double e1z = static_cast<double>(z1) - z0;
    double e2x = static_cast<double>(x2) - x0;
    double e2y = static_cast<double>(y2) - y0;
    double e2z = static_cast<double>(z2) - z0;
    double nx = e1y * e2z - e1z * e2y;
    double ny = e1z * e2x - e1x * e2z;
    double nz = e1x * e2y - e1y * e2x;
    double len = std::sqrt(nx * nx + ny * ny + nz * nz);
    area2 = len;
    t.nz = (len > 1e-12) ? static_cast<float>(nz / len) : 0.0f;

    double c12x = static_cast<double>(y1) * z2 - static_cast<double>(z1) * y2;
    double c12y = static_cast<double>(z1) * x2 - static_cast<double>(x1) * z2;
    double c12z = static_cast<double>(x1) * y2 - static_cast<double>(y1) * x2;
    vol6 = static_cast<double>(x0) * c12x + static_cast<double>(y0) * c12y + static_cast<double>(z0) * c12z;
    return t;
}

void recompute_mesh_stats(Mesh& mesh, ThreadPool* pool, float overhang_deg_threshold = 45.0f) {
    if (mesh.tris.empty()) {
        mesh.bbox_min = mesh.bbox_max = mesh.bbox_extent = {0, 0, 0};
        mesh.volume_mm3 = 0.0;
        mesh.area_mm2 = 0.0;
        mesh.overhang_face_pct = 0.0f;
        return;
    }

    const float overhang_nz_lim = -std::sin(overhang_deg_threshold * 3.14159265358979323846f / 180.0f);
    const size_t n = mesh.tris.size();
    const uint32_t nw = pool ? pool->size() : 1u;

    struct LocalStat {
        float minx = std::numeric_limits<float>::max();
        float miny = std::numeric_limits<float>::max();
        float minz = std::numeric_limits<float>::max();
        float maxx = std::numeric_limits<float>::lowest();
        float maxy = std::numeric_limits<float>::lowest();
        float maxz = std::numeric_limits<float>::lowest();
        double vol6 = 0.0;
        double area2 = 0.0;
        double overhang_area2 = 0.0;
    };
    std::vector<LocalStat> ls(nw);

    auto run_range = [&](uint32_t tid, size_t b, size_t e) {
        LocalStat& s = ls[tid];
        for (size_t i = b; i < e; ++i) {
            SliceTri& t = mesh.tris[i];
            s.minx = std::min(s.minx, std::min(t.x0, std::min(t.x1, t.x2)));
            s.miny = std::min(s.miny, std::min(t.y0, std::min(t.y1, t.y2)));
            s.minz = std::min(s.minz, t.zmin);
            s.maxx = std::max(s.maxx, std::max(t.x0, std::max(t.x1, t.x2)));
            s.maxy = std::max(s.maxy, std::max(t.y0, std::max(t.y1, t.y2)));
            s.maxz = std::max(s.maxz, t.zmax);

            double v6 = 0.0, a2 = 0.0;
            t = make_slice_tri(t.x0, t.y0, t.z0, t.x1, t.y1, t.z1, t.x2, t.y2, t.z2, v6, a2);
            s.vol6 += v6;
            s.area2 += a2;
            if (t.nz < overhang_nz_lim && t.zmin > 0.25f) {
                s.overhang_area2 += a2;
            }
        }
    };

    if (pool && n >= 16384) {
        pool->parallel_for_chunks(n, (n + nw - 1) / nw, run_range);
    } else {
        run_range(0, 0, n);
    }

    LocalStat tot{};
    for (uint32_t i = 0; i < nw; ++i) {
        if (ls[i].minx > ls[i].maxx) continue;
        tot.minx = std::min(tot.minx, ls[i].minx);
        tot.miny = std::min(tot.miny, ls[i].miny);
        tot.minz = std::min(tot.minz, ls[i].minz);
        tot.maxx = std::max(tot.maxx, ls[i].maxx);
        tot.maxy = std::max(tot.maxy, ls[i].maxy);
        tot.maxz = std::max(tot.maxz, ls[i].maxz);
        tot.vol6 += ls[i].vol6;
        tot.area2 += ls[i].area2;
        tot.overhang_area2 += ls[i].overhang_area2;
    }

    // Auto-fix globally inverted STL normals (negative signed volume)
    if (tot.vol6 < -1e-3) {
        mesh.inverted_winding_fixed = true;
        for (auto& t : mesh.tris) {
            std::swap(t.x1, t.x2);
            std::swap(t.y1, t.y2);
            std::swap(t.z1, t.z2);
            t.nz = -t.nz;
        }
        tot.vol6 = -tot.vol6;
    }

    mesh.bbox_min = {tot.minx, tot.miny, tot.minz};
    mesh.bbox_max = {tot.maxx, tot.maxy, tot.maxz};
    mesh.bbox_extent = {tot.maxx - tot.minx, tot.maxy - tot.miny, tot.maxz - tot.minz};
    mesh.volume_mm3 = std::fabs(tot.vol6) / 6.0;
    mesh.area_mm2 = tot.area2 * 0.5;
    mesh.overhang_face_pct = (tot.area2 > 1e-6) ? static_cast<float>((tot.overhang_area2 / tot.area2) * 100.0) : 0.0f;
}

bool parse_binary_stl(const uint8_t* data, uint32_t tri_count, Mesh& out, ThreadPool* pool) {
    out.is_binary = true;
    out.raw_tri_count = tri_count;
    out.tris.resize(tri_count);

    const uint8_t* recs = data + 84;
    const uint32_t nw = pool ? pool->size() : 1u;
    std::vector<uint32_t> degen_counts(nw, 0);

    auto worker = [&](uint32_t tid, size_t b, size_t e) {
        uint32_t deg = 0;
        for (size_t i = b; i < e; ++i) {
            const uint8_t* r = recs + i * 50ull;
            float x0, y0, z0, x1, y1, z1, x2, y2, z2;
            load_f3(r + 12, x0, y0, z0);
            load_f3(r + 24, x1, y1, z1);
            load_f3(r + 36, x2, y2, z2);
            if (!is_finite3(x0, y0, z0) || !is_finite3(x1, y1, z1) || !is_finite3(x2, y2, z2)) {
                out.tris[i] = SliceTri{};
                ++deg;
                continue;
            }
            SliceTri t{};
            t.x0 = x0; t.y0 = y0; t.z0 = z0;
            t.x1 = x1; t.y1 = y1; t.z1 = z1;
            t.x2 = x2; t.y2 = y2; t.z2 = z2;
            t.zmin = std::min(z0, std::min(z1, z2));
            t.zmax = std::max(z0, std::max(z1, z2));
            out.tris[i] = t;
        }
        degen_counts[tid] += deg;
    };

    if (pool && tri_count >= 16384) {
        pool->parallel_for_chunks(tri_count, (tri_count + nw - 1) / nw, worker);
    } else {
        worker(0, 0, tri_count);
    }

    uint32_t total_deg = 0;
    for (uint32_t d : degen_counts) total_deg += d;
    out.degenerate_skipped = total_deg;

    recompute_mesh_stats(out, pool);
    out.ok = !out.tris.empty();
    return out.ok;
}

inline const char* skip_spaces(const char* p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    return p;
}

inline bool parse_fast_float(const char*& p, const char* end, float& out) {
    p = skip_spaces(p, end);
    if (p >= end) return false;
    char* next = nullptr;
    // Fast strtof works directly on null-terminated or bounded buffer if we guard carefully
    char tmp[64];
    size_t len = 0;
    const char* s = p;
    while (s < end && len < 63 && !(*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) {
        tmp[len++] = *s++;
    }
    if (len == 0) return false;
    tmp[len] = '\0';
    out = std::strtof(tmp, &next);
    if (next == tmp) return false;
    p = s;
    return true;
}

bool parse_ascii_stl(const uint8_t* data, size_t size, Mesh& out, ThreadPool* pool) {
    out.is_binary = false;
    const char* p = reinterpret_cast<const char*>(data);
    const char* end = p + size;
    out.tris.reserve(std::max<size_t>(64, size / 180));

    float v[9];
    int vidx = 0;
    while (p < end) {
        p = skip_spaces(p, end);
        if (p >= end) break;
        if (end - p >= 6 && std::memcmp(p, "vertex", 6) == 0 && (p + 6 == end || p[6] == ' ' || p[6] == '\t')) {
            p += 6;
            float x = 0, y = 0, z = 0;
            if (parse_fast_float(p, end, x) && parse_fast_float(p, end, y) && parse_fast_float(p, end, z)) {
                v[vidx * 3 + 0] = x;
                v[vidx * 3 + 1] = y;
                v[vidx * 3 + 2] = z;
                ++vidx;
                if (vidx == 3) {
                    if (is_finite3(v[0], v[1], v[2]) && is_finite3(v[3], v[4], v[5]) && is_finite3(v[6], v[7], v[8])) {
                        SliceTri t{};
                        t.x0 = v[0]; t.y0 = v[1]; t.z0 = v[2];
                        t.x1 = v[3]; t.y1 = v[4]; t.z1 = v[5];
                        t.x2 = v[6]; t.y2 = v[7]; t.z2 = v[8];
                        t.zmin = std::min(t.z0, std::min(t.z1, t.z2));
                        t.zmax = std::max(t.z0, std::max(t.z1, t.z2));
                        out.tris.push_back(t);
                    } else {
                        ++out.degenerate_skipped;
                    }
                    vidx = 0;
                }
            }
        }
        // Advance to end of line
        while (p < end && *p != '\n') ++p;
        if (p < end) ++p;
    }

    out.raw_tri_count = static_cast<uint32_t>(out.tris.size());
    if (out.tris.empty()) {
        out.error = "ASCII STL contains 0 triangles";
        return false;
    }
    recompute_mesh_stats(out, pool);
    out.ok = true;
    return true;
}

bool parse_obj_mesh(const uint8_t* data, size_t size, Mesh& out, ThreadPool* pool) {
    out.is_binary = false;
    std::vector<Vec3f> verts;
    verts.reserve(size / 40);
    out.tris.reserve(size / 60);

    const char* p = reinterpret_cast<const char*>(data);
    const char* end = p + size;
    std::vector<int> face_idx;
    face_idx.reserve(16);

    while (p < end) {
        while (p < end && (*p == ' ' || *p == '\t')) ++p;
        if (p >= end) break;
        if (p[0] == 'v' && (p + 1 < end) && (p[1] == ' ' || p[1] == '\t')) {
            p += 2;
            float x = 0, y = 0, z = 0;
            if (parse_fast_float(p, end, x) && parse_fast_float(p, end, y) && parse_fast_float(p, end, z)) {
                verts.push_back({x, y, z});
            }
        } else if (p[0] == 'f' && (p + 1 < end) && (p[1] == ' ' || p[1] == '\t')) {
            p += 2;
            face_idx.clear();
            while (p < end && *p != '\n' && *p != '\r') {
                while (p < end && (*p == ' ' || *p == '\t')) ++p;
                if (p >= end || *p == '\n' || *p == '\r' || *p == '#') break;
                char* np = nullptr;
                char tmp[32];
                size_t l = 0;
                while (p < end && l < 31 && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
                    tmp[l++] = *p++;
                }
                tmp[l] = '\0';
                long idx = std::strtol(tmp, &np, 10);
                if (np != tmp && idx != 0) {
                    int vi = (idx > 0) ? static_cast<int>(idx - 1) : static_cast<int>(verts.size()) + static_cast<int>(idx);
                    if (vi >= 0 && vi < static_cast<int>(verts.size())) {
                        face_idx.push_back(vi);
                    }
                }
            }
            for (size_t k = 1; k + 1 < face_idx.size(); ++k) {
                const Vec3f& a = verts[face_idx[0]];
                const Vec3f& b = verts[face_idx[k]];
                const Vec3f& c = verts[face_idx[k + 1]];
                SliceTri t{};
                t.x0 = a.x; t.y0 = a.y; t.z0 = a.z;
                t.x1 = b.x; t.y1 = b.y; t.z1 = b.z;
                t.x2 = c.x; t.y2 = c.y; t.z2 = c.z;
                t.zmin = std::min(t.z0, std::min(t.z1, t.z2));
                t.zmax = std::max(t.z0, std::max(t.z1, t.z2));
                out.tris.push_back(t);
            }
        }
        while (p < end && *p != '\n') ++p;
        if (p < end) ++p;
    }

    out.raw_tri_count = static_cast<uint32_t>(out.tris.size());
    if (out.tris.empty()) {
        out.error = "OBJ mesh contains 0 triangles";
        return false;
    }
    recompute_mesh_stats(out, pool);
    out.ok = true;
    return true;
}

}  // namespace

Mesh load_mesh_memory(const uint8_t* data, size_t size, const std::string& name, ThreadPool* pool) {
    Mesh out{};
    out.name = name;
    out.file_bytes = size;
    if (!data || size == 0) {
        out.error = "Empty mesh buffer";
        return out;
    }

    uint32_t bin_tris = 0;
    if (is_binary_stl_buffer(data, size, bin_tris)) {
        parse_binary_stl(data, bin_tris, out, pool);
        return out;
    }
    if (is_obj_hint(data, size, name)) {
        parse_obj_mesh(data, size, out, pool);
        return out;
    }
    parse_ascii_stl(data, size, out, pool);
    return out;
}

Mesh load_mesh_file(const std::string& path, ThreadPool* pool) {
    Mesh out{};
    out.name = path;
#ifdef __EMSCRIPTEN__
    out.error = "File mmap not available in WASM";
    (void)pool;
    return out;
#else
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        out.error = "Cannot open file: " + path;
        return out;
    }
    struct stat st{};
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
        ::close(fd);
        out.error = "Invalid or empty file: " + path;
        return out;
    }
    size_t sz = static_cast<size_t>(st.st_size);
    struct statfs sfs{};
    bool is_fuse_or_net = (::fstatfs(fd, &sfs) == 0 &&
                           (static_cast<uint64_t>(sfs.f_type) == 0x65735546ull ||
                            static_cast<uint64_t>(sfs.f_type) == 0x6969ull ||
                            static_cast<uint64_t>(sfs.f_type) == 0xFF534D42ull));
    if (is_fuse_or_net) {
        // Bulk buffered read avoids thousands of 4KB page-fault roundtrips over SSHFS/NFS/SMB!
        std::vector<uint8_t> buf(sz);
        size_t got = 0;
        while (got < sz) {
            ssize_t r = ::read(fd, buf.data() + got, std::min<size_t>(sz - got, 1024 * 1024));
            if (r <= 0) break;
            got += static_cast<size_t>(r);
        }
        ::close(fd);
        if (got != sz) {
            out.error = "Short read from file: " + path;
            return out;
        }
        return load_mesh_memory(buf.data(), buf.size(), path, pool);
    }

    void* ptr = ::mmap(nullptr, sz, PROT_READ, MAP_PRIVATE, fd, 0);
    if (ptr == MAP_FAILED) {
        ::close(fd);
        out.error = "mmap failed for: " + path;
        return out;
    }
    ::madvise(ptr, sz, MADV_SEQUENTIAL | MADV_WILLNEED);
    out = load_mesh_memory(static_cast<const uint8_t*>(ptr), sz, path, pool);
    ::munmap(ptr, sz);
    ::close(fd);
    return out;
#endif
}

void transform_and_arrange_mesh(Mesh& mesh, const SlicerConfig& cfg, ThreadPool* pool) {
    if (!mesh.ok || mesh.tris.empty()) return;

    // Optional Auto-Lay-Flat: find dominant face normal cluster (largest planar area) and rotate to -Z
    float rx_deg = cfg.rotate_x_deg;
    float ry_deg = cfg.rotate_y_deg;
    float rz_deg = cfg.rotate_z_deg;

    constexpr float DEG2RAD = 3.14159265358979323846f / 180.0f;

    // Build 3x3 rotation matrix R = Rz * Ry * Rx
    auto build_rot = [](float rx, float ry, float rz, float m[9]) {
        float cx = std::cos(rx), sx = std::sin(rx);
        float cy = std::cos(ry), sy = std::sin(ry);
        float cz = std::cos(rz), sz = std::sin(rz);
        m[0] = cz * cy;  m[1] = cz * sy * sx - sz * cx;  m[2] = cz * sy * cx + sz * sx;
        m[3] = sz * cy;  m[4] = sz * sy * sx + cz * cx;  m[5] = sz * sy * cx - cz * sx;
        m[6] = -sy;      m[7] = cy * sx;                 m[8] = cy * cx;
    };

    float R[9];
    build_rot(rx_deg * DEG2RAD, ry_deg * DEG2RAD, rz_deg * DEG2RAD, R);

    if (cfg.auto_lay_flat) {
        // Test 6 axis-aligned orientations and pick the one with largest bottom contact area (z near zmin)
        const float cand[6][2] = {{0,0}, {180,0}, {90,0}, {-90,0}, {0,90}, {0,-90}};
        double best_score = -1.0;
        int best_idx = 0;
        for (int c = 0; c < 6; ++c) {
            float Rc[9];
            build_rot(cand[c][0] * DEG2RAD, cand[c][1] * DEG2RAD, 0.0f, Rc);
            float min_z = std::numeric_limits<float>::max();
            for (const auto& t : mesh.tris) {
                float za = Rc[6]*t.x0 + Rc[7]*t.y0 + Rc[8]*t.z0;
                float zb = Rc[6]*t.x1 + Rc[7]*t.y1 + Rc[8]*t.z1;
                float zc = Rc[6]*t.x2 + Rc[7]*t.y2 + Rc[8]*t.z2;
                min_z = std::min(min_z, std::min(za, std::min(zb, zc)));
            }
            double bottom_area = 0.0;
            for (const auto& t : mesh.tris) {
                float za = Rc[6]*t.x0 + Rc[7]*t.y0 + Rc[8]*t.z0;
                float zb = Rc[6]*t.x1 + Rc[7]*t.y1 + Rc[8]*t.z1;
                float zc = Rc[6]*t.x2 + Rc[7]*t.y2 + Rc[8]*t.z2;
                if (za <= min_z + 0.15f && zb <= min_z + 0.15f && zc <= min_z + 0.15f) {
                    float xa = Rc[0]*t.x0 + Rc[1]*t.y0 + Rc[2]*t.z0;
                    float ya = Rc[3]*t.x0 + Rc[4]*t.y0 + Rc[5]*t.z0;
                    float xb = Rc[0]*t.x1 + Rc[1]*t.y1 + Rc[2]*t.z1;
                    float yb = Rc[3]*t.x1 + Rc[4]*t.y1 + Rc[5]*t.z1;
                    float xc = Rc[0]*t.x2 + Rc[1]*t.y2 + Rc[2]*t.z2;
                    float yc = Rc[3]*t.x2 + Rc[4]*t.y2 + Rc[5]*t.z2;
                    bottom_area += 0.5 * std::fabs((xb - xa)*(yc - ya) - (xc - xa)*(yb - ya));
                }
            }
            if (bottom_area > best_score) {
                best_score = bottom_area;
                best_idx = c;
            }
        }
        build_rot((rx_deg + cand[best_idx][0]) * DEG2RAD,
                  (ry_deg + cand[best_idx][1]) * DEG2RAD,
                  rz_deg * DEG2RAD, R);
    }

    const float sx = cfg.scale_x * (cfg.mirror_x ? -1.0f : 1.0f);
    const float sy = cfg.scale_y * (cfg.mirror_y ? -1.0f : 1.0f);
    const float sz = cfg.scale_z;
    const bool flip_wind = (cfg.mirror_x != cfg.mirror_y);

    const float cx = (mesh.bbox_min.x + mesh.bbox_max.x) * 0.5f;
    const float cy = (mesh.bbox_min.y + mesh.bbox_max.y) * 0.5f;
    const float cz = (mesh.bbox_min.z + mesh.bbox_max.z) * 0.5f;

    const bool has_linear_xform =
        std::fabs(sx - 1.0f) > 1e-6f || std::fabs(sy - 1.0f) > 1e-6f || std::fabs(sz - 1.0f) > 1e-6f ||
        std::fabs(rx_deg) > 1e-6f || std::fabs(ry_deg) > 1e-6f || std::fabs(rz_deg) > 1e-6f ||
        cfg.auto_lay_flat || flip_wind;

    if (has_linear_xform) {
        auto apply_pt = [&](float& x, float& y, float& z) {
            float px = (x - cx) * sx;
            float py = (y - cy) * sy;
            float pz = (z - cz) * sz;
            x = R[0]*px + R[1]*py + R[2]*pz + cx;
            y = R[3]*px + R[4]*py + R[5]*pz + cy;
            z = R[6]*px + R[7]*py + R[8]*pz + cz;
        };
        const size_t n = mesh.tris.size();
        auto fn = [&](uint32_t, size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) {
                SliceTri& t = mesh.tris[i];
                apply_pt(t.x0, t.y0, t.z0);
                apply_pt(t.x1, t.y1, t.z1);
                apply_pt(t.x2, t.y2, t.z2);
                if (flip_wind) {
                    std::swap(t.x1, t.x2);
                    std::swap(t.y1, t.y2);
                    std::swap(t.z1, t.z2);
                }
                t.zmin = std::min(t.z0, std::min(t.z1, t.z2));
                t.zmax = std::max(t.z0, std::max(t.z1, t.z2));
            }
        };
        if (pool && n >= 16384) {
            uint32_t nw = pool->size();
            pool->parallel_for_chunks(n, (n + nw - 1) / nw, fn);
        } else {
            fn(0, 0, n);
        }
        recompute_mesh_stats(mesh, pool, cfg.support_overhang_angle);
    }

    // Translate to build plate
    float bed_cx = cfg.origin_center ? 0.0f : cfg.bed_size_x * 0.5f;
    float bed_cy = cfg.origin_center ? 0.0f : cfg.bed_size_y * 0.5f;
    float cur_cx = (mesh.bbox_min.x + mesh.bbox_max.x) * 0.5f;
    float cur_cy = (mesh.bbox_min.y + mesh.bbox_max.y) * 0.5f;

    float dx = cfg.auto_center ? (bed_cx - cur_cx + cfg.pos_x) : cfg.pos_x;
    float dy = cfg.auto_center ? (bed_cy - cur_cy + cfg.pos_y) : cfg.pos_y;
    float dz = cfg.auto_drop_to_bed ? (-mesh.bbox_min.z) : 0.0f;

    if (std::fabs(dx) > 1e-6f || std::fabs(dy) > 1e-6f || std::fabs(dz) > 1e-6f) {
        const size_t n = mesh.tris.size();
        auto shift_fn = [&](uint32_t, size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) {
                SliceTri& t = mesh.tris[i];
                t.x0 += dx; t.y0 += dy; t.z0 += dz;
                t.x1 += dx; t.y1 += dy; t.z1 += dz;
                t.x2 += dx; t.y2 += dy; t.z2 += dz;
                t.zmin += dz;
                t.zmax += dz;
            }
        };
        if (pool && n >= 16384) {
            uint32_t nw = pool->size();
            pool->parallel_for_chunks(n, (n + nw - 1) / nw, shift_fn);
        } else {
            shift_fn(0, 0, n);
        }
        mesh.bbox_min.x += dx; mesh.bbox_min.y += dy; mesh.bbox_min.z += dz;
        mesh.bbox_max.x += dx; mesh.bbox_max.y += dy; mesh.bbox_max.z += dz;
    }

    // Optional multi-copy grid arrangement on build plate
    if (cfg.copies > 1) {
        const int copies = cfg.copies;
        const int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(copies))));
        const int rows = (copies + cols - 1) / cols;
        const float step_x = mesh.bbox_extent.x + cfg.copy_spacing;
        const float step_y = mesh.bbox_extent.y + cfg.copy_spacing;
        const float grid_w = (cols - 1) * step_x;
        const float grid_h = (rows - 1) * step_y;

        std::vector<SliceTri> base = std::move(mesh.tris);
        mesh.tris.clear();
        mesh.tris.reserve(base.size() * static_cast<size_t>(copies));

        for (int c = 0; c < copies; ++c) {
            int col = c % cols;
            int row = c / cols;
            float ox = col * step_x - grid_w * 0.5f;
            float oy = row * step_y - grid_h * 0.5f;
            for (SliceTri t : base) {
                t.x0 += ox; t.y0 += oy;
                t.x1 += ox; t.y1 += oy;
                t.x2 += ox; t.y2 += oy;
                mesh.tris.push_back(t);
            }
        }
        recompute_mesh_stats(mesh, pool, cfg.support_overhang_angle);
    } else {
        // Ensure overhang percentage reflects the configured support angle
        const float overhang_nz_lim = -std::sin(cfg.support_overhang_angle * DEG2RAD);
        double area2 = 0.0, ov2 = 0.0;
        for (const auto& t : mesh.tris) {
            double e1x = t.x1 - t.x0, e1y = t.y1 - t.y0, e1z = t.z1 - t.z0;
            double e2x = t.x2 - t.x0, e2y = t.y2 - t.y0, e2z = t.z2 - t.z0;
            double nx = e1y*e2z - e1z*e2y, ny = e1z*e2x - e1x*e2z, nz = e1x*e2y - e1y*e2x;
            double a2 = std::sqrt(nx*nx + ny*ny + nz*nz);
            area2 += a2;
            if (t.nz < overhang_nz_lim && t.zmin > 0.25f) ov2 += a2;
        }
        mesh.overhang_face_pct = (area2 > 1e-6) ? static_cast<float>((ov2 / area2) * 100.0) : 0.0f;
    }
}

}  // namespace katman
