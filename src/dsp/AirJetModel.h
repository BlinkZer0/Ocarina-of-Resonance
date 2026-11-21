#pragma once

#include <cstdint>

namespace ocarina {

// Reduced jet-drive law averaged over a cavity cycle. Drive is the in-phase
// component of jet work on aperture flow. Saturation limits large excursions.
class AirJetModel {
public:
    void reset(uint32_t seed) noexcept;
    void configure(float pressure, float jetSpeed, float edge, float nonlinearity,
        float turbulence, float noiseColor, float onset, float rate) noexcept;
    float excite(float energy) noexcept;
    float noise() const noexcept { return breathNoise; }
    float disturbance() const noexcept { return turbulenceSeed; }

private:
    uint32_t random = 1;
    float work = 0, saturation = 0, noiseGain = 0, seedGain = 0;
    float slow = 0, fast = 0, slowStep = 0, fastStep = 0, breathNoise = 0, turbulenceSeed = 0;
};

} // namespace ocarina
