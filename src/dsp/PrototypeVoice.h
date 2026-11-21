#pragma once

#include "shared/Event.h"
#include "shared/Parameters.h"

#include <array>
#include <cstddef>

namespace ocarina {

// A single reference oscillator for exercising the three audio adapters.
class PrototypeVoice {
public:
    void prepare(double sampleRate) noexcept;
    void apply(Event event) noexcept;
    void releaseAll() noexcept;
    void setMaster(float percent) noexcept;
    float nextSample() noexcept;
    static double frequency(int note) noexcept;

private:
    struct HeldNote { uint16_t source; uint8_t channel, note; float velocity; };
    void updateNote() noexcept;

    std::array<HeldNote, 128> held {};
    size_t heldCount = 0;
    double rate = 48000.0, phase = 0.0, phaseIncrement = 0.0;
    float envelope = 0.0f, target = 0.0f, attack = 0.0f, release = 0.0f;
    float gain = masterGain.initial / 100.0f, targetGain = gain;
};

} // namespace ocarina
