#include "OcarinaVoice.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ocarina {

void OcarinaVoice::start(int note, float noteVelocity, uint32_t seed) noexcept {
    pitch = targetPitch = static_cast<float>(note);
    velocity = noteVelocity;
    active = gate = strike = true;
    envelope = pressure = time = lfo = output = bend = 0;
    driftPhase = static_cast<float>(seed & 0xffffu) * (1.0f / 65536.0f);
    cavity.reset();
    jet.reset(seed);
}

void OcarinaVoice::legato(int note, float noteVelocity) noexcept {
    targetPitch = static_cast<float>(note);
    velocity = noteVelocity;
    gate = true;
}

void OcarinaVoice::update(const ModelControls& c, int samples) noexcept {
    constexpr float tau = 2.0f * std::numbers::pi_v<float>;
    const float duration = static_cast<float>(samples) / c.rate;
    time = std::min(time + duration, 10.0f);
    lfo += (4.65f + 0.55f * c.onset) * duration;
    lfo -= std::floor(lfo);
    driftPhase += 0.69f * duration;
    driftPhase -= std::floor(driftPhase);

    const float velocityGain = 1.0f - c.velocity * (1.0f - velocity) * 0.72f;
    const float breath = std::pow(expression.breath, c.breathCurve);
    const float instability = std::sin(tau * driftPhase) * c.drift;
    const float desiredPressure = gate ? std::clamp(expression.expression * (c.pressure * velocityGain * breath
        * (1.0f + 0.045f * instability) + 0.22f * c.aftertouch * expression.pressure), 0.0f, 1.15f) : 0.0f;
    pressure += (desiredPressure - pressure) * (strike ? 1.0f : -std::expm1(-duration / 0.024f));
    if (desiredPressure == 0.0f && pressure < 1.0e-6f) {
        pressure = 0.0f;
        if (cavity.energy() < 1.0e-18f)
            cavity.reset();
    }

    pitch += (targetPitch - pitch) * (c.portamento > 0.0f
        ? -std::expm1(-duration / (c.portamento * 0.001f)) : 1.0f);
    bend += (expression.bend - bend) * (strike ? 1.0f : -std::expm1(-duration / 0.006f));
    const float vibrato = std::sin(tau * lfo) * c.vibrato * std::clamp((time - 0.12f) * 3.0f, 0.0f, 1.0f);
    const float cents = c.geometryCents + c.detune + (pressure - 0.55f) * 12.0f + instability * 2.8f + vibrato;
    const float frequency = std::min(440.0f * std::exp2((pitch - 69.0f + bend * c.bendRange + cents * 0.01f) / 12.0f),
        c.rate * 0.44f);

    const float onset = std::max(0.0f, 1.0f - time / (0.025f + 0.08f * c.articulation));
    jet.configure(pressure, c.jet, c.edge, c.excitation, c.noise, c.noiseColor, onset * c.onset, c.rate);
    const float radiationLoss = (4.0f + 18.0f * c.radiation) * frequency / (frequency + 1800.0f);
    cavity.configure(frequency, 17.0f + 30.0f * c.damping + radiationLoss, c.rate);
    if (strike) {
        cavity.seed(pressure * (0.09f + 0.16f * velocity * c.articulation));
        strike = false;
    }

    // Pressure polynomials represent the leading radiation harmonics. Remove
    // each partial before it reaches Nyquist so high notes stay clean.
    const float ratio = frequency / c.rate;
    const auto band = [ratio](float harmonic) { return std::clamp((0.46f - ratio * harmonic) * 36.0f, 0.0f, 1.0f); };
    second = c.second * (0.65f + 0.95f * c.formant) * band(2.0f);
    third = c.third * (0.30f + 1.40f * c.formant) * band(3.0f);
    fourth = c.fourth * band(4.0f);
    noiseMix = c.noiseMix;
    attackStep = -std::expm1(-1.0f / ((0.006f + 0.018f * (1.0f - c.articulation) + 0.01f * (1.0f - velocity)) * c.rate));
    releaseStep = -std::expm1(-1.0f / (0.018f * c.rate));
}

float OcarinaVoice::next() noexcept {
    envelope += gate ? (1.0f - envelope) * attackStep : -envelope * releaseStep;
    if (!gate && envelope < 0.00001f) {
        active = false;
        return output = 0.0f;
    }

    const float drive = jet.excite(cavity.energy());
    cavity.seed(jet.disturbance());
    cavity.step(drive);
    const float p = cavity.pressure;
    const float u = cavity.flow;
    const float p2 = p * p;
    const float u2 = u * u;
    const float radiation = p + second * (2.0f * p * u) + third * p * (p2 - 3.0f * u2)
        + fourth * ((p2 - u2) * (p2 - u2) - 4.0f * p2 * u2);
    return output = envelope * (0.38f * radiation + noiseMix * jet.noise());
}

} // namespace ocarina
