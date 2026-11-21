#pragma once

#include "AirJetModel.h"
#include "VesselResonator.h"

namespace ocarina {

struct ModelControls {
    float rate, pressure, jet, edge, noise, noiseColor, excitation, damping;
    float geometryCents, radiation, detune, vibrato, articulation, formant;
    float portamento, bendRange, velocity, aftertouch, breathCurve;
    float second, third, fourth, noiseMix, drift, onset;
};

struct VoiceExpression {
    float breath = 1, expression = 1, bend = 0, pressure = 0;
};

class OcarinaVoice {
public:
    void start(int note, float velocity, uint32_t seed) noexcept;
    void legato(int note, float velocity) noexcept;
    void setExpression(VoiceExpression state) noexcept { expression = state; }
    void release() noexcept { gate = false; }
    void update(const ModelControls& controls, int samples) noexcept;
    float next() noexcept;
    bool isActive() const noexcept { return active; }
    bool isReleasing() const noexcept { return !gate; }
    float currentOutput() const noexcept { return output; }

private:
    AirJetModel jet;
    VesselResonator cavity;
    VoiceExpression expression;
    float pitch = 69, targetPitch = 69, velocity = 1;
    float envelope = 0, pressure = 0, time = 0, lfo = 0, driftPhase = 0, bend = 0;
    float second = 0, third = 0, fourth = 0, noiseMix = 0;
    float attackStep = 0, releaseStep = 0, output = 0;
    bool active = false, gate = false, strike = false;
};

} // namespace ocarina
