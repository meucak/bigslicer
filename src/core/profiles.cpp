#include "profiles.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace katman {

namespace {

inline const char* flavor_str(GCodeFlavor f) {
    switch (f) {
        case GCodeFlavor::Klipper: return "klipper";
        case GCodeFlavor::RepRapFirmware: return "rrf";
        case GCodeFlavor::Bambu: return "bambu";
        default: return "marlin2";
    }
}

void scale_line_widths_for_nozzle(SlicerConfig& cfg, float nozzle) {
    cfg.nozzle_diameter = nozzle;
    cfg.outer_wall_width = std::round(nozzle * 1.05f * 100.0f) * 0.01f;
    cfg.inner_wall_width = std::round(nozzle * 1.125f * 100.0f) * 0.01f;
    cfg.first_layer_width = std::round(nozzle * 1.20f * 100.0f) * 0.01f;
    cfg.infill_width = std::round(nozzle * 1.125f * 100.0f) * 0.01f;
    cfg.top_solid_width = std::round(nozzle * 1.05f * 100.0f) * 0.01f;
    cfg.support_width = nozzle;
}

}  // namespace

const std::vector<PrinterPresetInfo>& list_printer_presets() {
    using F = GCodeFlavor;
    using C = CorneringModel;
    // 124 real-world 3D printers — from legendary classics (Ender-3 Pro, CR-10, Anet A8, i3 Mega, MK2S)
    // to ultra-high-speed CoreXY / Klipper / Bambu / Voron / RatRig / Delta machines!
    static const std::vector<PrinterPresetInfo> list = {
        // --- CREALITY (26 models) ---
        {"creality_ender3",         "Creality Ender-3 (Classic)",      "Creality", "Cartesian",      F::Marlin2, C::ClassicJerk,          220, 220, 250, 0.4f, false, false, 11.0f, 5.5f, 45,  40,  55,  65, 150,  500,  500,  700, 1000, 0.00f,  8.0f},
        {"creality_ender3_pro",     "Creality Ender-3 Pro",            "Creality", "Cartesian",      F::Marlin2, C::ClassicJerk,          220, 220, 250, 0.4f, false, false, 12.0f, 5.0f, 45,  45,  60,  70, 150,  500,  500,  800, 1200, 0.00f,  8.0f},
        {"creality_ender3_v2",      "Creality Ender-3 V2",             "Creality", "Cartesian",      F::Marlin2, C::JunctionDeviation,    220, 220, 250, 0.4f, false, false, 12.0f, 5.0f, 45,  45,  65,  80, 160,  700,  600, 1000, 1500, 0.00f, 0.02f},
        {"creality_ender3_v2_neo",  "Creality Ender-3 V2 Neo",         "Creality", "Cartesian",      F::Marlin2, C::JunctionDeviation,    220, 220, 250, 0.4f, false, false, 13.0f, 5.0f, 45,  50,  70,  85, 160,  800,  600, 1000, 1500, 0.00f, 0.02f},
        {"creality_ender3_neo",     "Creality Ender-3 Neo",            "Creality", "Cartesian",      F::Marlin2, C::ClassicJerk,          220, 220, 250, 0.4f, false, false, 12.0f, 5.0f, 45,  45,  60,  75, 150,  600,  500,  800, 1200, 0.00f,  8.0f},
        {"creality_ender3_max",     "Creality Ender-3 Max",            "Creality", "Cartesian",      F::Marlin2, C::ClassicJerk,          300, 300, 340, 0.4f, false, false, 12.0f, 6.0f, 45,  40,  55,  70, 150,  500,  450,  700, 1000, 0.00f,  7.0f},
        {"creality_ender3_max_neo", "Creality Ender-3 Max Neo",        "Creality", "Cartesian",      F::Marlin2, C::JunctionDeviation,    300, 300, 320, 0.4f, false, false, 13.0f, 5.5f, 45,  45,  65,  80, 160,  700,  500,  900, 1200, 0.00f, 0.02f},
        {"creality_ender3_s1",      "Creality Ender-3 S1",             "Creality", "Cartesian DD",   F::Marlin2, C::JunctionDeviation,    220, 220, 270, 0.4f, true,  false, 15.0f, 0.8f, 40,  60,  90, 110, 180, 1200,  800, 1500, 2000, 0.04f, 0.02f},
        {"creality_ender3_s1_pro",  "Creality Ender-3 S1 Pro",         "Creality", "Cartesian DD",   F::Marlin2, C::JunctionDeviation,    220, 220, 270, 0.4f, true,  false, 16.0f, 0.8f, 40,  65, 100, 120, 200, 1500, 1000, 2000, 2500, 0.04f, 0.02f},
        {"creality_ender3_s1_plus", "Creality Ender-3 S1 Plus",        "Creality", "Cartesian DD",   F::Marlin2, C::JunctionDeviation,    300, 300, 300, 0.4f, true,  false, 15.0f, 0.8f, 40,  55,  85, 105, 180, 1000,  700, 1400, 1800, 0.04f, 0.02f},
        {"creality_ender3_v3_se",   "Creality Ender-3 V3 SE",          "Creality", "Cartesian DD",   F::Marlin2, C::JunctionDeviation,    220, 220, 250, 0.4f, true,  false, 18.0f, 0.8f, 40,  90, 140, 180, 250, 2500, 1500, 3000, 3500, 0.04f, 0.02f},
        {"creality_ender3_v3_ke",   "Creality Ender-3 V3 KE",          "Creality", "Cartesian IS",   F::Klipper, C::SquareCornerVelocity, 220, 220, 240, 0.4f, true,  false, 22.0f, 0.8f, 40, 140, 210, 260, 400, 5000, 3000, 6000, 7000, 0.04f,  5.0f},
        {"ender3_v3",               "Creality Ender-3 V3 (CoreXZ)",    "Creality", "CoreXZ",         F::Klipper, C::SquareCornerVelocity, 220, 220, 250, 0.4f, true,  false, 24.0f, 0.8f, 45, 160, 240, 300, 450, 6000, 3500, 8000, 9000, 0.035f, 5.0f},
        {"creality_ender3_v3_plus", "Creality Ender-3 V3 Plus",        "Creality", "CoreXZ",         F::Klipper, C::SquareCornerVelocity, 300, 300, 330, 0.4f, true,  false, 24.0f, 0.8f, 45, 150, 230, 290, 420, 5000, 3000, 7000, 8000, 0.035f, 5.0f},
        {"creality_ender5",         "Creality Ender-5",                "Creality", "Cube Cartesian", F::Marlin2, C::ClassicJerk,          220, 220, 300, 0.4f, false, false, 12.0f, 6.0f, 45,  50,  70,  85, 160,  700,  500, 1000, 1500, 0.00f,  8.0f},
        {"creality_ender5_pro",     "Creality Ender-5 Pro",            "Creality", "Cube Cartesian", F::Marlin2, C::ClassicJerk,          220, 220, 300, 0.4f, false, false, 13.0f, 5.5f, 45,  55,  75,  90, 170,  800,  600, 1200, 1500, 0.00f,  8.0f},
        {"creality_ender5_plus",    "Creality Ender-5 Plus",           "Creality", "Cube Cartesian", F::Marlin2, C::ClassicJerk,          350, 350, 400, 0.4f, false, false, 13.0f, 6.5f, 45,  50,  70,  85, 160,  600,  500,  900, 1200, 0.00f,  8.0f},
        {"creality_ender5_s1",      "Creality Ender-5 S1",             "Creality", "Cube DD",        F::Marlin2, C::JunctionDeviation,    220, 220, 280, 0.4f, true,  false, 18.0f, 0.8f, 40, 100, 160, 200, 280, 2500, 1500, 3000, 3500, 0.04f, 0.02f},
        {"creality_cr10",           "Creality CR-10 (Classic)",        "Creality", "Cartesian",      F::Marlin2, C::ClassicJerk,          300, 300, 400, 0.4f, false, false, 11.0f, 6.0f, 45,  40,  55,  65, 140,  500,  400,  650,  900, 0.00f,  7.0f},
        {"creality_cr10s",          "Creality CR-10S",                 "Creality", "Cartesian",      F::Marlin2, C::ClassicJerk,          300, 300, 400, 0.4f, false, false, 12.0f, 5.5f, 45,  45,  60,  70, 150,  500,  450,  750, 1000, 0.00f,  7.0f},
        {"creality_cr10_smart_pro", "Creality CR-10 Smart Pro",        "Creality", "Cartesian DD",   F::Marlin2, C::JunctionDeviation,    300, 300, 400, 0.4f, true,  false, 15.0f, 0.8f, 40,  55,  85, 100, 180, 1000,  700, 1400, 1800, 0.04f, 0.02f},
        {"creality_cr10_se",        "Creality CR-10 SE",               "Creality", "Cartesian IS",   F::Klipper, C::SquareCornerVelocity, 220, 220, 265, 0.4f, true,  false, 22.0f, 0.8f, 40, 150, 220, 280, 400, 5000, 3000, 6500, 7500, 0.035f, 5.0f},
        {"creality_cr6_se",         "Creality CR-6 SE",                "Creality", "Cartesian",      F::Marlin2, C::JunctionDeviation,    235, 235, 250, 0.4f, false, false, 13.0f, 5.5f, 45,  45,  65,  80, 160,  700,  500, 1000, 1400, 0.00f, 0.02f},
        {"creality_k1",             "Creality K1",                     "Creality", "CoreXY",         F::Klipper, C::SquareCornerVelocity, 220, 220, 250, 0.4f, true,  false, 24.0f, 0.6f, 40, 180, 260, 320, 480, 6500, 4000, 9000, 10000, 0.03f, 6.0f},
        {"creality_k1c",            "Creality K1C",                    "Creality", "CoreXY",         F::Klipper, C::SquareCornerVelocity, 220, 220, 250, 0.4f, true,  false, 26.0f, 0.6f, 40, 190, 270, 340, 500, 7000, 4200, 9500, 11000, 0.03f, 6.0f},
        {"creality_k1_max",         "Creality K1 Max",                 "Creality", "CoreXY",         F::Klipper, C::SquareCornerVelocity, 300, 300, 300, 0.4f, true,  false, 26.0f, 0.6f, 40, 180, 260, 320, 480, 6500, 4000, 9000, 10000, 0.03f, 6.0f},
        {"creality_k2_plus",        "Creality K2 Plus",                "Creality", "CoreXY",         F::Klipper, C::SquareCornerVelocity, 350, 350, 350, 0.4f, true,  false, 30.0f, 0.6f, 45, 200, 290, 360, 550, 8000, 5000, 11000, 12000, 0.025f, 7.0f},

        // --- PRUSA RESEARCH (11 models) ---
        {"prusa_mk2s",              "Prusa i3 MK2S (Classic)",         "Prusa", "Cartesian DD",   F::Marlin2, C::ClassicJerk,          250, 210, 200, 0.4f, true,  false, 13.0f, 0.8f, 35,  45,  70,  90, 160,  800,  600, 1000, 1500, 0.04f,  8.0f},
        {"prusa_mk3",               "Prusa i3 MK3",                    "Prusa", "Cartesian DD",   F::Marlin2, C::ClassicJerk,          250, 210, 210, 0.4f, true,  false, 15.0f, 0.8f, 35,  50,  80, 110, 180, 1000,  800, 1250, 1800, 0.05f,  8.0f},
        {"prusa_mk3s",              "Prusa i3 MK3S+",                  "Prusa", "Cartesian DD",   F::Marlin2, C::ClassicJerk,          250, 210, 210, 0.4f, true,  false, 15.0f, 0.8f, 35,  55,  85, 120, 200, 1200,  800, 1500, 2000, 0.05f,  8.0f},
        {"prusa_mk35",              "Prusa MK3.5 (Input Shaper)",      "Prusa", "Cartesian IS",   F::Marlin2, C::JunctionDeviation,    250, 210, 210, 0.4f, true,  false, 18.0f, 0.8f, 35,  85, 130, 175, 260, 3000, 1800, 3500, 4000, 0.04f, 0.02f},
        {"prusa_mk39",              "Prusa MK3.9 (Nextruder)",         "Prusa", "Cartesian IS",   F::Marlin2, C::JunctionDeviation,    250, 210, 220, 0.4f, true,  false, 22.0f, 0.8f, 35,  90, 140, 190, 280, 3500, 2000, 4000, 4500, 0.035f, 0.025f},
        {"prusa_mk4",               "Prusa MK4 (Input Shaper)",        "Prusa", "Cartesian IS",   F::Marlin2, C::JunctionDeviation,    250, 210, 220, 0.4f, true,  false, 22.0f, 0.8f, 35,  90, 140, 195, 280, 3500, 2000, 4000, 4500, 0.035f, 0.025f},
        {"prusa_mk4s",              "Prusa MK4S (High-Flow IS)",       "Prusa", "Cartesian IS",   F::Marlin2, C::JunctionDeviation,    250, 210, 220, 0.4f, true,  false, 26.0f, 0.8f, 35, 110, 165, 220, 320, 4000, 2400, 4500, 5000, 0.035f, 0.025f},
        {"prusa_mini",              "Prusa MINI+ (Input Shaper)",      "Prusa", "Cantilever",     F::Marlin2, C::JunctionDeviation,    180, 180, 180, 0.4f, false, false, 15.0f, 3.2f, 55,  75, 120, 160, 220, 2500, 1500, 3200, 3500, 0.18f, 0.02f},
        {"prusa_mini_classic",      "Prusa MINI (Classic)",            "Prusa", "Cantilever",     F::Marlin2, C::ClassicJerk,          180, 180, 180, 0.4f, false, false, 13.0f, 3.2f, 55,  50,  75, 100, 180, 1200,  800, 1500, 1800, 0.20f,  8.0f},
        {"prusa_xl",                "Prusa XL (360mm CoreXY)",         "Prusa", "CoreXY",         F::Marlin2, C::JunctionDeviation,    360, 360, 360, 0.4f, true,  false, 26.0f, 0.8f, 35, 140, 210, 280, 420, 6000, 3500, 7500, 8500, 0.03f, 0.03f},
        {"prusa_core_one",          "Prusa CORE One",                  "Prusa", "CoreXY",         F::Marlin2, C::JunctionDeviation,    250, 220, 270, 0.4f, true,  false, 28.0f, 0.8f, 35, 160, 240, 310, 450, 7000, 4000, 8500, 9500, 0.03f, 0.03f},

        // --- BAMBU LAB (8 models) ---
        {"bambu_x1",                "Bambu Lab X1",                    "Bambu Lab", "CoreXY",     F::Bambu,   C::SquareCornerVelocity, 256, 256, 256, 0.4f, true,  false, 26.0f, 0.8f, 35, 190, 280, 350, 500, 8000, 5000, 10000, 12000, 0.02f, 9.0f},
        {"bambu_x1c",               "Bambu Lab X1 Carbon",             "Bambu Lab", "CoreXY",     F::Bambu,   C::SquareCornerVelocity, 256, 256, 256, 0.4f, true,  false, 28.0f, 0.8f, 35, 200, 300, 380, 500, 9000, 5000, 10000, 12000, 0.02f, 9.0f},
        {"bambu_x1e",               "Bambu Lab X1E",                   "Bambu Lab", "CoreXY",     F::Bambu,   C::SquareCornerVelocity, 256, 256, 256, 0.4f, true,  false, 28.0f, 0.8f, 35, 200, 300, 380, 500, 9000, 5000, 10000, 12000, 0.02f, 9.0f},
        {"bambu_p1p",               "Bambu Lab P1P",                   "Bambu Lab", "CoreXY",     F::Bambu,   C::SquareCornerVelocity, 256, 256, 256, 0.4f, true,  false, 26.0f, 0.8f, 35, 190, 290, 360, 500, 8000, 5000, 10000, 12000, 0.02f, 9.0f},
        {"bambu_p1s",               "Bambu Lab P1S",                   "Bambu Lab", "CoreXY",     F::Bambu,   C::SquareCornerVelocity, 256, 256, 256, 0.4f, true,  false, 28.0f, 0.8f, 35, 200, 300, 380, 500, 8000, 5000, 10000, 12000, 0.02f, 9.0f},
        {"bambu_a1",                "Bambu Lab A1",                    "Bambu Lab", "Cartesian IS", F::Bambu, C::SquareCornerVelocity, 256, 256, 256, 0.4f, true,  false, 24.0f, 0.8f, 35, 160, 240, 300, 450, 5500, 3500, 7000,  8000, 0.02f, 7.0f},
        {"bambu_a1_mini",           "Bambu Lab A1 Mini",               "Bambu Lab", "Cantilever IS", F::Bambu,C::SquareCornerVelocity, 180, 180, 180, 0.4f, true,  false, 22.0f, 0.8f, 35, 150, 230, 280, 420, 5500, 3500, 7000,  8000, 0.02f, 7.0f},
        {"bambu_h2d",               "Bambu Lab H2D (350mm)",           "Bambu Lab", "CoreXY",     F::Bambu,   C::SquareCornerVelocity, 350, 320, 325, 0.4f, true,  false, 32.0f, 0.8f, 40, 220, 320, 400, 600, 10000, 6000, 12000, 14000, 0.02f, 9.0f},

        // --- ANYCUBIC (15 models) ---
        {"anycubic_i3_mega",        "Anycubic i3 Mega (Classic)",      "Anycubic", "Cartesian",      F::Marlin2, C::ClassicJerk,          210, 210, 205, 0.4f, false, false, 11.0f, 5.5f, 40,  40,  55,  65, 140,  500,  450,  700, 1000, 0.00f,  8.0f},
        {"anycubic_mega_s",         "Anycubic Mega S",                 "Anycubic", "Cartesian",      F::Marlin2, C::ClassicJerk,          210, 210, 205, 0.4f, false, false, 12.0f, 5.5f, 40,  45,  60,  70, 150,  600,  500,  800, 1100, 0.00f,  8.0f},
        {"anycubic_mega_x",         "Anycubic Mega X",                 "Anycubic", "Cartesian",      F::Marlin2, C::ClassicJerk,          300, 300, 305, 0.4f, false, false, 12.0f, 6.0f, 40,  40,  55,  65, 140,  500,  450,  700, 1000, 0.00f,  7.0f},
        {"anycubic_mega_zero",      "Anycubic Mega Zero 2.0",          "Anycubic", "Cartesian",      F::Marlin2, C::ClassicJerk,          220, 220, 250, 0.4f, false, false, 12.0f, 5.5f, 40,  45,  60,  70, 150,  500,  500,  800, 1100, 0.00f,  8.0f},
        {"anycubic_vyper",          "Anycubic Vyper",                  "Anycubic", "Cartesian",      F::Marlin2, C::JunctionDeviation,    245, 245, 260, 0.4f, false, false, 14.0f, 5.5f, 45,  55,  75,  90, 170,  900,  600, 1200, 1600, 0.00f, 0.02f},
        {"anycubic_kobra",          "Anycubic Kobra",                  "Anycubic", "Cartesian DD",   F::Marlin2, C::JunctionDeviation,    220, 220, 250, 0.4f, true,  false, 15.0f, 1.0f, 40,  60,  90, 110, 180, 1200,  800, 1600, 2000, 0.04f, 0.02f},
        {"anycubic_kobra_go",       "Anycubic Kobra Go",               "Anycubic", "Cartesian",      F::Marlin2, C::JunctionDeviation,    220, 220, 250, 0.4f, false, false, 13.0f, 5.5f, 45,  50,  70,  85, 160,  800,  600, 1100, 1500, 0.00f, 0.02f},
        {"anycubic_kobra_neo",      "Anycubic Kobra Neo",              "Anycubic", "Cartesian DD",   F::Marlin2, C::JunctionDeviation,    220, 220, 250, 0.4f, true,  false, 14.0f, 1.0f, 40,  55,  80, 100, 170, 1000,  700, 1400, 1800, 0.04f, 0.02f},
        {"anycubic_kobra_plus",     "Anycubic Kobra Plus",             "Anycubic", "Cartesian",      F::Marlin2, C::JunctionDeviation,    300, 300, 350, 0.4f, false, false, 15.0f, 6.0f, 45,  55,  75,  95, 170,  900,  600, 1200, 1600, 0.00f, 0.02f},
        {"anycubic_kobra_max",      "Anycubic Kobra Max",              "Anycubic", "Cartesian",      F::Marlin2, C::JunctionDeviation,    400, 400, 450, 0.4f, false, false, 16.0f, 6.5f, 45,  50,  70,  90, 160,  700,  500, 1000, 1400, 0.00f, 0.02f},
        {"anycubic_kobra2",         "Anycubic Kobra 2",                "Anycubic", "Cartesian DD",   F::Marlin2, C::JunctionDeviation,    220, 220, 250, 0.4f, true,  false, 20.0f, 1.0f, 45, 110, 170, 220, 300, 2500, 1600, 3200, 4000, 0.04f, 0.02f},
        {"anycubic_kobra2_neo",     "Anycubic Kobra 2 Neo",            "Anycubic", "Cartesian DD",   F::Marlin2, C::JunctionDeviation,    220, 220, 250, 0.4f, true,  false, 19.0f, 1.0f, 45, 100, 150, 200, 280, 2500, 1500, 3000, 3500, 0.04f, 0.02f},
        {"anycubic_kobra2_pro",     "Anycubic Kobra 2 Pro",            "Anycubic", "Cartesian IS",   F::Klipper, C::SquareCornerVelocity, 220, 220, 250, 0.4f, true,  false, 24.0f, 0.8f, 45, 150, 230, 300, 420, 5000, 3000, 7000, 8000, 0.035f, 5.0f},
        {"anycubic_kobra2_max",     "Anycubic Kobra 2 Max",            "Anycubic", "Cartesian IS",   F::Klipper, C::SquareCornerVelocity, 420, 420, 500, 0.4f, true,  false, 24.0f, 0.8f, 45, 130, 200, 260, 380, 3500, 2200, 5000, 6000, 0.035f, 5.0f},
        {"anycubic_kobra3",         "Anycubic Kobra 3",                "Anycubic", "Cartesian IS",   F::Klipper, C::SquareCornerVelocity, 250, 250, 260, 0.4f, true,  false, 26.0f, 0.8f, 45, 160, 240, 300, 450, 6000, 3500, 8000, 9000, 0.03f,  5.0f},
        {"anycubic_kobra_s1",       "Anycubic Kobra S1",               "Anycubic", "CoreXY",         F::Klipper, C::SquareCornerVelocity, 250, 250, 250, 0.4f, true,  false, 28.0f, 0.8f, 45, 200, 290, 360, 500, 8000, 5000, 10000, 12000, 0.025f, 7.0f},

        // --- ELEGOO (11 models) ---
        {"elegoo_neptune2",         "Elegoo Neptune 2",                "Elegoo", "Cartesian",      F::Marlin2, C::ClassicJerk,          220, 220, 250, 0.4f, false, false, 12.0f, 5.0f, 45,  45,  60,  75, 150,  600,  500,  800, 1200, 0.00f,  8.0f},
        {"elegoo_neptune2s",        "Elegoo Neptune 2S",               "Elegoo", "Cartesian",      F::Marlin2, C::ClassicJerk,          220, 220, 250, 0.4f, false, false, 13.0f, 5.0f, 45,  50,  65,  80, 160,  700,  550,  900, 1300, 0.00f,  8.0f},
        {"elegoo_neptune3",         "Elegoo Neptune 3",                "Elegoo", "Cartesian",      F::Marlin2, C::JunctionDeviation,    220, 220, 280, 0.4f, false, false, 14.0f, 5.0f, 45,  50,  75,  90, 160,  800,  600, 1100, 1500, 0.00f, 0.02f},
        {"elegoo_neptune3_pro",     "Elegoo Neptune 3 Pro",            "Elegoo", "Cartesian DD",   F::Marlin2, C::JunctionDeviation,    225, 225, 280, 0.4f, true,  false, 16.0f, 1.0f, 40,  60,  95, 120, 190, 1400,  900, 1800, 2200, 0.04f, 0.02f},
        {"elegoo_neptune3_plus",    "Elegoo Neptune 3 Plus",           "Elegoo", "Cartesian DD",   F::Marlin2, C::JunctionDeviation,    320, 320, 400, 0.4f, true,  false, 16.0f, 1.0f, 40,  55,  85, 110, 180, 1000,  700, 1400, 1800, 0.04f, 0.02f},
        {"elegoo_neptune3_max",     "Elegoo Neptune 3 Max",            "Elegoo", "Cartesian DD",   F::Marlin2, C::JunctionDeviation,    420, 420, 500, 0.4f, true,  false, 16.0f, 1.0f, 40,  50,  80, 100, 170,  800,  600, 1100, 1500, 0.04f, 0.02f},
        {"elegoo_neptune4",         "Elegoo Neptune 4",                "Elegoo", "Cartesian IS",   F::Klipper, C::SquareCornerVelocity, 225, 225, 265, 0.4f, true,  false, 22.0f, 0.8f, 45, 140, 210, 260, 400, 4000, 2500, 5500, 6500, 0.035f, 5.0f},
        {"elegoo_neptune4_pro",     "Elegoo Neptune 4 Pro",            "Elegoo", "Cartesian IS",   F::Klipper, C::SquareCornerVelocity, 225, 225, 265, 0.4f, true,  false, 24.0f, 0.8f, 45, 150, 220, 280, 420, 4500, 2800, 6000, 7000, 0.035f, 5.0f},
        {"elegoo_neptune4_plus",    "Elegoo Neptune 4 Plus",           "Elegoo", "Cartesian IS",   F::Klipper, C::SquareCornerVelocity, 320, 320, 385, 0.4f, true,  false, 24.0f, 0.8f, 45, 130, 200, 250, 380, 3500, 2200, 4800, 5500, 0.035f, 5.0f},
        {"elegoo_neptune4_max",     "Elegoo Neptune 4 Max",            "Elegoo", "Cartesian IS",   F::Klipper, C::SquareCornerVelocity, 420, 420, 480, 0.4f, true,  false, 24.0f, 0.8f, 45, 120, 185, 230, 350, 3000, 1800, 4000, 5000, 0.035f, 5.0f},
        {"elegoo_centauri_carbon",  "Elegoo Centauri Carbon",          "Elegoo", "CoreXY",         F::Klipper, C::SquareCornerVelocity, 256, 256, 256, 0.4f, true,  false, 28.0f, 0.8f, 45, 200, 290, 360, 500, 8500, 5000, 11000, 12000, 0.025f, 7.0f},

        // --- VORON, RATRIG & VZBOT (12 models) ---
        {"voron_02",                "Voron 0.2 (120mm)",               "Voron",  "CoreXY",         F::Klipper, C::SquareCornerVelocity, 120, 120, 120, 0.4f, true,  false, 22.0f, 0.4f, 40, 160, 240, 320, 450, 7000, 4500, 10000, 12000, 0.03f, 5.0f},
        {"voron_switchwire",        "Voron Switchwire",                "Voron",  "CoreXZ",         F::Klipper, C::SquareCornerVelocity, 250, 210, 240, 0.4f, true,  false, 22.0f, 0.5f, 40, 140, 210, 280, 400, 5000, 3000, 7000,  8000, 0.035f, 5.0f},
        {"voron_trident_250",       "Voron Trident 250mm",             "Voron",  "CoreXY",         F::Klipper, C::SquareCornerVelocity, 250, 250, 250, 0.4f, true,  false, 28.0f, 0.4f, 40, 180, 260, 350, 500, 7000, 4500, 10000, 12000, 0.03f, 5.0f},
        {"voron_trident_300",       "Voron Trident 300mm",             "Voron",  "CoreXY",         F::Klipper, C::SquareCornerVelocity, 300, 300, 250, 0.4f, true,  false, 30.0f, 0.4f, 40, 180, 260, 350, 500, 6500, 4000,  9500, 11000, 0.03f, 5.0f},
        {"voron_trident_350",       "Voron Trident 350mm",             "Voron",  "CoreXY",         F::Klipper, C::SquareCornerVelocity, 350, 350, 250, 0.4f, true,  false, 30.0f, 0.4f, 40, 170, 250, 340, 480, 6000, 3800,  9000, 10000, 0.03f, 5.0f},
        {"voron_24_250",            "Voron 2.4 250mm (Klipper)",       "Voron",  "CoreXY",         F::Klipper, C::SquareCornerVelocity, 250, 250, 230, 0.4f, true,  false, 28.0f, 0.4f, 40, 185, 270, 360, 520, 7000, 4500, 10000, 12000, 0.03f, 5.0f},
        {"voron_24_300",            "Voron 2.4 300mm (Klipper)",       "Voron",  "CoreXY",         F::Klipper, C::SquareCornerVelocity, 300, 300, 280, 0.4f, true,  false, 30.0f, 0.4f, 40, 180, 260, 350, 500, 6500, 4000,  9500, 11000, 0.03f, 5.0f},
        {"voron_24_350",            "Voron 2.4 350mm (Klipper)",       "Voron",  "CoreXY",         F::Klipper, C::SquareCornerVelocity, 350, 350, 330, 0.4f, true,  false, 30.0f, 0.4f, 40, 170, 250, 340, 480, 6000, 3800,  9000, 10000, 0.03f, 5.0f},
        {"ratrig_vcore3_300",       "RatRig V-Core 3.1 300mm",         "RatRig", "CoreXY",         F::Klipper, C::SquareCornerVelocity, 300, 300, 300, 0.4f, true,  false, 32.0f, 0.4f, 45, 200, 300, 380, 550, 8000, 5000, 12000, 14000, 0.025f, 6.0f},
        {"ratrig_vcore3_400",       "RatRig V-Core 3.1 400mm",         "RatRig", "CoreXY",         F::Klipper, C::SquareCornerVelocity, 400, 400, 400, 0.4f, true,  false, 32.0f, 0.4f, 45, 180, 270, 350, 500, 6500, 4000, 10000, 12000, 0.025f, 6.0f},
        {"ratrig_vcore4_300",       "RatRig V-Core 4 300mm",           "RatRig", "Hybrid CoreXY",  F::Klipper, C::SquareCornerVelocity, 300, 300, 300, 0.4f, true,  false, 34.0f, 0.4f, 45, 220, 320, 420, 600, 10000, 6000, 14000, 16000, 0.025f, 7.0f},
        {"vzbot_235",               "VzBot 235 AWD",                   "VzBot",  "CoreXY AWD",     F::Klipper, C::SquareCornerVelocity, 235, 235, 235, 0.4f, true,  false, 36.0f, 0.4f, 50, 250, 360, 480, 700, 14000, 8000, 20000, 22000, 0.02f,  8.0f},
        {"vzbot_330",               "VzBot 330",                       "VzBot",  "CoreXY",         F::Klipper, C::SquareCornerVelocity, 330, 330, 400, 0.4f, true,  false, 35.0f, 0.4f, 50, 220, 320, 420, 600, 10000, 6000, 15000, 18000, 0.02f,  7.0f},

        // --- SOVOL (8 models) ---
        {"sovol_sv01",              "Sovol SV01",                      "Sovol", "Cartesian DD",   F::Marlin2, C::ClassicJerk,          280, 240, 300, 0.4f, true,  false, 13.0f, 1.2f, 40,  45,  65,  80, 150,  600,  500,  900, 1200, 0.04f,  8.0f},
        {"sovol_sv01_pro",          "Sovol SV01 Pro",                  "Sovol", "Cartesian DD",   F::Marlin2, C::JunctionDeviation,    280, 240, 300, 0.4f, true,  false, 15.0f, 0.8f, 40,  55,  80, 100, 170,  900,  650, 1300, 1600, 0.04f, 0.02f},
        {"sovol_sv04",              "Sovol SV04 IDEX",                 "Sovol", "Cartesian IDEX", F::Marlin2, C::JunctionDeviation,    300, 300, 400, 0.4f, true,  false, 15.0f, 1.0f, 40,  50,  75,  95, 160,  800,  600, 1100, 1500, 0.04f, 0.02f},
        {"sovol_sv06",              "Sovol SV06",                      "Sovol", "Cartesian DD",   F::Marlin2, C::JunctionDeviation,    220, 220, 250, 0.4f, true,  false, 16.0f, 0.6f, 40,  65, 100, 130, 200, 1500, 1000, 2000, 2500, 0.04f, 0.02f},
        {"sovol_sv06_plus",         "Sovol SV06 Plus",                 "Sovol", "Cartesian DD",   F::Marlin2, C::JunctionDeviation,    300, 300, 340, 0.4f, true,  false, 20.0f, 0.6f, 40,  75, 120, 160, 230, 1800, 1200, 2400, 3000, 0.035f, 0.02f},
        {"sovol_sv07",              "Sovol SV07 (Klipper)",            "Sovol", "Cartesian IS",   F::Klipper, C::SquareCornerVelocity, 220, 220, 250, 0.4f, true,  false, 22.0f, 0.6f, 40, 140, 210, 260, 400, 4500, 2800, 6000, 7000, 0.035f, 5.0f},
        {"sovol_sv07_plus",         "Sovol SV07 Plus (Klipper)",       "Sovol", "Cartesian IS",   F::Klipper, C::SquareCornerVelocity, 300, 300, 350, 0.4f, true,  false, 23.0f, 0.6f, 40, 130, 195, 250, 380, 3800, 2400, 5000, 6000, 0.035f, 5.0f},
        {"sovol_sv08",              "Sovol SV08 (350mm CoreXY)",       "Sovol", "CoreXY",         F::Klipper, C::SquareCornerVelocity, 350, 350, 345, 0.4f, true,  false, 28.0f, 0.5f, 45, 185, 270, 350, 520, 7000, 4500, 10000, 12000, 0.03f, 5.0f},

        // --- QIDI TECH (6 models) ---
        {"qidi_x_smart3",           "QIDI X-Smart 3",                  "QIDI Tech", "CoreXY",     F::Klipper, C::SquareCornerVelocity, 175, 180, 170, 0.4f, true,  false, 24.0f, 0.8f, 40, 170, 250, 320, 450, 7000, 4200,  9500, 11000, 0.03f, 6.0f},
        {"qidi_x_plus3",            "QIDI X-Plus 3",                   "QIDI Tech", "CoreXY",     F::Klipper, C::SquareCornerVelocity, 280, 280, 270, 0.4f, true,  false, 28.0f, 0.8f, 40, 190, 280, 350, 500, 8000, 4800, 11000, 12000, 0.03f, 6.0f},
        {"qidi_x_max3",             "QIDI X-Max 3",                    "QIDI Tech", "CoreXY",     F::Klipper, C::SquareCornerVelocity, 325, 325, 315, 0.4f, true,  false, 28.0f, 0.8f, 40, 180, 270, 340, 480, 7500, 4500, 10000, 12000, 0.03f, 6.0f},
        {"qidi_q1_pro",             "QIDI Q1 Pro",                     "QIDI Tech", "CoreXY",     F::Klipper, C::SquareCornerVelocity, 245, 245, 245, 0.4f, true,  false, 28.0f, 0.8f, 40, 195, 285, 360, 500, 8500, 5000, 11000, 12000, 0.025f, 6.0f},
        {"qidi_plus4",              "QIDI Plus4",                      "QIDI Tech", "CoreXY",     F::Klipper, C::SquareCornerVelocity, 305, 305, 280, 0.4f, true,  false, 30.0f, 0.8f, 45, 200, 290, 370, 520, 9000, 5200, 12000, 13000, 0.025f, 7.0f},
        {"qidi_imate_s",            "QIDI i-Mate S (Classic)",         "QIDI Tech", "Cube DD",    F::Marlin2, C::ClassicJerk,          260, 200, 200, 0.4f, true,  false, 13.0f, 1.2f, 35,  45,  65,  80, 150,  800,  600, 1000, 1400, 0.04f,  8.0f},

        // --- ARTILLERY (7 models) ---
        {"artillery_sw_x1",         "Artillery Sidewinder X1",         "Artillery", "Cartesian DD", F::Marlin2, C::ClassicJerk,        300, 300, 400, 0.4f, true,  false, 16.0f, 1.5f, 40,  50,  75,  90, 160,  700,  500, 1000, 1400, 0.04f,  8.0f},
        {"artillery_sw_x2",         "Artillery Sidewinder X2",         "Artillery", "Cartesian DD", F::Marlin2, C::JunctionDeviation,  300, 300, 400, 0.4f, true,  false, 16.0f, 1.5f, 40,  55,  80, 100, 170,  800,  600, 1100, 1500, 0.04f, 0.02f},
        {"artillery_sw_x3_pro",     "Artillery Sidewinder X3 Pro",     "Artillery", "Cartesian DD", F::Marlin2, C::JunctionDeviation,  240, 240, 260, 0.4f, true,  false, 19.0f, 1.0f, 40,  90, 140, 180, 250, 2000, 1300, 2600, 3200, 0.04f, 0.02f},
        {"artillery_sw_x4_pro",     "Artillery Sidewinder X4 Pro",     "Artillery", "Cartesian IS", F::Klipper, C::SquareCornerVelocity,240, 240, 260, 0.4f, true, false, 23.0f, 0.8f, 45, 140, 210, 270, 400, 4500, 2800, 6000, 7000, 0.035f, 5.0f},
        {"artillery_sw_x4_plus",    "Artillery Sidewinder X4 Plus",    "Artillery", "Cartesian IS", F::Klipper, C::SquareCornerVelocity,300, 300, 400, 0.4f, true, false, 23.0f, 0.8f, 45, 130, 195, 250, 380, 3500, 2200, 4800, 5800, 0.035f, 5.0f},
        {"artillery_genius_pro",    "Artillery Genius Pro",            "Artillery", "Cartesian DD", F::Marlin2, C::JunctionDeviation,  220, 220, 250, 0.4f, true,  false, 16.0f, 1.5f, 40,  55,  85, 105, 180,  900,  650, 1300, 1700, 0.04f, 0.02f},
        {"artillery_hornet",        "Artillery Hornet",                "Artillery", "Cartesian",    F::Marlin2, C::ClassicJerk,        220, 220, 250, 0.4f, false, false, 13.0f, 5.0f, 45,  45,  65,  80, 150,  700,  500,  950, 1300, 0.00f,  8.0f},

        // --- FLASHFORGE (6 models) ---
        {"flashforge_adv3",         "FlashForge Adventurer 3",         "FlashForge", "Cube",        F::Marlin2, C::ClassicJerk,        150, 150, 150, 0.4f, false, true,  12.0f, 4.5f, 40,  45,  60,  75, 140,  700,  500,  900, 1200, 0.00f,  8.0f},
        {"flashforge_adv4",         "FlashForge Adventurer 4",         "FlashForge", "Cube",        F::Marlin2, C::ClassicJerk,        220, 200, 250, 0.4f, false, true,  14.0f, 4.5f, 40,  50,  70,  90, 160,  900,  650, 1200, 1500, 0.00f,  8.0f},
        {"flashforge_ad5m",         "FlashForge Adventurer 5M",        "FlashForge", "CoreXY",      F::Klipper, C::SquareCornerVelocity,220, 220, 220, 0.4f, true,  true,  28.0f, 0.8f, 40, 190, 280, 350, 500, 8000, 5000, 10000, 12000, 0.025f, 7.0f},
        {"flashforge_ad5m_pro",     "FlashForge Adventurer 5M Pro",    "FlashForge", "CoreXY",      F::Klipper, C::SquareCornerVelocity,220, 220, 220, 0.4f, true,  true,  28.0f, 0.8f, 40, 190, 280, 350, 500, 8000, 5000, 10000, 12000, 0.025f, 7.0f},
        {"flashforge_ad5x",         "FlashForge AD5X",                 "FlashForge", "CoreXY",      F::Klipper, C::SquareCornerVelocity,220, 220, 220, 0.4f, true,  true,  28.0f, 0.8f, 40, 190, 280, 350, 500, 8000, 5000, 10000, 12000, 0.025f, 7.0f},
        {"flashforge_creator_pro2", "FlashForge Creator Pro 2 (IDEX)", "FlashForge", "Cube IDEX",   F::Marlin2, C::ClassicJerk,        200, 148, 150, 0.4f, true,  true,  13.0f, 1.3f, 35,  45,  65,  80, 140,  700,  500,  900, 1200, 0.04f,  8.0f},

        // --- ULTIMAKER (5 models) ---
        {"ultimaker_2plus",         "UltiMaker 2+ (Classic)",          "UltiMaker", "Cross-Gantry", F::Marlin2, C::ClassicJerk,        223, 223, 205, 0.4f, false, false, 12.0f, 4.5f, 25,  40,  55,  70, 150, 1500, 1000, 2000, 2500, 0.00f, 10.0f},
        {"ultimaker_3",             "UltiMaker 3",                     "UltiMaker", "Cross-Gantry", F::Marlin2, C::ClassicJerk,        215, 215, 200, 0.4f, false, false, 13.0f, 6.5f, 25,  45,  60,  75, 160, 1500, 1000, 2000, 2500, 0.00f, 10.0f},
        {"ultimaker_s3",            "UltiMaker S3",                    "UltiMaker", "Cross-Gantry", F::Marlin2, C::ClassicJerk,        230, 190, 200, 0.4f, false, false, 14.0f, 6.5f, 25,  45,  65,  80, 180, 2000, 1200, 2500, 3000, 0.00f, 12.0f},
        {"ultimaker_s5",            "UltiMaker S5",                    "UltiMaker", "Cross-Gantry", F::Marlin2, C::ClassicJerk,        330, 240, 300, 0.4f, false, false, 15.0f, 6.5f, 25,  45,  65,  80, 180, 2000, 1200, 2500, 3000, 0.00f, 12.0f},
        {"ultimaker_s7",            "UltiMaker S7",                    "UltiMaker", "Cross-Gantry", F::Marlin2, C::ClassicJerk,        330, 240, 300, 0.4f, false, false, 16.0f, 6.5f, 25,  50,  70,  90, 200, 2200, 1400, 2800, 3200, 0.00f, 12.0f},

        // --- FLSUN DELTA (5 models — origin_center = true!) ---
        {"flsun_q5",                "FLSUN Q5 (Delta)",                "FLSUN", "Delta",            F::Marlin2, C::JunctionDeviation,  200, 200, 200, 0.4f, false, true,  14.0f, 5.5f, 45,  60,  85, 110, 200, 2000, 1200, 2500, 3000, 0.00f, 0.02f},
        {"flsun_qqs_pro",           "FLSUN QQ-S Pro (Delta)",          "FLSUN", "Delta",            F::Marlin2, C::JunctionDeviation,  255, 255, 365, 0.4f, false, true,  15.0f, 6.0f, 45,  65,  95, 120, 220, 2200, 1400, 2800, 3200, 0.00f, 0.02f},
        {"flsun_sr",                "FLSUN Super Racer (SR)",          "FLSUN", "Delta",            F::Marlin2, C::JunctionDeviation,  260, 260, 330, 0.4f, false, true,  20.0f, 5.5f, 45, 110, 160, 200, 320, 3500, 2200, 4500, 5500, 0.00f, 0.025f},
        {"flsun_v400",              "FLSUN V400 (Klipper Delta)",      "FLSUN", "Delta IS",         F::Klipper, C::SquareCornerVelocity,300, 300, 410, 0.4f, true,  true,  28.0f, 0.8f, 45, 180, 270, 360, 500, 7500, 4500, 10000, 12000, 0.03f, 6.0f},
        {"flsun_t1_pro",            "FLSUN T1 Pro (Ultra Delta)",      "FLSUN", "Delta IS",         F::Klipper, C::SquareCornerVelocity,260, 260, 330, 0.4f, true,  true,  36.0f, 0.8f, 50, 240, 350, 480, 700, 14000, 8000, 20000, 24000, 0.02f, 8.0f},

        // --- KINGROON (4 models) ---
        {"kingroon_kp3s",           "Kingroon KP3S (3.0)",             "Kingroon", "Cantilever DD", F::Marlin2, C::ClassicJerk,        180, 180, 180, 0.4f, true,  false, 13.0f, 1.0f, 40,  50,  75,  95, 160, 1000,  700, 1400, 1800, 0.04f,  8.0f},
        {"kingroon_kp3s_pro",       "Kingroon KP3S Pro",               "Kingroon", "Cantilever DD", F::Marlin2, C::JunctionDeviation,  200, 200, 200, 0.4f, true,  false, 15.0f, 1.0f, 40,  60,  90, 115, 180, 1500, 1000, 2000, 2500, 0.04f, 0.02f},
        {"kingroon_kp3s_pro_v2",    "Kingroon KP3S Pro V2 (Klipper)",  "Kingroon", "Cantilever IS", F::Klipper, C::SquareCornerVelocity,200, 200, 200, 0.4f, true, false, 22.0f, 0.8f, 45, 140, 210, 260, 400, 5000, 3000, 6500, 7500, 0.035f, 5.0f},
        {"kingroon_klp1",           "Kingroon KLP1 (CoreXY)",          "Kingroon", "CoreXY",        F::Klipper, C::SquareCornerVelocity,210, 210, 210, 0.4f, true, false, 24.0f, 0.8f, 45, 170, 250, 320, 450, 7000, 4000, 9500, 11000, 0.03f, 6.0f},

        // --- ANKERMAKE (2 models) ---
        {"ankermake_m5",            "AnkerMake M5",                    "AnkerMake", "Cartesian IS", F::Marlin2, C::JunctionDeviation,  235, 235, 250, 0.4f, true,  false, 22.0f, 1.2f, 45, 140, 200, 250, 350, 4000, 2500, 5000, 6000, 0.035f, 0.025f},
        {"ankermake_m5c",           "AnkerMake M5C",                   "AnkerMake", "Cartesian IS", F::Marlin2, C::JunctionDeviation,  220, 220, 250, 0.4f, true,  false, 22.0f, 1.2f, 45, 140, 200, 250, 350, 4000, 2500, 5000, 6000, 0.035f, 0.025f},

        // --- CLASSICS, LEGACY & OTHER POPULAR BRANDS (22 models) ---
        {"anet_a8",                 "Anet A8 (Classic)",               "Anet",      "Cartesian DD", F::Marlin2, C::ClassicJerk,        220, 220, 240, 0.4f, true,  false, 10.0f, 1.5f, 35,  35,  50,  60, 120,  400,  400,  600,  800, 0.00f,  7.0f},
        {"anet_a6",                 "Anet A6 (Classic)",               "Anet",      "Cartesian DD", F::Marlin2, C::ClassicJerk,        220, 220, 250, 0.4f, true,  false, 10.0f, 1.5f, 35,  35,  50,  60, 120,  400,  400,  600,  800, 0.00f,  7.0f},
        {"anet_et4",                "Anet ET4 / ET4 Pro",              "Anet",      "Cartesian",    F::Marlin2, C::ClassicJerk,        220, 220, 250, 0.4f, false, false, 11.0f, 5.5f, 40,  40,  55,  65, 140,  500,  450,  700, 1000, 0.00f,  8.0f},
        {"tevo_tarantula",          "Tevo Tarantula (Classic)",        "Tevo",      "Cartesian",    F::Marlin2, C::ClassicJerk,        200, 200, 200, 0.4f, false, false, 11.0f, 5.5f, 40,  40,  55,  65, 140,  500,  450,  700, 1000, 0.00f,  8.0f},
        {"tevo_tornado",            "Tevo Tornado (Classic)",          "Tevo",      "Cartesian",    F::Marlin2, C::ClassicJerk,        300, 300, 400, 0.4f, false, false, 12.0f, 6.0f, 40,  40,  55,  70, 150,  500,  450,  750, 1000, 0.00f,  7.0f},
        {"wanhao_duplicator_i3",    "Wanhao Duplicator i3 / Maker Select", "Wanhao","Cartesian DD", F::Marlin2, C::ClassicJerk,        200, 200, 180, 0.4f, true,  false, 11.0f, 1.5f, 35,  40,  55,  65, 130,  500,  450,  700,  900, 0.00f,  8.0f},
        {"monoprice_mini_v2",       "Monoprice Select Mini V2",        "Monoprice", "Cantilever",   F::Marlin2, C::ClassicJerk,        120, 120, 120, 0.4f, false, false, 10.0f, 4.5f, 40,  35,  50,  60, 120,  500,  450,  700, 1000, 0.00f,  8.0f},
        {"tronxy_x5sa",             "Tronxy X5SA (CoreXY)",            "Tronxy",    "CoreXY",       F::Marlin2, C::ClassicJerk,        330, 330, 400, 0.4f, false, false, 14.0f, 6.5f, 45,  55,  80, 100, 180, 1000,  700, 1500, 2000, 0.00f,  8.0f},
        {"twotrees_sapphire_pro",   "TwoTrees Sapphire Pro",           "TwoTrees",  "CoreXY",       F::Marlin2, C::ClassicJerk,        235, 235, 235, 0.4f, false, false, 15.0f, 5.5f, 45,  65,  95, 120, 200, 1500, 1000, 2000, 2500, 0.00f, 10.0f},
        {"twotrees_bluer",          "TwoTrees Bluer",                  "TwoTrees",  "Cartesian",    F::Marlin2, C::ClassicJerk,        235, 235, 280, 0.4f, false, false, 12.0f, 5.5f, 45,  45,  65,  80, 150,  600,  500,  900, 1200, 0.00f,  8.0f},
        {"biqu_b1",                 "BIQU B1",                         "BIQU",      "Cartesian",    F::Marlin2, C::JunctionDeviation,  235, 235, 270, 0.4f, false, false, 14.0f, 5.0f, 45,  50,  75,  90, 170,  900,  650, 1200, 1600, 0.00f, 0.02f},
        {"biqu_hurakan",            "BIQU Hurakan (Klipper)",          "BIQU",      "Cartesian IS", F::Klipper, C::SquareCornerVelocity,235, 235, 270, 0.4f, false, false, 18.0f, 4.5f, 50, 110, 165, 210, 320, 3500, 2200, 4800, 5500, 0.15f, 5.0f},
        {"geeetech_a10",            "Geeetech A10",                    "Geeetech",  "Cartesian",    F::Marlin2, C::ClassicJerk,        220, 220, 260, 0.4f, false, false, 11.0f, 5.5f, 40,  40,  55,  70, 140,  500,  450,  750, 1000, 0.00f,  8.0f},
        {"geeetech_a20",            "Geeetech A20 / A20M",             "Geeetech",  "Cartesian",    F::Marlin2, C::ClassicJerk,        250, 250, 250, 0.4f, false, false, 12.0f, 5.5f, 40,  45,  60,  75, 150,  600,  500,  850, 1100, 0.00f,  8.0f},
        {"flyingbear_ghost5",       "FlyingBear Ghost 5",              "FlyingBear","CoreXY",       F::Marlin2, C::ClassicJerk,        255, 210, 200, 0.4f, false, false, 14.0f, 5.5f, 45,  55,  80, 100, 180, 1200,  800, 1600, 2000, 0.00f,  9.0f},
        {"flyingbear_ghost6",       "FlyingBear Ghost 6",              "FlyingBear","CoreXY DD",    F::Marlin2, C::JunctionDeviation,  255, 210, 210, 0.4f, true,  false, 18.0f, 1.0f, 45,  90, 135, 175, 260, 2500, 1500, 3500, 4000, 0.04f, 0.02f},
        {"snapmaker_a350",          "Snapmaker 2.0 A350",              "Snapmaker", "Cartesian DD", F::Marlin2, C::ClassicJerk,        320, 350, 330, 0.4f, true,  false, 14.0f, 1.0f, 35,  45,  65,  80, 150,  800,  600, 1000, 1400, 0.04f,  8.0f},
        {"raise3d_e2",              "Raise3D E2 (IDEX)",               "Raise3D",   "Cartesian IDEX",F::Marlin2,C::ClassicJerk,        330, 240, 240, 0.4f, true,  false, 15.0f, 1.0f, 40,  50,  75,  95, 180, 1000,  700, 1400, 1800, 0.04f,  8.0f},
        {"lulzbot_taz6",            "LulzBot TAZ 6 (Classic)",         "LulzBot",   "Cartesian DD", F::Marlin2, C::ClassicJerk,        280, 280, 250, 0.5f, true,  false, 15.0f, 1.2f, 30,  40,  55,  70, 140,  500,  400,  700, 1000, 0.00f,  8.0f},
        {"lulzbot_mini2",           "LulzBot Mini 2",                  "LulzBot",   "Cartesian DD", F::Marlin2, C::ClassicJerk,        160, 160, 180, 0.5f, true,  false, 15.0f, 1.2f, 30,  45,  60,  75, 150,  700,  500,  900, 1200, 0.00f,  8.0f},
        {"generic_220",             "Generic 220³ (Marlin 2)",         "Generic",   "Cartesian",    F::Marlin2, C::JunctionDeviation,  220, 220, 250, 0.4f, true,  false, 16.0f, 0.8f, 40,  60,  90, 120, 180, 2000, 1200, 2500, 3000, 0.04f, 0.02f},
        {"generic_300_klipper",     "Generic 300³ CoreXY (Klipper)",   "Generic",   "CoreXY",       F::Klipper, C::SquareCornerVelocity,300, 300, 300, 0.4f, true, false, 26.0f, 0.6f, 45, 160, 240, 300, 450, 6000, 3500, 8500, 10000, 0.03f, 5.0f},
        {"generic_rrf_300",         "Generic Duet / RepRapFirmware",   "Generic",   "CoreXY",       F::RepRapFirmware, C::JunctionDeviation, 300, 300, 300, 0.4f, true, false, 24.0f, 0.6f, 45, 140, 210, 270, 400, 5000, 3000, 7000, 8000, 0.035f, 0.025f},
        {"custom_printer",          "Custom Printer (Manual Bed)",     "Custom",    "Custom",       F::Marlin2, C::JunctionDeviation,  250, 250, 250, 0.4f, true,  false, 20.0f, 0.8f, 40,  80, 120, 160, 240, 2500, 1500, 3500, 4000, 0.035f, 0.02f},
    };
    return list;
}

