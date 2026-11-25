#include "gcode_writer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace katman {

namespace {

// 2-digit decimal lookup table for ultra-fast zero-locale fixed-point ASCII serialization
constexpr char DIGIT_PAIRS[201] =
    "00010203040506070809"
    "10111213141516171819"
    "20212223242526272829"
    "30313233343536373839"
    "40414243444546474849"
    "50515253545556575859"
    "60616263646566676869"
    "70717273747576777879"
    "80818283848586878889"
    "90919293949596979899";

// Ultra-fast raw-pointer stack-buffer writers (eliminates per-field std::string capacity checks!)
inline char* write_uint32_ptr(char* dst, uint32_t v) {
    char tmp[12];
    char* p = tmp + 12;
    while (v >= 100u) {
        uint32_t q = v / 100u;
        uint32_t r = (v - q * 100u) * 2u;
        v = q;
        *--p = DIGIT_PAIRS[r + 1];
        *--p = DIGIT_PAIRS[r];
    }
    if (v >= 10u) {
        uint32_t r = v * 2u;
        *--p = DIGIT_PAIRS[r + 1];
        *--p = DIGIT_PAIRS[r];
    } else {
        *--p = static_cast<char>('0' + v);
    }
    size_t len = static_cast<size_t>((tmp + 12) - p);
    std::memcpy(dst, p, len);
    return dst + len;
}

inline char* write_uint64_ptr(char* dst, uint64_t v) {
    if (v <= 0xFFFFFFFFull) return write_uint32_ptr(dst, static_cast<uint32_t>(v));
    char tmp[24];
    char* p = tmp + 24;
    while (v >= 100) {
        uint64_t q = v / 100;
        uint32_t r = static_cast<uint32_t>(v - q * 100) * 2u;
        v = q;
        *--p = DIGIT_PAIRS[r + 1];
        *--p = DIGIT_PAIRS[r];
    }
    if (v >= 10) {
        uint32_t r = static_cast<uint32_t>(v) * 2u;
        *--p = DIGIT_PAIRS[r + 1];
        *--p = DIGIT_PAIRS[r];
    } else {
        *--p = static_cast<char>('0' + v);
    }
    size_t len = static_cast<size_t>((tmp + 24) - p);
    std::memcpy(dst, p, len);
    return dst + len;
}

inline char* write_fixed_ptr(char* dst, double val, int decimals) {
    if (!std::isfinite(val)) val = 0.0;
    static constexpr double MULT[6] = {1.0, 10.0, 100.0, 1000.0, 10000.0, 100000.0};
    static constexpr uint32_t DIV[6] = {1u, 10u, 100u, 1000u, 10000u, 100000u};
    int d = std::clamp(decimals, 0, 5);
    double s = val * MULT[d];
    int64_t scaled = static_cast<int64_t>(s >= 0.0 ? (s + 0.5) : (s - 0.5));
    if (scaled < 0) {
        *dst++ = '-';
        scaled = -scaled;
    }
    uint64_t uval = static_cast<uint64_t>(scaled);
    uint64_t ip;
    uint32_t fp;
    if (uval <= 0xFFFFFFFFull) {
        uint32_t u32 = static_cast<uint32_t>(uval);
        uint32_t q = u32 / DIV[d];
        ip = q;
        fp = u32 - q * DIV[d];
    } else {
        ip = uval / DIV[d];
        fp = static_cast<uint32_t>(uval - ip * DIV[d]);
    }
    dst = write_uint64_ptr(dst, ip);
    if (d == 0 || fp == 0u) return dst;

    char fbuf[8];
    for (int k = d - 1; k >= 0; --k) {
        uint32_t q = fp / 10u;
        fbuf[k] = static_cast<char>('0' + (fp - q * 10u));
        fp = q;
    }
    int len = d;
    while (len > 0 && fbuf[len - 1] == '0') --len;
    if (len > 0) {
        *dst++ = '.';
        std::memcpy(dst, fbuf, static_cast<size_t>(len));
        dst += len;
    }
    return dst;
}

inline void append_uint(std::string& out, uint64_t v) {
    char buf[24];
    char* end = write_uint64_ptr(buf, v);
    out.append(buf, static_cast<size_t>(end - buf));
}

inline void append_fixed(std::string& out, double val, int decimals) {
    char buf[32];
    char* end = write_fixed_ptr(buf, val, decimals);
    out.append(buf, static_cast<size_t>(end - buf));
}

void append_accel_cmd(std::string& out, float accel, const SlicerConfig& cfg) {
    int a = std::max(100, static_cast<int>(std::lround(accel)));
    if (cfg.flavor == GCodeFlavor::Klipper) {
        out += "SET_VELOCITY_LIMIT ACCEL=";
        append_uint(out, static_cast<uint64_t>(a));
        out.push_back('\n');
    } else if (cfg.flavor == GCodeFlavor::RepRapFirmware) {
        out += "M204 P";
        append_uint(out, static_cast<uint64_t>(a));
        out += " T";
        append_uint(out, static_cast<uint64_t>(a));
        out.push_back('\n');
    } else {
        out += "M204 S";
        append_uint(out, static_cast<uint64_t>(a));
        out.push_back('\n');
    }
}

void append_fan_cmd(std::string& out, int fan_pct) {
    if (fan_pct <= 0) {
        out += "M107\n";
    } else {
        int pwm = std::clamp(static_cast<int>(std::lround(fan_pct * 2.55)), 0, 255);
        out += "M106 S";
        append_uint(out, static_cast<uint64_t>(pwm));
        out.push_back('\n');
    }
}

std::string build_header_and_start_gcode(const SlicerConfig& cfg, const SliceStats& stats) {
    std::string out;
    out.reserve(2048);

    const char* flavor_name = "Marlin 2";
    if (cfg.flavor == GCodeFlavor::Klipper) flavor_name = "Klipper";
    else if (cfg.flavor == GCodeFlavor::RepRapFirmware) flavor_name = "RepRapFirmware";
    else if (cfg.flavor == GCodeFlavor::Bambu) flavor_name = "Bambu";

    int hrs = static_cast<int>(stats.print_time_sec / 3600.0);
    int mins = static_cast<int>(std::fmod(stats.print_time_sec, 3600.0) / 60.0);
    int secs = static_cast<int>(std::fmod(stats.print_time_sec, 60.0));

    char hdr[1024];
    std::snprintf(hdr, sizeof(hdr),
        "; generated by BigSlicer 1.0.0 (C++20 / Clipper2 / Zero-Bloat Engine)\n"
        "; flavor: %s\n"
        "; printer: %s | material: %s | quality: %s\n"
        "; layer_height = %.3f | first_layer_height = %.3f | nozzle_diameter = %.2f\n"
        "; wall_count = %d | infill_density = %.0f%% | support = %d\n"
        "; estimated printing time (normal mode) = %dh %dm %ds\n"
        "; filament used [mm] = %.2f\n"
        "; filament used [cm3] = %.2f\n"
        "; filament used [g] = %.2f\n"
        "; total layers count = %u\n"
        ";TIME:%d\n"
        ";LAYER_COUNT:%u\n\n",
        flavor_name, cfg.printer_id.c_str(), cfg.material_id.c_str(), cfg.quality_id.c_str(),
        cfg.layer_height, cfg.first_layer_height, cfg.nozzle_diameter,
        cfg.wall_count, cfg.infill_density, static_cast<int>(cfg.support_type),
        hrs, mins, secs,
        stats.filament_mm, stats.filament_cm3, stats.filament_g,
        stats.layer_count,
        static_cast<int>(std::lround(stats.print_time_sec)),
        stats.layer_count);
    out += hdr;

    out += "; --- START G-CODE ---\n";
    out += "G21 ; millimeters\n";
    out += "G90 ; absolute positioning\n";
    out += cfg.relative_extrusion ? "M83 ; relative extrusion mode\n" : "M82 ; absolute extrusion mode\n";

    if (cfg.flavor == GCodeFlavor::Klipper) {
        char kbuf[256];
        std::snprintf(kbuf, sizeof(kbuf),
            "M140 S%.0f ; start heating bed\n"
            "M104 S%.0f ; start heating nozzle\n"
            "G28 ; home all axes\n"
            "M190 S%.0f ; wait for bed\n"
            "M109 S%.0f ; wait for nozzle\n",
            cfg.first_layer_bed_temp, cfg.first_layer_nozzle_temp,
            cfg.first_layer_bed_temp, cfg.first_layer_nozzle_temp);
        out += kbuf;
        if (cfg.pressure_advance > 1e-5f) {
            out += "SET_PRESSURE_ADVANCE ADVANCE=";
            append_fixed(out, cfg.pressure_advance, 4);
            out.push_back('\n');
        }
    } else if (cfg.flavor == GCodeFlavor::RepRapFirmware) {
        char rbuf[256];
        std::snprintf(rbuf, sizeof(rbuf),
            "M140 S%.0f\nM104 S%.0f\nG28\nM190 S%.0f\nM109 S%.0f\n",
            cfg.first_layer_bed_temp, cfg.first_layer_nozzle_temp,
            cfg.first_layer_bed_temp, cfg.first_layer_nozzle_temp);
        out += rbuf;
        if (cfg.pressure_advance > 1e-5f) {
            out += "M572 D0 S";
            append_fixed(out, cfg.pressure_advance, 4);
            out.push_back('\n');
        }
    } else {
        // Marlin 2 / Bambu compatible sequence
        char mbuf[256];
        std::snprintf(mbuf, sizeof(mbuf),
            "M140 S%.0f ; set bed temp\n"
            "M104 S%.0f ; set nozzle temp\n"
            "G28 ; home all axes\n"
            "M190 S%.0f ; wait for bed temp\n"
            "M109 S%.0f ; wait for nozzle temp\n",
            cfg.first_layer_bed_temp, cfg.first_layer_nozzle_temp,
            cfg.first_layer_bed_temp, cfg.first_layer_nozzle_temp);
        out += mbuf;
        if (cfg.pressure_advance > 1e-5f) {
            out += "M900 K";
            append_fixed(out, cfg.pressure_advance, 4);
            out += " ; linear advance\n";
        }
    }

    // Standard front-left nozzle prime line
    out += "G92 E0\n";
    out += "G1 Z2.0 F3000\n";
    if (!cfg.origin_center) {
        out += "G1 X5 Y10 Z0.3 F5000 ; prime line start\n";
        out += "G1 X5 Y110 Z0.3 F1500 E10 ; prime line\n";
        out += "G1 X5.4 Y110 Z0.3 F5000\n";
        out += "G1 X5.4 Y10 Z0.3 F1500 E10\n";
        out += "G92 E0\n";
    }
    // Optional User Custom Start G-Code (replaces {nozzle_temp}, {bed_temp}, {first_layer_nozzle_temp}, {first_layer_bed_temp})
    if (!cfg.custom_start_gcode.empty()) {
        auto repl = [](std::string& s, const char* pat, float val) {
            char nb[32];
            std::snprintf(nb, sizeof(nb), "%.0f", val);
            size_t p = 0, plen = std::strlen(pat);
            while ((p = s.find(pat, p)) != std::string::npos) {
                s.replace(p, plen, nb);
                p += std::strlen(nb);
            }
        };
        std::string custom = cfg.custom_start_gcode;
        repl(custom, "{nozzle_temp}", cfg.nozzle_temp);
        repl(custom, "{bed_temp}", cfg.bed_temp);
        repl(custom, "{first_layer_nozzle_temp}", cfg.first_layer_nozzle_temp);
        repl(custom, "{first_layer_bed_temp}", cfg.first_layer_bed_temp);
        out += "; --- CUSTOM START G-CODE ---\n";
        out += custom;
        if (!custom.empty() && custom.back() != '\n') out.push_back('\n');
    }

    out += "M107 ; fan off for first layer\n\n";
    return out;
}

std::string build_end_gcode(const SlicerConfig& cfg, float max_z) {
    std::string out;
    out.reserve(512);
    out += "\n; --- END G-CODE ---\n";
    if (!cfg.custom_end_gcode.empty()) {
        out += "; --- CUSTOM END G-CODE ---\n";
        out += cfg.custom_end_gcode;
        if (cfg.custom_end_gcode.back() != '\n') out.push_back('\n');
    }
    if (cfg.retract_length > 1e-4f) {
        out += cfg.relative_extrusion ? "G1 E-" : "G91\nG1 E-";
        append_fixed(out, cfg.retract_length, 3);
        out += " F";
        append_uint(out, static_cast<uint64_t>(std::lround(cfg.retract_speed * 60.0f)));
        out += cfg.relative_extrusion ? "\n" : "\nG90\n";
    }
    float park_z = std::min(cfg.bed_size_z, max_z + 10.0f);
    out += "G1 Z";
    append_fixed(out, park_z, 3);
    out += " F1200 ; lift nozzle\n";
    out += "G1 X0 Y";
    append_fixed(out, cfg.origin_center ? 0.0 : static_cast<double>(cfg.bed_size_y) * 0.9, 1);
    out += " F6000 ; present print\n";
    out += "M104 S0 ; hotend off\n";
    out += "M140 S0 ; bed off\n";
    out += "M107 ; cooling fan off\n";
    out += "M84 ; disable steppers\n";
    out += "; End of Gcode\n";
    return out;
}

void serialize_layer_chunk(const std::vector<PlannedLayer>& layers,
                           size_t b,
                           size_t e,
                           const SlicerConfig& cfg,
                           std::string& out) {
    size_t est_moves = 0;
    for (size_t i = b; i < e; ++i) est_moves += layers[i].moves.size();
    out.reserve(est_moves * 38 + (e - b) * 96);

    int last_fan = (b == 0) ? 0 : layers[b - 1].fan_pct;
    float last_accel = -1.0f;
    int last_f = -1;
    float last_z = -999.0f;
    FeatureType last_type = FeatureType::Count;

    const int f_ret = static_cast<int>(std::lround(cfg.retract_speed * 60.0f));
    const int f_unret = static_cast<int>(std::lround(cfg.unretract_speed * 60.0f));
    const int f_z = 900;  // 15 mm/s Z-hop

    for (size_t i = b; i < e; ++i) {
        const PlannedLayer& ly = layers[i];
        double cur_e_abs = ly.start_e_abs_mm;

        out += ";LAYER:";
        append_uint(out, ly.index);
        out += "\n;Z:";
        append_fixed(out, ly.z_top, 3);
        out += "\n;HEIGHT:";
        append_fixed(out, ly.height, 3);
        out.push_back('\n');

        // Optional Pause at Layer (M600 / PAUSE for filament change or magnet insertion!)
        if (!cfg.pause_layers.empty() &&
            std::binary_search(cfg.pause_layers.begin(), cfg.pause_layers.end(), static_cast<int>(ly.index))) {
            out += "; PAUSE AT LAYER ";
            append_uint(out, ly.index);
            out += "\n";
            if (cfg.flavor == GCodeFlavor::Klipper) {
                out += "PAUSE\n";
            } else {
                out += "M600 ; filament change / pause\n";
            }
        }

        // Transition temperatures on Layer 1
        if (ly.index == 1) {
            if (std::fabs(cfg.nozzle_temp - cfg.first_layer_nozzle_temp) > 0.5f) {
                out += "M104 S";
                append_uint(out, static_cast<uint64_t>(std::lround(cfg.nozzle_temp)));
                out.push_back('\n');
            }
            if (std::fabs(cfg.bed_temp - cfg.first_layer_bed_temp) > 0.5f) {
                out += "M140 S";
                append_uint(out, static_cast<uint64_t>(std::lround(cfg.bed_temp)));
                out.push_back('\n');
            }
        }

        // Cooling fan update when changed
        if (ly.fan_pct != last_fan) {
            append_fan_cmd(out, ly.fan_pct);
            last_fan = ly.fan_pct;
        }

        // Layer Z-step if changed (unless spiral vase ramps Z smoothly)
        if (!cfg.spiral_vase && std::fabs(ly.z_top - last_z) > 1e-4f) {
            out += "G0 Z";
            append_fixed(out, ly.z_top, 3);
            out += " F";
            append_uint(out, static_cast<uint64_t>(f_z));
            out.push_back('\n');
            last_z = ly.z_top;
            last_f = f_z;
        }

        for (const auto& m : ly.moves) {
            if (m.type != last_type && m.type != FeatureType::Travel) {
                out += ";TYPE:";
                out += feature_type_name(m.type);
                out.push_back('\n');
                last_type = m.type;
            }

            if (std::fabs(m.accel_mm_s2 - last_accel) > 10.0f) {
                append_accel_cmd(out, m.accel_mm_s2, cfg);
                last_accel = m.accel_mm_s2;
            }

            // Pre-travel retract + optional Z-hop
            if (m.flags & MOVE_FLAG_RETRACT) {
                if (cfg.relative_extrusion) {
                    out += "G1 E-";
                    append_fixed(out, cfg.retract_length, 4);
                } else {
                    cur_e_abs -= cfg.retract_length;
                    out += "G1 E";
                    append_fixed(out, cur_e_abs, 5);
                }
                if (last_f != f_ret) {
                    out += " F";
                    append_uint(out, static_cast<uint64_t>(f_ret));
                    last_f = f_ret;
                }
                out.push_back('\n');

                if (m.flags & MOVE_FLAG_ZHOP) {
                    out += "G0 Z";
                    append_fixed(out, ly.z_top + cfg.z_hop, 3);
                    out += " F";
                    append_uint(out, static_cast<uint64_t>(f_z));
                    out.push_back('\n');
                    last_z = ly.z_top + cfg.z_hop;
                    last_f = f_z;
                }
            }

            const int f_move = std::max(60, static_cast<int>(m.actual_speed_mm_s * 60.0f + 0.5f));
            char lbuf[144];
            char* lp = lbuf;

            if (m.type == FeatureType::Travel) {
                *lp++ = 'G'; *lp++ = '0'; *lp++ = ' '; *lp++ = 'X';
                lp = write_fixed_ptr(lp, m.x1, 3);
                *lp++ = ' '; *lp++ = 'Y';
                lp = write_fixed_ptr(lp, m.y1, 3);
                if (last_f != f_move) {
                    *lp++ = ' '; *lp++ = 'F';
                    lp = write_uint32_ptr(lp, static_cast<uint32_t>(f_move));
                    last_f = f_move;
                }
                *lp++ = '\n';
                out.append(lbuf, static_cast<size_t>(lp - lbuf));

                // Post-travel Z-drop + unretract
                if (m.flags & MOVE_FLAG_ZHOP) {
                    lp = lbuf;
                    *lp++ = 'G'; *lp++ = '0'; *lp++ = ' '; *lp++ = 'Z';
                    lp = write_fixed_ptr(lp, ly.z_top, 3);
                    *lp++ = ' '; *lp++ = 'F';
                    lp = write_uint32_ptr(lp, static_cast<uint32_t>(f_z));
                    *lp++ = '\n';
                    out.append(lbuf, static_cast<size_t>(lp - lbuf));
                    last_z = ly.z_top;
                    last_f = f_z;
                }
                if (m.flags & MOVE_FLAG_UNRETRACT) {
                    lp = lbuf;
                    *lp++ = 'G'; *lp++ = '1'; *lp++ = ' '; *lp++ = 'E';
                    if (cfg.relative_extrusion) {
                        lp = write_fixed_ptr(lp, cfg.retract_length, 4);
                    } else {
                        cur_e_abs += cfg.retract_length;
                        lp = write_fixed_ptr(lp, cur_e_abs, 5);
                    }
                    if (last_f != f_unret) {
                        *lp++ = ' '; *lp++ = 'F';
                        lp = write_uint32_ptr(lp, static_cast<uint32_t>(f_unret));
                        last_f = f_unret;
                    }
                    *lp++ = '\n';
                    out.append(lbuf, static_cast<size_t>(lp - lbuf));
                }
            } else if (m.flags & MOVE_FLAG_ARC) {
                *lp++ = 'G'; *lp++ = (m.flags & MOVE_FLAG_ARC_CW) ? '2' : '3';
                *lp++ = ' '; *lp++ = 'X';
                lp = write_fixed_ptr(lp, m.x1, 3);
                *lp++ = ' '; *lp++ = 'Y';
                lp = write_fixed_ptr(lp, m.y1, 3);
                *lp++ = ' '; *lp++ = 'I';
                lp = write_fixed_ptr(lp, m.arc_i, 3);
                *lp++ = ' '; *lp++ = 'J';
                lp = write_fixed_ptr(lp, m.arc_j, 3);
                *lp++ = ' '; *lp++ = 'E';
                if (cfg.relative_extrusion) {
                    lp = write_fixed_ptr(lp, m.e_delta_mm, 5);
                } else {
                    cur_e_abs += m.e_delta_mm;
                    lp = write_fixed_ptr(lp, cur_e_abs, 5);
                }
                if (last_f != f_move) {
                    *lp++ = ' '; *lp++ = 'F';
                    lp = write_uint32_ptr(lp, static_cast<uint32_t>(f_move));
                    last_f = f_move;
                }
                *lp++ = '\n';
                out.append(lbuf, static_cast<size_t>(lp - lbuf));
            } else {
                *lp++ = 'G'; *lp++ = '1'; *lp++ = ' '; *lp++ = 'X';
                lp = write_fixed_ptr(lp, m.x1, 3);
                *lp++ = ' '; *lp++ = 'Y';
                lp = write_fixed_ptr(lp, m.y1, 3);
                if (std::fabs(m.z1 - last_z) > 1e-4f) {
                    *lp++ = ' '; *lp++ = 'Z';
                    lp = write_fixed_ptr(lp, m.z1, 3);
                    last_z = m.z1;
                }
                *lp++ = ' '; *lp++ = 'E';
                if (cfg.relative_extrusion) {
                    lp = write_fixed_ptr(lp, m.e_delta_mm, 5);
                } else {
                    cur_e_abs += m.e_delta_mm;
                    lp = write_fixed_ptr(lp, cur_e_abs, 5);
                }
                if (last_f != f_move) {
                    *lp++ = ' '; *lp++ = 'F';
                    lp = write_uint32_ptr(lp, static_cast<uint32_t>(f_move));
                    last_f = f_move;
                }
                *lp++ = '\n';
                out.append(lbuf, static_cast<size_t>(lp - lbuf));
            }
        }
    }
}

}  // namespace

