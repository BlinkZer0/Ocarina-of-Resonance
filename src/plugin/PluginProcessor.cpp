#include "dsp/InstrumentEngine.h"
#include "PluginEditor.h"
#include "OcarinaPanel.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace {

class Processor final : public juce::AudioProcessor {
public:
    Processor()
        : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          parameters(*this, nullptr, "OcarinaState", makeParameters()) {
        for (size_t index = 0; index < controls.size(); ++index)
            controls[index] = parameters.getRawParameterValue(ocarina::parameters[index].id.data());
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
        engine.setHostTempo(position ? static_cast<float>(position->getBpm().orFallback(0.0)) : 0);
        keyboard.drain([&](auto event) { engine.apply(event); }, [&] {
            engine.apply({ocarina::EventType::releaseSource, InstrumentEditor::keyboardSource, 0, 0, 0.0f});
        });
        auto cursor = 0;
        for (const auto metadata : midi) {
            // Channel voice MIDI messages fit in three bytes. Skip SysEx and system data.
            if (metadata.numBytes < 2 || metadata.numBytes > 3 || metadata.data[0] >= 0xf0)
                continue;
            const auto message = metadata.getMessage();
            const auto frame = juce::jlimit(cursor, audio.getNumSamples(), metadata.samplePosition);
            render(audio, cursor, frame);
            cursor = frame;
            const auto channel = static_cast<uint8_t>(message.getChannel() - 1);
            sendMidi(message, channel, static_cast<uint32_t>(frame));
        }
        render(audio, cursor, audio.getNumSamples());
        midi.clear();
        meter.peak.store(audio.getMagnitude(0, audio.getNumSamples()), std::memory_order_relaxed);
        meter.voices.store(engine.soundingVoices(), std::memory_order_relaxed);
        meter.note.store(engine.latestNote(), std::memory_order_relaxed);
    }

    void processBlockBypassed(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override {
        engine.reset();
        audio.clear();
        midi.clear();
        meter.peak.store(0, std::memory_order_relaxed);
        meter.voices.store(0, std::memory_order_relaxed);
        meter.note.store(-1, std::memory_order_relaxed);
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
            return new InstrumentEditor(*this, parameters, keyboard, meter, patchRevision);
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
        state.setProperty("portablePreset", portablePreset(state), nullptr);
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
        for (size_t index = 0; index < snapshot.size(); ++index)
            engine.setParameter(index, snapshot[index]);
    }

    void sendMidi(const juce::MidiMessage& message, uint8_t channel, uint32_t frame) noexcept {
        using enum ocarina::EventType;
        const auto send = [&](ocarina::EventType type, float value = 0.0f, uint8_t note = 0) {
            engine.apply({type, hostSource, channel, note, value, frame});
        };
        if (message.isNoteOn() || message.isNoteOff()) {
            send(message.isNoteOn() ? noteOn : noteOff, message.getFloatVelocity(), static_cast<uint8_t>(message.getNoteNumber()));
        } else if (message.isAllNotesOff() || message.isAllSoundOff()) {
            send(releaseChannel);
        } else if (message.isPitchWheel()) {
            const auto bend = message.getPitchWheelValue() - 8192;
            send(pitchBend, static_cast<float>(bend) / (bend < 0 ? 8192.0f : 8191.0f));
        } else if (message.isAftertouch()) {
            send(notePressure, message.getAfterTouchValue() / 127.0f, static_cast<uint8_t>(message.getNoteNumber()));
        } else if (message.isChannelPressure()) {
            send(channelPressure, message.getChannelPressureValue() / 127.0f);
        } else if (message.isController()) {
            const float value = message.getControllerValue() / 127.0f;
            switch (message.getControllerNumber()) {
                case 2: send(breath, value); break;
                case 11: send(expression, value); break;
                case 64: send(sustain, value); break;
                case 121: send(sustain); send(breath, 1.0f); send(expression, 1.0f); send(pitchBend); send(channelPressure); break;
                default: break;
            }
        }
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
    ocarina::InstrumentEngine engine;
    juce::AudioProcessorValueTreeState parameters;
    std::array<std::atomic<float>*, ocarina::parameterCount> controls {};
    ocarina::EventQueue keyboard;
    InstrumentMeter meter;
    PatchRevision patchRevision;
};

} // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Processor(); }
