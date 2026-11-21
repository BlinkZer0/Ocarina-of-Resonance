#include "EffectsEngine.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ocarina {
namespace {
constexpr float tau = 2.0f * std::numbers::pi_v<float>;
constexpr int updateFrames = 64;

StereoFrame blend(StereoFrame dry, StereoFrame wet, float mix) noexcept {
    return {std::lerp(dry.left, wet.left, mix), std::lerp(dry.right, wet.right, mix)};
}

float cleanup(float value) noexcept { return std::abs(value) < 1.e-17f ? 0.0f : value; }

float limit(float value) noexcept {
    const float over = std::max(0.0f, std::abs(value) - .78f);
    return over > 0 ? std::copysign(.78f + over / (1.0f + over / .215f), value) : value;
}
} // namespace

void EffectsEngine::Ring::prepare(float rate, float seconds) {
    data.resize(static_cast<size_t>(rate * seconds) + 4);
    reset();
}

void EffectsEngine::Ring::write(float value) noexcept {
    data[cursor] = cleanup(value);
    if (++cursor == data.size()) cursor = 0;
    if (valid < data.size()) ++valid;
}

float EffectsEngine::Ring::read(float frames) const noexcept {
    const float age = std::clamp(frames, 1.0f, static_cast<float>(data.size() - 2));
    const auto whole = static_cast<size_t>(age);
    if (whole > valid) return 0.0f;
    const auto first = cursor >= whole ? cursor - whole : data.size() + cursor - whole;
    const auto older = first == 0 ? data.size() - 1 : first - 1;
    return std::lerp(data[first], whole < valid ? data[older] : 0.0f, age - static_cast<float>(whole));
}

void EffectsEngine::Grains::prepare(float rate) {
    for (auto& line : lines) line.prepare(rate, .20f);
    reset();
}

void EffectsEngine::Grains::reset() noexcept {
    for (auto& line : lines) line.reset();
    phase = 0;
}

StereoFrame EffectsEngine::Grains::process(StereoFrame frame, float width, float ratio) noexcept {
    phase += (1.0f - ratio) / width;
    phase -= std::floor(phase);
    const float second = phase < .5f ? phase + .5f : phase - .5f;
    const float weight = 1.0f - std::abs(2.0f * phase - 1.0f);
    const auto grain = [&](const Ring& line) { return std::lerp(line.read(3 + width * second), line.read(3 + width * phase), weight); };
    const StereoFrame output {grain(lines[0]), grain(lines[1])};
    lines[0].write(frame.left);
    lines[1].write(frame.right);
    return output;
}

void EffectsEngine::prepare(float sampleRate) {
    rate = sampleRate;
    for (auto& line : chorusLines) line.prepare(rate, .041f);
    for (auto& line : delayLines) line.prepare(rate, 3.05f);
    for (auto& channel : roomLines) for (auto& line : channel) line.prepare(rate, .26f);
    for (auto& channel : diffuser) for (auto& line : channel) line.prepare(rate, .036f);
    pitchGrains.prepare(rate);
    shimmerGrains.prepare(rate);
    smoothing = -std::expm1(-updateFrames / (.018f * rate));
    delaySlew = -std::expm1(-1.0f / (.044f * rate));
    masterStep = -std::expm1(-1.0f / (.013f * rate));
    fmStep = -std::expm1(-1.0f / (.018f * rate));
    values = targets;
    masterVolume = unit(master);
    driveCharacter = static_cast<int>(targets[driveMode]);
    delayRouting = static_cast<int>(targets[delayMode]);
    reverbSpace = static_cast<int>(targets[reverbMode]);
    reset();
    update();
    delayFrames = delayTarget;
}

void EffectsEngine::reset() noexcept {
    for (auto& line : chorusLines) line.reset();
    for (auto& line : delayLines) line.reset();
    for (auto& channel : roomLines) for (auto& line : channel) line.reset();
    for (auto& channel : diffuser) for (auto& line : channel) line.reset();
    pitchGrains.reset();
    shimmerGrains.reset();
    filterZ1 = filterZ2 = delayDamped = {};
    phaserState = {};
    roomDamped = {};
    heldSample = shimmerReturn = previous = transition = {};
    phaseTremolo = phasePhaser = phaseChorus = phaseFm = fmEnvelope = 0;
    phaseReducer = 1;
    changeMode = false;
    currentNote = 69;
    pitch = 440;
    countdown = 0;
}