std::string serialize_gcode(const std::vector<PlannedLayer>& layers,
                            const SlicerConfig& cfg,
                            const SliceStats& stats,
                            ThreadPool* pool) {
    std::string header = build_header_and_start_gcode(cfg, stats);
    const float max_z = layers.empty() ? 0.0f : layers.back().z_top;
    std::string footer = build_end_gcode(cfg, max_z);

    const size_t L = layers.size();
    if (L == 0) return header + footer;

    const uint32_t nw = (pool && L >= 8) ? pool->size() : 1u;
    if (nw <= 1) {
        std::string body;
        serialize_layer_chunk(layers, 0, L, cfg, body);
        std::string out;
        out.reserve(header.size() + body.size() + footer.size());
        out += header;
        out += body;
        out += footer;
        return out;
    }

    // Parallel chunk serialization across all CPU cores!
    std::vector<std::string> chunks(nw);
    pool->parallel_for_chunks(nw, 1, [&](uint32_t, size_t b, size_t e) {
        for (size_t c = b; c < e; ++c) {
            size_t l0 = (L * c) / nw;
            size_t l1 = (L * (c + 1)) / nw;
            serialize_layer_chunk(layers, l0, l1, cfg, chunks[c]);
        }
    });

    size_t total_sz = header.size() + footer.size();
    for (const auto& ch : chunks) total_sz += ch.size();
    std::string out;
    out.reserve(total_sz);
    out += header;
    for (const auto& ch : chunks) out += ch;
    out += footer;
    return out;
}

