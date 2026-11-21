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

enum ParameterIndex : size_t {
    master, breathPressure, jetVelocity, edgeInteraction, turbulence, cavityVolume,
    openHoleArea, apertureNeckLength, resonatorDamping, resonatorDetuning, vibratoDepth,
    attackArticulation, formantColor, model, voiceMode, voiceLimit, portamento,
    bendRange, velocityResponse, aftertouchResponse, breathCurve, turbulenceColor,
    excitationShape, harmonicEnhancement, parameterCount
};

inline constexpr std::array<Parameter, parameterCount> parameters = [] {
    std::array<Parameter, parameterCount> result {};
    result[master] = masterGain;
    for (size_t index = 0; index < holes.size(); ++index)
        result[index + 1] = holes[index];
    result[model] = {"model", "Sound Model", "", 0.0f, 3.0f, 0.0f};
    result[voiceMode] = {"voiceMode", "Voice Mode", "", 0.0f, 1.0f, 1.0f};
    result[voiceLimit] = {"voiceLimit", "Voice Limit", "voices", 1.0f, 16.0f, 16.0f};
    result[portamento] = {"portamento", "Legato Glide", "ms", 0.0f, 250.0f, 32.0f};
    result[bendRange] = {"bendRange", "Pitch Bend Range", "semitones", 0.0f, 24.0f, 2.0f};
    result[velocityResponse] = {"velocityResponse", "Velocity Response", "%", 0.0f, 100.0f, 72.0f};
    result[aftertouchResponse] = {"aftertouchResponse", "Aftertouch Response", "%", 0.0f, 100.0f, 40.0f};
    result[breathCurve] = {"breathCurve", "Breath Response Curve", "", 0.5f, 2.0f, 1.0f};
    result[turbulenceColor] = {"turbulenceColor", "Turbulence Color", "%", 0.0f, 100.0f, 45.0f};
    result[excitationShape] = {"excitationShape", "Excitation Nonlinearity", "%", 0.0f, 100.0f, 45.0f};
    result[harmonicEnhancement] = {"harmonicEnhancement", "Harmonic Enhancement", "%", 0.0f, 100.0f, 20.0f};
    return result;
}();

inline constexpr std::array<std::string_view, 4> modelNames {
    "Ceramic Authentic", "Retro Fantasy", "Crystal Resonator", "Electronic Hybrid"
};

} // namespace ocarina