void EffectsEngine::setParameter(size_t index, float value) noexcept {
    if (index >= parameterCount || !std::isfinite(value)) return;
    value = std::clamp(value, parameters[index].minimum, parameters[index].maximum);
    if (steppedEffectParameter(index)) value = std::round(value);
    targets[index] = value;
}

void EffectsEngine::setHostTempo(float bpm) noexcept {
    hostTempo = std::isfinite(bpm) && bpm > 0 ? std::clamp(bpm, 40.0f, 250.0f) : 0.0f;
}

void EffectsEngine::advance(float& phase, float hz) const noexcept {
    phase += hz / rate;
    if (phase >= 1) phase -= 1;
}

void EffectsEngine::update() noexcept {
    for (size_t index = patchGain; index < parameterCount; ++index) {
        values[index] += (targets[index] - values[index]) * smoothing;
        if (std::abs(targets[index] - values[index]) < 1.e-5f) values[index] = targets[index];
    }
    trim = std::pow(10.0f, values[patchGain] * .05f);
    const int driveChoice = std::clamp(static_cast<int>(std::lround(targets[driveMode])), 0, 2);
    const int delayChoice = targets[delayMode] >= .5f ? 1 : 0;
    const int roomChoice = std::clamp(static_cast<int>(std::lround(targets[reverbMode])), 0, 4);
    changeMode = driveChoice != driveCharacter || delayChoice != delayRouting || roomChoice != reverbSpace;
    driveCharacter = driveChoice;
    delayRouting = delayChoice;
    reverbSpace = roomChoice;

    if (values[filterEnabled] > .0001f) {
        const float w = tau * std::min(values[filterHz] / rate, .44f);
        const float gain = std::pow(10.0f, values[filterDb] * .025f);
        const float alpha = std::sin(w) / (2 * values[filterQ]);
        const float norm = 1.0f / (1.0f + alpha / gain);
        filterB0 = (1 + alpha * gain) * norm;
        filterB1 = -2 * std::cos(w) * norm;
        filterB2 = (1 - alpha * gain) * norm;
        filterA1 = filterB1;
        filterA2 = (1 - alpha / gain) * norm;
    }
    if (values[grainEnabled] > .0001f) grainRatio = std::exp2(values[grainPitch] / 12);
    if (values[delayEnabled] > .0001f) {
        constexpr std::array divisions {.25f, .5f, 1.0f, 1.5f, 2.0f, 2.0f / 3.0f};
        const auto division = static_cast<size_t>(std::clamp(std::lround(targets[delayDivision]), 0l, 5l));
        const float bpm = hostTempo > 0 ? hostTempo : values[tempo];
        delayTarget = rate * (targets[delaySync] >= .5f ? divisions[division] * 60.0f / bpm : values[delayTime] * .001f);
        delayLowpass = -std::expm1(-tau * std::min(values[delayCutoff] / rate, .45f));
    }
    if (values[reverbEnabled] > .0001f) {
        constexpr std::array scales {.67f, 1.13f, 1.49f, 2.07f, 1.59f};
        constexpr std::array lengths {.0367f, .0469f, .0533f, .0671f};
        roomLowpass = -std::expm1(-tau * (9000 - 8000 * unit(reverbDamping)) / rate);
        for (size_t channel = 0; channel < 2; ++channel) {
            for (size_t tap = 0; tap < lengths.size(); ++tap) {
                const float length = (lengths[tap] + .00214f * channel) * scales[reverbSpace];
                roomLengths[channel][tap] = rate * length;
                roomFeedback[channel][tap] = std::min(reverbSpace == 4 ? .88f : .97f,
                    std::pow(.001f, length / values[reverbDecay]));
            }
        }
    }
}

StereoFrame EffectsEngine::filter(StereoFrame frame) noexcept {
    const auto equalize = [&](size_t side, float input) {
        const float out = input * filterB0 + filterZ1[side];
        filterZ1[side] = cleanup(input * filterB1 + filterZ2[side] - out * filterA1);
        filterZ2[side] = cleanup(input * filterB2 - out * filterA2);
        return out;
    };
    return blend(frame, {equalize(0, frame.left), equalize(1, frame.right)}, values[filterEnabled]);
}

