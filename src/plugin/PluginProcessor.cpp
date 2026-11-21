#include "dsp/InstrumentEngine.h"
#include "shared/Midi.h"
#include "PluginEditor.h"
#include "OcarinaPanel.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include <limits>

namespace {

class Processor final : public juce::AudioProcessor {
public:
    Processor()
        : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          parameters(*this, nullptr, "OcarinaState", makeParameters()) {
        for (size_t index = 0; index < controls.size(); ++index) {
            controls[index] = parameters.getRawParameterValue(ocarina::parameters[index].id.data());
            meter.mapped[index].store(unmapped, std::memory_order_relaxed);
        }
    }

    void prepareToPlay(double rate, int) override { readParameters(); engine.prepare(rate); }
    void releaseResources() override { engine.reset(); }
    void reset() override { engine.reset(); }

    bool isBusesLayoutSupported(const BusesLayout& layout) const override {
        const auto output = layout.getMainOutputChannelSet();
        return output == juce::AudioChannelSet::stereo() || output == juce::AudioChannelSet::mono();
    }

    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override {
        juce::ScopedNoDenormals guard;
        readParameters();
        const auto* playhead = getPlayHead();
        const auto position = playhead ? playhead->getPosition() : juce::Optional<juce::AudioPlayHead::PositionInfo>{};
        ocarina::HostClock clock;
        if (position && position->getPpqPosition() && position->getBpm())
            clock = {*position->getPpqPosition(), *position->getBpm(), true, position->getIsPlaying()};
        engine.beginBlock(clock, position ? static_cast<float>(position->getBpm().orFallback(0.0)) : 0);
        keyboard.drain([&](auto event) { engine.apply(event); }, [&] {
            engine.apply({ocarina::EventType::releaseSource, InstrumentEditor::keyboardSource, 0, 0, 0.0f});
        });
        auto cursor = 0;
        for (const auto metadata : midi) {
            // Channel voice MIDI messages fit in three bytes. Skip SysEx and system data.
            if (metadata.numBytes < 2 || metadata.numBytes > 3 || metadata.data[0] >= 0xf0)
                continue;
            const auto frame = juce::jlimit(cursor, audio.getNumSamples(), metadata.samplePosition);
            render(audio, cursor, frame);
            cursor = frame;
            const int status = metadata.data[0], first = metadata.data[1];
            const int second = metadata.numBytes == 3 ? metadata.data[2] : 0;
            ocarina::dispatchMidi(hostSource, status, first, second, [&](auto event) { engine.apply(event); }, static_cast<uint32_t>(frame));
            if ((status & 0xf0) == 0xb0 && first < static_cast<int>(patchRevision.ccMap.size())) {
                meter.cc.store((++ccCount << 14) | (first << 7) | second, std::memory_order_relaxed);
                const int target = patchRevision.ccMap[static_cast<size_t>(first)].load(std::memory_order_relaxed) - 1;
                if (target >= 0) {
                    const auto& control = ocarina::parameters[static_cast<size_t>(target)];
                    const float value = std::lerp(control.minimum, control.maximum, second / 127.0f);
                    meter.mapped[static_cast<size_t>(target)].store(value, std::memory_order_relaxed);
                    engine.setParameter(static_cast<size_t>(target), value);
                }
            }
        }
        render(audio, cursor, audio.getNumSamples());
        midi.clear();
        meter.peak.store(audio.getMagnitude(0, audio.getNumSamples()), std::memory_order_relaxed);
        meter.voices.store(engine.soundingVoices(), std::memory_order_relaxed);
        meter.note.store(engine.latestNote(), std::memory_order_relaxed);
        meter.tick.store(engine.sequencePosition(), std::memory_order_relaxed);
        meter.transport.store(engine.sequenceActivity(), std::memory_order_relaxed);
        const auto held = engine.heldNotes();
        for (size_t index = 0; index < held.size(); ++index) meter.held[index].store(held[index], std::memory_order_relaxed);
    }