const std::vector<MaterialPresetInfo>& list_material_presets() {
    static const std::vector<MaterialPresetInfo> list = {
        {"pla",       "PLA (Standard)",        210,  60, 1.24f, 18.0f, 20.0f, 1,  60, 100, 100,  8.0f,  0.0f},
        {"pla_plus",  "PLA+ Tough",            218,  60, 1.24f, 20.0f, 23.0f, 1,  65, 100, 100,  7.5f,  0.0f},
        {"pla_hs",    "PLA High-Speed (Hyper)",225,  60, 1.24f, 28.0f, 25.0f, 1,  80, 100, 100,  5.5f,  0.0f},
        {"pla_matte", "PLA Matte",             212,  60, 1.28f, 16.0f, 24.0f, 1,  65, 100, 100,  8.0f,  0.0f},
        {"pla_silk",  "PLA Silk (Glossy)",     220,  60, 1.24f, 14.0f, 26.0f, 1,  65, 100, 100,  9.0f, 65.0f},
        {"pla_cf",    "PLA-CF (Carbon Fiber)", 225,  60, 1.29f, 16.0f, 34.0f, 1,  60, 100, 100,  8.0f,  0.0f},
        {"wood_pla",  "Wood PLA",              205,  55, 1.18f, 13.0f, 28.0f, 1,  55, 100, 100,  9.0f, 70.0f},
        {"petg",      "PETG",                  240,  80, 1.27f, 15.0f, 24.0f, 3,  20,  50,  80, 10.0f,  0.0f},
        {"petg_cf",   "PETG-CF (Carbon Fiber)",250,  82, 1.30f, 14.0f, 36.0f, 3,  15,  45,  75, 10.0f,  0.0f},
        {"abs",       "ABS",                   255, 100, 1.04f, 18.0f, 26.0f, 4,   0,  25,  55, 12.0f,  0.0f},
        {"asa",       "ASA (UV Outdoor)",      260, 105, 1.07f, 18.0f, 30.0f, 4,   0,  20,  50, 12.0f,  0.0f},
        {"tpu95a",    "TPU 95A (Flexible)",    225,  50, 1.21f,  6.0f, 34.0f, 2,  30,  60,  80, 10.0f, 40.0f},
        {"tpu85a",    "TPU 85A (Ultra-Soft)",  222,  45, 1.18f,  3.5f, 42.0f, 2,  25,  50,  70, 12.0f, 25.0f},
        {"pa6",       "Nylon PA6",             275,  90, 1.14f, 12.0f, 48.0f, 4,   0,  15,  40, 12.0f, 75.0f},
        {"pa_cf",     "PA-CF (Nylon Carbon)",  285,  95, 1.20f, 14.0f, 65.0f, 4,   0,  15,  40, 12.0f, 80.0f},
        {"pc",        "Polycarbonate (PC)",    285, 110, 1.20f, 15.0f, 45.0f, 4,   0,  15,  40, 12.0f, 85.0f},
        {"hips",      "HIPS",                  240, 100, 1.05f, 16.0f, 28.0f, 4,   0,  20,  50, 12.0f,  0.0f},
    };
    return list;
}