StereoFrame EffectsEngine::saturation(StereoFrame frame) noexcept {
    const float drive = 1 + 7 * unit(driveAmount);
    const auto distort = [&](float input) {
        const float x = drive * input;
        if (driveCharacter == 1) return std::round(31 * limit(x)) / 31;
        if (driveCharacter == 2) return x / std::sqrt(1 + x * x);
        return std::tanh(x + .12f * x * x / (1 + x * x));
    };
    const float makeup = 1.0f / std::sqrt(drive);
    return blend(frame, {makeup * distort(frame.left), makeup * distort(frame.right)}, values[driveEnabled] * unit(driveMix));
}

StereoFrame EffectsEngine::fm(StereoFrame frame) noexcept {
    const float modulator = .5f * (frame.left + frame.right);
    fmEnvelope = cleanup(fmEnvelope + (std::abs(modulator) - fmEnvelope) * fmStep);
    const float excursion = std::clamp(modulator * values[fmIndex], -.8f, .8f);
    advance(phaseFm, std::min(pitch * (values[fmRatio] + excursion), rate * .44f));
    phaseFm -= std::floor(phaseFm);
    const float carrier = std::sin(tau * phaseFm) * fmEnvelope * 1.35f;
    return blend(frame, {carrier, carrier}, values[fmEnabled] * unit(fmMix));
}

StereoFrame EffectsEngine::granular(StereoFrame frame) noexcept {
    const auto shifted = pitchGrains.process(frame, values[grainSize] * .001f * rate, grainRatio);
    return blend(frame, shifted, values[grainEnabled] * unit(grainMix));
}

StereoFrame EffectsEngine::tremolo(StereoFrame frame) noexcept {
    advance(phaseTremolo, values[tremoloRate]);
    const float amount = values[tremoloEnabled] * unit(tremoloDepth);
    return {frame.left * (1 - amount * (.5f + .5f * std::sin(tau * phaseTremolo))),
        frame.right * (1 - amount * (.5f + .5f * std::sin(tau * (phaseTremolo + .16f))))};
}

StereoFrame EffectsEngine::phaser(StereoFrame frame) noexcept {
    advance(phasePhaser, values[phaserRate]);
    const auto swept = [&](size_t channel, float input) {
        const float lfo = .5f + .5f * std::sin(tau * (phasePhaser + .14f * channel));
        const float w = std::min(.35f, (180 + 3500 * unit(phaserDepth) * lfo) / rate);
        const float g = std::tan(std::numbers::pi_v<float> * w);
        const float coeff = (1 - g) / (1 + g);
        for (auto& stage : phaserState[channel]) {
            const float output = stage - coeff * input;
            stage = cleanup(input + coeff * output);
            input = output;
        }
        return input;
    };
    return blend(frame, {swept(0, frame.left), swept(1, frame.right)}, values[phaserEnabled] * unit(phaserMix));
}

StereoFrame EffectsEngine::chorus(StereoFrame frame) noexcept {
    advance(phaseChorus, values[chorusRate]);
    const float depth = unit(chorusDepth) * .012f;
    const float left = chorusLines[0].read(rate * (.016f + depth * std::sin(tau * phaseChorus)));
    const float right = chorusLines[1].read(rate * (.016f + depth * std::sin(tau * (phaseChorus + .46f * unit(chorusWidth)))));
    chorusLines[0].write(frame.left);
    chorusLines[1].write(frame.right);
    return blend(frame, {left, right}, values[chorusEnabled] * unit(chorusMix));
}

StereoFrame EffectsEngine::crusher(StereoFrame frame) noexcept {
    const float scale = std::exp2(std::round(values[crusherBits]) - 1);
    return blend(frame, {std::round(frame.left * scale) / scale, std::round(frame.right * scale) / scale},
        values[crusherEnabled] * unit(crusherMix));
}

StereoFrame EffectsEngine::reducer(StereoFrame frame) noexcept {
    phaseReducer += std::min(values[reducerRate] / rate, 1.0f);
    if (phaseReducer >= 1) {
        phaseReducer -= 1;
        heldSample = frame;
    }
    return blend(frame, heldSample, values[reducerEnabled] * unit(reducerMix));
}

