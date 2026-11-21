#include "PrototypeVoice.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ocarina {

void PrototypeVoice::prepare(double sampleRate) noexcept {
    rate = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 48000.0;
    attack = static_cast<float>(-std::expm1(-1.0 / (0.008 * rate)));
    release = static_cast<float>(-std::expm1(-1.0 / (0.025 * rate)));
    releaseAll();
    phase = envelope = 0;
    gain = targetGain;
}

double PrototypeVoice::frequency(int note) noexcept {
    return 440.0 * std::exp2((note - 69) / 12.0);
}

void PrototypeVoice::apply(Event event) noexcept {
    const bool releaseSource = event.type == EventType::releaseSource;
    if (!releaseSource && (event.channel > 15 || event.note > 127))
        return;

    auto end = held.begin() + heldCount;
    heldCount = std::remove_if(held.begin(), end, [&](const HeldNote& note) {
        return note.source == event.source
            && (releaseSource || (note.channel == event.channel && note.note == event.note));
    }) - held.begin();

    if (event.type == EventType::noteOn && std::isfinite(event.velocity) && event.velocity > 0.0f) {
        if (heldCount == held.size()) {
            std::move(held.begin() + 1, held.end(), held.begin());
            --heldCount;
        }
        held[heldCount++] = {event.source, event.channel, event.note, std::min(event.velocity, 1.0f)};
    }
    updateNote();
}

void PrototypeVoice::releaseAll() noexcept {
    heldCount = 0;
    target = 0.0f;
}

void PrototypeVoice::setMaster(float percent) noexcept {
    if (std::isfinite(percent))
        targetGain = std::clamp(percent, masterGain.minimum, masterGain.maximum) / 100.0f;
}

void PrototypeVoice::updateNote() noexcept {
    if (heldCount == 0) {
        target = 0.0f;
        return;
    }
    const auto& note = held[heldCount - 1];
    phaseIncrement = std::min(frequency(note.note) / rate, 0.49);
    target = note.velocity;
}

float PrototypeVoice::nextSample() noexcept {
    gain += (targetGain - gain) * attack;
    envelope += (target - envelope) * (target > envelope ? attack : release);
    if (target == 0.0f && envelope < 0.00001f) {
        envelope = 0.0f;
        return 0.0f;
    }
    phase += phaseIncrement;
    if (phase >= 1.0)
        phase -= 1.0;
    return static_cast<float>(std::sin(2.0 * std::numbers::pi * phase)) * envelope * gain * 0.3f;
}

} // namespace ocarina