const std::vector<QualityPresetInfo>& list_quality_presets() {
    static const std::vector<QualityPresetInfo> list = {
        {"ultra_008",    "0.08 mm Ultra Detail",     0.08f, 0.16f, 4, 7, 5, 20.0f},
        {"fine_010",     "0.10 mm High Detail",      0.10f, 0.18f, 4, 6, 5, 20.0f},
        {"quality_012",  "0.12 mm Fine Quality",     0.12f, 0.20f, 3, 6, 4, 20.0f},
        {"optimal_016",  "0.16 mm Optimal",          0.16f, 0.20f, 3, 5, 4, 20.0f},
        {"standard_020", "0.20 mm Standard",         0.20f, 0.24f, 3, 4, 3, 20.0f},
        {"draft_028",    "0.28 mm Extra Draft Fast", 0.28f, 0.28f, 2, 3, 3, 15.0f},
        {"strong_020",   "0.20 mm Structural Strong",0.20f, 0.24f, 5, 6, 5, 40.0f},
    };
    return list;
}

bool apply_printer_preset(SlicerConfig& cfg, const std::string& id) {
    const auto& list = list_printer_presets();
    for (const auto& p : list) {
        if (p.id == id) {
            cfg.printer_id = p.id;
            cfg.flavor = p.flavor;
            cfg.cornering_model = p.cornering;
            cfg.origin_center = p.origin_center;
            cfg.bed_size_x = p.bed_x;
            cfg.bed_size_y = p.bed_y;
            cfg.bed_size_z = p.bed_z;
            scale_line_widths_for_nozzle(cfg, p.nozzle);
            cfg.retract_length = p.retract_length;
            cfg.retract_speed = p.retract_speed;
            cfg.unretract_speed = std::max(20.0f, p.retract_speed * 0.85f);
            cfg.speed_outer_wall = p.speed_outer;
            cfg.speed_inner_wall = p.speed_inner;
            cfg.speed_infill = p.speed_infill;
            cfg.speed_top_solid = std::max(30.0f, p.speed_outer * 1.1f);
            cfg.speed_bottom_solid = std::max(35.0f, p.speed_outer * 1.2f);
            cfg.speed_support = std::max(40.0f, p.speed_inner * 0.85f);
            cfg.speed_travel = p.speed_travel;
            cfg.accel_default = p.accel_default;
            cfg.accel_outer_wall = p.accel_outer;
            cfg.accel_infill = p.accel_infill;
            cfg.accel_travel = p.accel_travel;
            cfg.accel_first_layer = std::min(1000.0f, p.accel_outer);
            cfg.pressure_advance = p.pressure_advance;
            if (p.cornering == CorneringModel::SquareCornerVelocity) cfg.square_corner_velocity = p.corner_param;
            else if (p.cornering == CorneringModel::ClassicJerk) cfg.classic_jerk = p.corner_param;
            else cfg.junction_deviation = p.corner_param;
            cfg.max_volumetric_flow = p.max_flow;
            return true;
        }
    }
    return false;
}