StereoFrame EffectsEngine::delay(StereoFrame frame) noexcept {
    if (delayFrames < 1) delayFrames = delayTarget;
    delayFrames += (delayTarget - delayFrames) * delaySlew;
    const StereoFrame tap {delayLines[0].read(delayFrames), delayLines[1].read(delayFrames * (delayRouting == 0 ? 1.018f : 1.0f))};
    delayDamped[0] = cleanup(std::lerp(delayDamped[0], tap.left, delayLowpass));
    delayDamped[1] = cleanup(std::lerp(delayDamped[1], tap.right, delayLowpass));
    const float feedback = unit(delayFeedback);
    delayLines[0].write((delayRouting ? .5f * (frame.left + frame.right) : frame.left) + feedback * delayDamped[delayRouting]);
    delayLines[1].write((delayRouting ? 0 : frame.right) + feedback * delayDamped[1 - delayRouting]);
    return blend(frame, tap, values[delayEnabled] * unit(delayMix));
}

StereoFrame EffectsEngine::reverb(StereoFrame frame) noexcept {
    const auto render = [&](size_t channel, float dry, float cross) {
        float wet = 0;
        for (size_t index = 0; index < 4; ++index) {
            auto& line = roomLines[channel][index];
            auto& damping = roomDamped[channel][index];
            const float tap = line.read(roomLengths[channel][index]);
            damping = cleanup(std::lerp(damping, tap, roomLowpass));
            line.write(.41f * dry + roomFeedback[channel][index] * damping + cross);
            wet += tap;
        }
        wet *= .35f;
        for (size_t index = 0; index < 2; ++index) {
            auto& line = diffuser[channel][index];
            const float tap = line.read(rate * (.0049f + .0021f * index + .00067f * channel));
            const float front = wet + .54f * tap;
            wet = tap - .54f * front;
            line.write(front);
        }
        return wet;
    };
    const float shimmer = reverbSpace == 4 ? .08f : 0;
    StereoFrame wet {render(0, frame.left, shimmer * shimmerReturn.right), render(1, frame.right, shimmer * shimmerReturn.left)};
    if (reverbSpace == 4) {
        shimmerReturn = shimmerGrains.process(wet, .067f * rate, 2.0f);
        wet.left += .35f * shimmerReturn.left;
        wet.right += .35f * shimmerReturn.right;
    }
    return blend(frame, wet, values[reverbEnabled] * unit(reverbMix));
}

StereoFrame EffectsEngine::process(float mono, int note) noexcept {
    if (note >= 0 && note != currentNote) {
        currentNote = note;
        pitch = 440.0f * std::exp2((static_cast<float>(note) - 69) / 12);
    }
    if (countdown == 0) {
        update();
        countdown = updateFrames;
    }
    --countdown;
    StereoFrame frame {mono, mono};
    constexpr std::array stages {&EffectsEngine::filter, &EffectsEngine::saturation, &EffectsEngine::fm, &EffectsEngine::granular,
        &EffectsEngine::tremolo, &EffectsEngine::phaser, &EffectsEngine::chorus, &EffectsEngine::crusher,
        &EffectsEngine::reducer, &EffectsEngine::delay, &EffectsEngine::reverb};
    constexpr std::array enabled {filterEnabled, driveEnabled, fmEnabled, grainEnabled, tremoloEnabled, phaserEnabled,
        chorusEnabled, crusherEnabled, reducerEnabled, delayEnabled, reverbEnabled};
    for (size_t index = 0; index < stages.size(); ++index) {
        if (values[enabled[index]] > .0001f) frame = (this->*stages[index])(frame);
    }
    masterVolume += masterStep * (targets[master] * .01f - masterVolume);
    if (targets[master] == 0 && masterVolume < 1.e-6f) masterVolume = 0;
    const float level = masterVolume * trim;
    frame = {limit(frame.left * level), limit(frame.right * level)};
    if (changeMode) {
        transition = {previous.left - frame.left, previous.right - frame.right};
        changeMode = false;
    }
    previous = {limit(frame.left + transition.left), limit(frame.right + transition.right)};
    transition = {cleanup(transition.left * (1 - masterStep * 2)), cleanup(transition.right * (1 - masterStep * 2))};
    return previous;
}

} // namespace ocarina