std::vector<uint8_t> pack_toolpath_ktp(const std::vector<PlannedLayer>& layers,
                                       const Mesh& mesh,
                                       const SlicerConfig& cfg,
                                       const SliceStats& stats,
                                       ThreadPool* pool) {
    struct KtpMove {
        float x0, y0, x1, y1;
        uint16_t width_um;
        uint16_t speed_dmm;
        uint16_t flow_cmm3;
        uint8_t type;
        uint8_t flags;
    };
    static_assert(sizeof(KtpMove) == 24, "KtpMove must be 24 bytes");

    struct KtpLayer {
        float z_top;
        float height;
        uint32_t first_move;
        uint32_t move_count;
    };
    static_assert(sizeof(KtpLayer) == 16, "KtpLayer must be 16 bytes");

    const size_t L = layers.size();
    std::vector<std::vector<KtpMove>> per_layer_moves(L);

    auto pack_layer = [&](uint32_t, size_t li) {
        const auto& ly = layers[li];
        auto& out_m = per_layer_moves[li];
        out_m.reserve(ly.moves.size() + 16);
        for (const auto& m : ly.moves) {
            uint16_t w_um = static_cast<uint16_t>(std::clamp(static_cast<int>(m.width_mm * 1000.0f + 0.5f), 0, 65535));
            uint16_t sp_d = static_cast<uint16_t>(std::clamp(static_cast<int>(m.actual_speed_mm_s * 10.0f + 0.5f), 0, 65535));
            uint16_t fl_c = static_cast<uint16_t>(std::clamp(static_cast<int>(m.flow_mm3_s * 100.0f + 0.5f), 0, 65535));
            uint8_t ft = static_cast<uint8_t>(m.type);

            if ((m.flags & MOVE_FLAG_ARC) != 0) {
                double cx = m.x0 + m.arc_i;
                double cy = m.y0 + m.arc_j;
                double r = std::hypot(m.arc_i, m.arc_j);
                double a0 = std::atan2(m.y0 - cy, m.x0 - cx);
                double a1 = std::atan2(m.y1 - cy, m.x1 - cx);
                bool cw = (m.flags & MOVE_FLAG_ARC_CW) != 0;
                double da = a1 - a0;
                if (cw) { while (da >= 0.0) da -= 6.28318530718; }
                else    { while (da <= 0.0) da += 6.28318530718; }
                int segs = std::clamp(static_cast<int>(std::ceil(std::fabs(da) * r / 0.65)), 3, 40);
                float px = m.x0, py = m.y0;
                for (int s = 1; s <= segs; ++s) {
                    double a = a0 + da * (static_cast<double>(s) / segs);
                    float nx = (s == segs) ? m.x1 : static_cast<float>(cx + r * std::cos(a));
                    float ny = (s == segs) ? m.y1 : static_cast<float>(cy + r * std::sin(a));
                    out_m.push_back({px, py, nx, ny, w_um, sp_d, fl_c, ft, m.flags});
                    px = nx; py = ny;
                }
            } else {
                out_m.push_back({m.x0, m.y0, m.x1, m.y1, w_um, sp_d, fl_c, ft, m.flags});
            }
        }
    };

    if (pool && L >= 8) {
        pool->parallel_for(L, pack_layer, 2);
    } else {
        for (size_t li = 0; li < L; ++li) pack_layer(0, li);
    }

    std::vector<KtpLayer> k_layers(L);
    uint32_t total_k_moves = 0;
    for (size_t li = 0; li < L; ++li) {
        uint32_t cnt = static_cast<uint32_t>(per_layer_moves[li].size());
        k_layers[li] = {layers[li].z_top, layers[li].height, total_k_moves, cnt};
        total_k_moves += cnt;
    }

    const size_t total_bytes = 64 + L * sizeof(KtpLayer) + static_cast<size_t>(total_k_moves) * sizeof(KtpMove);
    std::vector<uint8_t> buf(total_bytes, 0);
    uint8_t* p = buf.data();

    auto wr_u32 = [&](size_t off, uint32_t v) { std::memcpy(p + off, &v, 4); };
    auto wr_f32 = [&](size_t off, float v) { std::memcpy(p + off, &v, 4); };

    p[0] = 'K'; p[1] = 'T'; p[2] = 'P'; p[3] = '1';
    wr_u32(4, static_cast<uint32_t>(L));
    wr_u32(8, total_k_moves);
    wr_f32(12, mesh.bbox_min.x); wr_f32(16, mesh.bbox_min.y); wr_f32(20, mesh.bbox_min.z);
    wr_f32(24, mesh.bbox_max.x); wr_f32(28, mesh.bbox_max.y); wr_f32(32, mesh.bbox_max.z);
    wr_f32(36, cfg.bed_size_x);  wr_f32(40, cfg.bed_size_y);  wr_f32(44, cfg.bed_size_z);
    wr_f32(48, static_cast<float>(stats.print_time_sec));
    wr_f32(52, static_cast<float>(stats.filament_mm));
    wr_f32(56, static_cast<float>(stats.filament_g));
    wr_f32(60, stats.max_volumetric_flow_mm3s);

    size_t off = 64;
    if (!k_layers.empty()) {
        std::memcpy(p + off, k_layers.data(), L * sizeof(KtpLayer));
        off += L * sizeof(KtpLayer);
    }
    for (size_t li = 0; li < L; ++li) {
        if (!per_layer_moves[li].empty()) {
            size_t nb = per_layer_moves[li].size() * sizeof(KtpMove);
            std::memcpy(p + off, per_layer_moves[li].data(), nb);
            off += nb;
        }
    }
    return buf;
}

}  // namespace katman
