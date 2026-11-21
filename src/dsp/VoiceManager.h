#pragma once

#include "OcarinaVoice.h"
#include "shared/Event.h"
#include "shared/Parameters.h"

namespace ocarina {

class VoiceManager {
public:
    static constexpr int maxVoices = 16;
    void prepare(double sampleRate) noexcept;
    void apply(Event event) noexcept;
    void releaseAll() noexcept;
    void reset() noexcept;
    void setParameter(size_t index, float value) noexcept;
    float nextSample() noexcept;
    int soundingVoices() const noexcept;

private:
    struct KeyId {
        uint16_t source;
        uint8_t channel, note;
        bool operator==(const KeyId&) const = default;
    };

    struct Key {
        KeyId id {};
        float velocity = 0, pressure = 0;
        bool down = false;
    };

    struct Channel {
        uint16_t source = 0;
        uint8_t channel = 0;
        bool used = false, sustain = false;
        VoiceExpression expression;
    };

    struct Slot {
        OcarinaVoice voice;
        KeyId key {};
        uint64_t age = 0;
    };

    void refreshControls(int elapsed) noexcept;
    void configureVoices() noexcept;
    void startVoice(Key key, bool legato) noexcept;
    void retire(Slot& slot) noexcept;
    void releaseMissingKeys() noexcept;
    void updateExpression(Slot& slot) noexcept;
    const Channel* findChannel(KeyId id) const noexcept;
    Channel& channelFor(KeyId id) noexcept;

    std::array<Key, 128> keys {};
    std::array<Channel, 64> channels {};
    std::array<Slot, maxVoices> voices {};
    size_t keyCount = 0, channelCursor = 0;
    uint64_t generation = 0;

    std::array<float, parameterCount> targets = [] {
        std::array<float, parameterCount> result {};
        for (size_t index = 0; index < parameterCount; ++index)
            result[index] = parameters[index].initial;
        return result;
    }();
    std::array<float, parameterCount> values = targets;
    std::array<float, 4> modelBlend {1, 0, 0, 0};
    ModelControls controls {};
    float rate = 48000.0f, gain = 0.7f, gainStep = 0, controlStep = 0, referenceFrequency = 0;
    float transition = 0, transitionStep = 0;
    static constexpr int controlFrames = 32;
    int untilControl = 0, count = maxVoices;
    bool mono = false, layoutDirty = false;
};

} // namespace ocarina
