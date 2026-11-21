#include "VesselResonator.h"

#include <cmath>
#include <numbers>

namespace ocarina {

VesselGeometry VesselGeometry::from(float volumeCm3, float areaMm2, float neckMm) noexcept {
    constexpr float airDensity = 1.204f;
    constexpr float soundSpeed = 343.0f;
    const float compliance = volumeCm3 * 1.0e-6f / (airDensity * soundSpeed * soundSpeed);
    const float mass = (airDensity * neckMm * 0.001f) / (areaMm2 * 1.0e-6f);
    return {compliance, mass, 1.0f / (2.0f * std::numbers::pi_v<float> * std::sqrt(compliance * mass))};
}

void VesselResonator::configure(float frequency, float loss, float rate) noexcept {
    const float turn = 2.0f * std::numbers::pi_v<float> * frequency / rate;
    cosine = std::cos(turn);
    sine = std::sin(turn);
    lossPerSample = loss / rate;
}

void VesselResonator::step(float jetWork) noexcept {
    const float gain = 1.0f + jetWork - lossPerSample;
    const float nextPressure = gain * (pressure * cosine + flow * sine);
    flow = gain * (flow * cosine - pressure * sine);
    pressure = nextPressure;
}

} // namespace ocarina
