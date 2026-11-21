#include "VoiceManager.h"

#include <algorithm>
#include <cmath>

namespace ocarina {

void VoiceManager::prepare(double sampleRate) noexcept {
    rate = std::isfinite(sampleRate) ? static_cast<float>(std::clamp(sampleRate, 8000.0, 384000.0)) : 48000.0f;
    gainStep = -std::expm1(-1.0f / (0.012f * rate));
    controlStep = -std::expm1(-static_cast<float>(controlFrames) / (0.022f * rate));
    transitionStep = -std::expm1(-1.0f / (0.0025f * rate));
    referenceFrequency = VesselGeometry::from(parameters[cavityVolume].initial, parameters[openHoleArea].initial,
        parameters[apertureNeckLength].initial).frequency;
    reset();
    values = targets;
    gain = targets[master] * 0.01f;
    modelBlend = {};
    modelBlend[static_cast<size_t>(targets[model])] = 1;
    refreshControls(0);
}

void VoiceManager::reset() noexcept {
    keyCount = channelCursor = 0;
    generation = 0;
    channels = {};
    voices = {};
    transition = 0;
    untilControl = 0;
    mono = targets[voiceMode] < 0.5f;
    count = mono ? 1 : static_cast<int>(targets[voiceLimit]);
    layoutDirty = false;
}

void VoiceManager::releaseAll() noexcept {
    keyCount = channelCursor = 0;
    channels = {};
    for (auto& slot : voices)
        slot.voice.release();
}

void VoiceManager::setParameter(size_t index, float value) noexcept {
    if (index >= synthesisParameterCount || !std::isfinite(value))
        return;
    const auto& spec = parameters[index];
    value = std::clamp(value, spec.minimum, spec.maximum);
    if (index >= model && index <= voiceLimit)
        value = std::round(value);
    if ((index == voiceMode || index == voiceLimit) && targets[index] != value)
        layoutDirty = true;
    targets[index] = value;
}

const VoiceManager::Channel* VoiceManager::findChannel(KeyId id) const noexcept {
    for (const auto& entry : channels) {
        if (entry.used && entry.source == id.source && entry.channel == id.channel)
            return &entry;
    }
    return nullptr;
}

VoiceManager::Channel& VoiceManager::channelFor(KeyId id) noexcept {
    if (const auto* existing = findChannel(id))
        return channels[existing - channels.data()];
    auto free = std::find_if(channels.begin(), channels.end(), [](const Channel& entry) { return !entry.used; });
    if (free == channels.end()) {
        free = channels.begin() + channelCursor;
        channelCursor = (channelCursor + 1) % channels.size();
        const auto sameChannel = [&](KeyId key) { return key.source == free->source && key.channel == free->channel; };
        keyCount = std::remove_if(keys.begin(), keys.begin() + keyCount,
            [&](const Key& key) { return sameChannel(key.id) && !key.down; }) - keys.begin();
        free->used = false;
        releaseMissingKeys();
        for (auto& slot : voices) {
            if (slot.voice.isActive() && sameChannel(slot.key))
                updateExpression(slot);
        }
    }
    *free = {id.source, id.channel, true, false, {}};
    return *free;
}

void VoiceManager::updateExpression(Slot& slot) noexcept {
    VoiceExpression expression;
    if (const auto* channel = findChannel(slot.key))
        expression = channel->expression;
    const auto end = keys.begin() + keyCount;
    const auto key = std::find_if(keys.begin(), end, [&](const Key& held) { return held.id == slot.key; });
    if (key != end)
        expression.pressure = std::max(expression.pressure, key->pressure);
    slot.voice.setExpression(expression);
}

void VoiceManager::retire(Slot& slot) noexcept {
    transition += slot.voice.currentOutput();
    slot = {};
}

void VoiceManager::startVoice(Key key, bool legato) noexcept {
    auto* slot = &voices.front();
    if (!mono) {
        const auto priority = [](const Slot& voice) {
            return !voice.voice.isActive() ? 0 : voice.voice.isReleasing() ? 1 : 2;
        };
        for (int index = 0; index < count; ++index) {
            auto& candidate = voices[index];
            if (candidate.voice.isActive() && candidate.key == key.id) {
                slot = &candidate;
                break;
            }
            if (priority(candidate) < priority(*slot)
                || (priority(candidate) == priority(*slot) && candidate.age < slot->age))
                slot = &candidate;
        }
    }

    if (legato && slot->voice.isActive() && !slot->voice.isReleasing()) {
        slot->voice.legato(key.id.note, key.velocity);
    } else {
        retire(*slot);
        const auto seed = static_cast<uint32_t>(generation + 1) * 0x9e3779b9u ^ (key.id.note + 1u) * 0x85ebca6bu;
        slot->voice.start(key.id.note, key.velocity, seed);
    }
    slot->key = key.id;
    slot->age = ++generation;
    updateExpression(*slot);
    slot->voice.update(controls, 0);
}

void VoiceManager::configureVoices() noexcept {
    layoutDirty = false;
    const bool nextMono = targets[voiceMode] < 0.5f;
    const auto nextCount = nextMono ? 1 : static_cast<int>(targets[voiceLimit]);
    if (nextMono == mono && nextCount == count)
        return;
    for (auto& slot : voices)
        retire(slot);
    mono = nextMono;
    count = nextCount;
    const size_t first = keyCount > static_cast<size_t>(count) ? keyCount - count : 0;
    for (size_t index = first; index < keyCount; ++index)
        startVoice(keys[index], false);
}

void VoiceManager::releaseMissingKeys() noexcept {
    if (mono && keyCount && voices.front().key != keys[keyCount - 1].id) {
        startVoice(keys[keyCount - 1], true);
        return;
    }
    const auto end = keys.begin() + keyCount;
    for (auto& slot : voices) {
        if (slot.voice.isActive() && std::none_of(keys.begin(), end, [&](const Key& key) { return key.id == slot.key; }))
            slot.voice.release();
    }
}

void VoiceManager::apply(Event event) noexcept {
    if (event.type == EventType::parameter) {
        setParameter(event.note, event.value);
        return;
    }
    if (event.channel > 15 || event.note > 127 || !std::isfinite(event.value))
        return;
    if (layoutDirty)
        configureVoices();

    const KeyId id {event.source, event.channel, event.note};
    const float value = std::clamp(event.value, 0.0f, 1.0f);
    const auto sameChannel = [&](const Key& key) { return key.id.source == id.source && key.id.channel == id.channel; };
    const auto end = keys.begin() + keyCount;
    const auto target = std::find_if(keys.begin(), end, [&](const Key& key) { return key.id == id; });

    if (event.type == EventType::noteOn && value > 0.0f) {
        const bool legato = mono && keyCount > 0 && keys[keyCount - 1].id != id;
        if (target != end) {
            std::move(target + 1, end, target);
            --keyCount;
        } else if (keyCount == keys.size()) {
            std::move(keys.begin() + 1, end, keys.begin());
            --keyCount;
            releaseMissingKeys();
        }
        keys[keyCount++] = {id, value, 0.0f, true};
        startVoice(keys[keyCount - 1], legato);
        return;
    }

    if (event.type == EventType::noteOff || event.type == EventType::noteOn) {
        if (target == end)
            return;
        const auto* c = findChannel(id);
        if (c && c->sustain) {
            target->down = false;
            return;
        }
        std::move(target + 1, end, target);
        --keyCount;
        releaseMissingKeys();
        return;
    }

    if (event.type == EventType::releaseSource || event.type == EventType::releaseChannel) {
        const auto match = [&](KeyId key) { return key.source == id.source && (event.type == EventType::releaseSource || key.channel == id.channel); };
        keyCount = std::remove_if(keys.begin(), end, [&](const Key& key) { return match(key.id); }) - keys.begin();
        if (event.type == EventType::releaseSource) {
            for (auto& channel : channels) {
                if (channel.used && channel.source == id.source)
                    channel = {};
            }
        }
        releaseMissingKeys();
        return;
    }

    if (event.type == EventType::notePressure) {
        if (target == end)
            return;
        target->pressure = value;
    } else {
        auto& channel = channelFor(id);
        switch (event.type) {
            case EventType::sustain:
                channel.sustain = value >= 0.5f;
                if (!channel.sustain) {
                    keyCount = std::remove_if(keys.begin(), keys.begin() + keyCount,
                        [&](const Key& key) { return sameChannel(key) && !key.down; }) - keys.begin();
                    releaseMissingKeys();
                }
                return;
            case EventType::breath: channel.expression.breath = value; break;
            case EventType::expression: channel.expression.expression = value; break;
            case EventType::pitchBend: channel.expression.bend = std::clamp(event.value, -1.0f, 1.0f); break;
            case EventType::channelPressure: channel.expression.pressure = value; break;
            case EventType::modulation: channel.expression.modulation = value; break;
            default: return;
        }
    }

    for (auto& slot : voices) {
        if (slot.voice.isActive() && slot.key.source == id.source && slot.key.channel == id.channel)
            updateExpression(slot);
    }
}

void VoiceManager::refreshControls(int elapsed) noexcept {
    for (size_t index = 1; index < values.size(); ++index)
        values[index] += controlStep * (targets[index] - values[index]);
    for (size_t index = 0; index < modelBlend.size(); ++index)
        modelBlend[index] += controlStep * ((index == static_cast<size_t>(targets[model]) ? 1.0f : 0.0f) - modelBlend[index]);

    const auto percent = [&](size_t index) { return values[index] * 0.01f; };
    const auto geometry = VesselGeometry::from(values[cavityVolume], values[openHoleArea], values[apertureNeckLength]);
    const float geometryRatio = std::log2(geometry.frequency / referenceFrequency);
    const auto blend = [&](std::array<float, 4> amounts) {
        return modelBlend[0] * amounts[0] + modelBlend[1] * amounts[1] + modelBlend[2] * amounts[2] + modelBlend[3] * amounts[3];
    };
    const float enhance = percent(harmonicEnhancement);
    controls = {
        .rate = rate,
        .pressure = percent(breathPressure) * blend({1.0f, 1.03f, 0.94f, 1.1f}),
        .jet = percent(jetVelocity),
        .edge = percent(edgeInteraction),
        .noise = percent(turbulence),
        .noiseColor = percent(turbulenceColor),
        .excitation = percent(excitationShape),
        .damping = percent(resonatorDamping),
        .geometryCents = std::clamp(11.0f * geometryRatio, -30.0f, 30.0f),
        .radiation = std::clamp(0.55f + geometryRatio * 0.26f, 0.1f, 1.2f),
        .detune = values[resonatorDetuning],
        .vibrato = values[vibratoDepth] * blend({1.0f, 0.25f, 0.7f, 1.1f}),
        .articulation = percent(attackArticulation),
        .formant = percent(formantColor),
        .portamento = values[portamento],
        .bendRange = values[bendRange],
        .velocity = percent(velocityResponse),
        .aftertouch = percent(aftertouchResponse),
        .breathCurve = values[breathCurve],
        .second = blend({0.075f, 0.030f, 0.29f, 0.24f}) + enhance * 0.11f,
        .third = blend({0.08f, 0.020f, 0.44f, 0.32f}) + enhance * 0.14f,
        .fourth = blend({0.0f, 0.0f, 0.09f, 0.54f}) * (0.6f + enhance),
        .noiseMix = blend({1.0f, 0.22f, 0.8f, 1.2f}),
        .drift = blend({1.0f, 0.12f, 0.55f, 1.25f}),
        .onset = blend({1.0f, 0.55f, 0.8f, 1.12f})
    };
    for (auto& slot : voices) {
        if (slot.voice.isActive())
            slot.voice.update(controls, elapsed);
    }
}

float VoiceManager::nextSample() noexcept {
    if (layoutDirty)
        configureVoices();
    if (untilControl == 0) {
        refreshControls(controlFrames);
        untilControl = controlFrames;
    }
    --untilControl;
    gain += gainStep * (targets[master] * 0.01f - gain);
    if (targets[master] == 0.0f && gain < 1.0e-6f)
        gain = 0.0f;
    float sample = transition;
    transition *= 1.0f - transitionStep;
    if (std::abs(transition) < 1.0e-7f)
        transition = 0.0f;
    for (int index = 0; index < count; ++index) {
        if (voices[index].voice.isActive())
            sample += voices[index].voice.next();
    }
    sample *= gain;
    return sample / std::sqrt(1.0f + sample * sample);
}

int VoiceManager::soundingVoices() const noexcept {
    return static_cast<int>(std::count_if(voices.begin(), voices.end(), [](const Slot& slot) { return slot.voice.isActive(); }));
}

} // namespace ocarina