bool apply_material_preset(SlicerConfig& cfg, const std::string& id) {
    const auto& list = list_material_presets();
    for (const auto& m : list) {
        if (m.id == id) {
            cfg.material_id = m.id;
            cfg.nozzle_temp = m.nozzle_temp;
            cfg.first_layer_nozzle_temp = m.nozzle_temp + 5.0f;
            cfg.bed_temp = m.bed_temp;
            cfg.first_layer_bed_temp = (m.bed_temp > 0.0f) ? (m.bed_temp + 5.0f) : 0.0f;
            cfg.filament_density_g_cm3 = m.density;
            cfg.filament_cost_per_kg = m.cost_per_kg;
            cfg.fan_disable_first_layers = m.fan_disable_layers;
            cfg.fan_min_pct = m.fan_min_pct;
            cfg.fan_max_pct = m.fan_max_pct;
            cfg.fan_bridge_pct = m.fan_bridge_pct;
            cfg.max_volumetric_flow = std::min(cfg.max_volumetric_flow, m.max_flow);
            cfg.min_layer_time_sec = m.min_layer_time;
            if (m.id == "petg" || m.id == "petg_cf") cfg.support_z_gap = 0.25f;
            if (m.max_speed_cap > 0.0f) {
                cfg.speed_outer_wall = std::min(cfg.speed_outer_wall, m.max_speed_cap);
                cfg.speed_inner_wall = std::min(cfg.speed_inner_wall, m.max_speed_cap * 1.15f);
                cfg.speed_infill = std::min(cfg.speed_infill, m.max_speed_cap * 1.25f);
            }
            if (m.id == "tpu95a" || m.id == "tpu85a") {
                cfg.retract_length = std::min(cfg.retract_length, 0.5f);
            }
            return true;
        }
    }
    return false;
}

