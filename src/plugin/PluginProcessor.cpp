#include "dsp/VoiceManager.h"

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
    }

    void processBlockBypassed(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override {
        engine.reset();
        audio.clear();
        midi.clear();
    }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.3; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor(*this); }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Ceramic Authentic"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destination) override {
        if (auto xml = parameters.copyState().createXml())
            copyXmlToBinary(*xml, destination);
    }

    void setStateInformation(const void* bytes, int size) override {
        if (auto xml = getXmlFromBinary(bytes, size); xml && xml->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xml));
    }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameters() {
        juce::AudioProcessorValueTreeState::ParameterLayout result;
        for (size_t index = 0; index < ocarina::parameters.size(); ++index) {
            const auto& info = ocarina::parameters[index];
            const juce::ParameterID id {info.id.data(), index == ocarina::master ? 1 : 2};
            if (index == ocarina::model) {
                juce::StringArray choices;
                for (auto name : ocarina::modelNames)
                    choices.add(name.data());
                result.add(std::make_unique<juce::AudioParameterChoice>(id, info.name.data(), choices, 0));
            } else if (index == ocarina::voiceMode) {
                result.add(std::make_unique<juce::AudioParameterChoice>(id, info.name.data(), juce::StringArray {"Mono legato", "Polyphonic"}, 1));
            } else if (index == ocarina::voiceLimit) {
                result.add(std::make_unique<juce::AudioParameterInt>(id, info.name.data(), 1, ocarina::VoiceManager::maxVoices, ocarina::VoiceManager::maxVoices));
            } else {
                result.add(std::make_unique<juce::AudioParameterFloat>(id, info.name.data(),
                    juce::NormalisableRange<float> {info.minimum, info.maximum}, info.initial,
                    juce::AudioParameterFloatAttributes().withLabel(juce::String::fromUTF8(info.unit.data()))));
            }
        }
        return result;
    }

    void readParameters() noexcept {
        for (size_t index = 0; index < controls.size(); ++index)
            engine.setParameter(index, controls[index]->load(std::memory_order_relaxed));
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
            const float value = engine.nextSample();
            for (int channel = 0; channel < audio.getNumChannels(); ++channel)
                channels[channel][sample] = value;
        }
    }

    static constexpr uint16_t hostSource = 1;
    ocarina::VoiceManager engine;
    juce::AudioProcessorValueTreeState parameters;
    std::array<std::atomic<float>*, ocarina::parameterCount> controls {};
};

} // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Processor(); }