    void processBlockBypassed(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override {
        engine.reset();
        audio.clear();
        midi.clear();
        meter.peak.store(0, std::memory_order_relaxed);
        meter.voices.store(0, std::memory_order_relaxed);
        meter.note.store(-1, std::memory_order_relaxed);
        meter.transport.store(0, std::memory_order_relaxed);
        for (auto& word : meter.held) word.store(0, std::memory_order_relaxed);
    }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override {
        const double room = controls[ocarina::reverbEnabled]->load() > .5f ? controls[ocarina::reverbDecay]->load() : 0.0;
        const double delayTail = controls[ocarina::delayEnabled]->load() > .5f ? 12.0 : 0.0;
        return .3 + room + delayTail;
    }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override {
        if (InstrumentEditor::available())
            return new InstrumentEditor(*this, parameters, keyboard, meter, patchRevision, engine);
        return new juce::GenericAudioProcessorEditor(*this);
    }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Ceramic Authentic"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destination) override {
        const std::scoped_lock lock(patchRevision.writer);
        auto state = parameters.copyState();
        for (size_t index = 0; index < controls.size(); ++index) {
            const float value = meter.mapped[index].load(std::memory_order_relaxed);
            if (std::isfinite(value) && controls[index]->load() == hostValues[index].load())
                state.getChildWithProperty("id", ocarina::parameters[index].id.data()).setProperty("value", value, nullptr);
        }
        state.setProperty("portablePreset", portablePreset(state), nullptr);
        if (patchRevision.sequencePacket.isArray())
            state.setProperty("pianoRoll", juce::JSON::toString(patchRevision.sequencePacket, true), nullptr);
        if (patchRevision.mapping.isArray())
            state.setProperty("midiMap", juce::JSON::toString(patchRevision.mapping, true), nullptr);
        if (auto xml = state.createXml())
            copyXmlToBinary(*xml, destination);
    }