bool apply_quality_preset(SlicerConfig& cfg, const std::string& id) {
    const auto& list = list_quality_presets();
    for (const auto& q : list) {
        if (q.id == id) {
            cfg.quality_id = q.id;
            cfg.layer_height = q.layer_height;
            cfg.first_layer_height = q.first_layer_height;
            cfg.wall_count = q.walls;
            cfg.top_layers = q.top_layers;
            cfg.bottom_layers = q.bottom_layers;
            cfg.infill_density = q.infill_pct;
            cfg.support_z_gap = std::max(0.16f, q.layer_height);
            if (q.id == "strong_020") cfg.infill_pattern = InfillPattern::Cubic;
            return true;
        }
    }
    return false;
}

namespace {

inline void skip_ws(const std::string& s, size_t& i) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
}

bool parse_json_string(const std::string& s, size_t& i, std::string& out) {
    skip_ws(s, i);
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    out.clear();
    while (i < s.size()) {
        char c = s[i++];
        if (c == '"') return true;
        if (c == '\\' && i < s.size()) {
            char e = s[i++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                default: out.push_back(e); break;
            }
        } else {
            out.push_back(c);
        }
    }
    return false;
}

bool parse_json_token(const std::string& s, size_t& i, std::string& val, bool& is_str) {
    skip_ws(s, i);
    if (i >= s.size()) return false;
    if (s[i] == '"') {
        is_str = true;
        return parse_json_string(s, i, val);
    }
    is_str = false;
    size_t start = i;
    while (i < s.size() && s[i] != ',' && s[i] != '}' && !std::isspace(static_cast<unsigned char>(s[i]))) {
        ++i;
    }
    val = s.substr(start, i - start);
    return !val.empty();
}

