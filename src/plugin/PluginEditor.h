#pragma once

#include "shared/Event.h"
#include "shared/Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

struct InstrumentMeter {
    std::atomic<float> peak {0};
    std::atomic<int> voices {0}, note {-1};
};

class InstrumentEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    InstrumentEditor(juce::AudioProcessor& processor, juce::AudioProcessorValueTreeState& state,
        ocarina::EventQueue& keyboard, const InstrumentMeter& meter);
    ~InstrumentEditor() override;
    void resized() override;
    static bool available();
    static constexpr uint16_t keyboardSource = 2;

private:
    static std::optional<juce::WebBrowserComponent::Resource> resource(const juce::String& path);
    juce::WebBrowserComponent::Options options();
    void receive(const juce::var& event);
    void timerCallback() override;

    ocarina::EventQueue& keyboard;
    const InstrumentMeter& meter;
    std::array<juce::RangedAudioParameter*, ocarina::parameterCount> parameters {};
    std::array<bool, ocarina::parameterCount> changing {};
    juce::WebBrowserComponent browser;
    bool ready = false;
};
