#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2)
        return 1;

    juce::ScopedJuceInitialiser_GUI initialise;
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    format.findAllTypesForFile(descriptions, juce::String::fromUTF8(argv[1]));
    if (descriptions.size() != 1 || !descriptions[0]->isInstrument)
        return 2;

    juce::String error;
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    auto plugin = format.createInstanceFromDescription(*descriptions[0], sampleRate, blockSize, error);
    if (plugin == nullptr || !plugin->acceptsMidi()) {
        std::cerr << error << '\n';
        return 3;
    }

    plugin->prepareToPlay(sampleRate, blockSize);
    juce::AudioBuffer<float> audio(2, blockSize);
    juce::MidiBuffer midi;
    constexpr int start = 64;
    audio.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), start);
    plugin->processBlock(audio, midi);
    if (audio.getMagnitude(0, start) != 0.0f || audio.getMagnitude(start, blockSize - start) < 0.001f)
        return 4;

    // Allow the jet to build pressure after verifying the precise MIDI onset.
    for (int block = 0; block < 6; ++block) {
        audio.clear();
        midi.clear();
        plugin->processBlock(audio, midi);
    }
    if (audio.getMagnitude(0, blockSize) < 0.05f)
        return 4;

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOff(1, 69), 0);
    for (int block = 0; block < 60; ++block) {
        audio.clear();
        plugin->processBlock(audio, midi);
        midi.clear();
    }
    if (audio.getMagnitude(0, blockSize) != 0.0f)
        return 5;

    juce::AudioProcessorParameter* master = nullptr;
    juce::AudioProcessorParameter* model = nullptr;
    for (auto* parameter : plugin->getParameters()) {
        if (parameter->getName(64) == "Master Volume")
            master = parameter;
        if (parameter->getName(64) == "Sound Model")
            model = parameter;
    }
    if (master == nullptr || model == nullptr)
        return 6;

    master->setValueNotifyingHost(0.42f);
    model->setValueNotifyingHost(1.0f);
    juce::MemoryBlock state;
    plugin->getStateInformation(state);
    master->setValueNotifyingHost(0.1f);
    model->setValueNotifyingHost(0.0f);
    plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    if (std::abs(master->getValue() - 0.42f) > 0.001f || model->getValue() != 1.0f)
        return 7;
    plugin->releaseResources();
    return 0;
}