std::string to_lower_copy(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void parse_int_csv(const std::string& s, std::vector<int>& out) {
    out.clear();
    size_t pos = 0;
    while (pos < s.size()) {
        while (pos < s.size() && !std::isdigit(static_cast<unsigned char>(s[pos])) && s[pos] != '-') ++pos;
        if (pos >= s.size()) break;
        char* endp = nullptr;
        long v = std::strtol(s.c_str() + pos, &endp, 10);
        if (endp == s.c_str() + pos) break;
        if (v >= 0 && v < 100000) out.push_back(static_cast<int>(v));
        pos = static_cast<size_t>(endp - s.c_str());
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

}  // namespace

bool parse_config_json(const std::string& json, SlicerConfig& cfg, std::string& err) {
    size_t i = 0;
    skip_ws(json, i);
    if (i >= json.size()) return true;
    if (json[i] != '{') {
        err = "Config JSON must start with '{'";
        return false;
    }
    ++i;

    struct KV { std::string k; std::string v; bool is_str; };
    std::vector<KV> kvs;

    while (i < json.size()) {
        skip_ws(json, i);
        if (i < json.size() && json[i] == '}') { ++i; break; }
        std::string key;
        if (!parse_json_string(json, i, key)) {
            err = "Invalid JSON key";
            return false;
        }
        skip_ws(json, i);
        if (i >= json.size() || json[i] != ':') {
            err = "Expected ':' after JSON key '" + key + "'";
            return false;
        }
        ++i;
        std::string val;
        bool is_str = false;
        if (!parse_json_token(json, i, val, is_str)) {
            err = "Invalid JSON value for '" + key + "'";
            return false;
        }
        kvs.push_back({key, val, is_str});
        skip_ws(json, i);
        if (i < json.size() && json[i] == ',') { ++i; continue; }
        if (i < json.size() && json[i] == '}') { ++i; break; }
    }

    // Pass 1: presets (printer -> material -> quality)
    for (const auto& kv : kvs) {
        if (kv.k == "printer" || kv.k == "printer_id") apply_printer_preset(cfg, kv.v);
    }
    for (const auto& kv : kvs) {
        if (kv.k == "material" || kv.k == "material_id") apply_material_preset(cfg, kv.v);
    }
    for (const auto& kv : kvs) {
        if (kv.k == "quality" || kv.k == "quality_id") apply_quality_preset(cfg, kv.v);
    }

    auto as_f = [](const std::string& v) { return static_cast<float>(std::strtod(v.c_str(), nullptr)); };
    auto as_i = [](const std::string& v) { return static_cast<int>(std::strtol(v.c_str(), nullptr, 10)); };
    auto as_b = [](const std::string& v) { return v == "true" || v == "1"; };

    // Pass 2: explicit field overrides (nozzle first if line widths not explicitly overridden)
    for (const auto& kv : kvs) {
        if (kv.k == "nozzle_diameter") {
            scale_line_widths_for_nozzle(cfg, std::clamp(as_f(kv.v), 0.15f, 1.5f));
        }
    }

    for (const auto& kv : kvs) {
        const std::string& k = kv.k;
        const std::string& v = kv.v;
        std::string vl = to_lower_copy(v);

        if (k == "flavor") {
            if (vl == "klipper") cfg.flavor = GCodeFlavor::Klipper;
            else if (vl == "rrf" || vl == "reprap" || vl == "reprapfirmware") cfg.flavor = GCodeFlavor::RepRapFirmware;
            else if (vl == "bambu") cfg.flavor = GCodeFlavor::Bambu;
            else cfg.flavor = GCodeFlavor::Marlin2;
        } else if (k == "cornering_model") {
            if (vl == "scv" || vl == "klipper") cfg.cornering_model = CorneringModel::SquareCornerVelocity;
            else if (vl == "jerk") cfg.cornering_model = CorneringModel::ClassicJerk;
            else cfg.cornering_model = CorneringModel::JunctionDeviation;
        } else if (k == "relative_extrusion") cfg.relative_extrusion = as_b(v);
        else if (k == "origin_center") cfg.origin_center = as_b(v);
        else if (k == "bed_size_x") cfg.bed_size_x = std::clamp(as_f(v), 50.0f, 1200.0f);
        else if (k == "bed_size_y") cfg.bed_size_y = std::clamp(as_f(v), 50.0f, 1200.0f);
        else if (k == "bed_size_z") cfg.bed_size_z = std::clamp(as_f(v), 50.0f, 1200.0f);
        else if (k == "filament_diameter") cfg.filament_diameter = std::clamp(as_f(v), 1.0f, 3.5f);
        else if (k == "filament_density_g_cm3") cfg.filament_density_g_cm3 = std::clamp(as_f(v), 0.5f, 5.0f);
        else if (k == "filament_cost_per_kg") cfg.filament_cost_per_kg = std::max(0.0f, as_f(v));
        else if (k == "auto_center") cfg.auto_center = as_b(v);
        else if (k == "auto_drop_to_bed") cfg.auto_drop_to_bed = as_b(v);
        else if (k == "auto_lay_flat") cfg.auto_lay_flat = as_b(v);
        else if (k == "pos_x") cfg.pos_x = as_f(v);
        else if (k == "pos_y") cfg.pos_y = as_f(v);
        else if (k == "scale") { float s = std::clamp(as_f(v), 0.01f, 100.0f); cfg.scale_x = cfg.scale_y = cfg.scale_z = s; }
        else if (k == "scale_x") cfg.scale_x = std::clamp(as_f(v), 0.01f, 100.0f);
        else if (k == "scale_y") cfg.scale_y = std::clamp(as_f(v), 0.01f, 100.0f);
        else if (k == "scale_z") cfg.scale_z = std::clamp(as_f(v), 0.01f, 100.0f);
        else if (k == "rotate_x_deg") cfg.rotate_x_deg = as_f(v);
        else if (k == "rotate_y_deg") cfg.rotate_y_deg = as_f(v);
        else if (k == "rotate_z_deg") cfg.rotate_z_deg = as_f(v);
        else if (k == "mirror_x") cfg.mirror_x = as_b(v);
        else if (k == "mirror_y") cfg.mirror_y = as_b(v);
        else if (k == "copies") cfg.copies = std::clamp(as_i(v), 1, 16);
        else if (k == "copy_spacing") cfg.copy_spacing = std::clamp(as_f(v), 1.0f, 50.0f);
        else if (k == "layer_height") cfg.layer_height = std::clamp(as_f(v), 0.04f, 0.80f);
        else if (k == "first_layer_height") cfg.first_layer_height = std::clamp(as_f(v), 0.06f, 0.80f);
        else if (k == "adaptive_layer_height") cfg.adaptive_layer_height = as_b(v);
        else if (k == "adaptive_min_layer") cfg.adaptive_min_layer = std::clamp(as_f(v), 0.04f, 0.50f);
        else if (k == "adaptive_max_layer") cfg.adaptive_max_layer = std::clamp(as_f(v), 0.08f, 0.80f);
        else if (k == "adaptive_cusp_error") cfg.adaptive_cusp_error = std::clamp(as_f(v), 0.005f, 0.20f);
        else if (k == "wall_count") cfg.wall_count = std::clamp(as_i(v), 1, 12);
        else if (k == "wall_order") cfg.wall_order = (vl == "outer" || vl == "outer_first") ? WallOrder::OuterFirst : WallOrder::InnerFirst;
        else if (k == "outer_wall_width") cfg.outer_wall_width = std::clamp(as_f(v), 0.15f, 2.0f);
        else if (k == "inner_wall_width") cfg.inner_wall_width = std::clamp(as_f(v), 0.15f, 2.0f);
        else if (k == "first_layer_width") cfg.first_layer_width = std::clamp(as_f(v), 0.15f, 2.0f);
        else if (k == "infill_width") cfg.infill_width = std::clamp(as_f(v), 0.15f, 2.0f);
        else if (k == "top_solid_width") cfg.top_solid_width = std::clamp(as_f(v), 0.15f, 2.0f);
        else if (k == "support_width") cfg.support_width = std::clamp(as_f(v), 0.15f, 2.0f);
        else if (k == "arachne_thin_walls") cfg.arachne_thin_walls = as_b(v);
        else if (k == "elephant_foot_comp") cfg.elephant_foot_comp = std::clamp(as_f(v), 0.0f, 1.0f);
        else if (k == "xy_contour_comp") cfg.xy_contour_comp = std::clamp(as_f(v), -1.0f, 1.0f);
        else if (k == "xy_hole_comp") cfg.xy_hole_comp = std::clamp(as_f(v), -1.0f, 1.0f);
        else if (k == "spiral_vase") cfg.spiral_vase = as_b(v);
        else if (k == "fuzzy_skin") {
            if (vl == "outside" || vl == "outer" || vl == "true" || vl == "1") cfg.fuzzy_skin = FuzzySkinMode::Outside;
            else if (vl == "all" || vl == "all_walls") cfg.fuzzy_skin = FuzzySkinMode::AllWalls;
            else cfg.fuzzy_skin = FuzzySkinMode::None;
        } else if (k == "fuzzy_skin_thickness") cfg.fuzzy_skin_thickness = std::clamp(as_f(v), 0.05f, 1.5f);
        else if (k == "fuzzy_skin_point_dist") cfg.fuzzy_skin_point_dist = std::clamp(as_f(v), 0.2f, 4.0f);
        else if (k == "small_perimeter_speed") cfg.small_perimeter_speed = std::clamp(as_f(v), 5.0f, 300.0f);
        else if (k == "small_perimeter_radius") cfg.small_perimeter_radius = std::clamp(as_f(v), 0.0f, 30.0f);
        else if (k == "top_layers") cfg.top_layers = std::clamp(as_i(v), 0, 20);
        else if (k == "bottom_layers") cfg.bottom_layers = std::clamp(as_i(v), 0, 20);
        else if (k == "top_pattern") {
            if (vl == "concentric") cfg.top_pattern = TopBottomPattern::Concentric;
            else if (vl == "rectilinear") cfg.top_pattern = TopBottomPattern::Rectilinear;
            else cfg.top_pattern = TopBottomPattern::Monotonic;
        } else if (k == "bottom_pattern") {
            if (vl == "concentric") cfg.bottom_pattern = TopBottomPattern::Concentric;
            else if (vl == "rectilinear") cfg.bottom_pattern = TopBottomPattern::Rectilinear;
            else cfg.bottom_pattern = TopBottomPattern::Monotonic;
        } else if (k == "detect_bridges") cfg.detect_bridges = as_b(v);
        else if (k == "bridge_flow") cfg.bridge_flow = std::clamp(as_f(v), 0.5f, 1.5f);
        else if (k == "bridge_speed") cfg.bridge_speed = std::clamp(as_f(v), 5.0f, 150.0f);
        else if (k == "ironing") cfg.ironing = as_b(v);
        else if (k == "ironing_flow") cfg.ironing_flow = std::clamp(as_f(v), 0.02f, 0.50f);
        else if (k == "ironing_spacing") cfg.ironing_spacing = std::clamp(as_f(v), 0.05f, 0.50f);
        else if (k == "ironing_speed") cfg.ironing_speed = std::clamp(as_f(v), 5.0f, 150.0f);
        else if (k == "infill_density") cfg.infill_density = std::clamp(as_f(v), 0.0f, 100.0f);
        else if (k == "infill_pattern") {
            if (vl == "rectilinear") cfg.infill_pattern = InfillPattern::Rectilinear;
            else if (vl == "grid") cfg.infill_pattern = InfillPattern::Grid;
            else if (vl == "triangles") cfg.infill_pattern = InfillPattern::Triangles;
            else if (vl == "cubic") cfg.infill_pattern = InfillPattern::Cubic;
            else if (vl == "honeycomb") cfg.infill_pattern = InfillPattern::Honeycomb;
            else if (vl == "concentric") cfg.infill_pattern = InfillPattern::Concentric;
            else if (vl == "lightning") cfg.infill_pattern = InfillPattern::Lightning;
            else cfg.infill_pattern = InfillPattern::Gyroid;
        } else if (k == "infill_overlap_pct") cfg.infill_overlap_pct = std::clamp(as_f(v), 0.0f, 60.0f);
        else if (k == "infill_angle_deg") cfg.infill_angle_deg = as_f(v);
        else if (k == "support_type") {
            if (vl == "grid" || vl == "normal") cfg.support_type = SupportType::NormalGrid;
            else if (vl == "snug" || vl == "normal_snug") cfg.support_type = SupportType::NormalSnug;
            else if (vl == "tree" || vl == "organic") cfg.support_type = SupportType::OrganicTree;
            else cfg.support_type = SupportType::None;
        } else if (k == "support_placement") {
            cfg.support_placement = (vl == "everywhere") ? SupportPlacement::Everywhere : SupportPlacement::BuildplateOnly;
        } else if (k == "support_overhang_angle") cfg.support_overhang_angle = std::clamp(as_f(v), 15.0f, 80.0f);
        else if (k == "support_density") cfg.support_density = std::clamp(as_f(v), 5.0f, 60.0f);
        else if (k == "support_z_gap") cfg.support_z_gap = std::clamp(as_f(v), 0.0f, 1.0f);
        else if (k == "support_xy_gap") cfg.support_xy_gap = std::clamp(as_f(v), 0.1f, 3.0f);
        else if (k == "support_interface_layers") cfg.support_interface_layers = std::clamp(as_i(v), 0, 5);
        else if (k == "support_interface_density") cfg.support_interface_density = std::clamp(as_f(v), 25.0f, 100.0f);
        else if (k == "tree_branch_diameter") cfg.tree_branch_diameter = std::clamp(as_f(v), 1.0f, 12.0f);
        else if (k == "tree_max_branch_angle") cfg.tree_max_branch_angle = std::clamp(as_f(v), 15.0f, 65.0f);
        else if (k == "support_brim") cfg.support_brim = as_b(v);
        else if (k == "adhesion") {
            if (vl == "none") cfg.adhesion = AdhesionType::None;
            else if (vl == "brim") cfg.adhesion = AdhesionType::Brim;
            else if (vl == "raft") cfg.adhesion = AdhesionType::Raft;
            else cfg.adhesion = AdhesionType::Skirt;
        } else if (k == "brim_width") cfg.brim_width = std::clamp(as_f(v), 1.0f, 30.0f);
        else if (k == "brim_gap") cfg.brim_gap = std::clamp(as_f(v), 0.0f, 1.0f);
        else if (k == "skirt_loops") cfg.skirt_loops = std::clamp(as_i(v), 0, 10);
        else if (k == "skirt_distance") cfg.skirt_distance = std::clamp(as_f(v), 1.0f, 30.0f);
        else if (k == "raft_layers") cfg.raft_layers = std::clamp(as_i(v), 1, 5);
        else if (k == "raft_z_gap") cfg.raft_z_gap = std::clamp(as_f(v), 0.05f, 0.50f);
        else if (k == "seam_position") {
            if (vl == "aligned") cfg.seam_position = SeamPosition::Aligned;
            else if (vl == "rear") cfg.seam_position = SeamPosition::Rear;
            else if (vl == "nearest") cfg.seam_position = SeamPosition::Nearest;
            else if (vl == "random") cfg.seam_position = SeamPosition::Random;
            else cfg.seam_position = SeamPosition::SharpestCorner;
        } else if (k == "scarf_seam") cfg.scarf_seam = as_b(v);
        else if (k == "scarf_length") cfg.scarf_length = std::clamp(as_f(v), 2.0f, 30.0f);
        else if (k == "combing") cfg.combing = as_b(v);
        else if (k == "retract_length") cfg.retract_length = std::clamp(as_f(v), 0.0f, 12.0f);
        else if (k == "retract_speed") cfg.retract_speed = std::clamp(as_f(v), 5.0f, 120.0f);
        else if (k == "unretract_speed") cfg.unretract_speed = std::clamp(as_f(v), 5.0f, 120.0f);
        else if (k == "retract_min_travel") cfg.retract_min_travel = std::clamp(as_f(v), 0.1f, 20.0f);
        else if (k == "z_hop") cfg.z_hop = std::clamp(as_f(v), 0.0f, 5.0f);
        else if (k == "wipe_on_retract") cfg.wipe_on_retract = as_b(v);
        else if (k == "wipe_distance") cfg.wipe_distance = std::clamp(as_f(v), 0.2f, 10.0f);
        else if (k == "arc_fitting") cfg.arc_fitting = as_b(v);
        else if (k == "nozzle_temp") cfg.nozzle_temp = std::clamp(as_f(v), 0.0f, 350.0f);
        else if (k == "first_layer_nozzle_temp") cfg.first_layer_nozzle_temp = std::clamp(as_f(v), 0.0f, 350.0f);
        else if (k == "bed_temp") cfg.bed_temp = std::clamp(as_f(v), 0.0f, 140.0f);
        else if (k == "first_layer_bed_temp") cfg.first_layer_bed_temp = std::clamp(as_f(v), 0.0f, 140.0f);
        else if (k == "flow_ratio") cfg.flow_ratio = std::clamp(as_f(v), 0.5f, 1.5f);
        else if (k == "max_volumetric_flow") cfg.max_volumetric_flow = std::clamp(as_f(v), 1.0f, 80.0f);
        else if (k == "pressure_advance") cfg.pressure_advance = std::clamp(as_f(v), 0.0f, 1.5f);
        else if (k == "speed_outer_wall") cfg.speed_outer_wall = std::clamp(as_f(v), 5.0f, 600.0f);
        else if (k == "speed_inner_wall") cfg.speed_inner_wall = std::clamp(as_f(v), 5.0f, 600.0f);
        else if (k == "speed_infill") cfg.speed_infill = std::clamp(as_f(v), 5.0f, 600.0f);
        else if (k == "speed_top_solid") cfg.speed_top_solid = std::clamp(as_f(v), 5.0f, 600.0f);
        else if (k == "speed_bottom_solid") cfg.speed_bottom_solid = std::clamp(as_f(v), 5.0f, 600.0f);
        else if (k == "speed_gap_fill") cfg.speed_gap_fill = std::clamp(as_f(v), 5.0f, 400.0f);
        else if (k == "speed_support") cfg.speed_support = std::clamp(as_f(v), 5.0f, 500.0f);
        else if (k == "speed_support_interface") cfg.speed_support_interface = std::clamp(as_f(v), 5.0f, 300.0f);
        else if (k == "speed_travel") cfg.speed_travel = std::clamp(as_f(v), 20.0f, 1000.0f);
        else if (k == "speed_first_layer") cfg.speed_first_layer = std::clamp(as_f(v), 5.0f, 150.0f);
        else if (k == "overhang_slowdown") cfg.overhang_slowdown = as_b(v);
        else if (k == "min_layer_time_sec") cfg.min_layer_time_sec = std::clamp(as_f(v), 0.0f, 60.0f);
        else if (k == "fan_disable_first_layers") cfg.fan_disable_first_layers = std::clamp(as_i(v), 0, 10);
        else if (k == "fan_min_pct") cfg.fan_min_pct = std::clamp(as_i(v), 0, 100);
        else if (k == "fan_max_pct") cfg.fan_max_pct = std::clamp(as_i(v), 0, 100);
        else if (k == "fan_bridge_pct") cfg.fan_bridge_pct = std::clamp(as_i(v), 0, 100);
        else if (k == "accel_default") cfg.accel_default = std::clamp(as_f(v), 100.0f, 30000.0f);
        else if (k == "accel_outer_wall") cfg.accel_outer_wall = std::clamp(as_f(v), 100.0f, 20000.0f);
        else if (k == "accel_infill") cfg.accel_infill = std::clamp(as_f(v), 100.0f, 30000.0f);
        else if (k == "accel_travel") cfg.accel_travel = std::clamp(as_f(v), 100.0f, 30000.0f);
        else if (k == "accel_first_layer") cfg.accel_first_layer = std::clamp(as_f(v), 100.0f, 10000.0f);
        else if (k == "junction_deviation") cfg.junction_deviation = std::clamp(as_f(v), 0.005f, 0.25f);
        else if (k == "square_corner_velocity") cfg.square_corner_velocity = std::clamp(as_f(v), 1.0f, 30.0f);
        else if (k == "classic_jerk") cfg.classic_jerk = std::clamp(as_f(v), 1.0f, 40.0f);
        else if (k == "pause_layers") parse_int_csv(v, cfg.pause_layers);
        else if (k == "custom_start_gcode") cfg.custom_start_gcode = v;
        else if (k == "custom_end_gcode") cfg.custom_end_gcode = v;
        else if (k == "threads") cfg.threads = std::clamp(as_i(v), 0, 64);
        else if (k == "enable_layer_cache") cfg.enable_layer_cache = as_b(v);
        else if (k == "emit_gcode") cfg.emit_gcode = as_b(v);
        else if (k == "emit_binary_toolpath") cfg.emit_binary_toolpath = as_b(v);
    }
    return true;
}

std::string presets_to_json() {
    std::string out;
    out.reserve(32768);
    out += "{\"printers\":[";
    const auto& pr = list_printer_presets();
    for (size_t i = 0; i < pr.size(); ++i) {
        char buf[512];
        std::snprintf(buf, sizeof(buf),
            "%s{\"id\":\"%s\",\"name\":\"%s\",\"vendor\":\"%s\",\"kinematics\":\"%s\","
            "\"flavor\":\"%s\",\"bedX\":%.0f,\"bedY\":%.0f,\"bedZ\":%.0f,\"nozzle\":%.2f,"
            "\"directDrive\":%s,\"originCenter\":%s,\"maxFlow\":%.1f,"
            "\"retractLen\":%.1f,\"retractSpd\":%.0f,\"speedOuter\":%.0f,\"speedInner\":%.0f,"
            "\"speedInfill\":%.0f,\"speedTravel\":%.0f,\"accelDefault\":%.0f,\"accelOuter\":%.0f,\"pa\":%.3f}",
            i ? "," : "", pr[i].id.c_str(), pr[i].name.c_str(), pr[i].vendor.c_str(),
            pr[i].kinematics.c_str(), flavor_str(pr[i].flavor),
            pr[i].bed_x, pr[i].bed_y, pr[i].bed_z, pr[i].nozzle,
            pr[i].direct_drive ? "true" : "false", pr[i].origin_center ? "true" : "false",
            pr[i].max_flow, pr[i].retract_length, pr[i].retract_speed,
            pr[i].speed_outer, pr[i].speed_inner, pr[i].speed_infill, pr[i].speed_travel,
            pr[i].accel_default, pr[i].accel_outer, pr[i].pressure_advance);
        out += buf;
    }
    out += "],\"materials\":[";
    const auto& mat = list_material_presets();
    for (size_t i = 0; i < mat.size(); ++i) {
        char buf[320];
        std::snprintf(buf, sizeof(buf),
            "%s{\"id\":\"%s\",\"name\":\"%s\",\"nozzleTemp\":%.0f,\"bedTemp\":%.0f,"
            "\"density\":%.2f,\"maxFlow\":%.1f,\"costPerKg\":%.1f,\"fanMax\":%d,\"minLayerTime\":%.1f}",
            i ? "," : "", mat[i].id.c_str(), mat[i].name.c_str(),
            mat[i].nozzle_temp, mat[i].bed_temp, mat[i].density, mat[i].max_flow,
            mat[i].cost_per_kg, mat[i].fan_max_pct, mat[i].min_layer_time);
        out += buf;
    }
    out += "],\"qualities\":[";
    const auto& q = list_quality_presets();
    for (size_t i = 0; i < q.size(); ++i) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "%s{\"id\":\"%s\",\"name\":\"%s\",\"layerHeight\":%.2f,\"firstLayerHeight\":%.2f,"
            "\"walls\":%d,\"topLayers\":%d,\"bottomLayers\":%d,\"infillPct\":%.0f}",
            i ? "," : "", q[i].id.c_str(), q[i].name.c_str(),
            q[i].layer_height, q[i].first_layer_height, q[i].walls,
            q[i].top_layers, q[i].bottom_layers, q[i].infill_pct);
        out += buf;
    }
    out += "]}";
    return out;
}

