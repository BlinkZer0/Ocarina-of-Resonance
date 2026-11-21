#pragma once

#include <array>
#include <string_view>

namespace ocarina {

struct Parameter {
    std::string_view id;
    std::string_view name;
    std::string_view unit;
    float minimum, maximum, initial;
};

inline constexpr Parameter masterGain {"masterGain", "Master Volume", "%", 0.0f, 100.0f, 70.0f};

inline constexpr std::array<Parameter, 12> holes {{
    {"breathPressure", "Breath Pressure", "%", 0.0f, 100.0f, 55.0f},
    {"jetVelocity", "Jet Velocity", "%", 0.0f, 100.0f, 50.0f},
    {"edgeInteraction", "Edge Interaction", "%", 0.0f, 100.0f, 50.0f},
    {"turbulence", "Turbulence", "%", 0.0f, 100.0f, 20.0f},
    {"cavityVolume", "Cavity Volume", "cm³", 10.0f, 500.0f, 110.0f},
    {"openHoleArea", "Open-Hole Area", "mm²", 5.0f, 300.0f, 45.0f},
    {"apertureNeckLength", "Aperture Neck Length", "mm", 1.0f, 35.0f, 7.0f},
    {"resonatorDamping", "Resonator Damping", "%", 0.0f, 100.0f, 35.0f},
    {"resonatorDetuning", "Resonator Detuning", "cents", -50.0f, 50.0f, 0.0f},
    {"vibratoDepth", "Vibrato Depth", "cents", 0.0f, 60.0f, 8.0f},
    {"attackArticulation", "Attack Articulation", "%", 0.0f, 100.0f, 45.0f},
    {"formantColor", "Formant Color", "%", 0.0f, 100.0f, 50.0f},
}};

} // namespace ocarina
