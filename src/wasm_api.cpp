// Emscripten C-ABI bridge for offline/GitHub Pages WebAssembly slicing!
#include "core/config.hpp"
#include "core/engine.hpp"
#include "core/profiles.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define KATMAN_EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#else
#define KATMAN_EXPORT extern "C"
#endif

namespace {
katman::SliceResult g_last_result;
std::string g_last_stats_json;
std::string g_presets_json;
}  // namespace

KATMAN_EXPORT int katman_wasm_slice(const uint8_t* stl_data,
                                    uint32_t stl_size,
                                    const char* model_name,
                                    const char* config_json) {
    katman::SlicerConfig cfg{};
    cfg.threads = 1;
    cfg.emit_gcode = true;
    cfg.emit_binary_toolpath = true;

    std::string err;
    if (config_json && config_json[0] != '\0') {
        if (!katman::parse_config_json(config_json, cfg, err)) {
            g_last_result = katman::SliceResult{};
            g_last_result.error = err;
            g_last_stats_json = "{\"ok\":false,\"error\":\"" + err + "\"}";
            return 0;
        }
    }

    g_last_result = katman::slice_memory(stl_data, stl_size, model_name ? model_name : "model.stl", cfg);
    if (!g_last_result.ok) {
        g_last_stats_json = "{\"ok\":false,\"error\":\"" + g_last_result.error + "\"}";
        return 0;
    }
    g_last_stats_json = katman::stats_to_json(g_last_result.stats, cfg);
    return 1;
}

KATMAN_EXPORT const char* katman_wasm_stats_ptr() { return g_last_stats_json.c_str(); }
KATMAN_EXPORT uint32_t katman_wasm_stats_len() { return static_cast<uint32_t>(g_last_stats_json.size()); }

KATMAN_EXPORT const uint8_t* katman_wasm_ktp_ptr() { return g_last_result.ktp_binary.data(); }
KATMAN_EXPORT uint32_t katman_wasm_ktp_len() { return static_cast<uint32_t>(g_last_result.ktp_binary.size()); }

KATMAN_EXPORT const char* katman_wasm_gcode_ptr() { return g_last_result.gcode.c_str(); }
KATMAN_EXPORT uint32_t katman_wasm_gcode_len() { return static_cast<uint32_t>(g_last_result.gcode.size()); }

KATMAN_EXPORT const char* katman_wasm_presets_ptr() {
    if (g_presets_json.empty()) g_presets_json = katman::presets_to_json();
    return g_presets_json.c_str();
}
KATMAN_EXPORT uint32_t katman_wasm_presets_len() {
    if (g_presets_json.empty()) g_presets_json = katman::presets_to_json();
    return static_cast<uint32_t>(g_presets_json.size());
}

KATMAN_EXPORT void katman_wasm_clear() {
    g_last_result = katman::SliceResult{};
    g_last_stats_json.clear();
}