std::string stats_to_json(const SliceStats& st, const SlicerConfig& cfg) {
    char buf[1536];
    std::snprintf(buf, sizeof(buf),
        "{\"triCount\":%u,\"layerCount\":%u,\"totalMoves\":%u,\"extrusionMoves\":%u,"
        "\"travelMoves\":%u,\"arcMoves\":%u,\"retractCount\":%u,\"cachedLayers\":%u,"
        "\"threadsUsed\":%u,\"modelSize\":[%.2f,%.2f,%.2f],\"bedSize\":[%.0f,%.0f,%.0f],"
        "\"modelVolumeCm3\":%.3f,\"overhangFacePct\":%.2f,\"fitsBed\":%s,\"openChains\":%u,"
        "\"printTimeSec\":%.2f,\"filamentMm\":%.2f,\"filamentCm3\":%.3f,\"filamentG\":%.2f,"
        "\"estimatedCost\":%.3f,\"maxVolumetricFlow\":%.2f,\"gcodeBytes\":%zu,\"ktpBytes\":%zu,"
        "\"timingsMs\":{\"loadTransform\":%.3f,\"sliceStitch\":%.3f,\"surfaces\":%.3f,"
        "\"supports\":%.3f,\"perimetersInfill\":%.3f,\"pathMotion\":%.3f,\"gcodeWrite\":%.3f,\"total\":%.3f},",
        st.tri_count, st.layer_count, st.total_moves, st.extrusion_moves,
        st.travel_moves, st.arc_moves, st.retract_count, st.cached_layers,
        st.threads_used, st.model_size_x, st.model_size_y, st.model_size_z,
        cfg.bed_size_x, cfg.bed_size_y, cfg.bed_size_z,
        st.model_volume_cm3, st.overhang_face_pct, st.fits_bed ? "true" : "false",
        st.non_manifold_open_chains,
        st.print_time_sec, st.filament_mm, st.filament_cm3, st.filament_g,
        st.estimated_cost, st.max_volumetric_flow_mm3s, st.gcode_bytes, st.ktp_bytes,
        st.timings.load_transform_ms, st.timings.slice_stitch_ms, st.timings.surfaces_overhang_ms,
        st.timings.supports_ms, st.timings.perimeters_infill_ms, st.timings.path_motion_plan_ms,
        st.timings.gcode_serialize_ms, st.timings.total_ms);

    std::string out(buf);
    out += "\"features\":[";
    for (size_t f = 0; f < static_cast<size_t>(FeatureType::Count); ++f) {
        char fb[160];
        std::snprintf(fb, sizeof(fb), "%s{\"id\":%zu,\"name\":\"%s\",\"timeSec\":%.2f,\"filamentMm\":%.2f}",
            f ? "," : "", f, feature_type_name(static_cast<FeatureType>(f)),
            st.feature_time_sec[f], st.feature_filament_mm[f]);
        out += fb;
    }
    out += "]}";
    return out;
}

}  // namespace katman
