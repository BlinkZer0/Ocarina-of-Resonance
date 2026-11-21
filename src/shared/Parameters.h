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
    excitationShape, harmonicEnhancement,
    patchGain, tempo, filterEnabled, filterHz, filterQ,
    filterDb, driveEnabled, driveMode, driveAmount, driveMix,
    fmEnabled, fmRatio, fmIndex, fmMix, grainEnabled,
    grainSize, grainPitch, grainMix, tremoloEnabled, tremoloRate,
    tremoloDepth, phaserEnabled, phaserRate, phaserDepth, phaserMix,
    chorusEnabled, chorusRate, chorusDepth, chorusWidth, chorusMix,
    crusherEnabled, crusherBits, crusherMix, reducerEnabled, reducerRate,
    reducerMix, delayEnabled, delayMode, delaySync, delayDivision,
    delayTime, delayFeedback, delayCutoff, delayMix, reverbEnabled,
    reverbMode, reverbDecay, reverbDamping, reverbMix,
    parameterCount
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
    result[patchGain] = {"patchGain", "Preset gain", "dB", -18.0f, 6.0f, 0.0f};
    result[tempo] = {"tempo", "Tempo", "BPM", 40.0f, 250.0f, 120.0f};
    result[filterEnabled] = {"filterEnabled", "Filter enabled", "", 0.0f, 1.0f, 0.0f};
    result[filterHz] = {"filterHz", "Frequency", "Hz", 80.0f, 10000.0f, 2100.0f};
    result[filterQ] = {"filterQ", "Resonance", "", 0.35f, 4.0f, 0.8f};
    result[filterDb] = {"filterDb", "Gain", "dB", -18.0f, 12.0f, 0.0f};
    result[driveEnabled] = {"driveEnabled", "Saturation enabled", "", 0.0f, 1.0f, 0.0f};
    result[driveMode] = {"driveMode", "Character", "", 0.0f, 2.0f, 0.0f};
    result[driveAmount] = {"driveAmount", "Drive", "%", 0.0f, 100.0f, 28.0f};
    result[driveMix] = {"driveMix", "Mix", "%", 0.0f, 100.0f, 25.0f};
    result[fmEnabled] = {"fmEnabled", "FM enabled", "", 0.0f, 1.0f, 0.0f};
    result[fmRatio] = {"fmRatio", "Carrier ratio", "", 0.5f, 4.0f, 2.0f};
    result[fmIndex] = {"fmIndex", "FM index", "", 0.0f, 5.0f, 1.1f};
    result[fmMix] = {"fmMix", "Mix", "%", 0.0f, 100.0f, 22.0f};
    result[grainEnabled] = {"grainEnabled", "Granular enabled", "", 0.0f, 1.0f, 0.0f};
    result[grainSize] = {"grainSize", "Grain size", "ms", 22.0f, 180.0f, 75.0f};
    result[grainPitch] = {"grainPitch", "Grain pitch", "st", -12.0f, 12.0f, 7.0f};
    result[grainMix] = {"grainMix", "Mix", "%", 0.0f, 100.0f, 22.0f};
    result[tremoloEnabled] = {"tremoloEnabled", "Tremolo enabled", "", 0.0f, 1.0f, 0.0f};
    result[tremoloRate] = {"tremoloRate", "Rate", "Hz", 0.15f, 16.0f, 4.2f};
    result[tremoloDepth] = {"tremoloDepth", "Depth", "%", 0.0f, 100.0f, 30.0f};
    result[phaserEnabled] = {"phaserEnabled", "Phaser enabled", "", 0.0f, 1.0f, 0.0f};
    result[phaserRate] = {"phaserRate", "Rate", "Hz", 0.05f, 8.0f, 0.65f};
    result[phaserDepth] = {"phaserDepth", "Sweep", "%", 0.0f, 100.0f, 64.0f};
    result[phaserMix] = {"phaserMix", "Mix", "%", 0.0f, 100.0f, 28.0f};
    result[chorusEnabled] = {"chorusEnabled", "Chorus enabled", "", 0.0f, 1.0f, 0.0f};
    result[chorusRate] = {"chorusRate", "Rate", "Hz", 0.05f, 8.0f, 0.73f};
    result[chorusDepth] = {"chorusDepth", "Depth", "%", 0.0f, 100.0f, 42.0f};
    result[chorusWidth] = {"chorusWidth", "Width", "%", 0.0f, 100.0f, 77.0f};
    result[chorusMix] = {"chorusMix", "Mix", "%", 0.0f, 100.0f, 28.0f};
    result[crusherEnabled] = {"crusherEnabled", "Bitcrusher enabled", "", 0.0f, 1.0f, 0.0f};
    result[crusherBits] = {"crusherBits", "Resolution", "bits", 3.0f, 16.0f, 10.0f};
    result[crusherMix] = {"crusherMix", "Mix", "%", 0.0f, 100.0f, 38.0f};
    result[reducerEnabled] = {"reducerEnabled", "Rate reducer enabled", "", 0.0f, 1.0f, 0.0f};
    result[reducerRate] = {"reducerRate", "Effective rate", "Hz", 450.0f, 24000.0f, 6500.0f};
    result[reducerMix] = {"reducerMix", "Mix", "%", 0.0f, 100.0f, 42.0f};
    result[delayEnabled] = {"delayEnabled", "Delay enabled", "", 0.0f, 1.0f, 0.0f};
    result[delayMode] = {"delayMode", "Routing", "", 0.0f, 1.0f, 0.0f};
    result[delaySync] = {"delaySync", "Tempo sync", "", 0.0f, 1.0f, 0.0f};
    result[delayDivision] = {"delayDivision", "Note value", "", 0.0f, 5.0f, 2.0f};
    result[delayTime] = {"delayTime", "Time", "ms", 30.0f, 1500.0f, 300.0f};
    result[delayFeedback] = {"delayFeedback", "Feedback", "%", 0.0f, 87.0f, 32.0f};
    result[delayCutoff] = {"delayCutoff", "Feedback filter", "Hz", 300.0f, 15000.0f, 4400.0f};
    result[delayMix] = {"delayMix", "Mix", "%", 0.0f, 100.0f, 23.0f};
    result[reverbEnabled] = {"reverbEnabled", "Reverb enabled", "", 0.0f, 1.0f, 0.0f};
    result[reverbMode] = {"reverbMode", "Space", "", 0.0f, 4.0f, 0.0f};
    result[reverbDecay] = {"reverbDecay", "Decay", "s", 0.2f, 13.0f, 2.2f};
    result[reverbDamping] = {"reverbDamping", "Damping", "%", 0.0f, 100.0f, 52.0f};
    result[reverbMix] = {"reverbMix", "Mix", "%", 0.0f, 100.0f, 24.0f};
    return result;
}();

inline constexpr std::array<std::string_view, 4> modelNames {
    "Ceramic Authentic", "Retro Fantasy", "Crystal Resonator", "Electronic Hybrid"
};

inline constexpr size_t synthesisParameterCount = patchGain;

inline constexpr bool steppedEffectParameter(size_t index) {
    return parameters[index].id.ends_with("Enabled") || index == driveMode || index == delayMode
        || index == delaySync || index == delayDivision || index == reverbMode || index == crusherBits;
}

inline constexpr auto parameterDefaults = [] {
    std::array<float, parameterCount> values {};
    for (size_t index = 0; index < values.size(); ++index)
        values[index] = parameters[index].initial;
    return values;
}();

} // namespace ocarina
