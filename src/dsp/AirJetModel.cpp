#include "AirJetModel.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ocarina {

void AirJetModel::reset(uint32_t seed) noexcept {
    random = seed == 0 ? 1 : seed;
    slow = fast = breathNoise = turbulenceSeed = 0;
}

void AirJetModel::configure(float pressure, float jetSpeed, float edge, float nonlinearity,
    float turbulence, float noiseColor, float onset, float rate) noexcept {
    // Pressure supplies the jet. A faster jet shortens convection time and
    // improves power transfer until the jet passes the useful phase window.
    const float speed = (0.62f + jetSpeed * 0.76f) * std::sqrt(pressure);
    const float phaseMismatch = speed - 0.88f;
    const float coupling = (0.72f + 0.52f * edge) / (1.0f + 1.4f * phaseMismatch * phaseMismatch);
    work = (320.0f * pressure * coupling * (1.0f + 0.18f * onset)) / rate;
    saturation = 5.5f + 13.0f * nonlinearity;
    noiseGain = pressure * turbulence * (0.045f + 0.105f * onset);
    seedGain = (pressure * 0.00005f + noiseGain * 0.006f) * std::sqrt(48000.0f / rate);
    slowStep = -std::expm1(-2.0f * std::numbers::pi_v<float> * (240.0f + 850.0f * noiseColor) / rate);
    fastStep = -std::expm1(-2.0f * std::numbers::pi_v<float> * (1800.0f + 6500.0f * noiseColor) / rate);
}

float AirJetModel::excite(float energy) noexcept {
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    const float white = static_cast<float>(random >> 8) * (1.0f / 8388608.0f) - 1.0f;
    fast += fastStep * (white - fast);
    slow += slowStep * (white - slow);
    breathNoise = noiseGain * (fast - slow);
    turbulenceSeed = white * seedGain;
    return work / (1.0f + saturation * energy);
}

} // namespace ocarina
