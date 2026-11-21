#pragma once

#include "shared/Parameters.h"

#include <vector>

namespace ocarina {

struct StereoFrame { float left = 0, right = 0; };

class EffectsEngine {
public:
    void prepare(float sampleRate);
    void reset() noexcept;
    void setParameter(size_t index, float value) noexcept;
    void setHostTempo(float bpm) noexcept;
    StereoFrame process(float mono, int note) noexcept;

private:
    struct Ring {
        void prepare(float rate, float seconds);
        void reset() noexcept { cursor = valid = 0; }
        void write(float value) noexcept;
        float read(float frames) const noexcept;
        std::vector<float> data;
        size_t cursor = 0, valid = 0;
    };

    struct Grains {
        void prepare(float rate);
        void reset() noexcept;
        StereoFrame process(StereoFrame frame, float width, float ratio) noexcept;
        std::array<Ring, 2> lines;
        float phase = 0;
    };

    using Stage = StereoFrame (EffectsEngine::*)(StereoFrame) noexcept;
    StereoFrame filter(StereoFrame frame) noexcept;
    StereoFrame saturation(StereoFrame frame) noexcept;
    StereoFrame fm(StereoFrame frame) noexcept;
    StereoFrame granular(StereoFrame frame) noexcept;
    StereoFrame tremolo(StereoFrame frame) noexcept;
    StereoFrame phaser(StereoFrame frame) noexcept;
    StereoFrame chorus(StereoFrame frame) noexcept;
    StereoFrame crusher(StereoFrame frame) noexcept;
    StereoFrame reducer(StereoFrame frame) noexcept;
    StereoFrame delay(StereoFrame frame) noexcept;
    StereoFrame reverb(StereoFrame frame) noexcept;
    void update() noexcept;
    float unit(size_t index) const noexcept { return values[index] * .01f; }
    void advance(float& phase, float hz) const noexcept;

    std::array<float, parameterCount> targets = parameterDefaults, values = targets;
    float rate = 48000, hostTempo = 0, smoothing = 0, masterStep = 0, masterVolume = .7f, trim = 1;
    int countdown = 0, currentNote = 69;
    bool changeMode = false;
    int driveCharacter = 0, delayRouting = 0, reverbSpace = 0;
    StereoFrame previous, transition;
    float phaseTremolo = 0, phasePhaser = 0, phaseChorus = 0, phaseReducer = 1, phaseFm = 0;
    float pitch = 440, fmEnvelope = 0, fmStep = 0;
    float filterB0 = 1, filterB1 = 0, filterB2 = 0, filterA1 = 0, filterA2 = 0;
    std::array<float, 2> filterZ1 {}, filterZ2 {};
    std::array<std::array<float, 4>, 2> phaserState {};
    std::array<Ring, 2> chorusLines, delayLines;
    std::array<std::array<Ring, 4>, 2> roomLines;
    std::array<std::array<Ring, 2>, 2> diffuser;
    std::array<std::array<float, 4>, 2> roomLengths {}, roomFeedback {}, roomDamped {};
    std::array<float, 2> delayDamped {};
    float delayTarget = 0, delayFrames = 0, delaySlew = 0, delayLowpass = 0;
    float roomLowpass = 0, grainRatio = 1;
    StereoFrame heldSample, shimmerReturn;
    Grains pitchGrains, shimmerGrains;
};

} // namespace ocarina
