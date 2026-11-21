#include "dsp/PrototypeVoice.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace {

class Processor final : public juce::AudioProcessor {
public:
    Processor()
        : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          parameters(*this, nullptr, "OcarinaState", makeParameters()),
          master(*parameters.getRawParameterValue(ocarina::masterGain.id.data())) {}

    void prepareToPlay(double rate, int) override { voice.prepare(rate); }
    void releaseResources() override { voice.releaseAll(); }
    void reset() override { voice.releaseAll(); }

    bool isBusesLayoutSupported(const BusesLayout& layout) const override {
        const auto output = layout.getMainOutputChannelSet();
        return output == juce::AudioChannelSet::stereo() || output == juce::AudioChannelSet::mono();
    }

    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override {
        juce::ScopedNoDenormals guard;
        voice.setMaster(master.load(std::memory_order_relaxed));
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
            if (message.isNoteOn() || message.isNoteOff()) {
                voice.apply({message.isNoteOn() ? ocarina::EventType::noteOn : ocarina::EventType::noteOff,
                    hostSource, channel, static_cast<uint8_t>(message.getNoteNumber()), message.getFloatVelocity()});
            } else if (message.isAllNotesOff() || message.isAllSoundOff()) {
                voice.apply({ocarina::EventType::releaseSource, hostSource, 0, 0, 0.0f});
            }
        }
        render(audio, cursor, audio.getNumSamples());
        midi.clear();
    }

    void processBlockBypassed(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override {
        voice.prepare(getSampleRate());
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
    const juce::String getProgramName(int) override { return "Reference Voice"; }
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
        const auto& info = ocarina::masterGain;
        juce::AudioProcessorValueTreeState::ParameterLayout result;
        result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID {info.id.data(), 1},
            info.name.data(), juce::NormalisableRange<float> {info.minimum, info.maximum}, info.initial,
            juce::AudioParameterFloatAttributes().withLabel(info.unit.data())));
        return result;
    }

    void render(juce::AudioBuffer<float>& audio, int start, int end) noexcept {
        auto channels = audio.getArrayOfWritePointers();
        for (int sample = start; sample < end; ++sample) {
            const float value = voice.nextSample();
            for (int channel = 0; channel < audio.getNumChannels(); ++channel)
                channels[channel][sample] = value;
        }
    }

    static constexpr uint16_t hostSource = 1;
    ocarina::PrototypeVoice voice;
    juce::AudioProcessorValueTreeState parameters;
    const std::atomic<float>& master;
};

} // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Processor(); }