    void setStateInformation(const void* bytes, int size) override {
        if (auto xml = getXmlFromBinary(bytes, size); xml && xml->hasTagName(parameters.state.getType())) {
            const PatchRevision::Write write(patchRevision);
            auto state = juce::ValueTree::fromXml(*xml);
            const auto portable = juce::JSON::parse(state.getProperty("portablePreset").toString());
            patchRevision.presetIdentity = juce::var();
            if (portable.isObject()) {
                auto identity = std::make_unique<juce::DynamicObject>();
                for (const auto* field : {"id", "name", "category", "description"})
                    identity->setProperty(field, portable.getProperty(field, {}));
                patchRevision.presetIdentity = juce::var(identity.release());
            }
            state.removeProperty("portablePreset", nullptr);
            const auto pattern = juce::JSON::parse(state.getProperty("pianoRoll").toString());
            if (!patchRevision.updateSequence(engine, pattern, true, true))
                patchRevision.updateSequence(engine, juce::var(juce::Array<juce::var>{7680, 12000, 1, 0, 0, 0, 0}), true, true);
            state.removeProperty("pianoRoll", nullptr);
            if (!patchRevision.updateMapping(juce::JSON::parse(state.getProperty("midiMap").toString())))
                patchRevision.updateMapping(juce::var(juce::Array<juce::var>{}));
            state.removeProperty("midiMap", nullptr);
            for (auto& value : meter.mapped) value.store(unmapped, std::memory_order_relaxed);
            parameters.replaceState(state);
        }
    }

private:
    juce::String portablePreset(const juce::ValueTree& state) const {
        // The shared build supplies a fully populated v1 snapshot with the canonical effect order.
        const auto catalogue = juce::JSON::parse(juce::String::fromUTF8(OcarinaPanel::parameters_json, OcarinaPanel::parameters_jsonSize));
        auto preset = catalogue.getProperty("factory", {})[0];
        if (!preset.isObject()) return {};
        const auto read = [&state](juce::StringRef id) -> juce::var {
            return state.getChildWithProperty("id", juce::String(id)).getProperty("value");
        };
        auto& identity = *preset.getDynamicObject();
        const auto& selected = patchRevision.presetIdentity;
        identity.setProperty("id", selected.getProperty("id", "vst3-current"));
        identity.setProperty("name", selected.getProperty("name", "Ocarina of Resonance"));
        identity.setProperty("category", selected.getProperty("category", "User"));
        identity.setProperty("description", selected.getProperty("description", ""));
        const auto writeValues = [&read](juce::var block) {
            auto& properties = block.getDynamicObject()->getProperties();
            for (int index = 0; index < properties.size(); ++index)
                properties.set(properties.getName(index), read(properties.getName(index).toString()));
        };
        writeValues(preset.getProperty("parameters", {}));
        const auto effects = preset.getProperty("effects", {});
        for (const auto& effect : *effects.getArray()) {
            effect.getDynamicObject()->setProperty("enabled", static_cast<float>(read(effect.getProperty("id", {}).toString() + "Enabled")) >= .5f);
            writeValues(effect.getProperty("parameters", {}));
        }
        constexpr const char* models[] {"ceramicAuthentic", "retroFantasy", "crystalResonator", "electronicHybrid"};
        const auto performance = preset.getProperty("performance", {});
        auto& fields = *performance.getDynamicObject();
        fields.setProperty("model", models[juce::jlimit(0, 3, static_cast<int>(read("model")))]);
        fields.setProperty("voiceMode", static_cast<int>(read("voiceMode")) == 0 ? "mono" : "poly");
        fields.setProperty("voiceLimit", read("voiceLimit"));
        fields.setProperty("gainCompensationDb", read("patchGain"));
        fields.setProperty("tempoBpm", read("tempo"));
        return juce::JSON::toString(preset, true);
    }

    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameters() {
        juce::AudioProcessorValueTreeState::ParameterLayout result;
        for (size_t index = 0; index < ocarina::parameters.size(); ++index) {
            const auto& info = ocarina::parameters[index];
            const juce::ParameterID id {info.id.data(), index == ocarina::master ? 1 : index < ocarina::synthesisParameterCount ? 2 : 3};
            auto name = juce::String(info.name.data());
            if (index >= ocarina::filterEnabled && !info.id.ends_with("Enabled")) {
                const auto key = juce::String(info.id.data());
                const auto split = key.indexOfAnyOf("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
                name = key.substring(0, 1).toUpperCase() + key.substring(1, split) + " " + name;
            }
            if (index == ocarina::model) {
                juce::StringArray choices;
                for (auto name : ocarina::modelNames)
                    choices.add(name.data());
                result.add(std::make_unique<juce::AudioParameterChoice>(id, info.name.data(), choices, 0));
            } else if (index == ocarina::voiceMode) {
                result.add(std::make_unique<juce::AudioParameterChoice>(id, info.name.data(), juce::StringArray {"Mono legato", "Polyphonic"}, 1));
            } else if (index == ocarina::voiceLimit) {
                result.add(std::make_unique<juce::AudioParameterInt>(id, info.name.data(), 1, ocarina::VoiceManager::maxVoices, ocarina::VoiceManager::maxVoices));
            } else if (index >= ocarina::filterEnabled && ocarina::steppedEffectParameter(index)) {
                result.add(std::make_unique<juce::AudioParameterInt>(id, name, static_cast<int>(info.minimum),
                    static_cast<int>(info.maximum), static_cast<int>(info.initial),
                    juce::AudioParameterIntAttributes().withLabel(juce::String::fromUTF8(info.unit.data()))));
            } else {
                result.add(std::make_unique<juce::AudioParameterFloat>(id, name,
                    juce::NormalisableRange<float> {info.minimum, info.maximum}, info.initial,
                    juce::AudioParameterFloatAttributes().withLabel(juce::String::fromUTF8(info.unit.data()))));
            }
        }
        return result;
    }

    void readParameters() noexcept {
        const auto revision = patchRevision.sequence.load();
        if (revision % 2 != 0) return;
        std::array<float, ocarina::parameterCount> snapshot;
        for (size_t index = 0; index < snapshot.size(); ++index)
            snapshot[index] = controls[index]->load();
        if (patchRevision.sequence.load() != revision) return;
        for (size_t index = 0; index < snapshot.size(); ++index) {
            const float last = hostValues[index].exchange(snapshot[index], std::memory_order_relaxed);
            if (snapshot[index] != last || revision != appliedPatch)
                meter.mapped[index].store(unmapped, std::memory_order_relaxed);
            const float mapped = meter.mapped[index].load(std::memory_order_relaxed);
            engine.setParameter(index, std::isfinite(mapped) ? mapped : snapshot[index]);
        }
        appliedPatch = revision;
    }

    void render(juce::AudioBuffer<float>& audio, int start, int end) noexcept {
        auto channels = audio.getArrayOfWritePointers();
        for (int sample = start; sample < end; ++sample) {
            const auto frame = engine.nextFrame();
            if (audio.getNumChannels() == 2) {
                channels[0][sample] = frame.left;
                channels[1][sample] = frame.right;
            } else if (audio.getNumChannels() == 1) {
                channels[0][sample] = .5f * (frame.left + frame.right);
            }
        }
    }

    static constexpr uint16_t hostSource = 1;
    static constexpr float unmapped = std::numeric_limits<float>::quiet_NaN();
    uint32_t ccCount = 0;
    unsigned appliedPatch = 0;
    ocarina::InstrumentEngine engine;
    juce::AudioProcessorValueTreeState parameters;
    std::array<std::atomic<float>*, ocarina::parameterCount> controls {};
    std::array<std::atomic<float>, ocarina::parameterCount> hostValues {};
    ocarina::EventQueue keyboard;
    InstrumentMeter meter;
    PatchRevision patchRevision;
};

} // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Processor(); }
